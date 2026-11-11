# AGENTS.md

## Project

This is a C++ OpenGL compute-shader path tracer developed as a university project.

The project demonstrates:

- Ray tracing
- Triangle geometry
- 3D model importing
- Bounding Volume Hierarchies
- GPU compute-shader traversal
- Performance benchmarking

The repository consists of four CMake targets plus an analysis directory:

- `Core` — static library containing reusable engine, rendering, OpenGL,
  ray-tracing, geometry, window, and event functionality.
- `App` — interactive executable using `Core`, containing application-specific
  code and shader resources.
- `Tests` — CPU geometry / BVH correctness suite (no OpenGL context).
- `Benchmark` — separate measurement executable (CPU and GPU).
- `Analysis/` — reproducible interpretation of the committed benchmark CSV
  (not a CMake target).

## Current Renderer

The renderer is an OpenGL compute-shader path tracer.

The implementation:

- Generates camera rays in `RayTracing.glsl`.
- Intersects rays with spheres and triangles.
- Uses SSBOs for spheres (binding 0, `std140`), triangles (binding 1,
  `std430`), BVH nodes (binding 2, `std430`), and BVH primitive IDs
  (binding 3, `std430`).
- Performs brute-force sphere intersection.
- Selects triangle traversal with `u_TraversalMode` (`BruteForce` or `BVH`).
- Uses simple diffuse/Lambertian scattering.
- Accumulates samples across frames.
- Renders a procedural sky when no geometry is hit.

The existing sphere implementation must remain functional throughout the project.

## Architecture Rules

### Core

New reusable rendering functionality belongs in `Core`.

This includes:

- Geometry representations
- Triangle intersection
- AABB
- BVH construction
- BVH data structures
- Ray-tracing abstractions
- GPU buffer abstractions where appropriate

### App

`App` contains application-specific behavior:

- Scene setup
- Camera controller
- Application layer
- Project-specific configuration

Do not move reusable engine functionality into `App`.

### Benchmark

Measurement orchestration belongs in the `Benchmark` target, not in `App`.
Running a benchmark must remain an explicit, deliberate action.

### Shaders

The current ray-tracing shader is located at:

`App/Shaders/RayTracing.glsl`

Do not relocate shader files or redesign shader resource management unless
required by the implementation and explicitly justified.

## Existing Code

Before implementing a feature, inspect:

- `Core/Source/Core/Renderer/Renderer.h`
- `Core/Source/Core/Renderer/Renderer.cpp`
- `Core/Source/Core/Renderer/Shader.h`
- `Core/Source/Core/Renderer/Shader.cpp`
- `Core/Source/Core/Renderer/Sphere.h`
- `App/Shaders/RayTracing.glsl`
- `App/Source/AppLayer.cpp`
- `App/Source/AppLayer.h`
- `App/Source/Main.cpp`

Understand the existing rendering and resource lifecycle before modifying it.

## Implementation Philosophy

Prefer the smallest change that satisfies the current specification.

Do not:

- Rewrite the renderer unnecessarily.
- Introduce an ECS.
- Introduce a scene graph unless required.
- Introduce a generic rendering framework.
- Replace existing OpenGL abstractions without a concrete reason.
- Add abstractions solely for theoretical extensibility.
- Modify unrelated code.

This is a university project. The implementation should remain understandable
enough to explain during a presentation or code review.

## BVH Philosophy

The BVH is an algorithmic feature of the project and should be implemented by
the project itself.

Do not use a third-party BVH implementation.

The initial BVH should prioritize:

1. Correctness
2. Deterministic behavior
3. Simplicity
4. Explainability
5. Performance

Do not prematurely implement advanced BVH construction algorithms.

The construction strategy is a median split based on primitive centroids.
SAH and dynamic / refitting BVHs are out of scope.

## CPU/GPU Separation

The CPU is responsible for:

- Model loading
- Mesh representation
- Triangle data preparation
- BVH construction
- Flattening the BVH
- Uploading data to OpenGL

The GPU is responsible for:

- Ray generation
- Ray-AABB intersection
- BVH traversal
- Ray-triangle intersection
- Path tracing

The BVH should not be rebuilt during every render frame for static geometry.

## Validation

Every major feature must have a validation strategy.

Correctness must be established before performance optimization.

For BVH traversal, compare its results against brute-force triangle traversal.

## Workflow

Before modifying code:

1. Read the relevant specification.
2. Inspect the existing implementation.
3. Identify the smallest required change.
4. Implement only the current milestone.
5. Build the project.
6. Run relevant validation.
7. Check that existing sphere rendering still works.
8. Report changes and validation results.

Do not silently skip failed validation.

## Scope Control

Do not implement future milestones unless explicitly requested.

If implementation of the current milestone reveals an architectural problem,
document it before making a large redesign.

The project is complete through M11 (cleanup / documentation). Do not start
another feature milestone after M11.

## Definition of Done

A milestone is complete when:

- The required functionality is implemented.
- The project builds successfully.
- Existing functionality still works.
- The implementation has been validated.
- Relevant documentation/specifications are still accurate.
