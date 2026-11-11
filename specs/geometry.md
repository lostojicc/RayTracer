# Geometry Specification

## Objective

Add triangle geometry while preserving existing sphere rendering.

## Triangle

A triangle consists of three vertex positions:

    v0
    v1
    v2

The initial renderer only requires position data.

Additional attributes such as:

- Normals
- UV coordinates
- Tangents
- Material indices

must not be introduced until they are required by the renderer.

## Mesh

A mesh contains:

- Vertex data
- Triangle/index data

The mesh representation must be independent of the source model file format.

The renderer and BVH must not depend on importer-specific vertex types.

## Triangle Identifier

Every triangle must have a stable integer identifier.

The identifier is used by the BVH to refer to a triangle without duplicating complete triangle data.

## AABB

Implement an axis-aligned bounding box:

    AABB
        min : vec3
        max : vec3

It must support:

- Construction
- Construction from a triangle
- Expansion by a point
- Expansion by another AABB
- Centroid calculation
- Ray intersection

## Triangle Bounds

For triangle vertices `v0`, `v1`, and `v2`:

    min = component-wise minimum(v0, v1, v2)
    max = component-wise maximum(v0, v1, v2)

The AABB must contain all three vertices.

## Triangle Centroid

The triangle centroid is:

    centroid = (v0 + v1 + v2) / 3

The centroid is used by BVH construction.

## Ray-Triangle Intersection

Implement a standard ray-triangle intersection algorithm.

The implementation must return:

- Whether an intersection occurred
- Intersection distance `t`
- Surface information required by the renderer
- Barycentric coordinates if useful to the implementation

The algorithm must reject intersections outside the triangle.

## Intersection Contract

Triangle intersection must follow the same conceptual contract as sphere intersection:

    ray + tMin + tMax
            |
            v
       intersection
            |
            +-- miss
            |
            +-- hit -> closest valid t + surface information

This allows triangle and sphere intersections to feed the same path-tracing logic.

## Triangle Normal

For the initial implementation, the triangle geometric normal may be calculated from its edges:

    normal = normalize(cross(v1 - v0, v2 - v0))

The normal orientation and front-face handling should remain compatible with the existing `HitRecord` semantics.

## Existing Sphere

Sphere rendering must remain supported.

Do not replace the sphere implementation with triangles.

Adding triangle geometry must not break the existing sphere path.

## Degenerate Triangles

A triangle with zero or near-zero area must not cause:

- Division by zero
- NaN propagation
- Infinite loops
- BVH construction failure
- GPU crashes

The exact handling may be documented as part of the implementation.

## Correctness Requirements

Test:

- Triangle hit
- Triangle miss
- Hit near an edge
- Hit near a vertex
- Ray parallel to triangle
- Back-facing triangle
- Ray starting near/on the triangle
- Degenerate triangle

The implementation must use a documented floating-point tolerance where appropriate.
