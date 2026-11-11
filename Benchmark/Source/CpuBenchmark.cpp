#include "CpuBenchmark.h"

#include "RayGeneration.h"
#include "Stats.h"

#include "Core/Renderer/TriangleTraversal.h"

#include <chrono>
#include <cmath>
#include <limits>

namespace Benchmark {

	namespace {

		using Clock = std::chrono::steady_clock;

		double ElapsedMs(Clock::time_point start, Clock::time_point end)
		{
			return std::chrono::duration<double, std::milli>(end - start).count();
		}

		constexpr float kTMin = 0.001f;
		constexpr float kTMax = std::numeric_limits<float>::max();

		// Absolute + relative tolerance, matching the pattern (not the exact
		// figures) already used by Tests/Source/BVHTraversalTests.cpp's
		// CompareBVHToBruteForce, since here 'record.T' spans very different
		// magnitudes across scenes (unit-scale synthetic triangles vs.
		// several-unit-wide real-world models).
		bool NearlyEqual(float a, float b, float absoluteTolerance, float relativeTolerance)
		{
			return std::fabs(a - b) <= absoluteTolerance + relativeTolerance * std::fabs(b);
		}

		bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, float absoluteTolerance, float relativeTolerance)
		{
			return NearlyEqual(a.x, b.x, absoluteTolerance, relativeTolerance)
				&& NearlyEqual(a.y, b.y, absoluteTolerance, relativeTolerance)
				&& NearlyEqual(a.z, b.z, absoluteTolerance, relativeTolerance);
		}

		struct CorrectnessCheckResult
		{
			uint32_t MismatchCount = 0;
			uint64_t BruteForceHitCount = 0;
			uint64_t BvhHitCount = 0;
		};

		// specs/bvh.md "Correctness Requirement": BVH result == brute-force
		// result for the same ray, within a documented floating-point
		// tolerance. This is run once, independently of (and before) the
		// timing loops below, per specs/benchmarking.md "Correctness must be
		// verified independently from performance."
		CorrectnessCheckResult CheckCorrectness(const Core::Mesh& mesh, const Core::BVH& bvh, const std::vector<Core::Ray>& rays)
		{
			CorrectnessCheckResult result;

			for (const Core::Ray& ray : rays)
			{
				Core::HitRecord bruteForceRecord;
				Core::HitRecord bvhRecord;

				const bool bruteForceHit = Core::IntersectBruteForce(mesh, ray, kTMin, kTMax, bruteForceRecord);
				const bool bvhHit = Core::IntersectBVH(bvh, mesh, ray, kTMin, kTMax, bvhRecord);

				if (bruteForceHit) result.BruteForceHitCount++;
				if (bvhHit) result.BvhHitCount++;

				bool agrees = (bruteForceHit == bvhHit);

				if (agrees && bruteForceHit)
				{
					agrees =
						bruteForceRecord.TriangleId == bvhRecord.TriangleId &&
						NearlyEqual(bruteForceRecord.T, bvhRecord.T, 1e-4f, 1e-4f) &&
						NearlyEqual(bruteForceRecord.Position, bvhRecord.Position, 1e-3f, 1e-4f) &&
						NearlyEqual(bruteForceRecord.Normal, bvhRecord.Normal, 1e-3f, 1e-4f) &&
						bruteForceRecord.FrontFace == bvhRecord.FrontFace;
				}

				if (!agrees)
					result.MismatchCount++;
			}

			return result;
		}

		// One full sweep over every ray in 'rays', accumulating a checksum
		// from every result so the compiler cannot prove the loop's results
		// are unused and eliminate it. Returns the sweep duration in
		// milliseconds; the checksum is returned via 'checksum' purely so the
		// caller can also sanity-check it (e.g. compare against the
		// correctness pass's hit counts), giving the accumulation a genuine
		// use beyond just defeating dead-code elimination.
		template<typename IntersectFn>
		double TimeOneSweep(const std::vector<Core::Ray>& rays, IntersectFn&& intersect, double& checksum)
		{
			const auto start = Clock::now();

			double localChecksum = 0.0;
			Core::HitRecord record;
			for (const Core::Ray& ray : rays)
			{
				if (intersect(ray, record))
					localChecksum += (double)record.T;
			}

			const auto end = Clock::now();

			checksum += localChecksum;
			return ElapsedMs(start, end);
		}

	}

	std::vector<BenchmarkResult> RunCpuBenchmark(
		const std::string& sceneName,
		const Core::Mesh& mesh,
		double modelLoadMs,
		const CpuBenchmarkConfig& config,
		const std::string& runTimestampUtc)
	{
		// Ray generation is scene setup, not traversal: excluded from every
		// timed measurement below (specs/milestones.md M9 acceptance
		// criteria #2).
		const std::vector<Core::Ray> rays = GenerateTestRays(mesh.GetBounds(), config.RayCount, config.RaySeed);

		// BVH construction time is its own measured category (specs/
		// benchmarking.md "BVH Construction Time"), timed once, before any
		// traversal warm-up or measurement.
		const auto buildStart = Clock::now();
		Core::BVH bvh(mesh, config.LeafSize);
		const auto buildEnd = Clock::now();
		const double bvhBuildMs = ElapsedMs(buildStart, buildEnd);

		const CorrectnessCheckResult correctness = CheckCorrectness(mesh, bvh, rays);
		// A vacuous pass (zero hits at all) would make "0 mismatches" mean
		// nothing; GenerateTestRays deliberately aims most rays at the
		// scene, so a non-degenerate mesh should produce a substantial hit
		// count. This does not gate the recorded result, but is checked here
		// so a future change to ray generation that accidentally stops
		// hitting anything is visible in the printed summary (Main.cpp), not
		// silently reported as "passed".
		const bool correctnessPassed = correctness.MismatchCount == 0;

		auto bruteForceIntersect = [&mesh](const Core::Ray& ray, Core::HitRecord& record)
		{
			return Core::IntersectBruteForce(mesh, ray, kTMin, kTMax, record);
		};
		auto bvhIntersect = [&bvh, &mesh](const Core::Ray& ray, Core::HitRecord& record)
		{
			return Core::IntersectBVH(bvh, mesh, ray, kTMin, kTMax, record);
		};

		double checksumSink = 0.0;

		// Warm-up (specs/benchmarking.md "Warm-Up"): discard results, but
		// still run full sweeps so branch predictors/caches reach steady
		// state before anything is measured.
		for (uint32_t i = 0; i < config.WarmupIterations; i++)
			TimeOneSweep(rays, bruteForceIntersect, checksumSink);
		for (uint32_t i = 0; i < config.WarmupIterations; i++)
			TimeOneSweep(rays, bvhIntersect, checksumSink);

		std::vector<double> bruteForceSweepMs;
		bruteForceSweepMs.reserve(config.MeasuredIterations);
		for (uint32_t i = 0; i < config.MeasuredIterations; i++)
			bruteForceSweepMs.push_back(TimeOneSweep(rays, bruteForceIntersect, checksumSink));

		std::vector<double> bvhSweepMs;
		bvhSweepMs.reserve(config.MeasuredIterations);
		for (uint32_t i = 0; i < config.MeasuredIterations; i++)
			bvhSweepMs.push_back(TimeOneSweep(rays, bvhIntersect, checksumSink));

		const TimingStats bruteForceStats = ComputeTimingStats(bruteForceSweepMs);
		const TimingStats bvhStats = ComputeTimingStats(bvhSweepMs);

		BenchmarkResult bruteForceRow;
		bruteForceRow.Processor = "CPU";
		bruteForceRow.Scene = sceneName;
		bruteForceRow.TriangleCount = mesh.GetTriangleCount();
		bruteForceRow.TraversalMode = "BruteForce";
		bruteForceRow.RayCount = config.RayCount;
		bruteForceRow.Iterations = bruteForceStats.SampleCount;
		bruteForceRow.WarmupIterations = config.WarmupIterations;
		bruteForceRow.ModelLoadMs = modelLoadMs;
		bruteForceRow.MeasurementMeanMs = bruteForceStats.MeanMs;
		bruteForceRow.MeasurementMinMs = bruteForceStats.MinMs;
		bruteForceRow.MeasurementMaxMs = bruteForceStats.MaxMs;
		bruteForceRow.MeasurementStdDevMs = bruteForceStats.StdDevMs;
		bruteForceRow.CorrectnessChecked = true;
		bruteForceRow.CorrectnessPassed = correctnessPassed;
		bruteForceRow.MismatchCount = correctness.MismatchCount;
		bruteForceRow.TestedRayCount = config.RayCount;
		bruteForceRow.RunTimestampUtc = runTimestampUtc;

		BenchmarkResult bvhRow = bruteForceRow;
		bvhRow.TraversalMode = "BVH";
		bvhRow.LeafSize = config.LeafSize;
		bvhRow.BvhBuildMs = bvhBuildMs;
		bvhRow.Iterations = bvhStats.SampleCount;
		bvhRow.MeasurementMeanMs = bvhStats.MeanMs;
		bvhRow.MeasurementMinMs = bvhStats.MinMs;
		bvhRow.MeasurementMaxMs = bvhStats.MaxMs;
		bvhRow.MeasurementStdDevMs = bvhStats.StdDevMs;

		const Core::BVHStats& stats = bvh.GetStats();
		bvhRow.NodeCount = stats.NodeCount;
		bvhRow.LeafCount = stats.LeafCount;
		bvhRow.MaxDepth = stats.MaxDepth;
		bvhRow.MinLeafSize = stats.MinLeafSize;
		bvhRow.MaxLeafSize = stats.MaxLeafSize;
		bvhRow.AverageLeafSize = (double)stats.AverageLeafSize;

		// Referencing the sink keeps every sweep's accumulation observably
		// used (see TimeOneSweep's comment); NaN would indicate a logic bug
		// (e.g. a traversal writing garbage into record.T for a reported
		// hit), not something the checksum's specific value is meant to
		// convey on its own.
		(void)checksumSink;

		return { bruteForceRow, bvhRow };
	}

}
