#pragma once

#include "BVH.h"
#include "Mesh.h"
#include "Ray.h"

namespace Core {

	// Brute-force closest-hit traversal over every triangle in a mesh.
	//
	// This is the correctness reference for the CPU BVH (specs/bvh.md). It is
	// deliberately the most obvious implementation possible, so keep it that way
	// rather than optimising it.
	//
	// The tMin/tMax contract matches Triangle::Intersect and hitSphere() in
	// App/Shaders/RayTracing.glsl: a hit is accepted only for tMin < t < tMax.
	// On a hit, record.TriangleId is the identifier of the closest triangle.
	//
	// The record is only written when the function returns true.
	//
	// Precondition: mesh.IsValid(). An invalid mesh trips an assert in
	// Mesh::GetTriangle.
	bool IntersectBruteForce(const Mesh& mesh, const Ray& ray, float tMin, float tMax, HitRecord& record);

	// Closest-hit traversal of a BVH built over 'mesh' (specs/bvh.md "CPU
	// Traversal"). Iterative: an explicit stack of node indices replaces
	// recursion, matching the GPU traversal in RayTracing.glsl.
	//
	// Must produce the same result as IntersectBruteForce(mesh, ray, tMin,
	// tMax, record) for the same mesh and ray, within floating-point
	// tolerance (specs/bvh.md "Correctness Requirement") -- same hit/miss,
	// same TriangleId, same distance and surface data. This is validated by
	// the Tests target (BVHTraversalTests.cpp), which cross-checks both
	// functions against each other on the same rays.
	//
	// The tMin/tMax contract and the "closest so far becomes the next tMax"
	// shrinking are identical to IntersectBruteForce; see its comments.
	//
	// Exact-tie determinism: IntersectBruteForce resolves an exact distance
	// tie between two different triangles to the lower TriangleId, as a side
	// effect of scanning ids in ascending order with a strict 't < closest'
	// shrink. This traversal's node/leaf visiting order is spatial (driven
	// by BVH structure and near/far child entry distance), not id order, so
	// it cannot rely on the same side effect -- two tied triangles can live
	// in different leaves that are visited in either order, independent of
	// which one has the lower id. IntersectBVH therefore reproduces the tie
	// rule *explicitly* in its leaf loop instead of inheriting it from
	// traversal order: strictly smaller t always wins; an exact tie
	// (candidate.T == closest) is won by the lower TriangleId, regardless of
	// which of the two is visited first. This is exercised directly by
	// BVHTraversalTests.cpp's cross-subtree exact-tie regression test.
	//
	// Precondition: 'bvh' was built over 'mesh' (same triangle ids/count).
	// An empty BVH (bvh.IsEmpty()) always misses.
	bool IntersectBVH(const BVH& bvh, const Mesh& mesh, const Ray& ray, float tMin, float tMax, HitRecord& record);

}
