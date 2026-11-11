#include "GpuBenchmark.h"

#include "CameraFraming.h"
#include "GpuUpload.h"
#include "Stats.h"

#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/Sphere.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace Benchmark {

	namespace {

		using Clock = std::chrono::steady_clock;

		double ElapsedMs(Clock::time_point start, Clock::time_point end)
		{
			return std::chrono::duration<double, std::milli>(end - start).count();
		}

		// Matches App/Shaders/RayTracing.glsl's '#define TRAVERSAL_MODE_*'
		// (specs/gpu-integration.md "Traversal Mode"). Defined locally
		// instead of depending on App's AppLayer.h: Benchmark does not link
		// against the App executable (executables are not libraries), and
		// these two integers are the entire shader-facing contract.
		constexpr int kTraversalModeBruteForce = 0;
		constexpr int kTraversalModeBvh = 1;

		constexpr uint32_t kWorkGroupSize = 8; // must match RayTracing.glsl's local_size_x/y

		// Result of one attempted dispatch-and-time cycle.
		struct DispatchTiming
		{
			double Ms = 0.0;
			bool TimedOut = false;
		};

		// Dispatches one frame and waits -- by bounded polling, never by an
		// unbounded blocking call -- for the GPU to actually finish it,
		// measuring elapsed GPU time via GL_TIME_ELAPSED (specs/
		// benchmarking.md "GPU Timing": "account for the asynchronous nature
		// of GPU execution... do not rely exclusively on CPU wall-clock time
		// around asynchronous OpenGL calls").
		DispatchTiming DispatchAndTime(GLuint query, uint32_t width, uint32_t height, std::chrono::milliseconds maxWait)
		{
			glBeginQuery(GL_TIME_ELAPSED, query);

			glDispatchCompute(
				(width + kWorkGroupSize - 1) / kWorkGroupSize,
				(height + kWorkGroupSize - 1) / kWorkGroupSize,
				1);

			// Matches AppLayer::OnRender()'s barrier exactly: required before
			// the accumulation image's contents (written via imageStore) can
			// be safely read back, either by the interactive present pass or,
			// here, by ReadAccumulationPixels() below.
			glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

			glEndQuery(GL_TIME_ELAPSED);

			const auto waitStart = Clock::now();
			GLint available = GL_FALSE;
			while (true)
			{
				glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
				if (available)
					break;

				if (Clock::now() - waitStart > maxWait)
					return { 0.0, true };

				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			GLuint64 nanoseconds = 0;
			glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
			return { (double)nanoseconds / 1.0e6, false };
		}

		std::vector<float> ReadAccumulationPixels(GLuint textureHandle, uint32_t width, uint32_t height)
		{
			std::vector<float> pixels((size_t)width * height * 4);
			glGetTextureImage(textureHandle, 0, GL_RGBA, GL_FLOAT,
				(GLsizei)(pixels.size() * sizeof(float)), pixels.data());
			return pixels;
		}

		struct PixelDiff
		{
			double MaxAbsDiff = 0.0;
			uint32_t MismatchCount = 0;
		};

		// specs/gpu-integration.md "GPU Validation": "GPU brute-force vs GPU
		// BVH must produce equivalent intersections and visually equivalent
		// rendered output within a documented floating-point tolerance."
		// 1e-4 in linear colour space is generous relative to floating-point
		// noise but tight enough to catch a genuine traversal divergence;
		// see TriangleTraversal.h's documented note that the GPU paths do not
		// reproduce Core::IntersectBVH's exact-tie id tie-break, which could
		// in principle (though it was never observed during M8's own manual
		// validation) cause a mismatch on a bit-identical distance tie.
		constexpr double kPixelTolerance = 1e-4;

		PixelDiff ComparePixels(const std::vector<float>& a, const std::vector<float>& b)
		{
			PixelDiff diff;
			const size_t count = std::min(a.size(), b.size());
			for (size_t i = 0; i < count; i++)
			{
				const double delta = std::fabs((double)a[i] - (double)b[i]);
				diff.MaxAbsDiff = std::max(diff.MaxAbsDiff, delta);
				if (delta > kPixelTolerance)
					diff.MismatchCount++;
			}
			return diff;
		}

		struct ModeRunOutcome
		{
			std::string Status = "ok";
			std::string StatusReason;
			std::vector<double> IterationMs;
			std::vector<float> Pixels;
			bool HavePixels = false;
		};

		ModeRunOutcome RunOneMode(
			uint32_t traversalMode,
			uint32_t computeShaderProgram,
			uint32_t triangleCount,
			uint32_t bvhNodeCount,
			GLuint accumulationTextureHandle,
			const GpuBenchmarkConfig& config,
			const CameraFraming& camera)
		{
			ModeRunOutcome outcome;

			glUseProgram(computeShaderProgram);
			glUniform1i(glGetUniformLocation(computeShaderProgram, "u_TraversalMode"), (GLint)traversalMode);
			glUniform1i(glGetUniformLocation(computeShaderProgram, "u_SphereCount"), 0);
			glUniform1i(glGetUniformLocation(computeShaderProgram, "u_TriangleCount"), (GLint)triangleCount);
			glUniform1i(glGetUniformLocation(computeShaderProgram, "u_BVHNodeCount"), (GLint)bvhNodeCount);
			glUniform1i(glGetUniformLocation(computeShaderProgram, "u_SamplesPerPixel"), (GLint)config.SamplesPerPixel);
			// Fixed at 0 for every iteration and every mode: RayTracing.glsl's
			// per-pixel RNG seed depends only on pixel coordinates and
			// u_FrameIndex, so this makes every dispatch -- warm-up or
			// measured, BruteForce or BVH -- render the exact same set of
			// camera/scatter rays. That is what makes BruteForce and BVH
			// directly comparable pixel-for-pixel (see ComparePixels above)
			// and makes repeated runs of this benchmark reproducible (specs/
			// milestones.md M9 acceptance criteria #9); accumulation
			// (imageLoad/mix) is also disabled by construction, since the
			// shader only blends when u_FrameIndex > 0.
			glUniform1ui(glGetUniformLocation(computeShaderProgram, "u_FrameIndex"), 0u);
			glUniform3f(glGetUniformLocation(computeShaderProgram, "u_CameraPos"), camera.Position.x, camera.Position.y, camera.Position.z);
			glUniform3f(glGetUniformLocation(computeShaderProgram, "u_CameraForward"), camera.Forward.x, camera.Forward.y, camera.Forward.z);
			glUniform3f(glGetUniformLocation(computeShaderProgram, "u_CameraUp"), camera.Up.x, camera.Up.y, camera.Up.z);
			glUniform1f(glGetUniformLocation(computeShaderProgram, "u_CameraFocal"), camera.FocalLength);

			glBindImageTexture(0, accumulationTextureHandle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

			GLuint query = 0;
			glGenQueries(1, &query);

			// Warm-up (specs/benchmarking.md "Warm-Up"): dispatch and fully
			// wait for completion (bounded), but discard the timing.
			for (uint32_t i = 0; i < config.WarmupIterations && outcome.Status == "ok"; i++)
			{
				const DispatchTiming timing = DispatchAndTime(query, config.ResolutionWidth, config.ResolutionHeight, config.MaxWaitPerIteration);
				if (timing.TimedOut)
				{
					outcome.Status = "timeout";
					outcome.StatusReason = "GPU warm-up dispatch did not complete within the configured wait budget";
				}
			}

			for (uint32_t i = 0; i < config.MeasuredIterations && outcome.Status == "ok"; i++)
			{
				const DispatchTiming timing = DispatchAndTime(query, config.ResolutionWidth, config.ResolutionHeight, config.MaxWaitPerIteration);
				if (timing.TimedOut)
				{
					outcome.Status = "timeout";
					outcome.StatusReason = "GPU dispatch did not complete within the configured wait budget after "
						+ std::to_string(outcome.IterationMs.size()) + " of " + std::to_string(config.MeasuredIterations) + " measured iterations";
					break;
				}
				outcome.IterationMs.push_back(timing.Ms);
			}

			glDeleteQueries(1, &query);

			// Only read back pixels when every warm-up and measured iteration
			// completed cleanly: after a timeout, the outstanding dispatch's
			// GPU-side completion state is unknown, so the texture cannot be
			// trusted for the pixel cross-check.
			if (outcome.Status == "ok")
			{
				glFinish();
				outcome.Pixels = ReadAccumulationPixels(accumulationTextureHandle, config.ResolutionWidth, config.ResolutionHeight);
				outcome.HavePixels = true;
			}

			return outcome;
		}

	}

	std::vector<BenchmarkResult> RunGpuBenchmark(
		const std::string& sceneName,
		const Core::Mesh& mesh,
		double modelLoadMs,
		uint32_t computeShaderProgram,
		const GpuContextInfo& glInfo,
		const GpuBenchmarkConfig& config,
		const std::string& runTimestampUtc)
	{
		std::vector<BenchmarkResult> results;

		const uint32_t triangleCount = mesh.GetTriangleCount();
		const CameraFraming camera = ComputeFramingCamera(mesh.GetBounds());

		BenchmarkResult baseRow;
		baseRow.Processor = "GPU";
		baseRow.Scene = sceneName;
		baseRow.TriangleCount = triangleCount;
		baseRow.ResolutionWidth = config.ResolutionWidth;
		baseRow.ResolutionHeight = config.ResolutionHeight;
		baseRow.SamplesPerPixel = config.SamplesPerPixel;
		baseRow.MaxPathDepth = config.MaxPathDepthConstant;
		baseRow.WarmupIterations = config.WarmupIterations;
		baseRow.ModelLoadMs = modelLoadMs;
		baseRow.RunTimestampUtc = runTimestampUtc;
		baseRow.GLVendor = glInfo.Vendor;
		baseRow.GLRenderer = glInfo.Renderer;
		baseRow.GLVersion = glInfo.Version;

		// --- Dummy sphere buffer -------------------------------------------
		// specs/benchmarking.md "The benchmark must change only the traversal
		// strategy": u_SphereCount is fixed at 0 for every GPU measurement, so
		// only triangle traversal cost is measured. Binding 0 must still
		// reference a valid buffer object (the shader's SphereBuffer SSBO is
		// declared unconditionally), so a 1-element placeholder is bound,
		// mirroring how AppLayer.cpp handles an empty triangle/BVH buffer.
		Core::Sphere dummySphere{};
		Renderer::StorageBuffer sphereBuffer = Renderer::CreateStorageBuffer(&dummySphere, sizeof(Core::Sphere));

		// --- Triangle buffer (timed) ----------------------------------------
		const std::vector<GpuUpload::GPUTriangle> gpuTriangles = GpuUpload::BuildGPUTriangles(mesh);

		const auto triangleUploadStart = Clock::now();
		Renderer::StorageBuffer triangleBuffer = gpuTriangles.empty()
			? Renderer::CreateStorageBuffer(nullptr, sizeof(GpuUpload::GPUTriangle))
			: Renderer::CreateStorageBuffer(gpuTriangles.data(), gpuTriangles.size() * sizeof(GpuUpload::GPUTriangle));
		glFinish();
		const auto triangleUploadEnd = Clock::now();
		const double gpuTriangleUploadMs = ElapsedMs(triangleUploadStart, triangleUploadEnd);

		// --- BVH: build + serialize (timed) then upload (timed) ------------
		const auto bvhBuildStart = Clock::now();
		Core::BVH bvh(mesh, config.LeafSize);
		const std::vector<GpuUpload::GPUBVHNode> gpuBvhNodes = GpuUpload::BuildGPUBVHNodes(bvh);
		const std::vector<uint32_t> gpuBvhPrimitiveIds = GpuUpload::BuildGPUBVHPrimitiveIds(bvh);
		const auto bvhBuildEnd = Clock::now();
		const double bvhConstructionSerializationMs = ElapsedMs(bvhBuildStart, bvhBuildEnd);

		const auto nodeUploadStart = Clock::now();
		Renderer::StorageBuffer bvhNodeBuffer = gpuBvhNodes.empty()
			? Renderer::CreateStorageBuffer(nullptr, sizeof(GpuUpload::GPUBVHNode))
			: Renderer::CreateStorageBuffer(gpuBvhNodes.data(), gpuBvhNodes.size() * sizeof(GpuUpload::GPUBVHNode));
		glFinish();
		const auto nodeUploadEnd = Clock::now();
		const double gpuBvhNodeUploadMs = ElapsedMs(nodeUploadStart, nodeUploadEnd);

		const auto primitiveUploadStart = Clock::now();
		Renderer::StorageBuffer bvhPrimitiveBuffer = gpuBvhPrimitiveIds.empty()
			? Renderer::CreateStorageBuffer(nullptr, sizeof(uint32_t))
			: Renderer::CreateStorageBuffer(gpuBvhPrimitiveIds.data(), gpuBvhPrimitiveIds.size() * sizeof(uint32_t));
		glFinish();
		const auto primitiveUploadEnd = Clock::now();
		const double gpuBvhPrimitiveUploadMs = ElapsedMs(primitiveUploadStart, primitiveUploadEnd);

		const uint32_t bvhNodeCount = (uint32_t)bvh.GetNodes().size();

		Renderer::Texture accumulationTexture = Renderer::CreateTexture((int)config.ResolutionWidth, (int)config.ResolutionHeight);

		Renderer::BindStorageBuffer(sphereBuffer, 0);
		Renderer::BindStorageBuffer(triangleBuffer, 1);
		Renderer::BindStorageBuffer(bvhNodeBuffer, 2);
		Renderer::BindStorageBuffer(bvhPrimitiveBuffer, 3);

		// --- GPU BruteForce ---------------------------------------------------
		bool haveBruteForcePixels = false;
		std::vector<float> bruteForcePixels;

		if (triangleCount > config.MaxBruteForceTriangles)
		{
			BenchmarkResult row = baseRow;
			row.TraversalMode = "BruteForce";
			row.Iterations = 0;
			row.GpuTriangleUploadMs = gpuTriangleUploadMs;
			row.Status = "skipped";
			row.StatusReason = "triangle_count (" + std::to_string(triangleCount) + ") exceeds the configured GPU "
				"brute-force ceiling (" + std::to_string(config.MaxBruteForceTriangles) + "). GPU brute-force "
				"traversal tests every triangle for every ray with no early-out, so above this scale a single "
				"dispatch risks an impractically long run; see specs/benchmarking.md 'Do not run enormous GPU "
				"brute-force cases indefinitely' and specs/milestones.md M9 acceptance criteria #7.";
			results.push_back(row);
		}
		else
		{
			const ModeRunOutcome outcome = RunOneMode(kTraversalModeBruteForce, computeShaderProgram, triangleCount, bvhNodeCount, accumulationTexture.Handle, config, camera);

			const TimingStats stats = ComputeTimingStats(outcome.IterationMs);

			BenchmarkResult row = baseRow;
			row.TraversalMode = "BruteForce";
			row.Iterations = stats.SampleCount;
			row.GpuTriangleUploadMs = gpuTriangleUploadMs;
			row.MeasurementMeanMs = stats.MeanMs;
			row.MeasurementMinMs = stats.MinMs;
			row.MeasurementMaxMs = stats.MaxMs;
			row.MeasurementStdDevMs = stats.StdDevMs;
			row.Status = outcome.Status;
			row.StatusReason = outcome.StatusReason;
			results.push_back(row);

			if (outcome.HavePixels)
			{
				bruteForcePixels = outcome.Pixels;
				haveBruteForcePixels = true;
			}
		}

		// --- GPU BVH ------------------------------------------------------------
		// No triangle-count ceiling: BVH traversal is expected sub-linear, and
		// the bounded per-iteration wait (MaxWaitPerIteration) is still in
		// effect as a safety net regardless.
		bool haveBvhPixels = false;
		std::vector<float> bvhPixels;

		{
			const ModeRunOutcome outcome = RunOneMode(kTraversalModeBvh, computeShaderProgram, triangleCount, bvhNodeCount, accumulationTexture.Handle, config, camera);

			const TimingStats stats = ComputeTimingStats(outcome.IterationMs);

			BenchmarkResult row = baseRow;
			row.TraversalMode = "BVH";
			row.LeafSize = config.LeafSize;
			row.Iterations = stats.SampleCount;
			row.BvhBuildMs = bvhConstructionSerializationMs;
			row.GpuTriangleUploadMs = gpuTriangleUploadMs;
			row.GpuBvhNodeUploadMs = gpuBvhNodeUploadMs;
			row.GpuBvhPrimitiveUploadMs = gpuBvhPrimitiveUploadMs;
			row.MeasurementMeanMs = stats.MeanMs;
			row.MeasurementMinMs = stats.MinMs;
			row.MeasurementMaxMs = stats.MaxMs;
			row.MeasurementStdDevMs = stats.StdDevMs;
			row.Status = outcome.Status;
			row.StatusReason = outcome.StatusReason;

			const Core::BVHStats& bvhStats = bvh.GetStats();
			row.NodeCount = bvhStats.NodeCount;
			row.LeafCount = bvhStats.LeafCount;
			row.MaxDepth = bvhStats.MaxDepth;
			row.MinLeafSize = bvhStats.MinLeafSize;
			row.MaxLeafSize = bvhStats.MaxLeafSize;
			row.AverageLeafSize = (double)bvhStats.AverageLeafSize;

			results.push_back(row);

			if (outcome.HavePixels)
			{
				bvhPixels = outcome.Pixels;
				haveBvhPixels = true;
			}
		}

		// --- Pixel-level cross-check (specs/gpu-integration.md "GPU
		// Validation") --------------------------------------------------------
		if (haveBruteForcePixels && haveBvhPixels)
		{
			const PixelDiff diff = ComparePixels(bruteForcePixels, bvhPixels);
			for (BenchmarkResult& row : results)
			{
				row.GpuPixelCheckPerformed = true;
				row.GpuPixelCheckPassed = (diff.MismatchCount == 0);
				row.GpuPixelMaxAbsDiff = diff.MaxAbsDiff;
				row.GpuPixelMismatchCount = diff.MismatchCount;
			}
		}
		else
		{
			for (BenchmarkResult& row : results)
			{
				if (!row.StatusReason.empty())
					row.StatusReason += " ";
				row.StatusReason += "(GPU pixel cross-check skipped: brute-force reference render unavailable for this scene)";
			}
		}

		Renderer::DestroyStorageBuffer(sphereBuffer);
		Renderer::DestroyStorageBuffer(triangleBuffer);
		Renderer::DestroyStorageBuffer(bvhNodeBuffer);
		Renderer::DestroyStorageBuffer(bvhPrimitiveBuffer);
		if (accumulationTexture.Handle != 0)
			glDeleteTextures(1, &accumulationTexture.Handle);

		return results;
	}

}
