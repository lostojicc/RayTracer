#include "ModelImporter.h"

#include <cassert>
#include <charconv>
#include <fstream>
#include <sstream>

namespace Core {

	namespace {

		// Returns the index of the first '/' in a face vertex reference token
		// such as "3", "3/1", "3/1/2", or "3//2", or the token's length if
		// there is none.
		size_t FindComponentEnd(std::string_view token)
		{
			const size_t slash = token.find('/');
			return slash == std::string_view::npos ? token.size() : slash;
		}

		// Parses only the position component of a face vertex reference.
		// Returns false if that component is missing or not a valid integer.
		bool ParseFaceIndexToken(std::string_view token, int& outValue)
		{
			const std::string_view positionPart = token.substr(0, FindComponentEnd(token));
			if (positionPart.empty())
				return false;

			const auto result = std::from_chars(positionPart.data(), positionPart.data() + positionPart.size(), outValue);
			return result.ec == std::errc{} && result.ptr == positionPart.data() + positionPart.size();
		}

		// Resolves a 1-based (or negative, relative-to-current) OBJ vertex
		// index into a 0-based index into a vertex array that currently holds
		// 'vertexCount' entries. OBJ negative indices count backward from the
		// last vertex defined so far, which is exactly 'vertexCount' at the
		// point a face line is processed during a single forward pass.
		bool ResolveVertexIndex(int rawIndex, size_t vertexCount, uint32_t& outIndex)
		{
			if (rawIndex == 0)
				return false;

			const long long resolved = rawIndex > 0
				? (long long)rawIndex - 1
				: (long long)vertexCount + rawIndex;

			if (resolved < 0 || (size_t)resolved >= vertexCount)
				return false;

			outIndex = (uint32_t)resolved;
			return true;
		}

		ModelImportResult Fail(std::string error)
		{
			ModelImportResult result;
			result.Success = false;
			result.Error = std::move(error);
			return result;
		}

	}

	ModelImportResult ImportObjFile(const std::filesystem::path& path, const glm::mat4& transform)
	{
		std::ifstream file(path);
		if (!file.is_open())
			return Fail("Failed to open model file: " + path.string());

		std::ostringstream contentStream;
		contentStream << file.rdbuf();

		return ImportObjSource(contentStream.str(), transform);
	}

	ModelImportResult ImportObjSource(std::string_view source, const glm::mat4& transform)
	{
		std::vector<glm::vec3> rawPositions;
		std::vector<glm::uvec3> triangles;

		std::istringstream stream{ std::string(source) };
		std::string line;
		int lineNumber = 0;

		while (std::getline(stream, line))
		{
			lineNumber++;

			// Tolerate CRLF line endings from Windows-authored files.
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			std::istringstream lineStream(line);
			std::string directive;
			if (!(lineStream >> directive) || directive.empty() || directive[0] == '#')
				continue;

			if (directive == "v")
			{
				// A 4th homogeneous 'w' component, if present, is ignored;
				// rational/homogeneous OBJ vertices are not supported.
				float x, y, z;
				if (!(lineStream >> x >> y >> z))
					return Fail("line " + std::to_string(lineNumber) + ": malformed vertex, expected 'v x y z'");

				rawPositions.emplace_back(x, y, z);
			}
			else if (directive == "f")
			{
				std::vector<uint32_t> faceIndices;
				std::string token;

				while (lineStream >> token)
				{
					int rawIndex;
					if (!ParseFaceIndexToken(token, rawIndex))
						return Fail("line " + std::to_string(lineNumber) + ": malformed face vertex reference '" + token + "'");

					uint32_t index;
					if (!ResolveVertexIndex(rawIndex, rawPositions.size(), index))
					{
						return Fail("line " + std::to_string(lineNumber) + ": face vertex index " + std::to_string(rawIndex)
							+ " is out of range (" + std::to_string(rawPositions.size()) + " vertices defined so far)");
					}

					faceIndices.push_back(index);
				}

				if (faceIndices.size() < 3)
					return Fail("line " + std::to_string(lineNumber) + ": face has fewer than 3 vertices");

				// Deterministic fan triangulation from the face's first vertex,
				// per specs/model-import.md.
				for (size_t i = 1; i + 1 < faceIndices.size(); i++)
					triangles.emplace_back(faceIndices[0], faceIndices[i], faceIndices[i + 1]);
			}
			// 'vt', 'vn', 'vp', 'o', 'g', 's', 'mtllib', 'usemtl', 'l', and any
			// other directive are intentionally ignored: they either fall
			// outside the initial scope (specs/model-import.md) or do not
			// affect the resulting Mesh, which stores positions only
			// (specs/geometry.md).
		}

		if (rawPositions.empty())
			return Fail("model contains no vertices");

		std::vector<glm::vec3> transformedPositions;
		transformedPositions.reserve(rawPositions.size());
		for (const glm::vec3& position : rawPositions)
			transformedPositions.push_back(glm::vec3(transform * glm::vec4(position, 1.0f)));

		ModelImportResult result;
		result.ImportedMesh = Mesh(std::move(transformedPositions), std::move(triangles));
		result.Success = true;

		// Every triangle index was validated against rawPositions.size() above,
		// and transformedPositions has the same size, so the mesh is always
		// valid on success. Asserted defensively rather than assumed.
		assert(result.ImportedMesh.IsValid());

		return result;
	}

}
