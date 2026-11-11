#include "TestFramework.h"

#include "Core/Renderer/ModelImporter.h"
#include "Core/Renderer/TriangleTraversal.h"

#include <glm/gtc/matrix_transform.hpp>

#include <limits>

// TESTS_ASSET_DIR is injected by Tests/CMakeLists.txt as an absolute path, so
// disk-based tests do not depend on the process's current working directory
// (specs/architecture.md notes this as a general risk for CWD-relative
// resource paths; the fix is scoped to Tests only).
#ifndef TESTS_ASSET_DIR
#define TESTS_ASSET_DIR "."
#endif

namespace {

	constexpr float Tolerance = 1e-5f;
	constexpr float Infinity = std::numeric_limits<float>::infinity();

}

TEST_CASE(ImportSingleTriangleFromSource)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"f 1 2 3\n");

	CHECK(result.Success);
	CHECK(result.ImportedMesh.IsValid());
	CHECK(result.ImportedMesh.GetVertexCount() == 3);
	CHECK(result.ImportedMesh.GetTriangleCount() == 1);

	const Core::Triangle triangle = result.ImportedMesh.GetTriangle(0);
	CHECK_NEAR(triangle.V0, glm::vec3(0.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(triangle.V1, glm::vec3(1.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(triangle.V2, glm::vec3(0.0f, 1.0f, 0.0f), Tolerance);
}

TEST_CASE(ImportQuadFaceTriangulatesDeterministically)
{
	// v0=(0,0,0), v1=(1,0,0), v2=(1,1,0), v3=(0,1,0): fan from v0 gives
	// (v0,v1,v2) and (v0,v2,v3), both with normal +Z (matches the convention
	// used throughout TriangleTests.cpp / TraversalTests.cpp).
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 1 1 0\n"
		"v 0 1 0\n"
		"f 1 2 3 4\n");

	CHECK(result.Success);
	CHECK(result.ImportedMesh.GetTriangleCount() == 2);

	const Core::Triangle first = result.ImportedMesh.GetTriangle(0);
	CHECK_NEAR(first.V0, glm::vec3(0.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(first.V1, glm::vec3(1.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(first.V2, glm::vec3(1.0f, 1.0f, 0.0f), Tolerance);
	CHECK_NEAR(first.Normal(), glm::vec3(0.0f, 0.0f, 1.0f), Tolerance);

	const Core::Triangle second = result.ImportedMesh.GetTriangle(1);
	CHECK_NEAR(second.V0, glm::vec3(0.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(second.V1, glm::vec3(1.0f, 1.0f, 0.0f), Tolerance);
	CHECK_NEAR(second.V2, glm::vec3(0.0f, 1.0f, 0.0f), Tolerance);
	CHECK_NEAR(second.Normal(), glm::vec3(0.0f, 0.0f, 1.0f), Tolerance);

	CHECK_NEAR(result.ImportedMesh.GetBounds().Min, glm::vec3(0.0f), Tolerance);
	CHECK_NEAR(result.ImportedMesh.GetBounds().Max, glm::vec3(1.0f, 1.0f, 0.0f), Tolerance);
}

TEST_CASE(ImportPentagonFaceTriangulatesIntoThreeTriangles)
{
	// A regular-ish pentagon (v0..v4). Fan from v0 must produce exactly
	// N - 2 = 3 triangles: (0,1,2), (0,2,3), (0,3,4).
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 1 1 0\n"
		"v 0.5 2 0\n"
		"v 0 1 0\n"
		"f 1 2 3 4 5\n");

	CHECK(result.Success);
	CHECK(result.ImportedMesh.GetTriangleCount() == 3);

	const std::vector<glm::uvec3>& indices = result.ImportedMesh.GetIndices();
	CHECK(indices[0] == glm::uvec3(0, 1, 2));
	CHECK(indices[1] == glm::uvec3(0, 2, 3));
	CHECK(indices[2] == glm::uvec3(0, 3, 4));
}

TEST_CASE(CommentsAndBlankLinesAreIgnored)
{
	// Only whole-line comments (a line whose first token is '#') are
	// recognised, matching the minimal scope documented in ModelImporter.h.
	// Trailing inline comments after other tokens are not supported.
	const Core::ModelImportResult result = Core::ImportObjSource(
		"# a leading comment\n"
		"\n"
		"v 0 0 0\n"
		"   \n"
		"# another comment\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"\n"
		"# a comment line directly before the face\n"
		"f 1 2 3\n");

	CHECK(result.Success);
	CHECK(result.ImportedMesh.GetTriangleCount() == 1);
}

TEST_CASE(IgnoredDirectivesDoNotAffectImport)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"mtllib scene.mtl\n"
		"o MyObject\n"
		"g Group1\n"
		"usemtl Material1\n"
		"s 1\n"
		"vp 0.5\n"
		"v 0 0 0\n"
		"vt 0.0 0.0\n"
		"v 1 0 0\n"
		"vt 1.0 0.0\n"
		"vn 0.0 0.0 1.0\n"
		"v 0 1 0\n"
		"l 1 2\n"
		"f 1 2 3\n");

	CHECK(result.Success);
	CHECK(result.ImportedMesh.GetVertexCount() == 3);
	CHECK(result.ImportedMesh.GetTriangleCount() == 1);
}

TEST_CASE(FaceWithTextureAndNormalIndicesUsesOnlyPosition)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"vt 0 0\n"
		"vn 0 0 1\n"
		"f 1/1/1 2/1/1 3/1/1\n");

	CHECK(result.Success);
	CHECK(result.ImportedMesh.GetTriangleCount() == 1);

	const Core::Triangle triangle = result.ImportedMesh.GetTriangle(0);
	CHECK_NEAR(triangle.V1, glm::vec3(1.0f, 0.0f, 0.0f), Tolerance);
}

TEST_CASE(FaceWithDoubleSlashFormUsesOnlyPosition)
{
	// 'v//vn' form: texture coordinate omitted.
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"vn 0 0 1\n"
		"f 1//1 2//1 3//1\n");

	CHECK(result.Success);
	CHECK(result.ImportedMesh.GetTriangleCount() == 1);
}

TEST_CASE(NegativeRelativeIndicesResolveCorrectly)
{
	// At the face line, 4 vertices have been defined so far. -1 => vertex 4
	// (0-based 3), -4 => vertex 1 (0-based 0), matching the OBJ spec.
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 10 0 0\n" // 0-based index 0
		"v 0 0 0\n"  // index 1
		"v 1 0 0\n"  // index 2
		"v 0 1 0\n"  // index 3
		"f -4 -2 -1\n"); // resolves to indices (0, 2, 3)

	CHECK(result.Success);
	CHECK(result.ImportedMesh.GetTriangleCount() == 1);

	const std::vector<glm::uvec3>& indices = result.ImportedMesh.GetIndices();
	CHECK(indices[0] == glm::uvec3(0, 2, 3));
}

TEST_CASE(OutOfRangeVertexIndexFails)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"f 1 2 5\n");

	CHECK(!result.Success);
	CHECK(!result.Error.empty());
	CHECK(result.ImportedMesh.GetTriangleCount() == 0);
}

TEST_CASE(ZeroVertexIndexFails)
{
	// OBJ indices are 1-based; 0 is never valid.
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"f 0 1 2\n");

	CHECK(!result.Success);
	CHECK(!result.Error.empty());
}

TEST_CASE(NegativeIndexOutOfRangeFails)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"f -5 1 2\n");

	CHECK(!result.Success);
}

TEST_CASE(FaceWithFewerThanThreeVerticesFails)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"f 1 2\n");

	CHECK(!result.Success);
	CHECK(!result.Error.empty());
}

TEST_CASE(EmptySourceFails)
{
	const Core::ModelImportResult result = Core::ImportObjSource("");

	CHECK(!result.Success);
	CHECK(!result.Error.empty());
}

TEST_CASE(SourceWithOnlyCommentsFails)
{
	const Core::ModelImportResult result = Core::ImportObjSource("# nothing here\n# still nothing\n");

	CHECK(!result.Success);
}

TEST_CASE(MalformedVertexLineFails)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 1.0 2.0\n" // missing z
		"v 0 1 0\n"
		"v 1 1 0\n"
		"f 1 2 3\n");

	CHECK(!result.Success);
	CHECK(!result.Error.empty());
}

TEST_CASE(MalformedFaceTokenFails)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"f 1 2 abc\n");

	CHECK(!result.Success);
	CHECK(!result.Error.empty());
}

TEST_CASE(VerticesWithNoFacesSucceedsWithZeroTriangles)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 2 3\n");

	CHECK(result.Success);
	CHECK(result.ImportedMesh.GetVertexCount() == 2);
	CHECK(result.ImportedMesh.GetTriangleCount() == 0);
	CHECK(!result.ImportedMesh.GetBounds().IsEmpty());
}

TEST_CASE(CRLFLineEndingsAreTolerated)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\r\n"
		"v 1 0 0\r\n"
		"v 0 1 0\r\n"
		"f 1 2 3\r\n");

	CHECK(result.Success);
	CHECK(result.ImportedMesh.GetTriangleCount() == 1);
}

TEST_CASE(IdentityTransformLeavesPositionsUnchanged)
{
	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 1 2 3\n"
		"v 4 5 6\n"
		"v 7 8 9\n"
		"f 1 2 3\n",
		glm::mat4(1.0f));

	CHECK(result.Success);
	const Core::Triangle triangle = result.ImportedMesh.GetTriangle(0);
	CHECK_NEAR(triangle.V0, glm::vec3(1.0f, 2.0f, 3.0f), Tolerance);
	CHECK_NEAR(triangle.V1, glm::vec3(4.0f, 5.0f, 6.0f), Tolerance);
	CHECK_NEAR(triangle.V2, glm::vec3(7.0f, 8.0f, 9.0f), Tolerance);
}

TEST_CASE(TranslationTransformIsAppliedToEveryVertex)
{
	const glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, -5.0f));

	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"f 1 2 3\n",
		translation);

	CHECK(result.Success);
	const Core::Triangle triangle = result.ImportedMesh.GetTriangle(0);
	CHECK_NEAR(triangle.V0, glm::vec3(10.0f, 0.0f, -5.0f), Tolerance);
	CHECK_NEAR(triangle.V1, glm::vec3(11.0f, 0.0f, -5.0f), Tolerance);
	CHECK_NEAR(triangle.V2, glm::vec3(10.0f, 1.0f, -5.0f), Tolerance);
}

TEST_CASE(ScaleTransformAffectsBoundsAndPreservesNormalDirection)
{
	// Non-uniform scale. Because Core::Mesh stores positions only and the
	// normal is derived after the transform is applied (see ModelImporter.h),
	// no separate inverse-transpose normal transform is needed: the derived
	// normal is simply the correct normal of the scaled triangle.
	const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 1.0f));

	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"f 1 2 3\n",
		scale);

	CHECK(result.Success);

	const Core::Triangle triangle = result.ImportedMesh.GetTriangle(0);
	CHECK_NEAR(triangle.V0, glm::vec3(0.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(triangle.V1, glm::vec3(2.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(triangle.V2, glm::vec3(0.0f, 3.0f, 0.0f), Tolerance);
	CHECK_NEAR(triangle.Normal(), glm::vec3(0.0f, 0.0f, 1.0f), Tolerance);

	CHECK_NEAR(result.ImportedMesh.GetBounds().Max, glm::vec3(2.0f, 3.0f, 0.0f), Tolerance);
}

TEST_CASE(RotationTransformReorientsTheNormal)
{
	// Rotate the same +Z-facing triangle 180 degrees about the X axis; the
	// derived normal must flip to -Z.
	const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	const Core::ModelImportResult result = Core::ImportObjSource(
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"f 1 2 3\n",
		rotation);

	CHECK(result.Success);
	CHECK_NEAR(result.ImportedMesh.GetTriangle(0).Normal(), glm::vec3(0.0f, 0.0f, -1.0f), Tolerance);
}

TEST_CASE(LoadTriangleObjFromDisk)
{
	const std::filesystem::path path = std::filesystem::path(TESTS_ASSET_DIR) / "Triangle.obj";
	const Core::ModelImportResult result = Core::ImportObjFile(path);

	CHECK(result.Success);
	CHECK(result.Error.empty());
	CHECK(result.ImportedMesh.IsValid());
	CHECK(result.ImportedMesh.GetTriangleCount() == 1);

	CHECK_NEAR(result.ImportedMesh.GetBounds().Min, glm::vec3(0.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(result.ImportedMesh.GetBounds().Max, glm::vec3(1.0f, 1.0f, 0.0f), Tolerance);

	// M4 acceptance (specs/milestones.md): the imported model can be
	// processed by the existing CPU brute-force traversal (M3).
	const Core::Ray ray{ glm::vec3(0.25f, 0.25f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };
	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(result.ImportedMesh, ray, 0.001f, Infinity, record));
	CHECK(record.TriangleId == 0);
	CHECK_NEAR(record.T, 5.0f, Tolerance);
	CHECK(record.FrontFace);
}

TEST_CASE(LoadQuadObjFromDisk)
{
	const std::filesystem::path path = std::filesystem::path(TESTS_ASSET_DIR) / "Quad.obj";
	const Core::ModelImportResult result = Core::ImportObjFile(path);

	CHECK(result.Success);
	CHECK(result.Error.empty());
	CHECK(result.ImportedMesh.IsValid());

	// The quad face is triangulated into 2 triangles despite the mtllib/o/
	// usemtl/vt/vn directives present in the file.
	CHECK(result.ImportedMesh.GetVertexCount() == 4);
	CHECK(result.ImportedMesh.GetTriangleCount() == 2);

	CHECK_NEAR(result.ImportedMesh.GetBounds().Min, glm::vec3(0.0f, 0.0f, 0.0f), Tolerance);
	CHECK_NEAR(result.ImportedMesh.GetBounds().Max, glm::vec3(1.0f, 1.0f, 0.0f), Tolerance);

	for (uint32_t triangleId = 0; triangleId < result.ImportedMesh.GetTriangleCount(); triangleId++)
		CHECK_NEAR(result.ImportedMesh.GetTriangle(triangleId).Normal(), glm::vec3(0.0f, 0.0f, 1.0f), Tolerance);

	// M4 acceptance: processed by CPU brute-force traversal.
	const Core::Ray ray{ glm::vec3(0.5f, 0.5f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f) };
	Core::HitRecord record;
	CHECK(Core::IntersectBruteForce(result.ImportedMesh, ray, 0.001f, Infinity, record));
	CHECK_NEAR(record.T, 5.0f, Tolerance);
	CHECK(record.FrontFace);
}

TEST_CASE(LoadingANonexistentFileFailsWithoutCrashing)
{
	const std::filesystem::path path = std::filesystem::path(TESTS_ASSET_DIR) / "DoesNotExist.obj";
	const Core::ModelImportResult result = Core::ImportObjFile(path);

	CHECK(!result.Success);
	CHECK(!result.Error.empty());
	CHECK(result.ImportedMesh.GetTriangleCount() == 0);
}
