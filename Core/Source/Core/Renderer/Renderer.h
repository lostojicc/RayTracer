#pragma once

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <filesystem>

namespace Renderer {

	struct Texture
	{
		GLuint Handle = 0;
		uint32_t Width = 0;
		uint32_t Height = 0;
	};

	struct Framebuffer
	{
		GLuint Handle = 0;
		Texture ColorAttachment;
	};

	Texture CreateTexture(int width, int height);
	Texture LoadTexture(const std::filesystem::path& path);
	Framebuffer CreateFramebufferWithTexture(const Texture texture);
	bool AttachTextureToFramebuffer(Framebuffer& framebuffer, const Texture texture);
	void BlitFramebufferToSwapchain(const Framebuffer framebuffer);

	// Thin OpenGL Shader Storage Buffer Object (SSBO) abstraction
	// (specs/gpu-integration.md "GPU Buffer Abstraction", specs/architecture.md
	// "Module Boundary Rules" -> "GPU buffer abstraction").
	//
	// Intentionally minimal: create, upload/replace, bind, destroy. Nothing
	// else. It does not know what a "triangle" or a "BVH node" is, does not
	// track binding points itself, and does not own any scene data.
	// `AppLayer` decides what is uploaded, which buffers exist, and which
	// binding points they use.
	struct StorageBuffer
	{
		GLuint Handle = 0;
	};

	// Creates a new SSBO and uploads 'data' (sizeBytes bytes). 'data' may be
	// nullptr to allocate storage without initializing it (sizeBytes bytes of
	// undefined content).
	StorageBuffer CreateStorageBuffer(const void* data, size_t sizeBytes, GLenum usage = GL_DYNAMIC_DRAW);

	// Replaces the contents of an existing SSBO. This re-allocates storage
	// sized to 'sizeBytes' (equivalent to glNamedBufferData), so it may change
	// the buffer's capacity, not just overwrite its existing bytes.
	void UploadStorageBuffer(StorageBuffer& buffer, const void* data, size_t sizeBytes, GLenum usage = GL_DYNAMIC_DRAW);

	// Binds 'buffer' as GL_SHADER_STORAGE_BUFFER at the given SSBO binding
	// point (glBindBufferBase). Note that SSBO binding points and image unit
	// bindings (glBindImageTexture) are separate namespaces.
	void BindStorageBuffer(const StorageBuffer& buffer, GLuint bindingIndex);

	// Deletes the underlying GL buffer object and resets Handle to 0.
	void DestroyStorageBuffer(StorageBuffer& buffer);
}