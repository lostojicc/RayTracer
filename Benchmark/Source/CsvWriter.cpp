#include "CsvWriter.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace Benchmark {

	namespace {

		// RFC4180-style field quoting. This matters here: GL_RENDERER strings
		// routinely contain literal commas, and StatusReason is free text that
		// may also contain commas. Any field containing a comma, double quote,
		// or newline is wrapped in double quotes, with internal double quotes
		// doubled.
		std::string EscapeCsvField(const std::string& field)
		{
			const bool needsQuoting =
				field.find(',') != std::string::npos ||
				field.find('"') != std::string::npos ||
				field.find('\n') != std::string::npos ||
				field.find('\r') != std::string::npos;

			if (!needsQuoting)
				return field;

			std::string escaped = "\"";
			for (char c : field)
			{
				if (c == '"')
					escaped += "\"\"";
				else
					escaped += c;
			}
			escaped += "\"";
			return escaped;
		}

		std::string FormatDouble(double value)
		{
			std::ostringstream stream;
			stream.precision(6);
			stream << std::fixed << value;
			return stream.str();
		}

		std::string Field(const std::string& value) { return EscapeCsvField(value); }
		std::string Field(bool value) { return value ? "true" : "false"; }
		std::string Field(uint32_t value) { return std::to_string(value); }
		std::string Field(double value) { return FormatDouble(value); }

		template<typename T>
		std::string Field(const std::optional<T>& value)
		{
			if (!value.has_value())
				return "NA";
			return Field(*value);
		}

		constexpr const char* kHeader =
			"processor,scene,triangle_count,traversal_mode,leaf_size,"
			"resolution_width,resolution_height,samples_per_pixel,max_path_depth,"
			"ray_count,iterations,warmup_iterations,"
			"model_load_ms,bvh_build_ms,"
			"gpu_triangle_upload_ms,gpu_bvh_node_upload_ms,gpu_bvh_primitive_upload_ms,"
			"measurement_mean_ms,measurement_min_ms,measurement_max_ms,measurement_stddev_ms,"
			"node_count,leaf_count,max_depth,min_leaf_size,max_leaf_size,average_leaf_size,"
			"correctness_checked,correctness_passed,mismatch_count,tested_ray_count,"
			"gpu_pixel_check_performed,gpu_pixel_check_passed,gpu_pixel_max_abs_diff,gpu_pixel_mismatch_count,"
			"status,status_reason,"
			"gl_vendor,gl_renderer,gl_version,run_timestamp_utc";

	}

	bool WriteResultsCsv(const std::vector<BenchmarkResult>& results, const std::filesystem::path& path)
	{
		std::error_code errorCode;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), errorCode);

		std::ofstream file(path, std::ios::out | std::ios::trunc);
		if (!file.is_open())
		{
			std::cerr << "Benchmark: failed to open output CSV for writing: " << path.string() << "\n";
			return false;
		}

		file << kHeader << "\n";

		for (const BenchmarkResult& r : results)
		{
			file
				<< Field(r.Processor) << ','
				<< Field(r.Scene) << ','
				<< Field(r.TriangleCount) << ','
				<< Field(r.TraversalMode) << ','
				<< Field(r.LeafSize) << ','
				<< Field(r.ResolutionWidth) << ','
				<< Field(r.ResolutionHeight) << ','
				<< Field(r.SamplesPerPixel) << ','
				<< Field(r.MaxPathDepth) << ','
				<< Field(r.RayCount) << ','
				<< Field(r.Iterations) << ','
				<< Field(r.WarmupIterations) << ','
				<< Field(r.ModelLoadMs) << ','
				<< Field(r.BvhBuildMs) << ','
				<< Field(r.GpuTriangleUploadMs) << ','
				<< Field(r.GpuBvhNodeUploadMs) << ','
				<< Field(r.GpuBvhPrimitiveUploadMs) << ','
				<< Field(r.MeasurementMeanMs) << ','
				<< Field(r.MeasurementMinMs) << ','
				<< Field(r.MeasurementMaxMs) << ','
				<< Field(r.MeasurementStdDevMs) << ','
				<< Field(r.NodeCount) << ','
				<< Field(r.LeafCount) << ','
				<< Field(r.MaxDepth) << ','
				<< Field(r.MinLeafSize) << ','
				<< Field(r.MaxLeafSize) << ','
				<< Field(r.AverageLeafSize) << ','
				<< Field(r.CorrectnessChecked) << ','
				<< Field(r.CorrectnessPassed) << ','
				<< Field(r.MismatchCount) << ','
				<< Field(r.TestedRayCount) << ','
				<< Field(r.GpuPixelCheckPerformed) << ','
				<< Field(r.GpuPixelCheckPassed) << ','
				<< Field(r.GpuPixelMaxAbsDiff) << ','
				<< Field(r.GpuPixelMismatchCount) << ','
				<< Field(r.Status) << ','
				<< Field(r.StatusReason) << ','
				<< Field(r.GLVendor) << ','
				<< Field(r.GLRenderer) << ','
				<< Field(r.GLVersion) << ','
				<< Field(r.RunTimestampUtc)
				<< "\n";
		}

		return true;
	}

}
