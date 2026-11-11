#pragma once

#include <memory>
#include <string>

struct GLFWwindow;

namespace Benchmark {

	struct GpuContextInfo
	{
		std::string Vendor;
		std::string Renderer;
		std::string Version;
	};

	// A minimal, hidden (never shown) OpenGL 4.6 core context, created and
	// owned independently of Core::Application/Core::Window/Core::Layer.
	//
	// The benchmark tool has no need for the windowing/event/input machinery
	// those classes provide: it only needs a current OpenGL context capable
	// of running the existing compute shader (App/Shaders/RayTracing.glsl)
	// and the existing Core::Renderer::StorageBuffer/Texture abstractions.
	// Building a separate, self-contained context here -- rather than
	// reusing or modifying Core::Window -- keeps Core untouched for M9 (see
	// specs/milestones.md M9, "Do not modify the underlying... unless
	// required to expose them for measurement": a GL context is
	// infrastructure the benchmark itself needs, not a change to any
	// existing traversal algorithm).
	class GpuContext
	{
	public:
		// Returns nullptr (after printing a diagnostic to stderr) if a
		// context could not be created -- e.g. no display/driver available
		// in the current environment -- so callers can skip GPU benchmarking
		// gracefully (see Main.cpp's --skip-gpu handling and its automatic
		// fallback) instead of crashing the whole benchmark run.
		static std::unique_ptr<GpuContext> Create();

		~GpuContext();

		GpuContext(const GpuContext&) = delete;
		GpuContext& operator=(const GpuContext&) = delete;

		const GpuContextInfo& GetInfo() const { return m_Info; }

	private:
		GpuContext() = default;

		GLFWwindow* m_Window = nullptr;
		GpuContextInfo m_Info;
	};

}
