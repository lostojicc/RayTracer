#pragma once

#include <cstdint>
#include <vector>

#include "Core/Layer.h"
#include "Core/Camera.h"
#include "Core/Renderer/BVH.h"
#include "Core/Renderer/Mesh.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/Sphere.h"

#include "EditorCameraController.h"

namespace Core {
	class KeyPressedEvent;
}

// The two GPU triangle traversal strategies (specs/gpu-integration.md
// "Traversal Mode"). Owned by AppLayer, selected via u_TraversalMode, and
// toggled at runtime without recompiling (see AppLayer::OnEvent).
enum class TraversalMode : int
{
	BruteForce = 0,
	BVH = 1,
};

class AppLayer : public Core::Layer
{
public:
	AppLayer();
	virtual ~AppLayer();

	virtual void OnUpdate(float ts) override;
	virtual void OnRender() override;
	virtual void OnEvent(Core::Event& event) override;
private:
	void EnsureAccumulationTexture(uint32_t width, uint32_t height);
	bool OnKeyPressed(Core::KeyPressedEvent& event);

	// Shaders
	uint32_t m_ComputeShader = 0;
	uint32_t m_QuadShader = 0;

	// Quad buffers
	uint32_t m_VertexArray = 0;
	uint32_t m_VertexBuffer = 0;
	uint32_t m_ElementBuffer = 0;

	// Scene buffers
	uint32_t m_SphereBuffer = 0;
	Renderer::StorageBuffer m_TriangleBuffer;

	// GPU BVH buffers (specs/gpu-integration.md "GPU BVH"). Built once
	// from m_BVH/m_TriangleMesh and never rebuilt per frame (specs/
	// gpu-integration.md "GPU Data Lifecycle"). See AppLayer.cpp for the
	// GPU node layout.
	Renderer::StorageBuffer m_BVHNodeBuffer;
	Renderer::StorageBuffer m_BVHPrimitiveBuffer;
	uint32_t m_BVHNodeCount = 0;

	// Progressive accumulation
	Renderer::Texture m_AccumulationTexture;
	uint32_t m_FrameIndex = 0;

	// Camera
	Core::Camera m_Camera;
	EditorCameraController m_CameraController;

	// Scene
	std::vector<Core::Sphere> m_Spheres;

	// Triangle geometry (specs/geometry.md, specs/gpu-integration.md).
	// Interactive scene: the imported Bunny mesh (see AppLayer.cpp).
	Core::Mesh m_TriangleMesh;
	uint32_t m_TriangleCount = 0;

	// Authoritative CPU BVH (Core::BVH) built once over m_TriangleMesh.
	// Construction remains entirely CPU-side; AppLayer only serializes the
	// already-built flattened arrays into GPU buffers.
	Core::BVH m_BVH;

	// Selects which GPU traversal path hitWorld() uses for triangles.
	// Defaults to BVH for the Bunny scene; press B to switch.
	TraversalMode m_TraversalMode = TraversalMode::BVH;
};
