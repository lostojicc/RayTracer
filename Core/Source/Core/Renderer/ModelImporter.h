#pragma once

#include "Mesh.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

	// Result of a model import.
	//
	// On failure, ImportedMesh is default-constructed (empty) and Error
	// describes what went wrong, with a 1-based source line number where
	// applicable. Callers must check Success before using ImportedMesh.
	struct ModelImportResult
	{
		Core::Mesh ImportedMesh;
		bool Success = false;
		std::string Error;
	};

	// Imports a static triangle mesh from a Wavefront OBJ file on disk.
	//
	// See ImportObjSource() for the format, coordinate system, triangulation,
	// and transform contract. This function only adds file reading on top of
	// that: if the file cannot be opened, Success is false and Error names
	// the path.
	ModelImportResult ImportObjFile(const std::filesystem::path& path, const glm::mat4& transform = glm::mat4(1.0f));

	// Imports a static triangle mesh from Wavefront OBJ source text.
	//
	// Scope (specs/model-import.md): only vertex positions ('v') and faces
	// ('f') are interpreted. Texture coordinates ('vt'), normals ('vn'),
	// parameter-space vertices ('vp'), object/group names ('o', 'g'),
	// smoothing groups ('s'), and material directives ('mtllib', 'usemtl')
	// are recognised and skipped; they do not cause an import failure and are
	// not required to appear at all. Per specs/geometry.md, Core::Mesh stores
	// positions only, so any 'vn' data is discarded; Triangle::Normal()
	// derives the geometric normal from the (already transformed) triangle
	// edges instead.
	//
	// Coordinate system: coordinates are passed through unchanged. OBJ is
	// conventionally right-handed and Y-up, matching this project's existing
	// scene and camera convention (Core::Camera's up axis, the existing
	// ground sphere in AppLayer), so no axis, handedness, or unit conversion
	// is performed. It is the caller's responsibility to ensure the source
	// file already uses a compatible coordinate system and unit scale.
	//
	// Triangulation: a face with more than three vertices is triangulated
	// deterministically as a fan from its first vertex: vertices
	// (v0, v1, v2, ..., vN-1) become triangles (v0,v1,v2), (v0,v2,v3), ...
	//
	// Transform: 'transform' is applied to every parsed vertex position
	// before the Mesh is constructed, so the returned mesh's coordinates are
	// already in final ray-intersection space (specs/model-import.md: "model
	// transforms must be resolved before BVH construction"). Because
	// Core::Mesh stores positions only and normals are always derived
	// per-triangle after the transform has been applied, non-uniform scale
	// does not require a separate inverse-transpose normal transform.
	//
	// Face vertex references may use any of the standard OBJ forms
	// ('v', 'v/vt', 'v/vt/vn', 'v//vn') and may be positive (1-based) or
	// negative (relative to the vertices defined so far, per the OBJ spec).
	// Only the position component is used.
	//
	// Comments: a line whose first token is '#' is skipped entirely. Trailing
	// inline comments after other content on the same line are not supported.
	//
	// Malformed input (unreadable numbers, zero or out-of-range vertex
	// indices, faces with fewer than three vertices, or a source with no
	// vertices) fails the import with a line-numbered Error rather than
	// producing a partial or garbage mesh.
	//
	// A source that defines vertices but no faces succeeds, producing a Mesh
	// with zero triangles (consistent with Core::Mesh's own empty-mesh
	// contract, see specs/milestones.md M1).
	ModelImportResult ImportObjSource(std::string_view source, const glm::mat4& transform = glm::mat4(1.0f));

}
