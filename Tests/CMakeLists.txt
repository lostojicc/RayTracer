#
# Tests
#
# CPU geometry validation. Links Core but never touches OpenGL, which keeps the
# geometry/BVH code provably independent of a GL context.
#

set(SOURCES
    Source/TestFramework.h
    Source/TestFramework.cpp
    Source/AABBTests.cpp
    Source/TriangleTests.cpp
    Source/MeshTests.cpp
    Source/TraversalTests.cpp
    Source/ModelImporterTests.cpp
    Source/BVHTests.cpp
    Source/BVHTraversalTests.cpp
    Source/Main.cpp
)

add_executable(Tests)

target_sources(Tests PRIVATE ${SOURCES})

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})

target_link_libraries(Tests Core)

target_include_directories(Tests PRIVATE Source)

# Absolute path to the on-disk .obj test assets, so file-based import tests
# do not depend on the process's current working directory (App has this
# problem for shader loading; Tests does not need to inherit it).
target_compile_definitions(Tests PRIVATE TESTS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Assets")

add_test(NAME GeometryTests COMMAND Tests)
