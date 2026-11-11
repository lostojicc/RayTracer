#include "TriangleTraversal.h"

#include <cmath>
#include <limits>
#include <vector>

namespace Core {

	bool IntersectBruteForce(const Mesh& mesh, const Ray& ray, float tMin, float tMax, HitRecord& record)
	{
		HitRecord candidate;
		bool hitAnything = false;
		float closest = tMax;

		const uint32_t triangleCount = mesh.GetTriangleCount();

		for (uint32_t triangleId = 0; triangleId < triangleCount; triangleId++)
		{
			// Passing 'closest' as tMax means a triangle is only accepted when
			// it is nearer than the best hit so far, which also makes ties
			// resolve to the lowest triangle id.
			if (!mesh.GetTriangle(triangleId).Intersect(ray, tMin, closest, candidate))
				continue;

			candidate.TriangleId = triangleId;

			hitAnything = true;
			closest = candidate.T;
			record = candidate;
		}

		return hitAnything;
	}

	bool IntersectBVH(const BVH& bvh, const Mesh& mesh, const Ray& ray, float tMin, float tMax, HitRecord& record)
	{
		if (bvh.IsEmpty())
			return false;

		const std::vector<BVHNode>& nodes = bvh.GetNodes();
		const std::vector<BVHPrimitive>& primitives = bvh.GetPrimitives();

		HitRecord candidate;
		bool hitAnything = false;
		float closest = tMax;

		// Explicit traversal stack of node indices (specs/bvh.md "Traversal
		// should be iterative if practical"). 64 covers a balanced tree many
		// levels deeper than anything this project's test/benchmark scenes
		// build; std::vector grows past it transparently for any degenerate
		// deeper tree, so correctness never depends on this figure.
		std::vector<uint32_t> stack;
		stack.reserve(64);
		stack.push_back(BVH::RootIndex);

		while (!stack.empty())
		{
			const uint32_t nodeIndex = stack.back();
			stack.pop_back();

			const BVHNode& node = nodes[nodeIndex];

			// Step 1/2 of specs/bvh.md's CPU traversal: test this node's AABB
			// before doing anything else with it. The root is the first node
			// ever popped, so this single test covers both "test the root"
			// and "test intersected child nodes" without special-casing the
			// root.
			//
			// This re-test is required even for a node whose bounds were
			// already checked before it was pushed (see the internal-node
			// branch below): 'closest' may have shrunk since then, because a
			// sibling subtree visited in between may have found a closer
			// hit, so a node that looked reachable when pushed can have
			// become unreachable by the time it is popped.
			if (!node.Bounds.Intersects(ray, tMin, closest))
				continue;

			if (node.IsLeaf())
			{
				// Step 3: test triangles in this leaf. Same shrinking-tMax
				// scan as IntersectBruteForce, just over this leaf's
				// primitive range instead of every triangle in the mesh --
				// except for the explicit tie-break below, which
				// IntersectBruteForce gets for free from its ascending id
				// scan order but this traversal cannot (see TriangleTraversal.h).
				for (uint32_t i = node.FirstPrimitive; i < node.FirstPrimitive + node.PrimitiveCount; i++)
				{
					const uint32_t triangleId = primitives[i].TriangleId;

					// Triangle::Intersect() rejects t >= tMax, so passing
					// 'closest' unmodified would make an exact tie
					// (candidate.T == closest) indistinguishable from a
					// miss: the second of two tied triangles would be
					// silently dropped before its TriangleId could ever be
					// compared, and whichever tied triangle this traversal
					// happens to visit first would win regardless of id.
					//
					// Widening the probe to one float step past 'closest'
					// lets a bit-exact tie still be returned. This cannot
					// admit anything actually farther than 'closest': for
					// any float t, 't < nextafter(closest, +inf)' holds iff
					// 't <= closest' (there is no representable value
					// strictly between the two), so the set of candidates
					// Intersect() can return is exactly "t <= closest" here
					// -- never a candidate genuinely beyond the caller's
					// tMax contract. The explicit comparison immediately
					// below is what actually decides accept/reject; the
					// external tMax contract of this function is untouched.
					const float probeTMax = hitAnything
						? std::nextafter(closest, std::numeric_limits<float>::infinity())
						: closest;

					if (!mesh.GetTriangle(triangleId).Intersect(ray, tMin, probeTMax, candidate))
						continue;

					// Explicit, order-independent tie-break, reproducing
					// IntersectBruteForce's result exactly: strictly smaller
					// t always wins; an exact tie is won by the lower
					// TriangleId. Anything else (a candidate the widened
					// probe let through that is neither strictly closer nor
					// an improving tie) is rejected here.
					const bool strictlyCloser = !hitAnything || candidate.T < closest;
					const bool tiedWithLowerId = hitAnything && candidate.T == closest && triangleId < record.TriangleId;

					if (!strictlyCloser && !tiedWithLowerId)
						continue;

					candidate.TriangleId = triangleId;

					hitAnything = true;
					closest = candidate.T;
					record = candidate;
				}

				continue;
			}

			// Internal node: test both children's AABBs now (rather than
			// deferring entirely to the pop-time test above) so that only
			// children that are actually reachable get pushed at all, and so
			// their entry distances are available to order the push.
			//
			// Node ordering decision: push the farther child first, so the
			// nearer child is popped and processed first (the stack is
			// LIFO). This does not change the result -- every intersected
			// node is still visited, and the pop-time re-test above still
			// protects every node against a 'closest' that shrank in the
			// meantime -- but visiting the nearer child first lets a genuine
			// nearby hit shrink 'closest' before the farther subtree is
			// visited, which prunes more of it. On a tied entry distance
			// (e.g. children with coincident bounds), '<=' prefers the left
			// child as the near side, consistent with BVH construction's own
			// left-biased tie-break for coincident centroids (BVH.cpp).
			const BVHNode& left = nodes[node.LeftChild];
			const BVHNode& right = nodes[node.RightChild];

			float tEnterLeft = 0.0f;
			float tEnterRight = 0.0f;
			const bool hitLeft = left.Bounds.Intersects(ray, tMin, closest, &tEnterLeft);
			const bool hitRight = right.Bounds.Intersects(ray, tMin, closest, &tEnterRight);

			if (hitLeft && hitRight)
			{
				if (tEnterLeft <= tEnterRight)
				{
					stack.push_back(node.RightChild);
					stack.push_back(node.LeftChild);
				}
				else
				{
					stack.push_back(node.LeftChild);
					stack.push_back(node.RightChild);
				}
			}
			else if (hitLeft)
			{
				stack.push_back(node.LeftChild);
			}
			else if (hitRight)
			{
				stack.push_back(node.RightChild);
			}
		}

		return hitAnything;
	}

}
