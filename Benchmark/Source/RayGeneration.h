#pragma once

#include "Core/Renderer/AABB.h"
#include "Core/Renderer/Ray.h"

#include <cstdint>
#include <vector>

namespace Benchmark {

	// Deterministically generates 'rayCount' rays aimed generally at (but not
	// exactly through the center of) 'bounds', from random points on an
	// enclosing sphere. This exercises both hits and near/complete misses
	// against the same scene (specs/geometry.md's correctness requirements
	// list "Triangle hit"/"Triangle miss" together), independent of the
	// contents of the mesh -- only its bounds are used, so this is scene-data
	// independent and safe to reuse for meshes of any triangle count.
	//
	// Determinism: seeded only by 'seed' and 'bounds'; the same arguments
	// always produce bit-identical rays (specs/milestones.md M9 acceptance
	// criteria #9, "deterministic scene/ray generation").
	//
	// This is ray *generation*, not a benchmark measurement: callers must
	// exclude the time spent here from any traversal timing (specs/
	// milestones.md M9 acceptance criteria #2, "The benchmark does not
	// measure scene generation or unrelated setup as traversal time").
	std::vector<Core::Ray> GenerateTestRays(const Core::AABB& bounds, uint32_t rayCount, uint32_t seed);

}
