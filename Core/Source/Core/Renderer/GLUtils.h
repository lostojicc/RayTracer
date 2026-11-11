#pragma once

#include <glad/gl.h>

namespace Renderer::Utils {

	const char* GLDebugSourceToString(GLenum source);
	const char* GLDebugTypeToString(GLenum type);
	const char* GLDebugSeverityToString(GLenum severity);

	void InitOpenGLDebugMessageCallback();

}