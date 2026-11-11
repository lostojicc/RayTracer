#pragma once

#include "AABB.h"
#include "Mesh.h"
#include "Ray.h"
#include "Triangle.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Core {

	// A BVH primitive reference (specs/bvh.md). It refers to a triangle by
	// id and caches the two quantities construction repeatedly needs (its
	// bounds and centroid) instead of duplicating the triangle's geometry.
	struct BVHPrimitive
	{
		uint32_t TriangleId = InvalidTriangleId;
		AABB Bounds;
		glm::vec3 Centroid{ 0.0f };
	};

	// A flattened BVH node (specs/bvh.md "Flattened Representation"). CPU-only;
	// contains no pointers, only indices, so it is already in a shape that is
	// suitable for GPU upload.
	//
	// Internal node: PrimitiveCount == 0. LeftChild/RightChild are indices
	// into BVH::GetNodes().
	//
	// Leaf node: PrimitiveCount > 0. The referenced primitives are the range
	// [FirstPrimitive, FirstPrimitive + PrimitiveCount) into
	// BVH::GetPrimitives(). LeftChild/RightChild are unused.
	//
	// A leaf always has PrimitiveCount >= 1: BVH::BVH() constructs no node at
	// all when there are zero triangles (see BVH::IsEmpty()), so a node's
	// PrimitiveCount reliably distinguishes leaf from internal.
	struct BVHNode
	{
		AABB Bounds;

		uint32_t FirstPrimitive = 0;
		uint32_t PrimitiveCount = 0;

		uint32_t LeftChild = 0;
		uint32_t RightChild = 0;

		bool IsLeaf() const { return PrimitiveCount > 0; }
	};

	// Aggregate statistics over a built BVH (specs/bvh.md "Statistics").
	// All fields are zero for an empty BVH (see BVH::IsEmpty()).
	struct BVHStats
	{
		uint32_t NodeCount = 0;
		uint32_t InternalCount = 0;
		uint32_t LeafCount = 0;

		// Depth of the root is 0.
		uint32_t MaxDepth = 0;

		// Only meaningful when LeafCount > 0.
		uint32_t MinLeafSize = 0;
		uint32_t MaxLeafSize = 0;
		float AverageLeafSize = 0.0f;

		// Sum of every leaf's PrimitiveCount. Equals the mesh's triangle
		// count for a non-empty BVH, since every triangle occurs in exactly
		// one leaf (specs/bvh.md "BVH Invariants").
		uint32_t PrimitiveCount = 0;
	};

	// Binary, AABB-based, centroid-median-split BVH over the triangles of a
	// Core::Mesh (specs/bvh.md). CPU-only and independent of OpenGL: it does
	// not include OpenGL headers or reference GLuint, SSBOs, GLSL types, or
	// any renderer type, so it is constructible and testable without a GL
	// context and without the App target (specs/architecture.md "Module
	// Boundary Rules").
	//
	// Construction is a single pass, building directly into the flattened
	// arrays (no intermediate pointer-based tree, so no separate flattening
	// step is required): a node's array slot is reserved before its children
	// are built, so the result is a deterministic pre-order layout with the
	// root at index 0, matching the example in specs/bvh.md.
	//
	// Construction only. CPU traversal is IntersectBVH; GPU traversal is
	// hitTrianglesBVH() after AppLayer serializes these arrays.
	class BVH
	{
	public:
		static constexpr uint32_t DefaultLeafSize = 4;

		BVH() = default;

		// Builds a BVH over every triangle in 'mesh'. Deterministic: building
		// twice from the same mesh and leafSize produces identical nodes and
		// primitives.
		//
		// leafSize is the configured maximum leaf size (specs/bvh.md
		// "Default Leaf Size"). It is not clamped to a minimum: a leaf size
		// of 0 is honoured as "split whenever more than one primitive
		// remains" rather than treated as an error, because a single
		// primitive can never be split into two non-empty halves regardless
		// of the configured leaf size -- construction always falls back to a
		// 1-primitive leaf in that case, which is what guarantees
		// termination (specs/bvh.md "Degenerate Cases").
		//
		// mesh.GetTriangleCount() == 0 produces an empty BVH (IsEmpty()),
		// per specs/bvh.md's "Zero primitives" degenerate case.
		//
		// Precondition: mesh.IsValid().
		explicit BVH(const Mesh& mesh, uint32_t leafSize = DefaultLeafSize);

		// True when the mesh had zero triangles. GetNodes() and
		// GetPrimitives() are both empty; GetStats() is all zero.
		bool IsEmpty() const { return m_Nodes.empty(); }

		// Index of the root node into GetNodes(). Only meaningful when
		// !IsEmpty().
		static constexpr uint32_t RootIndex = 0;

		// The flattened node array. nodes[RootIndex] is the root when
		// !IsEmpty().
		const std::vector<BVHNode>& GetNodes() const { return m_Nodes; }

		// The (possibly reordered, relative to mesh triangle order)
		// primitive array that leaves index into. See specs/bvh.md
		// "Primitive Ordering": each BVHPrimitive keeps its own TriangleId,
		// so a reorder never loses the mapping back to the original
		// triangle.
		const std::vector<BVHPrimitive>& GetPrimitives() const { return m_Primitives; }

		// The root's bounds, or an empty/inverted AABB if IsEmpty().
		const AABB& GetBounds() const { return m_Nodes.empty() ? m_EmptyBounds : m_Nodes[RootIndex].Bounds; }

		const BVHStats& GetStats() const { return m_Stats; }

	private:
		// Builds the subtree over primitives [begin, end) and returns its
		// node index. Appends to m_Nodes; may reorder m_Primitives[begin, end).
		uint32_t BuildRecursive(uint32_t begin, uint32_t end, uint32_t leafSize);

		BVHStats ComputeStats() const;

		std::vector<BVHNode> m_Nodes;
		std::vector<BVHPrimitive> m_Primitives;
		BVHStats m_Stats;

		// Never mutated; exists only so GetBounds() can return a reference
		// for the empty case.
		AABB m_EmptyBounds;
	};

}
