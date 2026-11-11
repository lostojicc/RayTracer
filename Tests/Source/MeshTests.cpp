#include "TestFramework.h"

#include "Core/Renderer/Mesh.h"

namespace {

	constexpr float Tolerance = 1e-5f;

	// Two triangles forming a unit quad in the z = 0 plane.
	Core::Mesh MakeQuad()
	{
		std::vector<glm::vec3> vertices = {
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f)
		};

		std::vector<glm::uvec3> indices = {
			glm::uvec3(0, 1, 2),
			glm::uvec3(0, 2, 3)
		};

		return Core::Mesh(std::move(vertices), std::move(indices));
	}

}

TEST_CASE(EmptyMeshIsValidAndHasEmptyBounds)
{
	const Core::Mesh mesh;

	CHECK(mesh.GetVertexCount() == 0);
	CHECK(mesh.GetTriangleCount() == 0);
	CHECK(mesh.IsValid());
	CHECK(mesh.GetBounds().IsEmpty());
}

TEST_CASE(MeshReportsVertexAndTriangleCounts)
{
	const Core::Mesh mesh = MakeQuad();

	CHECK(mesh.GetVertexCount() == 4);
	CHECK(mesh.GetTriangleCount() == 2);
	CHECK(mesh.IsValid());
}

TEST_CASE(MeshTriangleIdResolvesToTheCorrectVertices)
{
	const Core::Mesh mesh = MakeQuad();

	const Core::Triangle first = mesh.GetTriangle(0);
	CHECK_NEAR(first.V0, glm::vec3(0.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(first.V1, glm::vec3(1.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(first.V2, glm::vec3(1.0f, 1.0f, 0.0f), Tolerance);

	const Core::Triangle second = mesh.GetTriangle(1);
	CHECK_NEAR(second.V0, glm::vec3(0.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(second.V1, glm::vec3(1.0f, 1.0f, 0.0f), Tolerance);
	CHECK_NEAR(second.V2, glm::vec3(0.0f, 1.0f, 0.0f), Tolerance);
}

TEST_CASE(MeshTriangleIdsAreStableAcrossRepeatedAccess)
{
	const Core::Mesh mesh = MakeQuad();

	for (uint32_t triangleId = 0; triangleId < mesh.GetTriangleCount(); triangleId++)
	{
		const Core::Triangle first = mesh.GetTriangle(triangleId);
		const Core::Triangle second = mesh.GetTriangle(triangleId);

		CHECK_NEAR(first.V0, second.V0, Tolerance);
		CHECK_NEAR(first.V1, second.V1, Tolerance);
		CHECK_NEAR(first.V2, second.V2, Tolerance);
	}
}

TEST_CASE(MeshBoundsContainEveryTriangle)
{
	const Core::Mesh mesh = MakeQuad();
	const Core::AABB bounds = mesh.GetBounds();

	CHECK_NEAR(bounds.Min, glm::vec3(0.0f), Tolerance);
	CHECK_NEAR(bounds.Max, glm::vec3(1.0f, 1.0f, 0.0f), Tolerance);

	for (uint32_t triangleId = 0; triangleId < mesh.GetTriangleCount(); triangleId++)
		CHECK(bounds.Contains(mesh.GetTriangle(triangleId).Bounds()));
}

TEST_CASE(MeshWithOutOfRangeIndexIsInvalid)
{
	std::vector<glm::vec3> vertices = {
		glm::vec3(0.0f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)
	};

	std::vector<glm::uvec3> indices = { glm::uvec3(0, 1, 7) };

	const Core::Mesh mesh(std::move(vertices), std::move(indices));

	CHECK(!mesh.IsValid());

	// Bounds are computed from vertices, so they remain usable even though the
	// index data is broken.
	CHECK(!mesh.GetBounds().IsEmpty());
}

TEST_CASE(MeshTrianglesCanBeIntersected)
{
	const Core::Mesh mesh = MakeQuad();

	const Core::Ray ray{ glm::vec3(0.75f, 0.25f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f) };

	Core::HitRecord record;
	CHECK(mesh.GetTriangle(0).Intersect(ray, 0.001f, 1000.0f, record));
	CHECK_NEAR(record.T, 1.0f, Tolerance);

	CHECK(!mesh.GetTriangle(1).Intersect(ray, 0.001f, 1000.0f, record));
}
