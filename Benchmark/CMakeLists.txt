#
# Benchmark
#
# M9 benchmarking executable (specs/benchmarking.md, specs/milestones.md M9).
#
# This is a separate target from App and Tests, mirroring the existing
# App/Tests split in specs/architecture.md's "Repository Structure". Keeping
# it separate means running a benchmark is always an explicit, deliberate
# action ("./Benchmark ...") that can never be triggered accidentally by the
# interactive App simply loading a large model (specs/milestones.md M9
# acceptance criteria #8, "No accidental catastrophic interactive runs").
#
# Benchmark links Core directly and creates its own hidden GLFW/OpenGL
# context (Source/GpuContext.h/.cpp) -- it does not use Core::Application,
# Core::Window, or Core::Layer, since it needs no windowing/event/input
# machinery, only a GL context to drive the existing compute shader and
# Core::Renderer::StorageBuffer abstraction.
#
# Per specs/milestones.md M9 ("Do not modify the underlying brute-force or
# BVH algorithms unless required to expose them for measurement") and the
# instruction to keep the interactive application's own code path untouched,
# this target does not modify App/Source/AppLayer.* or
# App/Shaders/RayTracing.glsl. It reuses the existing compute shader file
# as-is and duplicates the small, already-reviewed GPU upload structs
# AppLayer.cpp defines for the same purpose (see Source/GpuUpload.h for the
# documented reasoning).

set(SOURCES
    Source/Main.cpp
    Source/BenchmarkResult.h
    Source/Stats.h
    Source/Stats.cpp
    Source/CsvWriter.h
    Source/CsvWriter.cpp
    Source/Scenes.h
    Source/Scenes.cpp
    Source/RayGeneration.h
    Source/RayGeneration.cpp
    Source/CpuBenchmark.h
    Source/CpuBenchmark.cpp
    Source/GpuContext.h
    Source/GpuContext.cpp
    Source/GpuUpload.h
    Source/GpuUpload.cpp
    Source/CameraFraming.h
    Source/CameraFraming.cpp
    Source/GpuBenchmark.h
    Source/GpuBenchmark.cpp
)

add_executable(Benchmark)

target_sources(Benchmark PRIVATE ${SOURCES})

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})

add_compile_definitions(
    GLFW_INCLUDE_NONE
)

target_link_libraries(Benchmark Core)

target_include_directories(Benchmark PRIVATE Source)

# Absolute paths to the App directory's compute shader and model assets, so
# Benchmark does not depend on being launched from a specific working
# directory the way App does (specs/architecture.md documents App's
# shader/asset loading as CWD-relative; Benchmark intentionally avoids
# reintroducing that fragility for a tool meant to be run from anywhere,
# e.g. scripts or CI).
target_compile_definitions(Benchmark PRIVATE
    BENCHMARK_APP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../App"
    BENCHMARK_DEFAULT_OUTPUT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Results"
)

set_target_properties(Benchmark PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/App"
)
