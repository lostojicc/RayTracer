#
# Core
#

set(SOURCES
vendor/stb/stb_image.h
vendor/stb/stb_image.cpp
Source/Core/Application.h
Source/Core/Application.cpp
Source/Core/Window.h
Source/Core/Window.cpp
Source/Core/Layer.h
Source/Core/Event.h
Source/Core/InputEvents.h
Source/Core/WindowEvents.h
Source/Core/Camera.h
Source/Core/Renderer/Renderer.h
Source/Core/Renderer/Renderer.cpp
Source/Core/Renderer/Shader.h
Source/Core/Renderer/Shader.cpp
Source/Core/Renderer/GLUtils.h
Source/Core/Renderer/GLUtils.cpp
Source/Core/Renderer/Sphere.h
Source/Core/Renderer/Ray.h
Source/Core/Renderer/AABB.h
Source/Core/Renderer/Triangle.h
Source/Core/Renderer/Mesh.h
Source/Core/Renderer/Mesh.cpp
Source/Core/Renderer/TriangleTraversal.h
Source/Core/Renderer/TriangleTraversal.cpp
Source/Core/Renderer/ModelImporter.h
Source/Core/Renderer/ModelImporter.cpp
Source/Core/Renderer/BVH.h
Source/Core/Renderer/BVH.cpp
)

add_library(Core STATIC)

target_sources(Core PRIVATE ${SOURCES})

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})

add_compile_definitions(
    GLFW_INCLUDE_NONE
)

target_link_libraries(Core glfw)
target_link_libraries(Core glad)
target_link_libraries(Core glm)

target_include_directories(Core PUBLIC "Source" "vendor/stb")