# Project Specification

## Goal

Extend the existing C++ OpenGL compute-shader path tracer with:

1. Triangle mesh support
2. 3D model importing
3. CPU-side BVH construction
4. GPU-side BVH traversal
5. Rendering of imported models
6. Rendering comparison between brute-force and BVH traversal
7. Reproducible performance benchmarks

The primary purpose of the project is to demonstrate understanding of acceleration structures and their impact on ray-tracing performance.

## Current State

The renderer:

- Uses OpenGL compute shaders.
- Implements path tracing on the GPU.
- Supports sphere geometry and triangle meshes, including imported OBJ models.
- Implements simple diffuse/Lambertian scattering.
- Uses brute-force sphere intersection.
- Constructs a CPU BVH (centroid median split) and traverses it on both CPU and GPU.
- Lets the user switch GPU triangle traversal between brute force and BVH.
- Is organized into `Core`, `App`, `Tests`, and `Benchmark`, with analysis under `Analysis/`.
- Uses `Core` as a static library consumed by `App`, `Tests`, and `Benchmark`.

## Target State

The renderer should be able to render scenes containing:

- Spheres
- Triangle meshes
- Imported 3D models

Triangle geometry should be accelerated using a Bounding Volume Hierarchy.

The renderer must provide a way to disable BVH traversal and perform brute-force triangle intersection so that both approaches can be compared.

## High-Level Pipeline

The intended pipeline is:

    Model File
        |
        v
    Model Import
        |
        v
    CPU Mesh Representation
        |
        v
    Triangle Data
        |
        v
    BVH Construction
        |
        v
    CPU Scene / Acceleration Structure
        |
        v
    GPU Buffer Upload
        |
        v
    Compute Shader
        |
        +--> Ray Generation
        |
        +--> Brute-Force or BVH Traversal
        |
        +--> Triangle Intersection
        |
        +--> Material Evaluation
        |
        v
    Path Traced Image

## Non-Goals

The initial implementation does not require:

- Production-quality model loading
- Physically complete material systems
- Texture mapping
- Skeletal animation
- Motion blur
- Dynamic BVH refitting
- Multi-level acceleration structures
- Hardware ray-tracing APIs
- Vendor-specific ray-tracing extensions
- Advanced BVH construction algorithms such as SAH

These may be considered later only if they do not interfere with the core project goals.

## Success Criteria

The project is successful when:

- An imported triangle mesh can be rendered correctly.
- The same scene can be rendered using brute-force triangle intersection.
- The same scene can be rendered using BVH traversal.
- Both approaches produce equivalent images within an explicitly defined numerical tolerance.
- BVH construction is performed independently of GPU traversal.
- Benchmark results provide a reproducible comparison between brute-force and BVH traversal.
- The implementation can be explained algorithmically as part of a university project presentation/report.

## Core Algorithmic Question

The project should ultimately answer:

> How does a Bounding Volume Hierarchy affect the cost of ray-triangle intersection compared with brute-force traversal as scene complexity increases?

The implementation and benchmark should make this comparison measurable.
