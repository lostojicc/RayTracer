#include "TestFramework.h"

#include "Core/Renderer/BVH.h"
#include "Core/Renderer/TriangleTraversal.h"

#include <cmath>
#include <limits>
#include <random>
#include <vector>

// M6 (specs/bvh.md "CPU Traversal" / "Correctness Requirement"): the CPU BVH
// traversal (Core::IntersectBVH) must agree with the brute-force reference
// (Core::IntersectBruteForce, M3) for every ray, within floating-point
// tolerance. Every test below runs both functions on the same mesh/ray and
// compares hit/miss, TriangleId, distance, and surface data, rather than
// asserting a hard-coded expectation against only one of the two.

namespace {

	constexpr float Tolerance = 1e-5f;
	constexpr float Infinity = std::numeric_limits<float>::infinity();

	// One right triangle per entry, each lying in the plane z = zPlanes[i] and
	// wound so that its normal is +Z. Triangle ids follow the array order.
	// Mirrors TraversalTests.cpp's MakeStackedTriangles so the two traversal
	// paths are exercised on the same kind of geometry.
	Core::Mesh MakeStackedTriangles(const std::vector<float>& zPlanes)
	{
		std::vector<glm::vec3> vertices;
		std::vector<glm::uvec3> indices;

		for (float z : zPlanes)
		{
			const uint32_t base = (uint32_t)vertices.size();

			vertices.push_back(glm::vec3(0.0f, 0.0f, z));
			vertices.push_back(glm::vec3(1.0f, 0.0f, z));
			vertices.push_back(glm::vec3(0.0f, 1.0f, z));

			indices.push_back(glm::uvec3(base, base + 1, base + 2));
		}

		return Core::Mesh(std::move(vertices), std::move(indices));
	}

	// A single triangle per entry in a 'gridSize' x 'gridSize' grid of
	// coplanar, well-separated, non-degenerate triangles in the z = 0 plane,
	// with gaps between triangles so that a ray can pass through a cell's
	// AABB region without hitting geometry. Triangle ids follow array order.
	Core::Mesh MakeSparseGridMesh(int gridSize, float spacing = 3.0f)
	{
		std::vector<glm::vec3> vertices;
		std::vector<glm::uvec3> indices;

		for (int y = 0; y < gridSize; y++)
		{
			for (int x = 0; x < gridSize; x++)
			{
				const uint32_t base = (uint32_t)vertices.size();
				const float originX = (float)x * spacing;
				const float originY = (float)y * spacing;

				vertices.push_back(glm::vec3(originX, originY, 0.0f));
				vertices.push_back(glm::vec3(originX + 1.0f, originY, 0.0f));
				vertices.push_back(glm::vec3(originX, originY + 1.0f, 0.0f));

				indices.push_back(glm::uvec3(base, base + 1, base + 2));
			}
		}

		return Core::Mesh(std::move(vertices), std::move(indices));
	}

	// N random, non-degenerate-with-overwhelming-probability triangles with a
	// fixed seed (deterministic per specs/AGENTS.md / specs/milestones.md).
	Core::Mesh MakeRandomTriangleMesh(uint32_t triangleCount, uint32_t seed, float range = 10.0f)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> coordinate(-range, range);

		std::vector<glm::vec3> vertices;
		std::vector<glm::uvec3> indices;

		for (uint32_t i = 0; i < triangleCount; i++)
		{
			const uint32_t base = (uint32_t)vertices.size();
			for (int vertex = 0; vertex < 3; vertex++)
				vertices.push_back(glm::vec3(coordinate(rng), coordinate(rng), coordinate(rng)));

			indices.push_back(glm::uvec3(base, base + 1, base + 2));
		}

		return Core::Mesh(std::move(vertices), std::move(indices));
	}

	Core::Ray RayDownZ(float x = 0.25f, float y = 0.25f, float startZ = 5.0f)
	{
		return Core::Ray{ glm::vec3(x, y, startZ), glm::vec3(0.0f, 0.0f, -1.0f) };
	}

	// Runs both traversal paths on the same mesh/BVH/ray/tMin/tMax and asserts
	// they agree on every field specs/bvh.md's "Correctness Requirement"
	// cares about. Returns whether both hit, so callers can add scenario-
	// specific checks (e.g. which triangle should have won) on top.
	bool CompareBVHToBruteForce(const Core::Mesh& mesh, const Core::BVH& bvh, const Core::Ray& ray, float tMin, float tMax)
	{
		Core::HitRecord bruteForceRecord;
		Core::HitRecord bvhRecord;

		const bool bruteForceHit = Core::IntersectBruteForce(mesh, ray, tMin, tMax, bruteForceRecord);
		const bool bvhHit = Core::IntersectBVH(bvh, mesh, ray, tMin, tMax, bvhRecord);

		// Hit vs miss.
		CHECK(bvhHit == bruteForceHit);

		if (!bruteForceHit || !bvhHit)
			return false;

		// TriangleId.
		CHECK(bvhRecord.TriangleId == bruteForceRecord.TriangleId);

		// Hit distance, within tolerance.
		CHECK_NEAR(bvhRecord.T, bruteForceRecord.T, Tolerance);

		// Relevant hit-record data.
		CHECK_NEAR(bvhRecord.Position, bruteForceRecord.Position, Tolerance);
		CHECK_NEAR(bvhRecord.Normal, bruteForceRecord.Normal, Tolerance);
		CHECK_NEAR(bvhRecord.U, bruteForceRecord.U, Tolerance);
		CHECK_NEAR(bvhRecord.V, bruteForceRecord.V, Tolerance);
		CHECK(bvhRecord.FrontFace == bruteForceRecord.FrontFace);

		return true;
	}

}

// --- Empty BVH ---------------------------------------------------------

TEST_CASE(BVHTraversalOnEmptyMeshMisses)
{
	const Core::Mesh mesh;
	const Core::BVH bvh(mesh);
	CHECK(bvh.IsEmpty());

	Core::HitRecord record;
	CHECK(!Core::IntersectBVH(bvh, mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == Core::InvalidTriangleId);

	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 0.001f, Infinity);
}

// --- Single triangle -----------------------------------------------------

TEST_CASE(BVHTraversalFindsASingleTriangle)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f });
	const Core::BVH bvh(mesh);
	CHECK(!bvh.IsEmpty());
	CHECK(bvh.GetNodes().size() == 1); // one triangle -> single leaf root

	Core::HitRecord record;
	CHECK(Core::IntersectBVH(bvh, mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == 0);
	CHECK_NEAR(record.T, 5.0f, Tolerance);
	CHECK_NEAR(record.Position, glm::vec3(0.25f, 0.25f, 0.0f), Tolerance);
	CHECK_NEAR(record.Normal, glm::vec3(0.0f, 0.0f, 1.0f), Tolerance);
	CHECK(record.FrontFace);

	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 0.001f, Infinity);
}

TEST_CASE(BVHTraversalMissesASingleTriangleWhenRayIsOffset)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f });
	const Core::BVH bvh(mesh);

	// (5, 5) is well outside the triangle's AABB, let alone the triangle.
	const Core::Ray ray = RayDownZ(5.0f, 5.0f);

	Core::HitRecord record;
	CHECK(!Core::IntersectBVH(bvh, mesh, ray, 0.001f, Infinity, record));

	CompareBVHToBruteForce(mesh, bvh, ray, 0.001f, Infinity);
}

// --- Rays that miss the root AABB entirely --------------------------------

TEST_CASE(BVHTraversalRayMissesTheRootAABB)
{
	const Core::Mesh mesh = MakeRandomTriangleMesh(200, 111u, 5.0f);
	const Core::BVH bvh(mesh);
	CHECK(!bvh.IsEmpty());

	// The mesh occupies roughly [-5, 5]^3. A ray far outside on every axis,
	// travelling parallel to an axis, cannot intersect the root AABB.
	const Core::Ray ray{ glm::vec3(1000.0f, 1000.0f, 1000.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

	CHECK(!bvh.GetBounds().Intersects(ray, 0.001f, Infinity));

	Core::HitRecord record;
	CHECK(!Core::IntersectBVH(bvh, mesh, ray, 0.001f, Infinity, record));

	CompareBVHToBruteForce(mesh, bvh, ray, 0.001f, Infinity);
}

TEST_CASE(BVHTraversalRayPointingAwayFromRootMisses)
{
	const Core::Mesh mesh = MakeRandomTriangleMesh(200, 222u, 5.0f);
	const Core::BVH bvh(mesh);

	// Starts near the geometry but points away from it entirely.
	const Core::Ray ray{ glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.0f, 0.0f, 1.0f) };

	Core::HitRecord record;
	CHECK(!Core::IntersectBVH(bvh, mesh, ray, 0.001f, Infinity, record));

	CompareBVHToBruteForce(mesh, bvh, ray, 0.001f, Infinity);
}

// --- Rays that intersect internal nodes but miss all triangles -----------

TEST_CASE(BVHTraversalRayHitsInternalNodeBoundsButMissesEveryTriangle)
{
	// A sparse 4x4 grid: each triangle occupies a 1x1 cell inside a 3x3
	// spaced-out region, so the *root* (and several internal node) AABBs
	// span empty space between triangles that a ray can pass through
	// without hitting any triangle.
	const Core::Mesh mesh = MakeSparseGridMesh(4, 3.0f);
	const Core::BVH bvh(mesh, 1); // leaf size 1 forces internal nodes to exist
	CHECK(!bvh.IsEmpty());
	CHECK(bvh.GetStats().InternalCount > 0);

	// Aim at (1.5, 1.5), which is inside the root's overall bounds and
	// inside at least one internal node's bounds, but strictly between grid
	// cells: no triangle covers x in [1, 3) at y = 1.5 exactly at this spot.
	const Core::Ray ray{ glm::vec3(1.5f, 1.5f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

	// Sanity: the ray does hit the root bounds (otherwise this wouldn't be
	// testing what it claims to).
	CHECK(bvh.GetBounds().Intersects(ray, 0.001f, Infinity));

	Core::HitRecord record;
	CHECK(!Core::IntersectBVH(bvh, mesh, ray, 0.001f, Infinity, record));

	CompareBVHToBruteForce(mesh, bvh, ray, 0.001f, Infinity);
}

// --- Closest triangle among multiple candidates must win ------------------

TEST_CASE(BVHTraversalReturnsTheClosestHitAmongManyStackedTriangles)
{
	// Triangle 0 is nearest (t = 5), triangle 4 is farthest (t = 9). Every
	// triangle's AABB is pierced by this ray, so a correct implementation
	// must not stop at the first (or any non-nearest) hit.
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, -1.0f, -2.0f, -3.0f, -4.0f });

	for (uint32_t leafSize : { 1u, 2u, Core::BVH::DefaultLeafSize })
	{
		const Core::BVH bvh(mesh, leafSize);

		Core::HitRecord record;
		CHECK(Core::IntersectBVH(bvh, mesh, RayDownZ(), 0.001f, Infinity, record));
		CHECK(record.TriangleId == 0);
		CHECK_NEAR(record.T, 5.0f, Tolerance);

		CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 0.001f, Infinity);
	}
}

TEST_CASE(BVHTraversalReturnsTheClosestHitWhenNearestIsBuiltLast)
{
	// Reversed order: triangle 4 is now nearest. Combined with the reversed
	// construction order, this exercises both traversal order and whichever
	// leaf/child ends up "first" in the flattened array.
	const Core::Mesh mesh = MakeStackedTriangles({ -4.0f, -3.0f, -2.0f, -1.0f, 0.0f });
	const Core::BVH bvh(mesh, 1);

	Core::HitRecord record;
	CHECK(Core::IntersectBVH(bvh, mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == 4);
	CHECK_NEAR(record.T, 5.0f, Tolerance);

	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 0.001f, Infinity);
}

TEST_CASE(BVHTraversalClosestHitIsIndependentOfLeafSize)
{
	const Core::Mesh mesh = MakeRandomTriangleMesh(300, 4242u);

	Core::HitRecord reference;
	const bool referenceHit = Core::IntersectBruteForce(mesh, RayDownZ(0.0f, 0.0f, 50.0f), 0.001f, Infinity, reference);

	for (uint32_t leafSize : { 1u, 2u, 4u, 8u, 32u })
	{
		const Core::BVH bvh(mesh, leafSize);

		Core::HitRecord record;
		const bool bvhHit = Core::IntersectBVH(bvh, mesh, RayDownZ(0.0f, 0.0f, 50.0f), 0.001f, Infinity, record);

		CHECK(bvhHit == referenceHit);
		if (bvhHit && referenceHit)
		{
			CHECK(record.TriangleId == reference.TriangleId);
			CHECK_NEAR(record.T, reference.T, Tolerance);
		}
	}
}

// --- tMin / tMax behaviour --------------------------------------------------

TEST_CASE(BVHTraversalRespectsTMin)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, -1.0f, -2.0f });
	const Core::BVH bvh(mesh, 1);

	// Skips past triangle 0 (t = 5) and lands on triangle 1 (t = 6).
	Core::HitRecord record;
	CHECK(Core::IntersectBVH(bvh, mesh, RayDownZ(), 5.5f, Infinity, record));
	CHECK(record.TriangleId == 1);
	CHECK_NEAR(record.T, 6.0f, Tolerance);

	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 5.5f, Infinity);

	// tMin past every triangle's t: misses entirely.
	CHECK(!Core::IntersectBVH(bvh, mesh, RayDownZ(), 8.0f, Infinity, record));
	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 8.0f, Infinity);
}

TEST_CASE(BVHTraversalRespectsTMax)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, -1.0f, -2.0f });
	const Core::BVH bvh(mesh, 1);

	Core::HitRecord record;
	CHECK(Core::IntersectBVH(bvh, mesh, RayDownZ(), 0.001f, 5.5f, record));
	CHECK(record.TriangleId == 0);
	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 0.001f, 5.5f);

	// tMax before every triangle's t: misses entirely.
	CHECK(!Core::IntersectBVH(bvh, mesh, RayDownZ(), 0.001f, 4.0f, record));
	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 0.001f, 4.0f);
}

TEST_CASE(BVHTraversalTMinTMaxAreExclusiveBoundaries)
{
	// Matches Triangle::Intersect's 't <= tMin || t >= tMax' contract: a hit
	// exactly on tMin or tMax is rejected by both traversal paths equally.
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f });
	const Core::BVH bvh(mesh);

	Core::HitRecord record;
	CHECK(!Core::IntersectBVH(bvh, mesh, RayDownZ(), 5.0f, Infinity, record)); // t == tMin
	CHECK(!Core::IntersectBVH(bvh, mesh, RayDownZ(), 0.001f, 5.0f, record));   // t == tMax

	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 5.0f, Infinity);
	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 0.001f, 5.0f);
}

// --- Degenerate / edge cases already covered by the geometry layer --------

TEST_CASE(BVHTraversalSkipsDegenerateTriangles)
{
	// A collinear triangle sits in front of a valid one and must not shadow
	// it, matching TraversalTests.cpp's brute-force equivalent.
	std::vector<glm::vec3> vertices = {
		glm::vec3(0.0f, 0.0f, 1.0f),
		glm::vec3(1.0f, 0.0f, 1.0f),
		glm::vec3(2.0f, 0.0f, 1.0f),

		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)
	};

	std::vector<glm::uvec3> indices = {
		glm::uvec3(0, 1, 2),
		glm::uvec3(3, 4, 5)
	};

	const Core::Mesh mesh(std::move(vertices), std::move(indices));
	CHECK(mesh.GetTriangle(0).IsDegenerate());

	const Core::BVH bvh(mesh, 1);

	Core::HitRecord record;
	CHECK(Core::IntersectBVH(bvh, mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == 1);
	CHECK_NEAR(record.T, 5.0f, Tolerance);

	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 0.001f, Infinity);
}

TEST_CASE(BVHTraversalMixOfDegenerateAndValidTrianglesMatchesBruteForce)
{
	// A larger mix of valid, zero-area (coincident-vertex) and zero-area
	// (collinear) triangles, spread out enough to force several BVH splits.
	std::vector<glm::vec3> vertices;
	std::vector<glm::uvec3> indices;

	auto addTriangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c)
	{
		const uint32_t base = (uint32_t)vertices.size();
		vertices.push_back(a);
		vertices.push_back(b);
		vertices.push_back(c);
		indices.push_back(glm::uvec3(base, base + 1, base + 2));
	};

	for (int i = 0; i < 10; i++)
	{
		const float offset = (float)i * 2.0f;
		addTriangle(glm::vec3(offset, 0, 0), glm::vec3(offset + 1, 0, 0), glm::vec3(offset, 1, 0));

		// Zero-area: all three vertices identical.
		addTriangle(glm::vec3(offset, 5, 5), glm::vec3(offset, 5, 5), glm::vec3(offset, 5, 5));

		// Zero-area: collinear vertices.
		addTriangle(glm::vec3(offset, -5, -5), glm::vec3(offset + 1, -5, -5), glm::vec3(offset + 2, -5, -5));
	}

	const Core::Mesh mesh(std::move(vertices), std::move(indices));
	CHECK(mesh.IsValid());

	const Core::BVH bvh(mesh, 4);
	CHECK(!bvh.IsEmpty());

	// A dense sweep of rays, aimed squarely at each valid triangle's centroid
	// plus a handful of rays over the degenerate clusters, from both sides.
	uint32_t hitCount = 0;
	for (uint32_t triangleId = 0; triangleId < mesh.GetTriangleCount(); triangleId++)
	{
		const glm::vec3 centroid = mesh.GetTriangle(triangleId).Centroid();

		for (float startZ : { 10.0f, -10.0f })
		{
			const glm::vec3 direction = startZ > 0.0f ? glm::vec3(0, 0, -1) : glm::vec3(0, 0, 1);
			const Core::Ray ray{ glm::vec3(centroid.x, centroid.y, startZ), direction };

			if (CompareBVHToBruteForce(mesh, bvh, ray, 0.001f, Infinity))
				hitCount++;
		}
	}

	// Guards against the comparison passing vacuously: the 10 valid
	// triangles must each be reachable from at least one side.
	CHECK(hitCount >= 10);
}

TEST_CASE(BVHTraversalResolvesCoincidentTrianglesConsistentlyWithBruteForce)
{
	// Two identical triangles occupy the same leaf (2 <= default leaf size
	// 4), so both traversal paths visit them in the same array order and
	// must resolve the tie identically.
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, 0.0f });
	const Core::BVH bvh(mesh);
	CHECK(bvh.GetNodes().size() == 1);

	Core::HitRecord record;
	CHECK(Core::IntersectBVH(bvh, mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == 0);

	CompareBVHToBruteForce(mesh, bvh, RayDownZ(), 0.001f, Infinity);
}

TEST_CASE(BVHTraversalReportsBackFacingHitsLikeBruteForce)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f });
	const Core::BVH bvh(mesh);

	const Core::Ray fromBehind{ glm::vec3(0.25f, 0.25f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f) };

	Core::HitRecord record;
	CHECK(Core::IntersectBVH(bvh, mesh, fromBehind, 0.001f, Infinity, record));
	CHECK(record.TriangleId == 0);
	CHECK(!record.FrontFace);
	CHECK_NEAR(record.Normal, glm::vec3(0.0f, 0.0f, -1.0f), Tolerance);

	CompareBVHToBruteForce(mesh, bvh, fromBehind, 0.001f, Infinity);
}

// --- Broad deterministic cross-check --------------------------------------

TEST_CASE(BVHTraversalMatchesBruteForceOverManyDeterministicRays)
{
	// Fixed seed: reproducible, per specs/AGENTS.md / specs/milestones.md.
	std::mt19937 rng(20260920u);
	std::uniform_real_distribution<float> coordinate(-8.0f, 8.0f);

	std::vector<glm::vec3> vertices;
	std::vector<glm::uvec3> indices;

	constexpr uint32_t triangleCount = 400;
	for (uint32_t i = 0; i < triangleCount; i++)
	{
		const uint32_t base = (uint32_t)vertices.size();
		for (int vertex = 0; vertex < 3; vertex++)
			vertices.push_back(glm::vec3(coordinate(rng), coordinate(rng), coordinate(rng)));

		indices.push_back(glm::uvec3(base, base + 1, base + 2));
	}

	const Core::Mesh mesh(std::move(vertices), std::move(indices));
	CHECK(mesh.IsValid());

	// Cross-check at several leaf sizes so the comparison isn't tied to one
	// particular tree shape.
	for (uint32_t leafSize : { 1u, 2u, 4u, 8u, Core::BVH::DefaultLeafSize, 16u })
	{
		const Core::BVH bvh(mesh, leafSize);
		CHECK(!bvh.IsEmpty());

		std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
		std::uniform_real_distribution<float> height(-1.0f, 1.0f);
		std::uniform_real_distribution<float> tMinDist(0.0f, 3.0f);

		uint32_t rayCount = 0;
		uint32_t hitCount = 0;

		for (uint32_t i = 0; i < 1500; i++)
		{
			const float z = height(rng);
			const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
			const float theta = angle(rng);

			const glm::vec3 direction = glm::vec3(radius * std::cos(theta), radius * std::sin(theta), z);
			const glm::vec3 origin = -direction * 20.0f;

			// Occasionally vary tMin/tMax too, so the comparison exercises
			// that contract, not only the default [near-epsilon, infinity).
			const float tMin = tMinDist(rng);
			const float tMax = (i % 5 == 0) ? 25.0f : Infinity;

			const Core::Ray ray{ origin, direction };

			rayCount++;
			if (CompareBVHToBruteForce(mesh, bvh, ray, tMin, tMax))
				hitCount++;
		}

		CHECK(rayCount == 1500);
		// Guards against the comparison passing vacuously because nothing
		// was ever hit, for every leaf size under test.
		CHECK(hitCount > 100);
	}
}

// --- Cross-subtree exact-hit tie regression -------------------------------

namespace {

	// Builds a mesh containing exactly two large, non-coincident triangles
	// that both cross the same ray at a bit-identical distance, plus filler
	// triangles that pull their centroids to opposite ends of the X axis so
	// a small-leaf-size BVH places them in different leaves/subtrees.
	//
	// 'lowIdOnPositiveX' controls which side of the split the *lower*
	// TriangleId ends up on: false places it on the near/left side (id 0's
	// leaf tends to be visited first), true places it on the opposite side
	// (id 0's leaf tends to be visited last). Building both variants proves
	// the tie-break does not depend on which of the two tied triangles the
	// traversal happens to reach first.
	//
	// Returns the mesh; triangle id 0 and triangle id 4 are the tied pair,
	// both intersecting RayDownZ(0, 0, 10) at t = 5 (point (0, 0, 5)).
	Core::Mesh MakeCrossSubtreeExactTieMesh(bool lowIdOnPositiveX)
	{
		std::vector<glm::vec3> vertices;
		std::vector<glm::uvec3> indices;

		auto addTriangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c)
		{
			const uint32_t base = (uint32_t)vertices.size();
			vertices.push_back(a);
			vertices.push_back(b);
			vertices.push_back(c);
			indices.push_back(glm::uvec3(base, base + 1, base + 2));
		};

		// A right triangle with legs of very different lengths: a short leg
		// containing the origin nearby, and a long leg that drags the
		// centroid far out along +X. Wound so v0 is the "short corner" that
		// stays near the origin regardless of which side is mirrored below.
		const float farX = lowIdOnPositiveX ? 1000.0f : -1000.0f;
		const float nearX = lowIdOnPositiveX ? -1.0f : 1.0f;

		// id 0 ("A", the lower id): far corner on 'farX' side.
		addTriangle(glm::vec3(nearX, -1, 5), glm::vec3(farX, -1, 5), glm::vec3(nearX, 1, 5));

		// ids 1-3: filler triangles clustered on the same side as A, well
		// away from the ray, so A's leaf/subtree groups with them.
		for (int i = 0; i < 3; i++)
		{
			const float off = farX + (farX > 0.0f ? 1.0f : -1.0f) * (float)(10 + i * 10);
			addTriangle(glm::vec3(off, 50, 5), glm::vec3(off + 1, 50, 5), glm::vec3(off, 51, 5));
		}

		// id 4 ("B", the higher id): the mirror image of A across x = 0, so
		// it also contains (0, 0, 5) but its far corner (and centroid) sits
		// on the opposite side from A.
		addTriangle(glm::vec3(-nearX, -1, 5), glm::vec3(-farX, -1, 5), glm::vec3(-nearX, 1, 5));

		// ids 5-7: filler triangles clustered on B's side.
		for (int i = 0; i < 3; i++)
		{
			const float off = -farX + (farX > 0.0f ? -1.0f : 1.0f) * (float)(10 + i * 10);
			addTriangle(glm::vec3(off, -50, 5), glm::vec3(off - 1, -50, 5), glm::vec3(off, -51, 5));
		}

		return Core::Mesh(std::move(vertices), std::move(indices));
	}

}

TEST_CASE(BVHTraversalResolvesCrossSubtreeExactTieToTheLowerTriangleId)
{
	const Core::Ray ray = RayDownZ(0.0f, 0.0f, 10.0f);

	for (bool lowIdOnPositiveX : { false, true })
	{
		const Core::Mesh mesh = MakeCrossSubtreeExactTieMesh(lowIdOnPositiveX);
		CHECK(mesh.IsValid());
		CHECK(mesh.GetTriangleCount() == 8);

		// Sanity: both id 0 and id 4 individually hit at the exact same t,
		// directly against the triangle geometry, independent of any
		// traversal. If this ever stops holding (e.g. the fixture is
		// changed), the rest of the test would be checking nothing.
		Core::HitRecord directA;
		Core::HitRecord directB;
		CHECK(mesh.GetTriangle(0).Intersect(ray, 0.001f, Infinity, directA));
		CHECK(mesh.GetTriangle(4).Intersect(ray, 0.001f, Infinity, directB));
		CHECK(directA.T == directB.T); // bit-identical, not just within tolerance

		Core::HitRecord bruteForceRecord;
		CHECK(Core::IntersectBruteForce(mesh, ray, 0.001f, Infinity, bruteForceRecord));
		CHECK(bruteForceRecord.TriangleId == 0); // the reference always picks the lower id

		for (uint32_t leafSize : { 1u, 2u })
		{
			const Core::BVH bvh(mesh, leafSize);
			CHECK(!bvh.IsEmpty());

			Core::HitRecord bvhRecord;
			CHECK(Core::IntersectBVH(bvh, mesh, ray, 0.001f, Infinity, bvhRecord));

			// The regression: without the tie-break fix, the BVH could
			// return id 4 here on whichever 'lowIdOnPositiveX' variant
			// visits triangle B's subtree before triangle A's.
			CHECK(bvhRecord.TriangleId == bruteForceRecord.TriangleId);
			CHECK(bvhRecord.TriangleId == 0);
			CHECK_NEAR(bvhRecord.T, bruteForceRecord.T, Tolerance);

			CompareBVHToBruteForce(mesh, bvh, ray, 0.001f, Infinity);
		}
	}
}
