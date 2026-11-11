#pragma once

#include "BenchmarkResult.h"

#include "Core/Renderer/BVH.h"
#include "Core/Renderer/Mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Benchmark {

	struct CpuBenchmarkConfig
	{
		uint32_t RayCount = 3000;
		uint32_t WarmupIterations = 3;
		uint32_t MeasuredIterations = 12;
		uint32_t LeafSize = Core::BVH::DefaultLeafSize;
		uint32_t RaySeed = 0xC0FFEEu;
	};

	// Runs the M9 CPU benchmark for one already-loaded scene, producing
	// exactly two rows: "BruteForce" and "BVH" (specs/milestones.md M9
	// acceptance criteria #1/#2).
	//
	// Reuses Core::IntersectBruteForce and Core::IntersectBVH exactly as
	// implemented for M3/M6 -- no traversal algorithm is modified or
	// reimplemented here.
	//
	// 'modelLoadMs' is recorded on both rows for reference; it is measured by
	// the caller (Scenes::LoadScene) and is not part of either timed
	// traversal loop below.
	std::vector<BenchmarkResult> RunCpuBenchmark(
		const std::string& sceneName,
		const Core::Mesh& mesh,
		double modelLoadMs,
		const CpuBenchmarkConfig& config,
		const std::string& runTimestampUtc);

}
