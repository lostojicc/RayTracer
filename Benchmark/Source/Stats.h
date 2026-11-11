#pragma once

#include <cstdint>
#include <vector>

namespace Benchmark {

	// Repeated-measurement summary (specs/benchmarking.md "Repetitions",
	// specs/milestones.md M9 acceptance criteria #5: "at least number of
	// measured iterations, minimum, maximum, mean, standard deviation").
	struct TimingStats
	{
		uint32_t SampleCount = 0;
		double MeanMs = 0.0;
		double MinMs = 0.0;
		double MaxMs = 0.0;
		double StdDevMs = 0.0;
	};

	// StdDevMs is the *sample* standard deviation (divide by N-1, Bessel's
	// correction), which is the conventional choice when the samples are
	// themselves a sample of a run's variability rather than the entire
	// population of interest. 0 when fewer than 2 samples are provided, since
	// a sample standard deviation is undefined for N < 2.
	TimingStats ComputeTimingStats(const std::vector<double>& samplesMs);

}
