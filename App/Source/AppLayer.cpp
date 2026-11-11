#include "AppLayer.h"

#include "Core/Application.h"
#include "Core/InputEvents.h"
#include "Core/Renderer/BVH.h"
#include "Core/Renderer/Mesh.h"
#include "Core/Renderer/ModelImporter.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/Shader.h"
#include "Core/Renderer/Sphere.h"
#include "Core/Renderer/Triangle.h"
#include "glad/gl.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

static constexpr uint32_t s_WorkGroupSize = 8;

// --- M7: GPU triangle upload representation --------------------------------
//
// specs/gpu-integration.md ("CPU Structures vs GPU Buffer Layouts") requires
// every GPU-facing structure to document its CPU layout, GLSL layout,
// alignment, array stride, and serialisation path before it is used, and to
// validate that agreement with a static_assert rather than relying on
// sizeof() matching by coincidence (the sphere buffer is explicitly called
// out as the coincidence *not* to repeat).
//
// Core::Triangle (Core/Source/Core/Renderer/Triangle.h) is 3 tightly packed
// glm::vec3 (36 bytes, 4-byte aligned) and is documented there as CPU-only:
// it is deliberately *not* uploaded directly.
//
// GLSL 'Triangle' (App/Shaders/RayTracing.glsl), inside the std430
// 'TriangleBuffer' SSBO:
//   vec3 v0;  // offset  0, base alignment 16 (a GLSL vec3's base alignment
//   vec3 v1;  // offset 16,  is always rounded up to vec4's, in both std140
//   vec3 v2;  // offset 32,  and std430 -- this is not a std140-only rule)
//   // struct size 48 (rounded up to a multiple of the 16-byte base
//   // alignment), array stride 48.
//
// AppGPUTriangle below is the explicit CPU-side mirror of that layout: each
// glm::vec3 is followed by an explicit 4-byte pad field. Because glm::vec3
// has no compiler-inserted padding of its own (alignof(glm::vec3) == 4), the
// three (vec3, float) pairs pack contiguously with no additional compiler
// padding, so sizeof/offsetof exactly match the GLSL side. This is verified
// below by static_assert, not assumed.
//
// Serialisation: BuildGPUTriangles() converts every Core::Triangle in a
// Core::Mesh into an AppGPUTriangle explicitly (copying V0/V1/V2 and leaving
// the pad fields default-initialised); nothing is memcpy'd from Core::Triangle.
//
// Buffer type: GL_SHADER_STORAGE_BUFFER, std430, binding index 1 (binding 0
// remains the existing std140 sphere buffer; see specs/gpu-integration.md
// "the sphere SSBO keeps std140 and binding point 0" / "new SSBOs start at
// binding 1"). AppLayer owns this decision, per specs/architecture.md
// "Module Boundary Rules" ("AppLayer decides what is uploaded, which buffers
// exist, and which binding points they use").
namespace {

	struct AppGPUTriangle
	{
		glm::vec3 V0{ 0.0f }; float _Pad0 = 0.0f;
		glm::vec3 V1{ 0.0f }; float _Pad1 = 0.0f;
		glm::vec3 V2{ 0.0f }; float _Pad2 = 0.0f;
	};

	static_assert(sizeof(AppGPUTriangle) == 48,
		"AppGPUTriangle must match the GLSL std430 'Triangle' struct size "
		"(3 vec3 members, each rounded up to a 16-byte stride).");
	static_assert(offsetof(AppGPUTriangle, V0) == 0, "GLSL Triangle.v0 offset");
	static_assert(offsetof(AppGPUTriangle, V1) == 16, "GLSL Triangle.v1 offset");
	static_assert(offsetof(AppGPUTriangle, V2) == 32, "GLSL Triangle.v2 offset");

	// Explicit CPU -> GPU serialisation. Never memcpy Core::Triangle directly
	// (see the comment block above): its layout is not the GLSL layout.
	std::vector<AppGPUTriangle> BuildGPUTriangles(const Core::Mesh& mesh)
	{
		std::vector<AppGPUTriangle> result;
		result.reserve(mesh.GetTriangleCount());

		for (uint32_t triangleId = 0; triangleId < mesh.GetTriangleCount(); triangleId++)
		{
			const Core::Triangle triangle = mesh.GetTriangle(triangleId);

			AppGPUTriangle gpuTriangle;
			gpuTriangle.V0 = triangle.V0;
			gpuTriangle.V1 = triangle.V1;
			gpuTriangle.V2 = triangle.V2;
			result.push_back(gpuTriangle);
		}

		return result;
	}

	// --- M8: GPU BVH upload representation ---------------------------------
	//
	// specs/bvh.md requires GPU BVH construction to be out of scope: the CPU
	// Core::BVH (M5) is the only construction algorithm. This section only
	// serializes its already-flattened Core::BVHNode/Core::BVHPrimitive
	// arrays (Core/Source/Core/Renderer/BVH.h) into an explicit GPU upload
	// layout, exactly mirroring the AppGPUTriangle derivation above.
	//
	// Core::BVHNode (CPU, Core/Source/Core/Renderer/BVH.h) is:
	//   AABB Bounds;          // 24 bytes: glm::vec3 Min, glm::vec3 Max (4-byte aligned)
	//   uint32_t FirstPrimitive;
	//   uint32_t PrimitiveCount;
	//   uint32_t LeftChild;
	//   uint32_t RightChild;
	// sizeof(Core::BVHNode) == 40, tightly packed, 4-byte aligned -- CPU-only,
	// not uploaded directly (same reasoning as Core::Triangle).
	//
	// GLSL 'BVHNode' (App/Shaders/RayTracing.glsl), inside the std430
	// 'BVHNodeBuffer' SSBO:
	//   vec3 boundsMin;       // offset  0, base alignment 16
	//   vec3 boundsMax;       // offset 16, base alignment 16
	//   uint firstPrimitive;  // offset 28, base alignment 4 (a scalar
	//   uint primitiveCount;  // offset 32,  immediately after a vec3 only
	//   uint leftChild;       // offset 36,  needs 4-byte alignment, not 16 --
	//   uint rightChild;      // offset 40,  unlike vec3-after-vec3 above)
	//   // raw size 44, struct base alignment 16 (from vec3) => struct
	//   // size/array stride rounds up to 48.
	//
	// This offset pattern (28, not 32, for the first scalar after two vec3s)
	// is *not* the same shape as AppGPUTriangle (three consecutive vec3s,
	// where every member needs 16-byte alignment). Blindly reusing the
	// "pad every vec3 to 16 bytes" rule here would misplace FirstPrimitive
	// at CPU offset 32 while the GPU reads it from offset 28 -- silent
	// corruption. AppGPUBVHNode below places padding only where the *next*
	// member's own alignment actually requires it, and every offset is
	// verified by static_assert rather than assumed, per
	// specs/gpu-integration.md "Static validation".
	//
	// PrimitiveCount > 0 means leaf (FirstPrimitive/PrimitiveCount valid);
	// PrimitiveCount == 0 means internal (LeftChild/RightChild valid) --
	// identical convention to Core::BVHNode::IsLeaf().
	struct AppGPUBVHNode
	{
		glm::vec3 BoundsMin{ 0.0f }; float _Pad0 = 0.0f; // pads BoundsMax to offset 16
		glm::vec3 BoundsMax{ 0.0f };                     // no trailing pad: next member only needs 4-byte alignment
		uint32_t FirstPrimitive = 0;
		uint32_t PrimitiveCount = 0;
		uint32_t LeftChild = 0;
		uint32_t RightChild = 0;
		uint32_t _Pad1 = 0; // trailing pad: GLSL struct/array stride rounds up to 48 (multiple of the vec3 base alignment, 16)
	};

	static_assert(sizeof(AppGPUBVHNode) == 48,
		"AppGPUBVHNode must match the GLSL std430 'BVHNode' array stride "
		"(raw size 44, rounded up to a multiple of 16).");
	static_assert(offsetof(AppGPUBVHNode, BoundsMin) == 0, "GLSL BVHNode.boundsMin offset");
	static_assert(offsetof(AppGPUBVHNode, BoundsMax) == 16, "GLSL BVHNode.boundsMax offset");
	static_assert(offsetof(AppGPUBVHNode, FirstPrimitive) == 28, "GLSL BVHNode.firstPrimitive offset");
	static_assert(offsetof(AppGPUBVHNode, PrimitiveCount) == 32, "GLSL BVHNode.primitiveCount offset");
	static_assert(offsetof(AppGPUBVHNode, LeftChild) == 36, "GLSL BVHNode.leftChild offset");
	static_assert(offsetof(AppGPUBVHNode, RightChild) == 40, "GLSL BVHNode.rightChild offset");

	// Explicit CPU -> GPU serialisation, mirroring BuildGPUTriangles(). Never
	// memcpy Core::BVHNode directly: its layout is not the GLSL layout.
	std::vector<AppGPUBVHNode> BuildGPUBVHNodes(const Core::BVH& bvh)
	{
		std::vector<AppGPUBVHNode> result;
		result.reserve(bvh.GetNodes().size());

		for (const Core::BVHNode& node : bvh.GetNodes())
		{
			AppGPUBVHNode gpuNode;
			gpuNode.BoundsMin = node.Bounds.Min;
			gpuNode.BoundsMax = node.Bounds.Max;
			gpuNode.FirstPrimitive = node.FirstPrimitive;
			gpuNode.PrimitiveCount = node.PrimitiveCount;
			gpuNode.LeftChild = node.LeftChild;
			gpuNode.RightChild = node.RightChild;
			result.push_back(gpuNode);
		}

		return result;
	}

	// The BVH's flattened primitive-reference array, reduced to just the
	// TriangleId each slot refers to (specs/bvh.md "Use the existing
	// flattened primitive-reference array and triangle IDs" -- Bounds and
	// Centroid are only needed for CPU construction and are not uploaded).
	//
	// GLSL side: a plain 'uint' array in a std430 SSBO
	// (BVHPrimitiveBuffer.primitiveTriangleIds[]). std430 does not round a
	// scalar array's stride up to 16 bytes (specs/gpu-integration.md
	// "Layout qualifier"), so this is a direct 4-byte-per-element upload with
	// no wrapper struct or padding required; sizeof(uint32_t) == 4 matches
	// the GLSL 'uint' array element size exactly.
	std::vector<uint32_t> BuildGPUBVHPrimitiveIds(const Core::BVH& bvh)
	{
		std::vector<uint32_t> result;
		result.reserve(bvh.GetPrimitives().size());

		for (const Core::BVHPrimitive& primitive : bvh.GetPrimitives())
			result.push_back(primitive.TriangleId);

		return result;
	}

	// Interactive scene: the Stanford Bunny (App/Assets/bunny.obj).
	// GPU BVH is the default traversal mode for this scene because brute-force
	// over large meshes is not interactive. Press B to switch modes.
	Core::Mesh BuildTriangleScene()
	{
		Core::ModelImportResult importResult = Core::ImportObjFile("Assets/bunny.obj");
		if (!importResult.Success)
		{
			std::cerr << "Failed to import Assets/bunny.obj: " << importResult.Error << "\n";
			return Core::Mesh();
		}

		return std::move(importResult.ImportedMesh);
	}

	void FrameCameraOnMesh(Core::Camera& camera, const Core::Mesh& mesh)
	{
		const Core::AABB& bounds = mesh.GetBounds();
		const glm::vec3 center = bounds.IsEmpty() ? glm::vec3(0.0f) : bounds.Centroid();
		const float boundingRadius = bounds.IsEmpty() ? 1.0f : (glm::length(bounds.Extent()) * 0.5f);
		const float safeRadius = std::max(boundingRadius, 0.05f);
		const glm::vec3 forward = glm::normalize(glm::vec3(-0.35f, -0.22f, -0.9f));
		const float focalLength = camera.GetFocalLength();
		const float halfFovY = std::atan(1.0f / focalLength);
		constexpr float kMarginFactor = 1.35f;
		const float distance = (safeRadius / std::sin(halfFovY)) * kMarginFactor;

		camera.SetPosition(center - forward * distance);
		camera.SetDirection(forward);
		camera.SetUp(glm::vec3(0.0f, 1.0f, 0.0f));
		camera.ClearUpdated();
	}

}

AppLayer::AppLayer()
	: m_CameraController(m_Camera)
{
	const auto* vendor = glGetString(GL_VENDOR);
	const auto* renderer = glGetString(GL_RENDERER);
	const auto* version = glGetString(GL_VERSION);

	std::cout << "OpenGL Vendor:   "
			<< reinterpret_cast<const char*>(vendor) << '\n';

	std::cout << "OpenGL Renderer: "
			<< reinterpret_cast<const char*>(renderer) << '\n';

	std::cout << "OpenGL Version:  "
			<< reinterpret_cast<const char*>(version) << '\n';

	m_ComputeShader = Renderer::CreateComputeShader("Shaders/RayTracing.glsl");

	m_QuadShader = Renderer::CreateGraphicsShader(
		"Shaders/Vertex.glsl",
		"Shaders/Fragment.glsl"
	);

	float quadVertices[] = {
		1.0f,  1.0f, 0.0f,  // top right
		1.0f, -1.0f, 0.0f,  // bottom right
		-1.0f, -1.0f, 0.0f,  // bottom left
		-1.0f,  1.0f, 0.0f   // top left
	};
	unsigned int quadIndices[] = {  // note that we start from 0!
		0, 1, 3,   // first triangle
		1, 2, 3    // second triangle
	};

	glGenVertexArrays(1, &m_VertexArray);
	glGenBuffers(1, &m_VertexBuffer);
	glGenBuffers(1, &m_ElementBuffer);

	glBindVertexArray(m_VertexArray);

	glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ElementBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	// Armadillo-only triangle scene: no spheres, so hitWorld() is entirely
	// triangle/BVH work. A 1-element placeholder SSBO is still bound so a
	// zero-sized buffer is never used while u_SphereCount is 0.
	m_Spheres.clear();
	{
		Core::Sphere empty{};
		glGenBuffers(1, &m_SphereBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_SphereBuffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Core::Sphere), &empty, GL_DYNAMIC_DRAW);
	}

	// M7: triangle geometry. Build the CPU scene once (Core::Mesh is
	// OpenGL-independent; this call touches no GL state), then upload the
	// explicit GPU triangle representation to a std430 SSBO at binding 1
	// using the thin Core::Renderer storage-buffer abstraction. This mirrors
	// specs/gpu-integration.md's "GPU Data Lifecycle" up through "Upload
	// triangle data".
	m_TriangleMesh = BuildTriangleScene();
	m_TriangleCount = m_TriangleMesh.GetTriangleCount();

	const std::vector<AppGPUTriangle> gpuTriangles = BuildGPUTriangles(m_TriangleMesh);

	// gpuTriangles may be empty if every import failed; upload a 1-element
	// buffer in that case so a 0-sized SSBO is never bound while
	// u_TriangleCount could plausibly be nonzero elsewhere. The shader never
	// indexes past u_TriangleCount, so the extra element's contents are
	// unused, but a defined buffer size is one less thing to reason about.
	if (gpuTriangles.empty())
	{
		AppGPUTriangle empty;
		m_TriangleBuffer = Renderer::CreateStorageBuffer(&empty, sizeof(AppGPUTriangle));
	}
	else
	{
		m_TriangleBuffer = Renderer::CreateStorageBuffer(gpuTriangles.data(), gpuTriangles.size() * sizeof(AppGPUTriangle));
	}

	// M8: build the CPU BVH once over the same mesh (Core::BVH, M5 -- the
	// only BVH construction algorithm in this project; see BVH.h/.cpp) and
	// serialize its flattened arrays into two GPU buffers: nodes (binding 2)
	// and the primitive -> TriangleId reference array (binding 3). Like the
	// triangle buffer above, this happens once in the constructor, not per
	// frame (specs/gpu-integration.md "GPU Data Lifecycle": "The BVH must
	// not be rebuilt every frame").
	m_BVH = Core::BVH(m_TriangleMesh);
	m_BVHNodeCount = (uint32_t)m_BVH.GetNodes().size();

	const std::vector<AppGPUBVHNode> gpuBVHNodes = BuildGPUBVHNodes(m_BVH);
	const std::vector<uint32_t> gpuBVHPrimitiveIds = BuildGPUBVHPrimitiveIds(m_BVH);

	// An empty mesh produces an empty BVH (Core::BVH::IsEmpty()); upload a
	// 1-element placeholder buffer in that case for the same reason as the
	// triangle buffer above. u_BVHNodeCount is 0, so the shader's BVH
	// traversal never reads it.
	if (gpuBVHNodes.empty())
	{
		AppGPUBVHNode empty;
		m_BVHNodeBuffer = Renderer::CreateStorageBuffer(&empty, sizeof(AppGPUBVHNode));
	}
	else
	{
		m_BVHNodeBuffer = Renderer::CreateStorageBuffer(gpuBVHNodes.data(), gpuBVHNodes.size() * sizeof(AppGPUBVHNode));
	}

	if (gpuBVHPrimitiveIds.empty())
	{
		uint32_t empty = 0;
		m_BVHPrimitiveBuffer = Renderer::CreateStorageBuffer(&empty, sizeof(uint32_t));
	}
	else
	{
		m_BVHPrimitiveBuffer = Renderer::CreateStorageBuffer(gpuBVHPrimitiveIds.data(), gpuBVHPrimitiveIds.size() * sizeof(uint32_t));
	}

	FrameCameraOnMesh(m_Camera, m_TriangleMesh);
	m_CameraController.SyncOrientationFromCamera();

	const Core::BVHStats& stats = m_BVH.GetStats();
	std::cout << "Scene: Bunny (" << m_TriangleCount << " triangles)\n";
	std::cout << "BVH: nodes=" << stats.NodeCount
		<< " leaves=" << stats.LeafCount
		<< " maxDepth=" << stats.MaxDepth << '\n';
	std::cout << "Triangle traversal mode: BVH (press B to toggle)\n";
}

AppLayer::~AppLayer()
{
	glDeleteVertexArrays(1, &m_VertexArray);
	glDeleteBuffers(1, &m_VertexBuffer);
	glDeleteBuffers(1, &m_ElementBuffer);
	glDeleteBuffers(1, &m_SphereBuffer);
	Renderer::DestroyStorageBuffer(m_TriangleBuffer);
	Renderer::DestroyStorageBuffer(m_BVHNodeBuffer);
	Renderer::DestroyStorageBuffer(m_BVHPrimitiveBuffer);

	if (m_AccumulationTexture.Handle != 0)
		glDeleteTextures(1, &m_AccumulationTexture.Handle);

	glDeleteProgram(m_QuadShader);
	glDeleteProgram(m_ComputeShader);
}

void AppLayer::EnsureAccumulationTexture(uint32_t width, uint32_t height)
{
	if (m_AccumulationTexture.Handle != 0 &&
		m_AccumulationTexture.Width == width &&
		m_AccumulationTexture.Height == height)
		return;

	if (m_AccumulationTexture.Handle != 0)
		glDeleteTextures(1, &m_AccumulationTexture.Handle);

	m_AccumulationTexture = Renderer::CreateTexture((int)width, (int)height);
	m_FrameIndex = 0;
}

void AppLayer::OnUpdate(float ts)
{
	m_CameraController.OnUpdate(ts);
}

void AppLayer::OnEvent(Core::Event& event)
{
	m_CameraController.OnEvent(event);

	// M8: traversal-mode toggle (specs/gpu-integration.md "Traversal Mode":
	// "The mode must be selectable without recompiling the application").
	// AppLayer owns render configuration (specs/architecture.md), so the key
	// is handled here rather than in EditorCameraController.
	Core::EventDispatcher dispatcher(event);
	dispatcher.Dispatch<Core::KeyPressedEvent>([this](Core::KeyPressedEvent& e) { return OnKeyPressed(e); });
}

bool AppLayer::OnKeyPressed(Core::KeyPressedEvent& event)
{
	if (event.IsRepeat() || event.GetKeyCode() != GLFW_KEY_B)
		return false;

	m_TraversalMode = (m_TraversalMode == TraversalMode::BruteForce)
		? TraversalMode::BVH
		: TraversalMode::BruteForce;

	std::cout << "Triangle traversal mode: "
		<< (m_TraversalMode == TraversalMode::BVH ? "BVH" : "BruteForce") << '\n';

	// specs/gpu-integration.md "Traversal Mode": "Changing traversal mode
	// must reset frame accumulation, otherwise two different images are
	// blended into one accumulation buffer."
	m_FrameIndex = 0;

	return true;
}

void AppLayer::OnRender()
{
	glm::vec2 framebufferSize =
		Core::Application::Get().GetFramebufferSize();

	uint32_t width = (uint32_t)framebufferSize.x;
	uint32_t height = (uint32_t)framebufferSize.y;

	if (width == 0 || height == 0)
		return;

	EnsureAccumulationTexture(width, height);

	if (m_Camera.WasUpdated())
	{
		m_FrameIndex = 0;
		m_Camera.ClearUpdated();
	}

	glm::vec3 cameraPosition = m_Camera.GetPosition();
	glm::vec3 cameraForward = m_Camera.GetDirection();
	glm::vec3 cameraUp = m_Camera.GetUp();

	// Ray tracing pass
	glUseProgram(m_ComputeShader);

	glUniform3f(glGetUniformLocation(m_ComputeShader, "u_CameraPos"), cameraPosition.x, cameraPosition.y, cameraPosition.z);
	glUniform3f(glGetUniformLocation(m_ComputeShader, "u_CameraForward"), cameraForward.x, cameraForward.y, cameraForward.z);
	glUniform3f(glGetUniformLocation(m_ComputeShader, "u_CameraUp"), cameraUp.x, cameraUp.y, cameraUp.z);
	glUniform1f(glGetUniformLocation(m_ComputeShader, "u_CameraFocal"), m_Camera.GetFocalLength());
	glUniform1i(glGetUniformLocation(m_ComputeShader, "u_SphereCount"), (GLint)m_Spheres.size());
	glUniform1i(glGetUniformLocation(m_ComputeShader, "u_TriangleCount"), (GLint)m_TriangleCount);
	glUniform1i(glGetUniformLocation(m_ComputeShader, "u_BVHNodeCount"), (GLint)m_BVHNodeCount);
	glUniform1i(glGetUniformLocation(m_ComputeShader, "u_TraversalMode"), (GLint)m_TraversalMode);
	glUniform1i(glGetUniformLocation(m_ComputeShader, "u_SamplesPerPixel"), 1);
	glUniform1ui(glGetUniformLocation(m_ComputeShader, "u_FrameIndex"), m_FrameIndex);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_SphereBuffer);
	Renderer::BindStorageBuffer(m_TriangleBuffer, 1);
	Renderer::BindStorageBuffer(m_BVHNodeBuffer, 2);
	Renderer::BindStorageBuffer(m_BVHPrimitiveBuffer, 3);
	glBindImageTexture(0, m_AccumulationTexture.Handle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

	glDispatchCompute(
		(width + s_WorkGroupSize - 1) / s_WorkGroupSize,
		(height + s_WorkGroupSize - 1) / s_WorkGroupSize,
		1
	);

	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	// Present pass
	glViewport(0, 0, (GLsizei)width, (GLsizei)height);

	glUseProgram(m_QuadShader);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_AccumulationTexture.Handle);
	glUniform1i(glGetUniformLocation(m_QuadShader, "u_Texture"), 0);

	glBindVertexArray(m_VertexArray);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

	m_FrameIndex++;
}
