#include "GpuContext.h"

#include "Core/Renderer/GLUtils.h"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <iostream>

namespace Benchmark {

	std::unique_ptr<GpuContext> GpuContext::Create()
	{
		if (!glfwInit())
		{
			std::cerr << "Benchmark: glfwInit() failed. GPU benchmarks require a display/OpenGL driver; "
				"use --skip-gpu to run only the CPU benchmarks.\n";
			return nullptr;
		}

		// Same context hints as Core::Window::Create() (Core/Source/Core/Window.cpp),
		// except GLFW_VISIBLE=false: this context backs no on-screen window at
		// all, since the benchmark never presents to a swapchain -- it only
		// dispatches the compute shader into its own accumulation texture and,
		// for the GPU pixel cross-check, reads that texture back directly
		// (see GpuBenchmark.cpp).
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		// Size is irrelevant (never rendered to or shown); kept small.
		GLFWwindow* window = glfwCreateWindow(64, 64, "RayTracer Benchmark (hidden)", nullptr, nullptr);
		if (!window)
		{
			std::cerr << "Benchmark: failed to create a hidden OpenGL context (glfwCreateWindow).\n";
			glfwTerminate();
			return nullptr;
		}

		glfwMakeContextCurrent(window);

		if (!gladLoadGL(glfwGetProcAddress))
		{
			std::cerr << "Benchmark: gladLoadGL() failed to load OpenGL function pointers.\n";
			glfwDestroyWindow(window);
			glfwTerminate();
			return nullptr;
		}

		Renderer::Utils::InitOpenGLDebugMessageCallback();

		std::unique_ptr<GpuContext> context(new GpuContext());
		context->m_Window = window;

		const auto* vendor = glGetString(GL_VENDOR);
		const auto* renderer = glGetString(GL_RENDERER);
		const auto* version = glGetString(GL_VERSION);

		context->m_Info.Vendor = vendor ? reinterpret_cast<const char*>(vendor) : "unknown";
		context->m_Info.Renderer = renderer ? reinterpret_cast<const char*>(renderer) : "unknown";
		context->m_Info.Version = version ? reinterpret_cast<const char*>(version) : "unknown";

		return context;
	}

	GpuContext::~GpuContext()
	{
		if (m_Window)
		{
			glfwDestroyWindow(m_Window);
			glfwTerminate();
		}
	}

}
