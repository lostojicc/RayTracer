#include "BVH.h"

#include <algorithm>
#include <limits>

namespace Core {

	namespace {

		// Threshold on the centroid bounds' largest extent, below which every
		// primitive's centroid is treated as coincident (specs/bvh.md "Zero
		// centroid extent" / "All centroids equal"). Like the epsilons in
		// Triangle.h, this is an absolute tolerance and therefore scale
		// dependent; it is tuned for unit-scale scene geometry.
		//
		// A median split can never structurally fail to produce two
		// non-empty ranges (it partitions by count, not by a spatial
		// threshold), so this check is not required for termination. It
		// exists so that construction stops descending once a split can no
		// longer separate anything spatially, rather than building several
		// pointless extra levels of coincident-bounds nodes down to
		// leafSize.
		constexpr float DegenerateCentroidExtentEpsilon = 1e-8f;

	}

	BVH::BVH(const Mesh& mesh, uint32_t leafSize)
	{
		const uint32_t triangleCount = mesh.GetTriangleCount();

		// specs/bvh.md "Degenerate Cases": zero primitives. No root is built
		// at all; IsEmpty() is the caller-visible signal.
		if (triangleCount == 0)
			return;

		m_Primitives.reserve(triangleCount);
		for (uint32_t triangleId = 0; triangleId < triangleCount; triangleId++)
		{
			const Triangle triangle = mesh.GetTriangle(triangleId);

			BVHPrimitive primitive;
			primitive.TriangleId = triangleId;
			primitive.Bounds = triangle.Bounds();
			primitive.Centroid = triangle.Centroid();

			m_Primitives.push_back(primitive);
		}

		// A binary tree over N leaves has at most 2N-1 nodes; reserving
		// avoids reallocation during the common case and is harmless when
		// degenerate splits produce fewer, larger leaves.
		m_Nodes.reserve(2 * (size_t)triangleCount);

		BuildRecursive(0, triangleCount, leafSize);

		m_Stats = ComputeStats();
	}

	uint32_t BVH::BuildRecursive(uint32_t begin, uint32_t end, uint32_t leafSize)
	{
		// Reserve this node's slot before recursing into children, so the
		// flattened array ends up in pre-order (root first), matching the
		// nodes[0]/[1]/[2] example in specs/bvh.md. The node's own contents
		// are filled in via index (not a held reference) after the
		// recursive calls below, since they may reallocate m_Nodes.
		const uint32_t nodeIndex = (uint32_t)m_Nodes.size();
		m_Nodes.emplace_back();

		// Step 1: bounds over the current primitive range.
		AABB bounds;
		for (uint32_t i = begin; i < end; i++)
			bounds.Expand(m_Primitives[i].Bounds);

		const uint32_t count = end - begin;

		auto makeLeaf = [&]()
		{
			BVHNode leaf;
			leaf.Bounds = bounds;
			leaf.FirstPrimitive = begin;
			leaf.PrimitiveCount = count;
			m_Nodes[nodeIndex] = leaf;
			return nodeIndex;
		};

		// Step 2: leaf condition. A floor of 1 primitive is enforced in
		// addition to the configured leafSize: a single primitive can never
		// be split into two non-empty halves, so this is what guarantees
		// construction always terminates even if leafSize is configured as 0
		// (specs/bvh.md "Degenerate Cases" / "Construction must always
		// terminate").
		if (count <= leafSize || count <= 1)
			return makeLeaf();

		// Step 3: centroid bounds over the current primitive range.
		AABB centroidBounds;
		for (uint32_t i = begin; i < end; i++)
			centroidBounds.Expand(m_Primitives[i].Centroid);

		// Step 4: split axis = largest centroid extent. X wins ties with Y;
		// whichever of those wins ties with Z; deterministic per
		// specs/bvh.md ("X if extent.x is largest, Y if extent.y is
		// largest, Z otherwise").
		const glm::vec3 extent = centroidBounds.Extent();
		int axis = 0;
		if (extent.y > extent[axis]) axis = 1;
		if (extent.z > extent[axis]) axis = 2;

		// Degenerate case: every centroid in range is (numerically)
		// coincident, so no position along any axis can separate them
		// spatially. 'axis' was chosen as the largest extent, so checking
		// only that axis is equivalent to checking all three.
		if (extent[axis] <= DegenerateCentroidExtentEpsilon)
			return makeLeaf();

		// Steps 5-6: partition by centroid on 'axis', split at the median.
		// std::nth_element always produces two ranges of the requested
		// sizes, so -- unlike a threshold-based partition -- this step
		// cannot itself produce an empty range; "both resulting ranges
		// contain at least one primitive" (specs/bvh.md) follows directly
		// from count > 1 above.
		//
		// The comparator orders primarily by Centroid[axis] and, when that
		// is equal, by TriangleId. Without the tie-break, primitives with
		// equal Centroid[axis] would be "equivalent" to std::nth_element,
		// which only guarantees a strict-weak-ordering comparator, not a
		// stable one: for a partial tie straddling 'mid', the standard does
		// not specify which equivalent elements land on which side, so that
		// placement would be free to vary across standard-library
		// implementations/versions even though it is incidentally repeatable
		// within a single fixed build. TriangleId is unique per primitive
		// and never changes across partition steps, so this comparator is a
		// strict *total* order over the range: no two distinct primitives
		// can compare equivalent, so nth_element's result is fully
		// determined for any conforming implementation. This is the
		// deterministic tie-break for the partition step, matching the
		// deterministic tie-break already used for axis selection above.
		const uint32_t mid = begin + count / 2;
		std::nth_element(
			m_Primitives.begin() + begin,
			m_Primitives.begin() + mid,
			m_Primitives.begin() + end,
			[axis](const BVHPrimitive& a, const BVHPrimitive& b)
			{
				if (a.Centroid[axis] != b.Centroid[axis])
					return a.Centroid[axis] < b.Centroid[axis];
				return a.TriangleId < b.TriangleId;
			});

		// Step 7: recurse. Left is built (and its whole subtree appended)
		// before right, per the pre-order layout described above.
		const uint32_t leftChild = BuildRecursive(begin, mid, leafSize);
		const uint32_t rightChild = BuildRecursive(mid, end, leafSize);

		BVHNode node;
		node.Bounds = bounds;
		node.FirstPrimitive = 0;
		node.PrimitiveCount = 0;
		node.LeftChild = leftChild;
		node.RightChild = rightChild;
		m_Nodes[nodeIndex] = node;

		return nodeIndex;
	}

	BVHStats BVH::ComputeStats() const
	{
		BVHStats stats;

		if (m_Nodes.empty())
			return stats;

		stats.MinLeafSize = std::numeric_limits<uint32_t>::max();

		// Iterative depth-first walk (index + depth pairs), matching the
		// iterative style used by IntersectBVH and the GPU traversal.
		std::vector<std::pair<uint32_t, uint32_t>> stack;
		stack.reserve(m_Nodes.size());
		stack.emplace_back(RootIndex, 0u);

		while (!stack.empty())
		{
			const auto [nodeIndex, depth] = stack.back();
			stack.pop_back();

			const BVHNode& node = m_Nodes[nodeIndex];

			stats.NodeCount++;
			stats.MaxDepth = std::max(stats.MaxDepth, depth);

			if (node.IsLeaf())
			{
				stats.LeafCount++;
				stats.PrimitiveCount += node.PrimitiveCount;
				stats.MinLeafSize = std::min(stats.MinLeafSize, node.PrimitiveCount);
				stats.MaxLeafSize = std::max(stats.MaxLeafSize, node.PrimitiveCount);
			}
			else
			{
				stats.InternalCount++;
				stack.emplace_back(node.LeftChild, depth + 1);
				stack.emplace_back(node.RightChild, depth + 1);
			}
		}

		stats.AverageLeafSize = stats.LeafCount > 0
			? (float)stats.PrimitiveCount / (float)stats.LeafCount
			: 0.0f;

		if (stats.LeafCount == 0)
			stats.MinLeafSize = 0;

		return stats;
	}

}
