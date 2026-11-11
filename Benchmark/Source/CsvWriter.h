#pragma once

#include "BenchmarkResult.h"

#include <filesystem>
#include <vector>

namespace Benchmark {

	// Writes every result row to a single CSV file (specs/benchmarking.md
	// "Benchmark Output", specs/milestones.md M9 acceptance criteria #11:
	// "Results are emitted in a structured form suitable for later
	// analysis... enough information to reconstruct tables/plots... without
	// manually copying values from console output").
	//
	// Returns false (and prints an error) if the file could not be opened for
	// writing.
	bool WriteResultsCsv(const std::vector<BenchmarkResult>& results, const std::filesystem::path& path);

}
