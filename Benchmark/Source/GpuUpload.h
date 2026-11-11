#pragma once

#include "Core/Renderer/BVH.h"
#include "Core/Renderer/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// GPU upload representations for triangle and BVH data.
//
// These structs and their serialization functions are a deliberate,
// documented duplication of the equivalent (file-local, anonymous-namespace)
// types in App/Source/AppLayer.cpp (AppGPUTriangle / AppGPUBVHNode /
// BuildGPUTriangles / BuildGPUBVHNodes / BuildGPUBVHPrimitiveIds), not a
// shared header included by both.
//
// This was a deliberate choice for M9, recorded in specs/milestones.md as a
// decision worth revisiting: the instruction for this milestone was to leave
// App/Source/AppLayer.cpp and App/Shaders/RayTracing.glsl completely
// unmodified so the interactive application's already-reviewed M7/M8 code
// path carries zero risk of regression from benchmarking work. Extracting a
// shared header would have required editing AppLayer.cpp to use it. The cost
// is that these two copies must be kept in sync by hand if the GLSL buffer
// layouts in App/Shaders/RayTracing.glsl ever change; both copies carry the
// same static_assert-verified layout documentation as AppLayer.cpp so a
// future edit to one side without the other fails to compile rather than
// silently corrupting GPU data.
namespace GpuUpload {

	// Mirrors AppGPUTriangle (App/Source/AppLayer.cpp) exactly: 3 padded
	// glm::vec3, matching the GLSL std430 'Triangle' struct (App/Shaders/
	// RayTracing.glsl) at binding 1 -- vec3 v0/v1/v2 at offsets 0/16/32,
	// struct size 48.
	struct GPUTriangle
	{
		glm::vec3 V0{ 0.0f }; float _Pad0 = 0.0f;
		glm::vec3 V1{ 0.0f }; float _Pad1 = 0.0f;
		glm::vec3 V2{ 0.0f }; float _Pad2 = 0.0f;
	};

	static_assert(sizeof(GPUTriangle) == 48, "GpuUpload::GPUTriangle must match the GLSL std430 'Triangle' struct size.");
	static_assert(offsetof(GPUTriangle, V0) == 0, "GLSL Triangle.v0 offset");
	static_assert(offsetof(GPUTriangle, V1) == 16, "GLSL Triangle.v1 offset");
	static_assert(offsetof(GPUTriangle, V2) == 32, "GLSL Triangle.v2 offset");

	// Mirrors AppGPUBVHNode (App/Source/AppLayer.cpp) exactly: matches the
	// GLSL std430 'BVHNode' struct (App/Shaders/RayTracing.glsl) at binding
	// 2 -- vec3 boundsMin/boundsMax at 0/16, then firstPrimitive/
	// primitiveCount/leftChild/rightChild at 28/32/36/40 (a scalar
	// immediately after a vec3 only needs 4-byte alignment, unlike
	// vec3-after-vec3), struct size 48.
	struct GPUBVHNode
	{
		glm::vec3 BoundsMin{ 0.0f }; float _Pad0 = 0.0f;
		glm::vec3 BoundsMax{ 0.0f };
		uint32_t FirstPrimitive = 0;
		uint32_t PrimitiveCount = 0;
		uint32_t LeftChild = 0;
		uint32_t RightChild = 0;
		uint32_t _Pad1 = 0;
	};

	static_assert(sizeof(GPUBVHNode) == 48, "GpuUpload::GPUBVHNode must match the GLSL std430 'BVHNode' array stride.");
	static_assert(offsetof(GPUBVHNode, BoundsMin) == 0, "GLSL BVHNode.boundsMin offset");
	static_assert(offsetof(GPUBVHNode, BoundsMax) == 16, "GLSL BVHNode.boundsMax offset");
	static_assert(offsetof(GPUBVHNode, FirstPrimitive) == 28, "GLSL BVHNode.firstPrimitive offset");
	static_assert(offsetof(GPUBVHNode, PrimitiveCount) == 32, "GLSL BVHNode.primitiveCount offset");
	static_assert(offsetof(GPUBVHNode, LeftChild) == 36, "GLSL BVHNode.leftChild offset");
	static_assert(offsetof(GPUBVHNode, RightChild) == 40, "GLSL BVHNode.rightChild offset");

	// Explicit CPU -> GPU serialisation. Never memcpy Core::Triangle/
	// Core::BVHNode directly; see App/Source/AppLayer.cpp's equivalent
	// comment block for the full layout derivation this mirrors.
	std::vector<GPUTriangle> BuildGPUTriangles(const Core::Mesh& mesh);
	std::vector<GPUBVHNode> BuildGPUBVHNodes(const Core::BVH& bvh);
	std::vector<uint32_t> BuildGPUBVHPrimitiveIds(const Core::BVH& bvh);

}
