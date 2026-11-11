#include "TestFramework.h"

#include "Core/Renderer/TriangleTraversal.h"

#include <algorithm>
#include <limits>
#include <random>
#include <vector>

namespace {

	constexpr float Tolerance = 1e-5f;
	constexpr float Infinity = std::numeric_limits<float>::infinity();

	// One right triangle per entry, each lying in the plane z = zPlanes[i] and
	// wound so that its normal is +Z. Triangle ids follow the array order.
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

	// Travels along -Z from z = 5, so a triangle in plane z hits at t = 5 - z.
	Core::Ray RayDownZ(float x = 0.25f, float y = 0.25f)
	{
		return Core::Ray{ glm::vec3(x, y, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };
	}

	// Independent closest-hit reference: gather every intersection over the full
	// t range, then sort. Structurally different from the incremental tMax
	// shrinking in IntersectBruteForce, so the two agreeing is meaningful.
	//
	// stable_sort keeps insertion (id) order for equal distances, matching how
	// the shrinking scan resolves ties.
	bool ClosestHitBySorting(const Core::Mesh& mesh, const Core::Ray& ray, float tMin, float tMax, Core::HitRecord& record)
	{
		std::vector<Core::HitRecord> hits;

		for (uint32_t triangleId = 0; triangleId < mesh.GetTriangleCount(); triangleId++)
		{
			Core::HitRecord hit;
			if (!mesh.GetTriangle(triangleId).Intersect(ray, tMin, tMax, hit))
				continue;

			hit.TriangleId = triangleId;
			hits.push_back(hit);
		}

		if (hits.empty())
			return false;

		std::stable_sort(hits.begin(), hits.end(),
			[](const Core::HitRecord& a, const Core::HitRecord& b) { return a.T < b.T; });

		record = hits.front();
		return true;
	}

}

TEST_CASE(BruteForceOnEmptyMeshMisses)
{
	const Core::Mesh mesh;

	Core::HitRecord record;
	CHECK(!Core::IntersectBruteForce(mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == Core::InvalidTriangleId);
}

TEST_CASE(BruteForceFindsASingleTriangle)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f });

	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == 0);
	CHECK_NEAR(record.T, 5.0f, Tolerance);
	CHECK_NEAR(record.Position, glm::vec3(0.25f, 0.25f, 0.0f), Tolerance);
	CHECK_NEAR(record.Normal, glm::vec3(0.0f, 0.0f, 1.0f), Tolerance);
	CHECK(record.FrontFace);
}

TEST_CASE(BruteForceMissesWhenNoTriangleIsHit)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, -1.0f, -2.0f });

	Core::HitRecord record;
	CHECK(!Core::IntersectBruteForce(mesh, RayDownZ(5.0f, 5.0f), 0.001f, Infinity, record));
}

TEST_CASE(BruteForceReturnsTheClosestHitWhenNearestIsFirst)
{
	// Triangle 0 is nearest (t = 5), triangle 2 is farthest (t = 7).
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, -1.0f, -2.0f });

	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == 0);
	CHECK_NEAR(record.T, 5.0f, Tolerance);
}

TEST_CASE(BruteForceReturnsTheClosestHitWhenNearestIsLast)
{
	// Reversed: triangle 2 is now the nearest, so the scan must overwrite
	// earlier hits rather than keeping the first one it finds.
	const Core::Mesh mesh = MakeStackedTriangles({ -2.0f, -1.0f, 0.0f });

	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == 2);
	CHECK_NEAR(record.T, 5.0f, Tolerance);
}

TEST_CASE(BruteForceResultIsIndependentOfTriangleOrder)
{
	const Core::Mesh ascending = MakeStackedTriangles({ 0.0f, -1.0f, -2.0f });
	const Core::Mesh descending = MakeStackedTriangles({ -2.0f, -1.0f, 0.0f });

	Core::HitRecord ascendingRecord;
	Core::HitRecord descendingRecord;

	CHECK(Core::IntersectBruteForce(ascending, RayDownZ(), 0.001f, Infinity, ascendingRecord));
	CHECK(Core::IntersectBruteForce(descending, RayDownZ(), 0.001f, Infinity, descendingRecord));

	// Ids differ because the meshes order the same geometry differently, but
	// the geometric result must be identical.
	CHECK_NEAR(ascendingRecord.T, descendingRecord.T, Tolerance);
	CHECK_NEAR(ascendingRecord.Position, descendingRecord.Position, Tolerance);
	CHECK_NEAR(ascendingRecord.Normal, descendingRecord.Normal, Tolerance);
}

TEST_CASE(BruteForceRespectsTMin)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, -1.0f, -2.0f });

	// Skips past triangle 0 (t = 5) and lands on triangle 1 (t = 6).
	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(mesh, RayDownZ(), 5.5f, Infinity, record));
	CHECK(record.TriangleId == 1);
	CHECK_NEAR(record.T, 6.0f, Tolerance);

	CHECK(!Core::IntersectBruteForce(mesh, RayDownZ(), 8.0f, Infinity, record));
}

TEST_CASE(BruteForceRespectsTMax)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, -1.0f, -2.0f });

	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(mesh, RayDownZ(), 0.001f, 5.5f, record));
	CHECK(record.TriangleId == 0);

	CHECK(!Core::IntersectBruteForce(mesh, RayDownZ(), 0.001f, 4.0f, record));
}

TEST_CASE(BruteForceSkipsDegenerateTriangles)
{
	// A collinear triangle sits in front of a valid one and must not shadow it.
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

	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == 1);
	CHECK_NEAR(record.T, 5.0f, Tolerance);
}

TEST_CASE(BruteForceResolvesCoincidentTrianglesToTheLowestId)
{
	// Two identical triangles. The strict 't < closest' shrink means the second
	// cannot displace the first, so the result is deterministic.
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, 0.0f });

	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(mesh, RayDownZ(), 0.001f, Infinity, record));
	CHECK(record.TriangleId == 0);
	CHECK_NEAR(record.T, 5.0f, Tolerance);
}

TEST_CASE(BruteForceReportsBackFacingHits)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f });

	const Core::Ray fromBehind{ glm::vec3(0.25f, 0.25f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f) };

	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(mesh, fromBehind, 0.001f, Infinity, record));
	CHECK(record.TriangleId == 0);
	CHECK(!record.FrontFace);
	CHECK_NEAR(record.Normal, glm::vec3(0.0f, 0.0f, -1.0f), Tolerance);
}

TEST_CASE(BruteForceAgreesWithDirectTriangleIntersection)
{
	const Core::Mesh mesh = MakeStackedTriangles({ 0.0f, -1.0f, -2.0f });
	const Core::Ray ray = RayDownZ(0.1f, 0.7f);

	Core::HitRecord traversalRecord;
	CHECK(Core::IntersectBruteForce(mesh, ray, 0.001f, Infinity, traversalRecord));

	Core::HitRecord directRecord;
	CHECK(mesh.GetTriangle(traversalRecord.TriangleId).Intersect(ray, 0.001f, Infinity, directRecord));

	CHECK_NEAR(traversalRecord.T, directRecord.T, Tolerance);
	CHECK_NEAR(traversalRecord.Position, directRecord.Position, Tolerance);
	CHECK_NEAR(traversalRecord.Normal, directRecord.Normal, Tolerance);
	CHECK_NEAR(traversalRecord.U, directRecord.U, Tolerance);
	CHECK_NEAR(traversalRecord.V, directRecord.V, Tolerance);
	CHECK(traversalRecord.FrontFace == directRecord.FrontFace);
}

TEST_CASE(BruteForceScansEveryTriangleAcrossAGrid)
{
	// A 4x4 grid of coplanar triangles, each hit by its own ray. Verifies that
	// the scan can reach every id, not just the first or last.
	std::vector<glm::vec3> vertices;
	std::vector<glm::uvec3> indices;

	for (int y = 0; y < 4; y++)
	{
		for (int x = 0; x < 4; x++)
		{
			const uint32_t base = (uint32_t)vertices.size();
			const float originX = (float)x;
			const float originY = (float)y;

			vertices.push_back(glm::vec3(originX, originY, 0.0f));
			vertices.push_back(glm::vec3(originX + 1.0f, originY, 0.0f));
			vertices.push_back(glm::vec3(originX, originY + 1.0f, 0.0f));

			indices.push_back(glm::uvec3(base, base + 1, base + 2));
		}
	}

	const Core::Mesh mesh(std::move(vertices), std::move(indices));
	CHECK(mesh.GetTriangleCount() == 16);

	for (uint32_t expectedId = 0; expectedId < mesh.GetTriangleCount(); expectedId++)
	{
		const glm::vec3 target = mesh.GetTriangle(expectedId).Centroid();

		Core::HitRecord record;
		const Core::Ray ray{ glm::vec3(target.x, target.y, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

		CHECK(Core::IntersectBruteForce(mesh, ray, 0.001f, Infinity, record));
		CHECK(record.TriangleId == expectedId);
		CHECK_NEAR(record.T, 5.0f, Tolerance);
	}
}

TEST_CASE(BruteForceMatchesAnIndependentClosestHitReference)
{
	// Fixed seed: the comparison must be reproducible, per specs/AGENTS.md.
	std::mt19937 rng(1337u);
	std::uniform_real_distribution<float> coordinate(-2.0f, 2.0f);

	std::vector<glm::vec3> vertices;
	std::vector<glm::uvec3> indices;

	constexpr uint32_t triangleCount = 60;
	for (uint32_t i = 0; i < triangleCount; i++)
	{
		const uint32_t base = (uint32_t)vertices.size();

		for (int vertex = 0; vertex < 3; vertex++)
			vertices.push_back(glm::vec3(coordinate(rng), coordinate(rng), coordinate(rng)));

		indices.push_back(glm::uvec3(base, base + 1, base + 2));
	}

	const Core::Mesh mesh(std::move(vertices), std::move(indices));
	CHECK(mesh.IsValid());
	CHECK(mesh.GetTriangleCount() == triangleCount);

	uint32_t rayCount = 0;
	uint32_t hitCount = 0;

	// Rays fired inward from a sphere around the geometry, so a good share of
	// them actually hit something.
	std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
	std::uniform_real_distribution<float> height(-1.0f, 1.0f);

	for (uint32_t i = 0; i < 2000; i++)
	{
		const float z = height(rng);
		const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
		const float theta = angle(rng);

		const glm::vec3 direction = glm::vec3(radius * std::cos(theta), radius * std::sin(theta), z);
		const glm::vec3 origin = -direction * 8.0f;

		const Core::Ray ray{ origin, direction };

		Core::HitRecord actual;
		Core::HitRecord expected;

		const bool actualHit = Core::IntersectBruteForce(mesh, ray, 0.001f, Infinity, actual);
		const bool expectedHit = ClosestHitBySorting(mesh, ray, 0.001f, Infinity, expected);

		rayCount++;
		if (expectedHit)
			hitCount++;

		CHECK(actualHit == expectedHit);

		if (!actualHit || !expectedHit)
			continue;

		CHECK(actual.TriangleId == expected.TriangleId);
		CHECK_NEAR(actual.T, expected.T, Tolerance);
		CHECK_NEAR(actual.Position, expected.Position, Tolerance);
		CHECK_NEAR(actual.Normal, expected.Normal, Tolerance);
		CHECK(actual.FrontFace == expected.FrontFace);
	}

	// Guards against the comparison passing vacuously because nothing was hit.
	CHECK(rayCount == 2000);
	CHECK(hitCount > 200);
}
