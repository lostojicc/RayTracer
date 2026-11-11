#include "Mesh.h"

#include <cassert>

namespace Core {

	Mesh::Mesh(std::vector<glm::vec3> vertices, std::vector<glm::uvec3> indices)
		: m_Vertices(std::move(vertices)), m_Indices(std::move(indices))
	{
		RecomputeBounds();
	}

	Triangle Mesh::GetTriangle(uint32_t triangleId) const
	{
		assert(triangleId < m_Indices.size());

		const glm::uvec3& index = m_Indices[triangleId];

		assert(index.x < m_Vertices.size());
		assert(index.y < m_Vertices.size());
		assert(index.z < m_Vertices.size());

		return Triangle{ m_Vertices[index.x], m_Vertices[index.y], m_Vertices[index.z] };
	}

	bool Mesh::IsValid() const
	{
		const uint32_t vertexCount = GetVertexCount();

		for (const glm::uvec3& index : m_Indices)
		{
			if (index.x >= vertexCount || index.y >= vertexCount || index.z >= vertexCount)
				return false;
		}

		return true;
	}

	void Mesh::RecomputeBounds()
	{
		m_Bounds = AABB();

		// Bounds are taken over the vertex array rather than over triangles so
		// that construction cannot dereference an out-of-range index. Unused
		// vertices only make the bounds more conservative, never incorrect.
		for (const glm::vec3& vertex : m_Vertices)
			m_Bounds.Expand(vertex);
	}

}
