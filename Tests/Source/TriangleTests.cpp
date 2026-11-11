#include "TestFramework.h"

#include "Core/Renderer/Triangle.h"

#include <limits>

namespace {

	constexpr float Tolerance = 1e-5f;
	constexpr float Infinity = std::numeric_limits<float>::infinity();

	// Right triangle in the z = 0 plane. Wound so that its normal is +Z.
	const Core::Triangle UnitTriangle{
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)
	};

	// Ray travelling along -Z towards the triangle plane from z = 1.
	Core::Ray RayTowards(float x, float y)
	{
		return Core::Ray{ glm::vec3(x, y, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f) };
	}

}

TEST_CASE(TriangleBoundsCentroidAndNormal)
{
	const Core::AABB bounds = UnitTriangle.Bounds();

	CHECK_NEAR(bounds.Min, glm::vec3(0.0f), Tolerance);
	CHECK_NEAR(bounds.Max, glm::vec3(1.0f, 1.0f, 0.0f), Tolerance);

	CHECK_NEAR(UnitTriangle.Centroid(), glm::vec3(1.0f / 3.0f, 1.0f / 3.0f, 0.0f), Tolerance);
	CHECK_NEAR(UnitTriangle.Normal(), glm::vec3(0.0f, 0.0f, 1.0f), Tolerance);

	CHECK(!UnitTriangle.IsDegenerate());
}

TEST_CASE(TriangleHitAtCenter)
{
	Core::HitRecord record;

	CHECK(UnitTriangle.Intersect(RayTowards(0.25f, 0.25f), 0.001f, Infinity, record));
	CHECK_NEAR(record.T, 1.0f, Tolerance);
	CHECK_NEAR(record.Position, glm::vec3(0.25f, 0.25f, 0.0f), Tolerance);
	CHECK_NEAR(record.Normal, glm::vec3(0.0f, 0.0f, 1.0f), Tolerance);
	CHECK(record.FrontFace);
}

TEST_CASE(TriangleBarycentricCoordinates)
{
	Core::HitRecord record;

	CHECK(UnitTriangle.Intersect(RayTowards(0.25f, 0.5f), 0.001f, Infinity, record));
	CHECK_NEAR(record.U, 0.25f, Tolerance);
	CHECK_NEAR(record.V, 0.5f, Tolerance);
}

TEST_CASE(TriangleMissOutside)
{
	Core::HitRecord record;

	CHECK(!UnitTriangle.Intersect(RayTowards(2.0f, 2.0f), 0.001f, Infinity, record));
	CHECK(!UnitTriangle.Intersect(RayTowards(-0.5f, 0.25f), 0.001f, Infinity, record));
	CHECK(!UnitTriangle.Intersect(RayTowards(0.25f, -0.5f), 0.001f, Infinity, record));
}

TEST_CASE(TriangleHitNearEdge)
{
	Core::HitRecord record;

	// Just inside the hypotenuse, where u + v approaches 1.
	CHECK(UnitTriangle.Intersect(RayTowards(0.4999f, 0.4999f), 0.001f, Infinity, record));
	CHECK_NEAR(record.T, 1.0f, Tolerance);

	// Just inside the two axis-aligned edges.
	CHECK(UnitTriangle.Intersect(RayTowards(0.5f, 0.0001f), 0.001f, Infinity, record));
	CHECK(UnitTriangle.Intersect(RayTowards(0.0001f, 0.5f), 0.001f, Infinity, record));
}

TEST_CASE(TriangleMissJustOutsideEdge)
{
	Core::HitRecord record;

	CHECK(!UnitTriangle.Intersect(RayTowards(0.5001f, 0.5001f), 0.001f, Infinity, record));
	CHECK(!UnitTriangle.Intersect(RayTowards(0.5f, -0.0001f), 0.001f, Infinity, record));
	CHECK(!UnitTriangle.Intersect(RayTowards(-0.0001f, 0.5f), 0.001f, Infinity, record));
}

TEST_CASE(TriangleHitNearVertex)
{
	Core::HitRecord record;

	CHECK(UnitTriangle.Intersect(RayTowards(0.0001f, 0.0001f), 0.001f, Infinity, record));
	CHECK(UnitTriangle.Intersect(RayTowards(0.9995f, 0.0001f), 0.001f, Infinity, record));
	CHECK(UnitTriangle.Intersect(RayTowards(0.0001f, 0.9995f), 0.001f, Infinity, record));
}

TEST_CASE(RayParallelToTriangleMisses)
{
	Core::HitRecord record;

	const Core::Ray parallel{ glm::vec3(0.25f, 0.25f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f) };
	CHECK(!UnitTriangle.Intersect(parallel, 0.001f, Infinity, record));

	// Parallel and coplanar.
	const Core::Ray coplanar{ glm::vec3(-1.0f, 0.25f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f) };
	CHECK(!UnitTriangle.Intersect(coplanar, 0.001f, Infinity, record));
}

TEST_CASE(BackFacingTriangleIsHitWithFlippedNormal)
{
	Core::HitRecord record;

	const Core::Ray fromBehind{ glm::vec3(0.25f, 0.25f, -1.0f), glm::vec3(0.0f, 0.0f, 1.0f) };

	CHECK(UnitTriangle.Intersect(fromBehind, 0.001f, Infinity, record));
	CHECK_NEAR(record.T, 1.0f, Tolerance);
	CHECK(!record.FrontFace);
	CHECK_NEAR(record.Normal, glm::vec3(0.0f, 0.0f, -1.0f), Tolerance);
}

TEST_CASE(RayStartingOnTriangleIsRejectedByTMin)
{
	Core::HitRecord record;

	const Core::Ray onSurface{ glm::vec3(0.25f, 0.25f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f) };
	CHECK(!UnitTriangle.Intersect(onSurface, 0.001f, Infinity, record));

	// Offsetting past tMin makes the same intersection valid again.
	const Core::Ray offset{ glm::vec3(0.25f, 0.25f, 0.002f), glm::vec3(0.0f, 0.0f, -1.0f) };
	CHECK(UnitTriangle.Intersect(offset, 0.001f, Infinity, record));
	CHECK_NEAR(record.T, 0.002f, Tolerance);
}

TEST_CASE(TriangleBehindRayMisses)
{
	Core::HitRecord record;

	const Core::Ray awayFromTriangle{ glm::vec3(0.25f, 0.25f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f) };
	CHECK(!UnitTriangle.Intersect(awayFromTriangle, 0.001f, Infinity, record));
}

TEST_CASE(TriangleRespectsTMinAndTMax)
{
	Core::HitRecord record;
	const Core::Ray ray = RayTowards(0.25f, 0.25f);

	CHECK(!UnitTriangle.Intersect(ray, 0.001f, 0.5f, record));
	CHECK(!UnitTriangle.Intersect(ray, 2.0f, Infinity, record));
	CHECK(UnitTriangle.Intersect(ray, 0.5f, 1.5f, record));
}

TEST_CASE(DegenerateTriangleWithIdenticalVerticesMisses)
{
	const Core::Triangle degenerate{ glm::vec3(0.25f, 0.25f, 0.0f), glm::vec3(0.25f, 0.25f, 0.0f), glm::vec3(0.25f, 0.25f, 0.0f) };

	CHECK(degenerate.IsDegenerate());
	CHECK_NEAR(degenerate.Normal(), glm::vec3(0.0f), Tolerance);

	Core::HitRecord record;
	CHECK(!degenerate.Intersect(RayTowards(0.25f, 0.25f), 0.001f, Infinity, record));

	// A rejected intersection must leave the record untouched, never NaN.
	CHECK(!std::isnan(record.T));
	CHECK(!std::isnan(record.Position.x));
	CHECK(!std::isnan(record.Normal.x));
}

TEST_CASE(DegenerateTriangleWithCollinearVerticesMisses)
{
	const Core::Triangle collinear{
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(2.0f, 0.0f, 0.0f)
	};

	CHECK(collinear.IsDegenerate());
	CHECK_NEAR(collinear.Normal(), glm::vec3(0.0f), Tolerance);

	Core::HitRecord record;
	const Core::Ray ray{ glm::vec3(0.5f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) };
	CHECK(!collinear.Intersect(ray, 0.001f, Infinity, record));
	CHECK(!std::isnan(record.T));
}

TEST_CASE(DegenerateTriangleBoundsRemainValid)
{
	const Core::Triangle collinear{
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(2.0f, 0.0f, 0.0f)
	};

	const Core::AABB bounds = collinear.Bounds();

	CHECK(!bounds.IsEmpty());
	CHECK_NEAR(bounds.Min, glm::vec3(0.0f), Tolerance);
	CHECK_NEAR(bounds.Max, glm::vec3(2.0f, 0.0f, 0.0f), Tolerance);
	CHECK(bounds.Contains(collinear.Centroid()));
}

TEST_CASE(ThinButValidTriangleStillHits)
{
	// Small enough to be a sliver, large enough to clear the degeneracy epsilon.
	const Core::Triangle sliver{
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1e-3f, 0.0f)
	};

	CHECK(!sliver.IsDegenerate());

	Core::HitRecord record;
	CHECK(sliver.Intersect(RayTowards(0.5f, 1e-4f), 0.001f, Infinity, record));
	CHECK_NEAR(record.T, 1.0f, Tolerance);
}
