#include "Scenes.h"

#include "Core/Renderer/ModelImporter.h"

#include <chrono>
#include <cmath>
#include <random>

namespace Benchmark {

	namespace {
		using Clock = std::chrono::steady_clock;

		double ElapsedMs(Clock::time_point start, Clock::time_point end)
		{
			return std::chrono::duration<double, std::milli>(end - start).count();
		}
	}

	Core::Mesh GenerateSyntheticMesh(uint32_t triangleCount, uint32_t seed)
	{
		std::vector<glm::vec3> vertices;
		std::vector<glm::uvec3> indices;
		vertices.reserve((size_t)triangleCount * 3);
		indices.reserve(triangleCount);

		std::mt19937 rng(seed);

		// Scale the scattering volume with triangle count (cube root, since
		// it is a volume) so density stays roughly constant across scales
		// instead of every scale packing the same fixed-size box ever more
		// densely. Values are unit-scale, consistent with the rest of the
		// project's documented epsilon tuning (Triangle.h: "tuned for...
		// unit-scale geometry").
		const float extent = std::max(2.0f, std::cbrt((float)std::max(triangleCount, 1u)) * 0.6f);
		std::uniform_real_distribution<float> centerDist(-extent, extent);
		std::uniform_real_distribution<float> offsetDist(-0.15f, 0.15f);

		for (uint32_t i = 0; i < triangleCount; i++)
		{
			const glm::vec3 center(centerDist(rng), centerDist(rng), centerDist(rng));

			const glm::vec3 v0 = center + glm::vec3(offsetDist(rng), offsetDist(rng), offsetDist(rng));
			const glm::vec3 v1 = center + glm::vec3(offsetDist(rng), offsetDist(rng), offsetDist(rng));
			const glm::vec3 v2 = center + glm::vec3(offsetDist(rng), offsetDist(rng), offsetDist(rng));

			const uint32_t base = (uint32_t)vertices.size();
			vertices.push_back(v0);
			vertices.push_back(v1);
			vertices.push_back(v2);
			indices.emplace_back(base, base + 1, base + 2);
		}

		return Core::Mesh(std::move(vertices), std::move(indices));
	}

	std::vector<SceneDescriptor> GetDefaultSceneDescriptors(bool quick)
	{
		std::vector<SceneDescriptor> scenes;

		if (quick)
		{
			scenes.push_back({ "Synthetic_100", true, 100, 1001, "" });
			scenes.push_back({ "Synthetic_1000", true, 1000, 1002, "" });
			scenes.push_back({ "Bunny", false, 0, 0, "bunny.obj" });
			return scenes;
		}

		scenes.push_back({ "Synthetic_100", true, 100, 1001, "" });
		scenes.push_back({ "Synthetic_1000", true, 1000, 1002, "" });
		scenes.push_back({ "Synthetic_10000", true, 10000, 1003, "" });
		scenes.push_back({ "Synthetic_100000", true, 100000, 1004, "" });
		scenes.push_back({ "Bunny", false, 0, 0, "bunny.obj" });
		scenes.push_back({ "Armadillo", false, 0, 0, "armadillo.obj" });

		return scenes;
	}

	LoadedScene LoadScene(const SceneDescriptor& descriptor, const std::filesystem::path& assetsDir)
	{
		LoadedScene result;
		result.Name = descriptor.Name;
		result.IsSynthetic = descriptor.IsSynthetic;

		if (descriptor.IsSynthetic)
		{
			const auto start = Clock::now();
			result.Mesh = GenerateSyntheticMesh(descriptor.SyntheticTriangleCount, descriptor.SyntheticSeed);
			const auto end = Clock::now();

			result.LoadOrGenerateMs = ElapsedMs(start, end);
			result.Success = true;
			return result;
		}

		const std::filesystem::path fullPath = assetsDir / descriptor.SourceFileName;

		const auto start = Clock::now();
		Core::ModelImportResult importResult = Core::ImportObjFile(fullPath);
		const auto end = Clock::now();

		result.LoadOrGenerateMs = ElapsedMs(start, end);
		result.Success = importResult.Success;
		result.Error = importResult.Error;

		if (importResult.Success)
			result.Mesh = std::move(importResult.ImportedMesh);

		return result;
	}

}
