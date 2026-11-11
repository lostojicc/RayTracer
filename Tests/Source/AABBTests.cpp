#include "TestFramework.h"

#include "Core/Renderer/AABB.h"
#include "Core/Renderer/Triangle.h"

#include <limits>

namespace {

	constexpr float Tolerance = 1e-5f;
	constexpr float Infinity = std::numeric_limits<float>::infinity();

	const Core::AABB UnitBox{ glm::vec3(-1.0f), glm::vec3(1.0f) };

}

TEST_CASE(DefaultAABBIsEmpty)
{
	const Core::AABB box;

	CHECK(box.IsEmpty());
	CHECK(!UnitBox.IsEmpty());
}

TEST_CASE(AABBExpandByPoint)
{
	Core::AABB box;
	box.Expand(glm::vec3(1.0f, 2.0f, 3.0f));

	CHECK(!box.IsEmpty());
	CHECK_NEAR(box.Min, glm::vec3(1.0f, 2.0f, 3.0f), Tolerance);
	CHECK_NEAR(box.Max, glm::vec3(1.0f, 2.0f, 3.0f), Tolerance);

	box.Expand(glm::vec3(-4.0f, 0.0f, 5.0f));

	CHECK_NEAR(box.Min, glm::vec3(-4.0f, 0.0f, 3.0f), Tolerance);
	CHECK_NEAR(box.Max, glm::vec3(1.0f, 2.0f, 5.0f), Tolerance);
}

TEST_CASE(AABBExpandByAABB)
{
	Core::AABB box{ glm::vec3(0.0f), glm::vec3(1.0f) };
	box.Expand(Core::AABB{ glm::vec3(-2.0f), glm::vec3(0.5f) });

	CHECK_NEAR(box.Min, glm::vec3(-2.0f), Tolerance);
	CHECK_NEAR(box.Max, glm::vec3(1.0f), Tolerance);
}

TEST_CASE(ExpandingByAnEmptyAABBIsIdentity)
{
	Core::AABB box{ glm::vec3(0.0f), glm::vec3(1.0f) };
	box.Expand(Core::AABB());

	CHECK_NEAR(box.Min, glm::vec3(0.0f), Tolerance);
	CHECK_NEAR(box.Max, glm::vec3(1.0f), Tolerance);
}

TEST_CASE(AABBCentroidAndExtent)
{
	const Core::AABB box{ glm::vec3(-1.0f, 0.0f, 2.0f), glm::vec3(3.0f, 4.0f, 2.0f) };

	CHECK_NEAR(box.Centroid(), glm::vec3(1.0f, 2.0f, 2.0f), Tolerance);
	CHECK_NEAR(box.Extent(), glm::vec3(4.0f, 4.0f, 0.0f), Tolerance);
}

TEST_CASE(AABBContains)
{
	CHECK(UnitBox.Contains(glm::vec3(0.0f)));
	CHECK(UnitBox.Contains(glm::vec3(1.0f)));
	CHECK(!UnitBox.Contains(glm::vec3(1.0001f, 0.0f, 0.0f)));

	CHECK(UnitBox.Contains(Core::AABB{ glm::vec3(-0.5f), glm::vec3(0.5f) }));
	CHECK(!UnitBox.Contains(Core::AABB{ glm::vec3(-2.0f), glm::vec3(0.5f) }));
	CHECK(UnitBox.Contains(Core::AABB()));
}

TEST_CASE(AABBFromTriangleContainsItsVertices)
{
	const Core::Triangle triangle{
		glm::vec3(-1.0f, 0.0f, 2.0f),
		glm::vec3(3.0f, -4.0f, 0.0f),
		glm::vec3(0.0f, 5.0f, 1.0f)
	};

	const Core::AABB bounds = triangle.Bounds();

	CHECK_NEAR(bounds.Min, glm::vec3(-1.0f, -4.0f, 0.0f), Tolerance);
	CHECK_NEAR(bounds.Max, glm::vec3(3.0f, 5.0f, 2.0f), Tolerance);

	CHECK(bounds.Contains(triangle.V0));
	CHECK(bounds.Contains(triangle.V1));
	CHECK(bounds.Contains(triangle.V2));
	CHECK(bounds.Contains(triangle.Centroid()));
}

TEST_CASE(RayHitsAABB)
{
	const Core::Ray ray{ glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

	float tEnter = -1.0f;
	CHECK(UnitBox.Intersects(ray, 0.0f, Infinity, &tEnter));
	CHECK_NEAR(tEnter, 4.0f, Tolerance);
}

TEST_CASE(RayMissesAABB)
{
	const Core::Ray ray{ glm::vec3(5.0f, 5.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

	CHECK(!UnitBox.Intersects(ray, 0.0f, Infinity));
}

TEST_CASE(RayStartingInsideAABBHitsAtTMin)
{
	const Core::Ray ray{ glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

	float tEnter = -1.0f;
	CHECK(UnitBox.Intersects(ray, 0.0f, Infinity, &tEnter));
	CHECK_NEAR(tEnter, 0.0f, Tolerance);
}

TEST_CASE(RayParallelToSlabInsideTheBoxHits)
{
	const Core::Ray ray{ glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f) };

	CHECK(UnitBox.Intersects(ray, 0.0f, Infinity));
}

TEST_CASE(RayParallelToSlabOutsideTheBoxMisses)
{
	const Core::Ray ray{ glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f) };

	CHECK(!UnitBox.Intersects(ray, 0.0f, Infinity));

	const Core::Ray behind{ glm::vec3(0.0f, -5.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f) };

	CHECK(!UnitBox.Intersects(behind, 0.0f, Infinity));
}

TEST_CASE(RayOriginOnSlabPlaneDoesNotPropagateNaN)
{
	// Produces 0 * inf on the y axis.
	const Core::Ray ray{ glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f) };

	float tEnter = -1.0f;
	CHECK(UnitBox.Intersects(ray, 0.0f, Infinity, &tEnter));
	CHECK(!std::isnan(tEnter));
	CHECK_NEAR(tEnter, 0.0f, Tolerance);
}

TEST_CASE(ZeroThicknessAABBIsIntersectable)
{
	const Core::AABB flat{ glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f) };
	const Core::Ray ray{ glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

	float tEnter = -1.0f;
	CHECK(flat.Intersects(ray, 0.0f, Infinity, &tEnter));
	CHECK_NEAR(tEnter, 1.0f, Tolerance);
}

TEST_CASE(AABBRespectsTMinAndTMax)
{
	const Core::Ray ray{ glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

	// Box is entered at t = 4 and exited at t = 6.
	CHECK(!UnitBox.Intersects(ray, 0.0f, 2.0f));
	CHECK(!UnitBox.Intersects(ray, 10.0f, Infinity));
	CHECK(UnitBox.Intersects(ray, 0.0f, 4.5f));
	CHECK(UnitBox.Intersects(ray, 4.5f, Infinity));
}
