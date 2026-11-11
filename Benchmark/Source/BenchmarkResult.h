#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Benchmark {

	// One row of the M9 benchmark CSV output (specs/benchmarking.md
	// "Benchmark Output", specs/milestones.md M9 acceptance criteria #6/#11).
	//
	// A single schema is shared by CPU and GPU rows so every measurement can
	// be loaded into one table for the later M10 analysis. Fields that do not
	// apply to a given row (e.g. resolution for a CPU row, BVH stats for a
	// BruteForce row) are left unset and are written as the literal string
	// "NA" by CsvWriter, never as 0, so a missing measurement can never be
	// misread as a real zero measurement.
	struct BenchmarkResult
	{
		// --- Identity / controlled configuration -----------------------------
		std::string Processor;     // "CPU" | "GPU"
		std::string Scene;         // e.g. "Synthetic_100", "Bunny", "Armadillo"
		uint32_t TriangleCount = 0;
		std::string TraversalMode; // "BruteForce" | "BVH"
		std::optional<uint32_t> LeafSize; // BVH rows only

		std::optional<uint32_t> ResolutionWidth;  // GPU rows only
		std::optional<uint32_t> ResolutionHeight; // GPU rows only
		std::optional<uint32_t> SamplesPerPixel;  // GPU rows only
		std::optional<uint32_t> MaxPathDepth;     // GPU rows only; documented
		                                           // shader constant (see
		                                           // Main.cpp), not currently
		                                           // a runtime uniform.

		std::optional<uint32_t> RayCount; // CPU rows only: number of
		                                   // deterministic test rays used.

		uint32_t Iterations = 0;       // measured iterations actually completed
		uint32_t WarmupIterations = 0; // requested warm-up iterations

		// --- Timing categories (specs/benchmarking.md "Measurements",
		// specs/milestones.md M9 acceptance criteria #4) ----------------------
		std::optional<double> ModelLoadMs;             // import/generation time
		std::optional<double> BvhBuildMs;               // CPU BVH construction
		std::optional<double> GpuTriangleUploadMs;      // triangle SSBO upload
		std::optional<double> GpuBvhNodeUploadMs;       // BVH node SSBO upload
		std::optional<double> GpuBvhPrimitiveUploadMs;  // BVH primitive-id SSBO upload

		// The primary measurement for this row: CPU traversal time (one full
		// sweep over every test ray) for CPU rows, or completed-GPU-work
		// render time for one dispatched frame for GPU rows.
		double MeasurementMeanMs = 0.0;
		double MeasurementMinMs = 0.0;
		double MeasurementMaxMs = 0.0;
		double MeasurementStdDevMs = 0.0; // sample stddev (n-1); 0 if Iterations < 2

		// --- BVH structure statistics (specs/bvh.md "Statistics") -----------
		std::optional<uint32_t> NodeCount;
		std::optional<uint32_t> LeafCount;
		std::optional<uint32_t> MaxDepth;
		std::optional<uint32_t> MinLeafSize;
		std::optional<uint32_t> MaxLeafSize;
		std::optional<double>   AverageLeafSize;

		// --- CPU correctness (specs/bvh.md "Correctness Requirement",
		// specs/milestones.md M9 acceptance criteria #2) ----------------------
		bool CorrectnessChecked = false;
		bool CorrectnessPassed = false;
		std::optional<uint32_t> MismatchCount;
		std::optional<uint32_t> TestedRayCount;

		// --- GPU pixel-level cross-check between the GPU BruteForce and GPU
		// BVH renders of the same scene/camera/frame (specs/gpu-integration.md
		// "GPU Validation"). Filled in on both the BruteForce and BVH rows for
		// the same scene when both renders completed successfully.
		bool GpuPixelCheckPerformed = false;
		bool GpuPixelCheckPassed = false;
		std::optional<double>   GpuPixelMaxAbsDiff;
		std::optional<uint32_t> GpuPixelMismatchCount;

		// --- Execution status -------------------------------------------------
		// "ok"      - measurement completed normally.
		// "skipped" - not attempted at all (e.g. GPU brute force above the
		//             configured triangle ceiling; specs/milestones.md M9
		//             acceptance criteria #7).
		// "timeout" - attempted, but a bounded wait for GPU completion
		//             expired before the work finished; partial iteration
		//             results (if any completed) are still reported.
		// "error"   - an unexpected failure (e.g. model import failure).
		std::string Status = "ok";
		std::string StatusReason;

		// --- Reproducibility fields (specs/benchmarking.md "Reproducibility
		// Fields") --------------------------------------------------------------
		std::optional<std::string> GLVendor;
		std::optional<std::string> GLRenderer;
		std::optional<std::string> GLVersion;
		std::string RunTimestampUtc;
	};

}
