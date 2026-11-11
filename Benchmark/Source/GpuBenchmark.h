#pragma once

#include "BenchmarkResult.h"
#include "GpuContext.h"

#include "Core/Renderer/BVH.h"
#include "Core/Renderer/Mesh.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace Benchmark {

	struct GpuBenchmarkConfig
	{
		uint32_t ResolutionWidth = 320;
		uint32_t ResolutionHeight = 240;
		uint32_t SamplesPerPixel = 1;

		// Documented shader constant (App/Shaders/RayTracing.glsl's trace()
		// hard-codes 'for (int i = 0; i < 50; i++)'). Recorded on every GPU
		// row rather than assumed (specs/benchmarking.md: "Rendering
		// parameters that are currently hardcoded in the shader, such as the
		// bounce limit... must be made explicit and recorded rather than
		// assumed"). Deliberately NOT wired up as a new uniform for M9: doing
		// so would require editing RayTracing.glsl, which this milestone
		// avoids (see Main.cpp and specs/milestones.md's M9 entry).
		uint32_t MaxPathDepthConstant = 50;

		uint32_t WarmupIterations = 3;
		uint32_t MeasuredIterations = 10;
		uint32_t LeafSize = Core::BVH::DefaultLeafSize;

		// GPU brute-force traversal tests every triangle for every ray with
		// no early-out (specs/gpu-integration.md "Brute-Force GPU
		// Traversal"). Above this triangle count, GPU brute force is skipped
		// outright -- before any GPU work is submitted -- rather than risking
		// a run that takes an impractically long time (specs/milestones.md
		// M9 acceptance criteria #7). GPU BVH has no such ceiling.
		uint32_t MaxBruteForceTriangles = 20000;

		// Safety net independent of the ceiling above: the maximum time this
		// process will *wait* (polling, not blocking) for a single dispatch's
		// GPU-completion query to become available before giving up on that
		// configuration and reporting a timeout. This bounds the benchmark
		// process's own wall-clock behaviour even if the ceiling above is
		// ever misconfigured or an unexpectedly slow driver/hardware
		// combination is encountered; it does not, and cannot, cancel
		// already-submitted GPU work (specs/milestones.md M9 acceptance
		// criteria: "cannot accidentally hang the interactive application
		// indefinitely" -- this is the benchmark's own equivalent guarantee
		// for itself).
		std::chrono::milliseconds MaxWaitPerIteration{ 15000 };
	};

	// Runs the M9 GPU benchmark for one already-loaded scene, producing one
	// or two rows ("BruteForce" is omitted -- recorded as status "skipped" --
	// when the triangle count exceeds MaxBruteForceTriangles).
	//
	// Reuses the existing compute shader (App/Shaders/RayTracing.glsl,
	// compiled once by the caller) and its existing u_TraversalMode uniform
	// exactly as implemented for M7/M8; no shader code is modified.
	//
	// 'computeShaderProgram' must be a successfully linked program from
	// Renderer::CreateComputeShader() using App/Shaders/RayTracing.glsl.
	std::vector<BenchmarkResult> RunGpuBenchmark(
		const std::string& sceneName,
		const Core::Mesh& mesh,
		double modelLoadMs,
		uint32_t computeShaderProgram,
		const GpuContextInfo& glInfo,
		const GpuBenchmarkConfig& config,
		const std::string& runTimestampUtc);

}
