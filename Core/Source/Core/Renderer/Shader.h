#pragma once

#include <filesystem>

namespace Renderer {

	uint32_t CreateComputeShader(const std::filesystem::path& path);
	uint32_t ReloadComputeShader(uint32_t shaderHandle, const std::filesystem::path& path);

	uint32_t CreateGraphicsShader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
	uint32_t ReloadGraphicsShader(uint32_t shaderHandle, const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);

}