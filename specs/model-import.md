# Model Import Specification

## Objective

Import static 3D models and convert them into the project's internal mesh representation.

## Import Boundary

The importer is responsible only for translating an external model format into project-owned geometry.

The intended boundary is:

    External model
          |
          v
       Importer
          |
          v
        Mesh
          |
          +----> BVH
          |
          +----> GPU buffers

The BVH must not know which model format produced the triangles.

## Initial Scope

Support static triangle geometry.

The first implementation should support:

- Vertex positions
- Triangle indices

Normals and other attributes may be added when required by shading.

## Triangulation

Polygonal faces must be converted into triangles during import.

The renderer and BVH operate exclusively on triangles.

The triangulation process should be deterministic.

## Transforms

Model transforms must be resolved before BVH construction.

The triangle positions used by the BVH must represent the final coordinate space used for ray intersection.

## Coordinate System

The importer must document:

- Coordinate system
- Up axis
- Handedness
- Units
- Transform application

The renderer and imported assets should use a consistent coordinate system.

## Dependency

A third-party model-loading library may be used for parsing if appropriate.

However:

- External library types must not leak into renderer interfaces.
- External library types must not leak into BVH interfaces.
- Imported data must be converted into project-owned structures.
- The importer must be replaceable without changing BVH code.

## Existing Dependencies

The repository currently contains `stb` under:

    Core/vendor/stb

This existing dependency should not be repurposed as a 3D model loader unless it is actually appropriate for the required model format.

If a new model-loading dependency is introduced, document:

- Why it is needed
- Which format(s) it supports
- Where it is integrated
- How it is isolated from the renderer/BVH

## Initial Test Assets

Use progressively more complex models:

1. Single triangle
2. Simple low-poly model
3. Moderate triangle-count model
4. Large real-world model

The exact models are part of the benchmark configuration rather than the geometry implementation.

## Validation

An imported model is considered successfully supported when:

- It loads without errors.
- Its vertices are represented correctly.
- Its triangle indices are correct.
- Its bounds are correct.
- It can be rendered using brute-force triangle traversal.

## Static Geometry

The initial project only requires static models.

Dynamic model deformation, animation, and BVH refitting are outside the initial scope.
