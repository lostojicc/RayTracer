#include "Stats.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Benchmark {

	TimingStats ComputeTimingStats(const std::vector<double>& samplesMs)
	{
		TimingStats stats;
		stats.SampleCount = (uint32_t)samplesMs.size();

		if (samplesMs.empty())
			return stats;

		double sum = 0.0;
		double minValue = std::numeric_limits<double>::max();
		double maxValue = std::numeric_limits<double>::lowest();

		for (double sample : samplesMs)
		{
			sum += sample;
			minValue = std::min(minValue, sample);
			maxValue = std::max(maxValue, sample);
		}

		const double mean = sum / (double)samplesMs.size();

		double varianceSum = 0.0;
		for (double sample : samplesMs)
		{
			const double delta = sample - mean;
			varianceSum += delta * delta;
		}

		stats.MeanMs = mean;
		stats.MinMs = minValue;
		stats.MaxMs = maxValue;
		stats.StdDevMs = samplesMs.size() > 1
			? std::sqrt(varianceSum / (double)(samplesMs.size() - 1))
			: 0.0;

		return stats;
	}

}
