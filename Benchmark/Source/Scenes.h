#pragma once

#include "Core/Renderer/Mesh.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Benchmark {

	// Describes one benchmark scene without loading it yet (specs/
	// benchmarking.md "Scene Complexity": "~100 / ~1,000 / ~10,000 /
	// ~100,000 triangles" plus "a larger real-world model", and specs/
	// milestones.md M9 acceptance criteria #7's "small synthetic scene /
	// medium real-world mesh / large real-world mesh").
	struct SceneDescriptor
	{
		std::string Name;
		bool IsSynthetic = true;

		// Synthetic scenes: the requested triangle count (actual count is
		// always exactly this, since the generator emits one independent
		// triangle per requested count).
		uint32_t SyntheticTriangleCount = 0;
		uint32_t SyntheticSeed = 0;

		// Real-world scenes: source file, relative to the assets directory
		// passed to LoadScene().
		std::string SourceFileName;
	};

	// The default scene list, spanning every scale requested by
	// specs/benchmarking.md: ~100 / ~1,000 / ~10,000 / ~100,000 synthetic
	// triangles, plus the two real-world OBJ models already vendored under
	// App/Assets (Stanford Bunny, ~69,630 triangles; Stanford Armadillo,
	// ~212,574 triangles), which double as the required "medium" and "large"
	// real-world meshes.
	//
	// 'quick' selects a much smaller subset with lower triangle counts, used
	// for fast smoke-testing of the benchmark tool itself (see Main.cpp
	// --quick); it is not intended to produce report-quality measurements.
	std::vector<SceneDescriptor> GetDefaultSceneDescriptors(bool quick);

	struct LoadedScene
	{
		std::string Name;
		bool IsSynthetic = true;
		Core::Mesh Mesh;
		double LoadOrGenerateMs = 0.0; // excluded from all traversal/render timing
		bool Success = false;
		std::string Error;
	};

	// Loads (real models) or deterministically generates (synthetic scenes)
	// the mesh described by 'descriptor'. Timing starts immediately before
	// the importer/generator call and stops immediately after, so
	// LoadOrGenerateMs never includes anything else (specs/milestones.md M9
	// acceptance criteria #2: "The benchmark does not measure scene
	// generation or unrelated setup as traversal time").
	LoadedScene LoadScene(const SceneDescriptor& descriptor, const std::filesystem::path& assetsDir);

	// Deterministically generates a synthetic mesh of exactly
	// 'triangleCount' independent triangles (specs/bvh.md's BVH construction
	// operates over whatever triangle soup a Core::Mesh presents; this
	// generator does not need to produce connected/manifold geometry).
	//
	// Determinism: seeded by 'seed' only; calling this twice with the same
	// arguments produces bit-identical vertex data (specs/milestones.md M9
	// acceptance criteria #9, "Reproducibility").
	Core::Mesh GenerateSyntheticMesh(uint32_t triangleCount, uint32_t seed);

}
