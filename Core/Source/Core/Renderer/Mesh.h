#pragma once

#include "AABB.h"
#include "Triangle.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Core {

	// Indexed triangle mesh owned by the project. CPU-only, and independent of
	// any model file format, per specs/geometry.md.
	//
	// A triangle's identifier is its index into the index array. Identifiers are
	// stable for the lifetime of the mesh, so the BVH can reference a triangle
	// without duplicating its geometry.
	class Mesh
	{
	public:
		Mesh() = default;
		Mesh(std::vector<glm::vec3> vertices, std::vector<glm::uvec3> indices);

		const std::vector<glm::vec3>& GetVertices() const { return m_Vertices; }
		const std::vector<glm::uvec3>& GetIndices() const { return m_Indices; }

		uint32_t GetVertexCount() const { return (uint32_t)m_Vertices.size(); }
		uint32_t GetTriangleCount() const { return (uint32_t)m_Indices.size(); }

		Triangle GetTriangle(uint32_t triangleId) const;

		const AABB& GetBounds() const { return m_Bounds; }

		// True when every index refers to an existing vertex.
		bool IsValid() const;

	private:
		void RecomputeBounds();

		std::vector<glm::vec3> m_Vertices;
		std::vector<glm::uvec3> m_Indices;
		AABB m_Bounds;
	};

}
