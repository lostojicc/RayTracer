// M9 Benchmark tool (specs/benchmarking.md, specs/milestones.md M9).
//
// This is a separate executable from the interactive App: running it is
// always an explicit action, never something the interactive renderer can
// trigger on its own by loading a large model (specs/milestones.md M9
// acceptance criteria #8).
//
// Usage (see also ParseArgs() below):
//   Benchmark [--quick] [--skip-cpu] [--skip-gpu] [--output <path.csv>]
//
// Default behaviour: runs the full CPU benchmark (brute force vs BVH, over
// synthetic scenes at ~100/1,000/10,000/100,000 triangles plus the Stanford
// Bunny and Armadillo models) and the full GPU benchmark (same scenes,
// brute force vs BVH, with GPU brute force skipped above a configured
// triangle-count ceiling), then writes every measurement to a single CSV.
//
// --quick runs a much smaller subset with lower iteration counts, meant for
// smoke-testing the benchmark tool itself, not for producing report-quality
// numbers.

#include "BenchmarkResult.h"
#include "CpuBenchmark.h"
#include "CsvWriter.h"
#include "GpuBenchmark.h"
#include "GpuContext.h"
#include "Scenes.h"

#include "Core/Renderer/Shader.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

	struct CliOptions
	{
		bool Quick = false;
		bool SkipCpu = false;
		bool SkipGpu = false;
		std::filesystem::path OutputPath;
		bool ShowHelp = false;
	};

	void PrintUsage()
	{
		std::cout <<
			"RayTracer Benchmark (M9)\n"
			"\n"
			"Usage: Benchmark [options]\n"
			"\n"
			"Options:\n"
			"  --quick             Run a small, fast subset (smoke test; not report-quality).\n"
			"  --skip-cpu          Skip the CPU benchmark (brute force vs BVH traversal).\n"
			"  --skip-gpu          Skip the GPU benchmark (brute force vs BVH rendering).\n"
			"  --output <path>     Write the results CSV to <path> (default: Benchmark/Results/benchmark_results.csv).\n"
			"  --help              Show this message.\n";
	}

	CliOptions ParseArgs(int argc, char** argv)
	{
		CliOptions options;

		for (int i = 1; i < argc; i++)
		{
			const std::string arg = argv[i];

			if (arg == "--quick")
				options.Quick = true;
			else if (arg == "--skip-cpu")
				options.SkipCpu = true;
			else if (arg == "--skip-gpu")
				options.SkipGpu = true;
			else if (arg == "--help" || arg == "-h")
				options.ShowHelp = true;
			else if (arg == "--output" && i + 1 < argc)
				options.OutputPath = argv[++i];
			else
				std::cerr << "Benchmark: ignoring unrecognized argument '" << arg << "'\n";
		}

		return options;
	}

	std::string CurrentUtcTimestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t nowTimeT = std::chrono::system_clock::to_time_t(now);

		std::tm utcTime{};
#if defined(_WIN32)
		gmtime_s(&utcTime, &nowTimeT);
#else
		gmtime_r(&nowTimeT, &utcTime);
#endif

		std::ostringstream stream;
		stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
		return stream.str();
	}

	void PrintRow(const Benchmark::BenchmarkResult& row)
	{
		std::cout << "  [" << row.Processor << "] " << row.Scene
			<< " (" << row.TriangleCount << " tri) " << row.TraversalMode
			<< " -> status=" << row.Status;

		if (row.Status == "ok")
		{
			std::cout << " mean=" << row.MeasurementMeanMs << "ms"
				<< " min=" << row.MeasurementMinMs << "ms"
				<< " max=" << row.MeasurementMaxMs << "ms"
				<< " stddev=" << row.MeasurementStdDevMs << "ms"
				<< " (n=" << row.Iterations << ")";
		}

		if (!row.StatusReason.empty())
			std::cout << " [" << row.StatusReason << "]";

		if (row.CorrectnessChecked && !row.CorrectnessPassed)
			std::cout << " *** CORRECTNESS MISMATCH (" << row.MismatchCount.value_or(0) << " rays) ***";

		if (row.GpuPixelCheckPerformed && !row.GpuPixelCheckPassed)
			std::cout << " *** GPU PIXEL MISMATCH (" << row.GpuPixelMismatchCount.value_or(0) << " pixels) ***";

		std::cout << "\n";
	}

}

int main(int argc, char** argv)
{
	const CliOptions options = ParseArgs(argc, argv);

	if (options.ShowHelp)
	{
		PrintUsage();
		return 0;
	}

	const std::filesystem::path appDir = BENCHMARK_APP_DIR;
	const std::filesystem::path assetsDir = appDir / "Assets";
	const std::filesystem::path shaderPath = appDir / "Shaders" / "RayTracing.glsl";

	const std::filesystem::path outputPath = !options.OutputPath.empty()
		? options.OutputPath
		: std::filesystem::path(BENCHMARK_DEFAULT_OUTPUT_DIR) / "benchmark_results.csv";

	const std::string runTimestamp = CurrentUtcTimestamp();

	std::cout << "RayTracer Benchmark (M9) -- run " << runTimestamp << "\n";
	std::cout << "Mode: " << (options.Quick ? "quick (smoke test)" : "full") << "\n";
	std::cout << "Assets directory: " << assetsDir.string() << "\n";
	std::cout << "Output CSV: " << outputPath.string() << "\n\n";

	std::vector<Benchmark::BenchmarkResult> results;
	bool anyCorrectnessFailure = false;

	// --- Load every scene up front, so CPU and GPU benchmarks (if both run)
	// operate on exactly the same Core::Mesh instances -----------------------
	const std::vector<Benchmark::SceneDescriptor> descriptors = Benchmark::GetDefaultSceneDescriptors(options.Quick);
	std::vector<Benchmark::LoadedScene> scenes;
	scenes.reserve(descriptors.size());

	for (const Benchmark::SceneDescriptor& descriptor : descriptors)
	{
		Benchmark::LoadedScene scene = Benchmark::LoadScene(descriptor, assetsDir);

		if (!scene.Success)
		{
			std::cerr << "Benchmark: failed to load scene '" << descriptor.Name << "': " << scene.Error << "\n";

			Benchmark::BenchmarkResult errorRow;
			errorRow.Processor = "N/A";
			errorRow.Scene = descriptor.Name;
			errorRow.TraversalMode = "N/A";
			errorRow.Status = "error";
			errorRow.StatusReason = "scene load failed: " + scene.Error;
			errorRow.RunTimestampUtc = runTimestamp;
			results.push_back(errorRow);
			continue;
		}

		std::cout << "Loaded scene '" << scene.Name << "': " << scene.Mesh.GetTriangleCount()
			<< " triangles in " << scene.LoadOrGenerateMs << " ms\n";

		scenes.push_back(std::move(scene));
	}

	std::cout << "\n";

	// --- CPU benchmark ---------------------------------------------------------
	if (!options.SkipCpu)
	{
		Benchmark::CpuBenchmarkConfig cpuConfig;
		if (options.Quick)
		{
			cpuConfig.RayCount = 500;
			cpuConfig.WarmupIterations = 1;
			cpuConfig.MeasuredIterations = 3;
		}

		std::cout << "--- CPU benchmark (rays=" << cpuConfig.RayCount
			<< ", warmup=" << cpuConfig.WarmupIterations
			<< ", iterations=" << cpuConfig.MeasuredIterations
			<< ", leaf_size=" << cpuConfig.LeafSize << ") ---\n";

		for (const Benchmark::LoadedScene& scene : scenes)
		{
			std::vector<Benchmark::BenchmarkResult> rows =
				Benchmark::RunCpuBenchmark(scene.Name, scene.Mesh, scene.LoadOrGenerateMs, cpuConfig, runTimestamp);

			for (const Benchmark::BenchmarkResult& row : rows)
			{
				PrintRow(row);
				if (row.CorrectnessChecked && !row.CorrectnessPassed)
					anyCorrectnessFailure = true;
			}

			results.insert(results.end(), rows.begin(), rows.end());
		}

		std::cout << "\n";
	}
	else
	{
		std::cout << "--skip-cpu: CPU benchmark skipped.\n\n";
	}

	// --- GPU benchmark ---------------------------------------------------------
	if (!options.SkipGpu)
	{
		std::unique_ptr<Benchmark::GpuContext> gpuContext = Benchmark::GpuContext::Create();

		if (!gpuContext)
		{
			std::cerr << "Benchmark: could not create an OpenGL context; GPU benchmark skipped. "
				"Pass --skip-gpu to silence this message.\n\n";
		}
		else
		{
			const uint32_t computeShaderProgram = Renderer::CreateComputeShader(shaderPath);
			if (computeShaderProgram == (uint32_t)-1)
			{
				std::cerr << "Benchmark: failed to compile/link " << shaderPath.string() << "; GPU benchmark skipped.\n\n";
			}
			else
			{
				Benchmark::GpuBenchmarkConfig gpuConfig;
				if (options.Quick)
				{
					gpuConfig.ResolutionWidth = 96;
					gpuConfig.ResolutionHeight = 72;
					gpuConfig.WarmupIterations = 1;
					gpuConfig.MeasuredIterations = 3;
					gpuConfig.MaxBruteForceTriangles = 5000;
				}

				std::cout << "--- GPU benchmark (" << gpuConfig.ResolutionWidth << "x" << gpuConfig.ResolutionHeight
					<< ", spp=" << gpuConfig.SamplesPerPixel
					<< ", max_path_depth=" << gpuConfig.MaxPathDepthConstant
					<< ", warmup=" << gpuConfig.WarmupIterations
					<< ", iterations=" << gpuConfig.MeasuredIterations
					<< ", leaf_size=" << gpuConfig.LeafSize
					<< ", brute_force_ceiling=" << gpuConfig.MaxBruteForceTriangles << " triangles) ---\n";
				std::cout << "GL_RENDERER: " << gpuContext->GetInfo().Renderer << "\n";

				for (const Benchmark::LoadedScene& scene : scenes)
				{
					std::vector<Benchmark::BenchmarkResult> rows = Benchmark::RunGpuBenchmark(
						scene.Name, scene.Mesh, scene.LoadOrGenerateMs,
						computeShaderProgram, gpuContext->GetInfo(), gpuConfig, runTimestamp);

					for (const Benchmark::BenchmarkResult& row : rows)
					{
						PrintRow(row);
						if (row.GpuPixelCheckPerformed && !row.GpuPixelCheckPassed)
							anyCorrectnessFailure = true;
					}

					results.insert(results.end(), rows.begin(), rows.end());
				}

				std::cout << "\n";
			}
		}
	}
	else
	{
		std::cout << "--skip-gpu: GPU benchmark skipped.\n\n";
	}

	// --- Write results -----------------------------------------------------
	const bool wroteCsv = Benchmark::WriteResultsCsv(results, outputPath);

	uint32_t okCount = 0, skippedCount = 0, timeoutCount = 0, errorCount = 0;
	for (const Benchmark::BenchmarkResult& row : results)
	{
		if (row.Status == "ok") okCount++;
		else if (row.Status == "skipped") skippedCount++;
		else if (row.Status == "timeout") timeoutCount++;
		else errorCount++;
	}

	std::cout << "Summary: " << results.size() << " rows"
		<< " (ok=" << okCount << ", skipped=" << skippedCount
		<< ", timeout=" << timeoutCount << ", error=" << errorCount << ")\n";

	if (wroteCsv)
		std::cout << "Wrote " << outputPath.string() << "\n";

	if (anyCorrectnessFailure)
		std::cerr << "Benchmark: at least one correctness/pixel cross-check FAILED. See rows marked above.\n";

	if (!wroteCsv)
		return 1;

	return anyCorrectnessFailure ? 1 : 0;
}
