#include "TestFramework.h"

#include "Core/Renderer/BVH.h"

#include <algorithm>
#include <limits>
#include <random>
#include <utility>
#include <vector>

namespace {

	// A single triangle per entry in a 'gridSize' x 'gridSize' grid of
	// coplanar, non-degenerate triangles in the z = 0 plane. Triangle ids
	// follow array order. Reused across several tests as a mesh with plenty
	// of well-separated centroids.
	Core::Mesh MakeGridMesh(int gridSize)
	{
		std::vector<glm::vec3> vertices;
		std::vector<glm::uvec3> indices;

		for (int y = 0; y < gridSize; y++)
		{
			for (int x = 0; x < gridSize; x++)
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

		return Core::Mesh(std::move(vertices), std::move(indices));
	}

	// N random, non-degenerate-with-overwhelming-probability triangles with
	// a fixed seed, so the test is reproducible (per specs/AGENTS.md).
	Core::Mesh MakeRandomTriangleMesh(uint32_t triangleCount, uint32_t seed)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> coordinate(-10.0f, 10.0f);

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

	// One degenerate (zero-area) triangle per point: all 3 vertices equal
	// the given point, so its centroid is exactly that point and its bounds
	// are a single point. Used to give split-axis tests exact, unambiguous
	// centroid positions without relying on triangle-centroid arithmetic.
	Core::Mesh MakePointTriangleMesh(const std::vector<glm::vec3>& points)
	{
		std::vector<glm::vec3> vertices;
		std::vector<glm::uvec3> indices;

		for (const glm::vec3& point : points)
		{
			const uint32_t base = (uint32_t)vertices.size();
			vertices.push_back(point);
			vertices.push_back(point);
			vertices.push_back(point);
			indices.push_back(glm::uvec3(base, base + 1, base + 2));
		}

		return Core::Mesh(std::move(vertices), std::move(indices));
	}

	// Exhaustively validates specs/bvh.md's invariants for a BVH built over
	// 'mesh', and cross-checks BVH::GetStats() against an independent walk.
	void CheckBVHInvariants(const Core::BVH& bvh, const Core::Mesh& mesh)
	{
		const uint32_t triangleCount = mesh.GetTriangleCount();

		if (triangleCount == 0)
		{
			CHECK(bvh.IsEmpty());
			CHECK(bvh.GetNodes().empty());
			CHECK(bvh.GetPrimitives().empty());
			CHECK(bvh.GetStats().NodeCount == 0);
			CHECK(bvh.GetStats().PrimitiveCount == 0);
			return;
		}

		CHECK(!bvh.IsEmpty());

		const std::vector<Core::BVHNode>& nodes = bvh.GetNodes();
		const std::vector<Core::BVHPrimitive>& primitives = bvh.GetPrimitives();
		CHECK(primitives.size() == triangleCount);

		std::vector<bool> primitiveCovered(primitives.size(), false);

		uint32_t visitedNodeCount = 0;
		uint32_t visitedLeafCount = 0;
		uint32_t visitedInternalCount = 0;
		uint32_t visitedMaxDepth = 0;
		uint32_t visitedPrimitiveCount = 0;
		uint32_t visitedMinLeafSize = std::numeric_limits<uint32_t>::max();
		uint32_t visitedMaxLeafSize = 0;

		std::vector<std::pair<uint32_t, uint32_t>> stack;
		stack.push_back({ Core::BVH::RootIndex, 0u });

		while (!stack.empty())
		{
			const auto [nodeIndex, depth] = stack.back();
			stack.pop_back();

			CHECK(nodeIndex < nodes.size());
			if (nodeIndex >= nodes.size())
				continue;

			const Core::BVHNode& node = nodes[nodeIndex];

			visitedNodeCount++;
			visitedMaxDepth = std::max(visitedMaxDepth, depth);

			if (node.IsLeaf())
			{
				visitedLeafCount++;
				visitedPrimitiveCount += node.PrimitiveCount;
				visitedMinLeafSize = std::min(visitedMinLeafSize, node.PrimitiveCount);
				visitedMaxLeafSize = std::max(visitedMaxLeafSize, node.PrimitiveCount);

				CHECK(node.PrimitiveCount >= 1);
				CHECK((size_t)node.FirstPrimitive + node.PrimitiveCount <= primitives.size());

				for (uint32_t i = node.FirstPrimitive; i < node.FirstPrimitive + node.PrimitiveCount; i++)
				{
					// No two leaves may reference the same primitive slot.
					CHECK(!primitiveCovered[i]);
					primitiveCovered[i] = true;

					const uint32_t triangleId = primitives[i].TriangleId;
					CHECK(triangleId < triangleCount);

					// The leaf bounds must contain the *actual* triangle
					// geometry (recomputed from the mesh), not just the
					// cached primitive bounds, guarding against a
					// stale-cache bug in BVHPrimitive construction.
					CHECK(node.Bounds.Contains(mesh.GetTriangle(triangleId).Bounds()));
				}
			}
			else
			{
				visitedInternalCount++;

				CHECK(node.LeftChild < nodes.size());
				CHECK(node.RightChild < nodes.size());
				CHECK(node.Bounds.Contains(nodes[node.LeftChild].Bounds));
				CHECK(node.Bounds.Contains(nodes[node.RightChild].Bounds));

				stack.push_back({ node.LeftChild, depth + 1 });
				stack.push_back({ node.RightChild, depth + 1 });
			}
		}

		for (bool covered : primitiveCovered)
			CHECK(covered);

		// Every triangle id 0..triangleCount-1 appears in the primitive
		// array exactly once (specs/bvh.md "Every triangle must occur in
		// exactly one leaf").
		std::vector<uint32_t> seenCount(triangleCount, 0);
		for (const Core::BVHPrimitive& primitive : primitives)
		{
			CHECK(primitive.TriangleId < triangleCount);
			if (primitive.TriangleId < triangleCount)
				seenCount[primitive.TriangleId]++;
		}
		for (uint32_t count : seenCount)
			CHECK(count == 1);

		// Root bounds contain every triangle directly, not just transitively
		// through the parent/child chain checked above.
		for (uint32_t triangleId = 0; triangleId < triangleCount; triangleId++)
			CHECK(bvh.GetBounds().Contains(mesh.GetTriangle(triangleId).Bounds()));

		// A binary tree where every internal node has exactly two children
		// is "full": leaves = internal + 1. True regardless of how many
		// splits were degenerate.
		CHECK(visitedLeafCount == visitedInternalCount + 1);

		// Cross-check the independent walk above against BVH::GetStats().
		const Core::BVHStats& stats = bvh.GetStats();
		CHECK(stats.NodeCount == visitedNodeCount);
		CHECK(stats.LeafCount == visitedLeafCount);
		CHECK(stats.InternalCount == visitedInternalCount);
		CHECK(stats.MaxDepth == visitedMaxDepth);
		CHECK(stats.PrimitiveCount == visitedPrimitiveCount);
		CHECK(stats.MinLeafSize == visitedMinLeafSize);
		CHECK(stats.MaxLeafSize == visitedMaxLeafSize);
		CHECK(stats.PrimitiveCount == triangleCount);
	}

}

TEST_CASE(EmptyMeshProducesAnEmptyBVH)
{
	const Core::Mesh mesh;
	const Core::BVH bvh(mesh);

	CHECK(bvh.IsEmpty());
	CHECK(bvh.GetNodes().empty());
	CHECK(bvh.GetPrimitives().empty());

	const Core::BVHStats& stats = bvh.GetStats();
	CHECK(stats.NodeCount == 0);
	CHECK(stats.LeafCount == 0);
	CHECK(stats.InternalCount == 0);
	CHECK(stats.MaxDepth == 0);
	CHECK(stats.PrimitiveCount == 0);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(SingleTriangleProducesASingleLeafRoot)
{
	const Core::Mesh mesh = MakeGridMesh(1);
	CHECK(mesh.GetTriangleCount() == 1);

	const Core::BVH bvh(mesh);

	CHECK(!bvh.IsEmpty());
	CHECK(bvh.GetNodes().size() == 1);

	const Core::BVHNode& root = bvh.GetNodes()[Core::BVH::RootIndex];
	CHECK(root.IsLeaf());
	CHECK(root.FirstPrimitive == 0);
	CHECK(root.PrimitiveCount == 1);

	const Core::BVHStats& stats = bvh.GetStats();
	CHECK(stats.NodeCount == 1);
	CHECK(stats.LeafCount == 1);
	CHECK(stats.InternalCount == 0);
	CHECK(stats.MaxDepth == 0);
	CHECK(stats.MinLeafSize == 1);
	CHECK(stats.MaxLeafSize == 1);
	CHECK_NEAR(stats.AverageLeafSize, 1.0f, 1e-6f);
	CHECK(stats.PrimitiveCount == 1);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(LeafSizeLargerThanTriangleCountProducesASingleLeaf)
{
	const Core::Mesh mesh = MakeGridMesh(3); // 9 triangles
	const Core::BVH bvh(mesh, 1000);

	CHECK(bvh.GetNodes().size() == 1);
	CHECK(bvh.GetNodes()[Core::BVH::RootIndex].IsLeaf());
	CHECK(bvh.GetStats().PrimitiveCount == 9);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(DefaultLeafSizeIsFour)
{
	CHECK(Core::BVH::DefaultLeafSize == 4);

	// 5 triangles with default leaf size (4) must split at least once,
	// since a single leaf could not hold all 5 under the default.
	const Core::Mesh mesh = MakeGridMesh(3); // 9 triangles, well separated
	const Core::BVH bvh(mesh);

	CHECK(bvh.GetStats().InternalCount >= 1);
	for (const Core::BVHNode& node : bvh.GetNodes())
		if (node.IsLeaf())
			CHECK(node.PrimitiveCount <= 4);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(ConfigurableLeafSizeOfOneProducesOnePrimitivePerLeaf)
{
	const Core::Mesh mesh = MakeGridMesh(4); // 16 well-separated triangles
	const Core::BVH bvh(mesh, 1);

	const Core::BVHStats& stats = bvh.GetStats();
	CHECK(stats.LeafCount == 16);
	CHECK(stats.InternalCount == 15); // full binary tree: leaves - 1
	CHECK(stats.MinLeafSize == 1);
	CHECK(stats.MaxLeafSize == 1);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(BVHContainsEveryTriangleExactlyOnceOnAModerateGrid)
{
	const Core::Mesh mesh = MakeGridMesh(6); // 36 triangles
	const Core::BVH bvh(mesh, Core::BVH::DefaultLeafSize);

	// CheckBVHInvariants already asserts the "exactly once" property, but
	// this test exists to name the acceptance criterion explicitly.
	CheckBVHInvariants(bvh, mesh);
	CHECK(bvh.GetStats().PrimitiveCount == 36);
}

TEST_CASE(ParentBoundsContainChildBoundsAndRootContainsEveryTriangle)
{
	const Core::Mesh mesh = MakeRandomTriangleMesh(200, 4242u);
	const Core::BVH bvh(mesh);

	// CheckBVHInvariants walks every internal node and every leaf and
	// asserts both properties named in this test.
	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(ConstructionIsDeterministicForIdenticalInput)
{
	const Core::Mesh mesh = MakeRandomTriangleMesh(150, 99u);

	const Core::BVH first(mesh, 4);
	const Core::BVH second(mesh, 4);

	CHECK(first.GetNodes().size() == second.GetNodes().size());
	CHECK(first.GetPrimitives().size() == second.GetPrimitives().size());

	for (size_t i = 0; i < first.GetNodes().size(); i++)
	{
		const Core::BVHNode& a = first.GetNodes()[i];
		const Core::BVHNode& b = second.GetNodes()[i];

		CHECK_NEAR(a.Bounds.Min, b.Bounds.Min, 1e-6f);
		CHECK_NEAR(a.Bounds.Max, b.Bounds.Max, 1e-6f);
		CHECK(a.FirstPrimitive == b.FirstPrimitive);
		CHECK(a.PrimitiveCount == b.PrimitiveCount);
		CHECK(a.LeftChild == b.LeftChild);
		CHECK(a.RightChild == b.RightChild);
	}

	for (size_t i = 0; i < first.GetPrimitives().size(); i++)
	{
		CHECK(first.GetPrimitives()[i].TriangleId == second.GetPrimitives()[i].TriangleId);
		CHECK_NEAR(first.GetPrimitives()[i].Centroid, second.GetPrimitives()[i].Centroid, 1e-6f);
	}

	const Core::BVHStats& statsA = first.GetStats();
	const Core::BVHStats& statsB = second.GetStats();
	CHECK(statsA.NodeCount == statsB.NodeCount);
	CHECK(statsA.LeafCount == statsB.LeafCount);
	CHECK(statsA.MaxDepth == statsB.MaxDepth);
}

TEST_CASE(SplitAxisPrefersXWhenStrictlyLargestExtent)
{
	// Centroids: extent.x = 10 (largest), extent.y = 1, extent.z = 1.
	const Core::Mesh mesh = MakePointTriangleMesh({
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(10.0f, 1.0f, 1.0f)
	});

	const Core::BVH bvh(mesh, 1);
	const Core::BVHNode& root = bvh.GetNodes()[Core::BVH::RootIndex];
	CHECK(!root.IsLeaf());

	// Sorting ascending by centroid.x puts triangle 0 (x=0) on the left.
	const Core::BVHNode& left = bvh.GetNodes()[root.LeftChild];
	CHECK(left.IsLeaf());
	CHECK(bvh.GetPrimitives()[left.FirstPrimitive].TriangleId == 0);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(SplitAxisPrefersYWhenStrictlyLargestAndNotTiedWithX)
{
	// extent.x = 1, extent.y = 10 (largest), extent.z = 1.
	const Core::Mesh mesh = MakePointTriangleMesh({
		glm::vec3(1.0f, 0.0f, 1.0f),
		glm::vec3(0.0f, 10.0f, 0.0f)
	});

	const Core::BVH bvh(mesh, 1);
	const Core::BVHNode& root = bvh.GetNodes()[Core::BVH::RootIndex];
	CHECK(!root.IsLeaf());

	// Sorting ascending by centroid.y puts triangle 0 (y=0) on the left.
	const Core::BVHNode& left = bvh.GetNodes()[root.LeftChild];
	CHECK(bvh.GetPrimitives()[left.FirstPrimitive].TriangleId == 0);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(SplitAxisPrefersZWhenStrictlyLargestExtent)
{
	// extent.x = 1, extent.y = 1, extent.z = 10 (largest).
	const Core::Mesh mesh = MakePointTriangleMesh({
		glm::vec3(1.0f, 1.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 10.0f)
	});

	const Core::BVH bvh(mesh, 1);
	const Core::BVHNode& root = bvh.GetNodes()[Core::BVH::RootIndex];
	CHECK(!root.IsLeaf());

	const Core::BVHNode& left = bvh.GetNodes()[root.LeftChild];
	CHECK(bvh.GetPrimitives()[left.FirstPrimitive].TriangleId == 0);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(TieBetweenXAndYExtentPrefersX)
{
	// extent.x == extent.y == 1, extent.z = 0. Per specs/bvh.md, X must win.
	// Triangle 0 has centroid x=0 (smallest x); if X is chosen it lands in
	// the left child. If Y had been chosen instead, triangle 0 (y=1, the
	// *largest* y) would land in the right child -- the two axes disagree,
	// which is what makes this test meaningful.
	const Core::Mesh mesh = MakePointTriangleMesh({
		glm::vec3(0.0f, 1.0f, 0.0f),
		glm::vec3(1.0f, 0.0f, 0.0f)
	});

	const Core::BVH bvh(mesh, 1);
	const Core::BVHNode& root = bvh.GetNodes()[Core::BVH::RootIndex];
	CHECK(!root.IsLeaf());

	const Core::BVHNode& left = bvh.GetNodes()[root.LeftChild];
	CHECK(bvh.GetPrimitives()[left.FirstPrimitive].TriangleId == 0);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(TieBetweenYAndZExtentPrefersY)
{
	// extent.x = 0, extent.y == extent.z == 1. Y must win over Z.
	// Triangle 0 has the smallest y (0) but the largest z (1); the two axes
	// disagree on which side triangle 0 belongs.
	const Core::Mesh mesh = MakePointTriangleMesh({
		glm::vec3(0.0f, 0.0f, 1.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)
	});

	const Core::BVH bvh(mesh, 1);
	const Core::BVHNode& root = bvh.GetNodes()[Core::BVH::RootIndex];
	CHECK(!root.IsLeaf());

	const Core::BVHNode& left = bvh.GetNodes()[root.LeftChild];
	CHECK(bvh.GetPrimitives()[left.FirstPrimitive].TriangleId == 0);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(PartialCentroidTiesCrossingTheMedianAreOrderedByTriangleId)
{
	// 4 primitives on the x axis; ids 1 and 2 share the exact same
	// centroid.x = 5, and that shared value straddles the median split
	// point (mid = begin + 4/2 = 2): plain centroid-only comparison can't
	// tell the two apart, so std::nth_element is free to put *either* of
	// them in the left partition and the other in the right one -- unlike
	// the "all centroids equal" degenerate case, this is only a *partial*
	// tie among otherwise well-separated primitives, and it is exactly the
	// case the TriangleId tie-break in BVH.cpp resolves. With the
	// tie-break, id 1 (the smaller id) must always land left and id 2 must
	// always land right.
	const Core::Mesh mesh = MakePointTriangleMesh({
		glm::vec3(0.0f, 0.0f, 0.0f),  // id 0, unambiguously leftmost
		glm::vec3(5.0f, 0.0f, 0.0f),  // id 1, tied with id 2 on x
		glm::vec3(5.0f, 0.0f, 0.0f),  // id 2, tied with id 1 on x
		glm::vec3(10.0f, 0.0f, 0.0f), // id 3, unambiguously rightmost
	});

	// Returns 0 if 'triangleId' is reachable under the root's left child,
	// 1 if under the right child, or -1 if not found.
	auto FindRootSide = [](const Core::BVH& bvh, uint32_t triangleId) -> int
	{
		const Core::BVHNode& root = bvh.GetNodes()[Core::BVH::RootIndex];

		for (int side = 0; side < 2; side++)
		{
			std::vector<uint32_t> stack{ side == 0 ? root.LeftChild : root.RightChild };

			while (!stack.empty())
			{
				const uint32_t nodeIndex = stack.back();
				stack.pop_back();
				const Core::BVHNode& node = bvh.GetNodes()[nodeIndex];

				if (node.IsLeaf())
				{
					for (uint32_t i = node.FirstPrimitive; i < node.FirstPrimitive + node.PrimitiveCount; i++)
						if (bvh.GetPrimitives()[i].TriangleId == triangleId)
							return side;
				}
				else
				{
					stack.push_back(node.LeftChild);
					stack.push_back(node.RightChild);
				}
			}
		}

		return -1;
	};

	// Rebuilt at two leaf sizes that both force the top-level split, to
	// show the placement is not incidental to one particular tree shape.
	for (uint32_t leafSize : { 1u, 2u })
	{
		const Core::BVH bvh(mesh, leafSize);
		CHECK(!bvh.GetNodes()[Core::BVH::RootIndex].IsLeaf());

		CHECK(FindRootSide(bvh, 0) == 0);
		CHECK(FindRootSide(bvh, 3) == 1);

		// The deterministic outcome for the tied pair.
		CHECK(FindRootSide(bvh, 1) == 0);
		CHECK(FindRootSide(bvh, 2) == 1);

		CheckBVHInvariants(bvh, mesh);
	}
}

TEST_CASE(AllCoincidentCentroidsTerminateAsASingleLeaf)
{
	// specs/bvh.md "Degenerate Cases": all centroids equal / zero centroid
	// extent. Every triangle occupies the exact same point, so no
	// centroid-based split can separate them; construction must still
	// terminate rather than recurse indefinitely.
	std::vector<glm::vec3> points(50, glm::vec3(3.0f, -1.0f, 2.0f));
	const Core::Mesh mesh = MakePointTriangleMesh(points);

	const Core::BVH bvh(mesh, 4);

	CHECK(!bvh.IsEmpty());
	CHECK(bvh.GetStats().PrimitiveCount == 50);
	// The degenerate-extent guard stops descending immediately, so this
	// collapses to one oversized leaf rather than a deep, pointless tree.
	CHECK(bvh.GetNodes().size() == 1);
	CHECK(bvh.GetNodes()[Core::BVH::RootIndex].PrimitiveCount == 50);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(DegenerateZeroAreaTrianglesAreIncludedWithoutBreakingInvariants)
{
	// A mix of valid triangles and zero-area (collinear/coincident-vertex)
	// triangles. specs/bvh.md requires degenerate triangles not to break
	// construction; specs/geometry.md already guarantees they have a valid
	// (possibly zero-volume) AABB and centroid.
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
	CHECK(bvh.GetStats().PrimitiveCount == mesh.GetTriangleCount());

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(LeafSizeOfZeroStillTerminatesWithOnePrimitivePerLeaf)
{
	// A configured leaf size of 0 is not clamped away; a floor of 1 keeps
	// construction from trying (and failing) to split a single primitive.
	const Core::Mesh mesh = MakeGridMesh(3); // 9 well-separated triangles
	const Core::BVH bvh(mesh, 0);

	CHECK(!bvh.IsEmpty());
	CHECK(bvh.GetStats().MaxLeafSize == 1);
	CHECK(bvh.GetStats().PrimitiveCount == 9);

	CheckBVHInvariants(bvh, mesh);
}

TEST_CASE(RandomizedMeshOfModerateSizeSatisfiesAllInvariants)
{
	// Fixed seed for reproducibility (per specs/AGENTS.md).
	const Core::Mesh mesh = MakeRandomTriangleMesh(500, 20260920u);
	const Core::BVH bvh(mesh, Core::BVH::DefaultLeafSize);

	CHECK(!bvh.IsEmpty());
	CheckBVHInvariants(bvh, mesh);

	// Sanity: the BVH must be doing *some* subdividing on 500 spread-out
	// triangles with a leaf size of 4, not collapsing to one giant leaf.
	CHECK(bvh.GetStats().LeafCount > 1);
	CHECK(bvh.GetStats().MaxDepth > 0);
}

TEST_CASE(DifferentLeafSizesOnTheSameMeshAllSatisfyInvariants)
{
	const Core::Mesh mesh = MakeRandomTriangleMesh(120, 7u);

	for (uint32_t leafSize : { 1u, 2u, 4u, 8u, 16u, 64u })
	{
		const Core::BVH bvh(mesh, leafSize);
		CheckBVHInvariants(bvh, mesh);

		for (const Core::BVHNode& node : bvh.GetNodes())
			if (node.IsLeaf())
				CHECK(node.PrimitiveCount <= std::max(leafSize, 1u));
	}
}
