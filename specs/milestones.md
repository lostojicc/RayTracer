# Implementation Milestones

The project must be implemented incrementally.

Do not implement future milestones unless explicitly requested.

Each milestone should leave the project in a buildable and understandable state.

---

## M0 — Repository and Architecture Analysis

### Objective

Understand the existing implementation before modifying it.

### Inspect

At minimum:

- `Core/Source/Core/Renderer/Renderer.h`
- `Core/Source/Core/Renderer/Renderer.cpp`
- `Core/Source/Core/Renderer/Shader.h`
- `Core/Source/Core/Renderer/Shader.cpp`
- `Core/Source/Core/Renderer/Sphere.h`
- `App/Shaders/RayTracing.glsl`
- `App/Source/AppLayer.h`
- `App/Source/AppLayer.cpp`
- `App/Source/Main.cpp`
- `Core/CMakeLists.txt`
- `App/CMakeLists.txt`
- `CMakeLists.txt`

### Determine

Document:

- How the renderer is created.
- How the compute shader is loaded.
- How the compute shader is dispatched.
- How sphere data is represented.
- How sphere data is uploaded.
- How shader uniforms are configured.
- How the accumulation image is managed.
- How the application configures the renderer.
- Where new geometry should integrate.
- Where BVH construction should integrate.
- Where GPU BVH data should be uploaded.

### Restrictions

Do not implement:

- Triangle geometry
- Model importing
- BVH
- GPU traversal

### Acceptance Criteria

The agent can explain the existing CPU → OpenGL → shader data flow.

### Status: COMPLETE

Key findings recorded in the specifications:

- There is no `Renderer` class. `Renderer` is a namespace of free functions in
  `Core`; ray-tracing orchestration lives in `AppLayer` in `App`. See
  `architecture.md`.
- The sphere SSBO uses `std140` at binding 0 and matches its C++ struct only by
  coincidence of struct shape. New SSBOs prefer `std430`. See
  `gpu-integration.md`.
- `Core` has no GPU buffer abstraction; the sphere SSBO is created inline in
  `AppLayer`. A thin one is to be added to `Core`'s `Renderer` namespace.
- Shader and asset paths are resolved relative to the working directory with no
  build-system support; the program must be run from `App/`.
- `Core::Sphere` is absent from `Core/CMakeLists.txt`.
- `Application::PushLayer<T>()` default-constructs and takes no arguments, so a
  layer cannot receive configuration through its constructor. This constrains
  benchmark orchestration.
- Frame accumulation resets only on camera change and resize.
- There is no GPU timing infrastructure.

---

# M1 — CPU Geometry Foundation

## Objective

Establish the CPU geometry foundation independently of the GPU renderer.

This milestone covers triangle, AABB, and mesh representation. It absorbs what
was previously listed as M2.

No BVH in this milestone.

### Implement

- Triangle representation
- Ray and hit representation mirroring the shader's intersection contract
- Ray-triangle intersection
- AABB
- AABB expansion (by point and by AABB)
- AABB centroid
- Ray-AABB intersection
- Mesh representation
- Vertex data
- Triangle/index data
- Triangle identifiers
- Mesh bounds

All of the above lives in `Core` and must not depend on OpenGL.

### Validation

Test:

- Triangle hits
- Triangle misses
- Edge cases
- Vertex cases
- Parallel rays
- Back-facing triangles
- Rays starting on the triangle
- Degenerate triangles
- AABB construction, expansion, centroid
- Ray-AABB hit/miss, including rays parallel to a slab
- Mesh triangle access, triangle identifiers, and bounds

### Acceptance Criteria

CPU geometry functionality is correct and independently testable without a GL
context.

A manually created mesh can be represented entirely using project-owned data
structures. No model importer is required yet.

The existing application still builds and sphere rendering is unaffected.

### Status: COMPLETE

Implemented in `Core/Source/Core/Renderer/`: `Ray.h`, `AABB.h`, `Triangle.h`,
`Mesh.h`, `Mesh.cpp`.

Validated by the `Tests` target (38 cases, registered with CTest). The geometry
headers were confirmed to compile with no OpenGL include path available.

Documented tolerances:

- `TriangleIntersectionEpsilon` = 1e-8, a Möller-Trumbore determinant threshold.
  It scales with `|direction| * |edge1| * |edge2|` and is tuned for normalized
  directions and unit-scale geometry.
- `DegenerateAreaEpsilon` = 1e-8, a threshold on `|cross(edge1, edge2)|`.

Documented behaviour:

- Triangle intersection is double sided. `HitRecord::FrontFace` and the stored
  normal follow the same convention as `hitSphere()`.
- The `tMin`/`tMax` rejection uses `t <= tMin || t >= tMax`, matching
  `hitSphere()`.
- Degenerate triangles are rejected by the determinant test; `Normal()` returns
  the zero vector rather than NaN.
- Ray-AABB uses the slab method. A zero direction component combined with an
  origin on a slab plane produces NaN, which fails both interval comparisons and
  correctly leaves the axis unconstrained.
- Ray-AABB uses a strict `tMax < tMin` rejection so that zero-thickness boxes
  around axis-aligned geometry remain intersectable. Grazing hits are reported,
  which is conservative and safe for an acceleration structure.
- `Mesh` bounds are computed over the vertex array, not over triangles, so that
  construction cannot dereference an out-of-range index. Unused vertices make
  the bounds more conservative, never incorrect.

---

# M2 — CPU Mesh

Merged into M1. See "M1 — CPU Geometry Foundation".

---

# M3 — CPU Brute-Force Triangle Traversal

## Objective

Create a correctness reference for triangle intersection.

### Implement

A CPU traversal path equivalent to:

    closest = tMax

    for every triangle:
        test intersection

        if hit and t < closest:
            update closest hit

### Acceptance Criteria

The closest triangle hit is returned correctly.

This implementation becomes the reference against which the CPU BVH is tested.

### Status: COMPLETE

Implemented as `Core::IntersectBruteForce(mesh, ray, tMin, tMax, record)` in
`Core/Source/Core/Renderer/TriangleTraversal.h` / `.cpp`.

A free function rather than a `Mesh` member, because M6 adds an equivalent BVH
traversal that cannot be a `Mesh` member. Keeping both in one header places the
reference and the accelerated path side by side.

Documented behaviour:

- The scan passes the running `closest` distance as `tMax` to each triangle
  test, mirroring `hitWorld()` in `App/Shaders/RayTracing.glsl`.
- `record.TriangleId` is set to the winning triangle's identifier. Comparing
  triangle identifiers is required by the BVH correctness check in `bvh.md`.
- `record` is written only when the function returns true.
- Equal distances resolve to the lowest triangle identifier, because the strict
  `t < closest` shrink prevents a later coincident triangle from displacing an
  earlier one. The CPU BVH must reproduce this tie-breaking rule.
- Degenerate triangles are skipped by `Triangle::Intersect` and do not
  interrupt the scan.
- Precondition: `mesh.IsValid()`. An invalid mesh trips the assert in
  `Mesh::GetTriangle`.

Validated by the `Tests` target, now 52 cases. Alongside targeted cases for
ordering, `tMin`/`tMax`, degenerate triangles, coincident triangles, and
back-facing hits, the traversal is cross-checked against an independent
closest-hit implementation that gathers all intersections and sorts them. That
comparison runs 2000 deterministic rays (fixed seed) against 60 random
triangles and asserts a minimum hit count so it cannot pass vacuously.

---

# M4 — Model Import

## Objective

Import an external static 3D model.

### Implement

- Model importer abstraction
- Supported model format
- Conversion to project-owned mesh representation
- Polygon triangulation if required
- Model transforms

### Acceptance Criteria

At least one external model:

- Loads successfully.
- Produces valid triangles.
- Produces correct bounds.
- Can be processed by CPU brute-force traversal.

Do not integrate the model directly with the BVH yet.

### Status: COMPLETE

Implemented as `Core::ImportObjFile()` / `Core::ImportObjSource()` in
`Core/Source/Core/Renderer/ModelImporter.h` / `.cpp`, returning a
`Core::ModelImportResult { ImportedMesh, Success, Error }`.

Free functions rather than a class, consistent with `IntersectBruteForce`:
the importer has no state to own, so a class with only static methods would
be an abstraction with no purpose (`specs/AGENTS.md`: "Do not add abstractions
solely for theoretical extensibility").

Format decision: a small dependency-free Wavefront OBJ parser, not a
third-party model-loading library. OBJ is text-based, requires no new build
dependency, is trivial to hand-author as a test asset, and is sufficient for
the required scope (vertex positions and triangle/polygon faces only). Per
`specs/model-import.md`, `Core/vendor/stb` is not a 3D model loader and was
correctly left untouched. If a later milestone needs a richer format (e.g.
materials, textures, skinning), that decision should be revisited then
rather than anticipated now.

Documented scope and behaviour:

- Only `v` (vertex position) and `f` (face) directives are interpreted.
  `vt`, `vn`, `vp`, `o`, `g`, `s`, `mtllib`, `usemtl`, and `l` are recognised
  and skipped without error. Unknown directives are also skipped rather than
  rejected.
- Coordinate system: passed through unchanged. No axis, handedness, or unit
  conversion is performed; OBJ's conventional right-handed, Y-up axes already
  match this project's existing scene/camera. This is documented in
  `ModelImporter.h` per `specs/model-import.md`'s coordinate-system
  requirement.
- Triangulation: faces with more than 3 vertices are triangulated by a
  deterministic fan from the face's first vertex.
- Transform: an optional `glm::mat4` (default identity) is applied to every
  vertex position before the `Mesh` is constructed, so the returned mesh is
  already in final ray-intersection space, satisfying
  "`model transforms must be resolved before BVH construction`." Because
  `Core::Mesh` stores positions only and `Triangle::Normal()` always derives
  the normal after the transform is applied, non-uniform scale does not need
  a separate inverse-transpose normal transform — this was verified by a
  dedicated test.
- Face vertex references support all four standard OBJ forms
  (`v`, `v/vt`, `v/vt/vn`, `v//vn`) and both positive (1-based) and negative
  (relative) indices, using only the position component.
- Malformed input (unreadable numbers, zero/out-of-range/malformed indices,
  faces with fewer than 3 vertices, or no vertices at all) fails with a
  line-numbered `Error` string instead of producing a partial mesh. A model
  with vertices but no faces succeeds with zero triangles, consistent with
  `Core::Mesh`'s own empty-mesh contract.
- `ImportObjFile()` only adds file reading on top of `ImportObjSource()`, so
  the parsing logic itself is tested without any disk I/O.

Validated by the `Tests` target, now 77 cases (25 new). Coverage includes:
triangulation of a quad and a pentagon face, comments/blank lines, every
ignorable directive, all four face-reference forms, positive and negative
indices, every documented failure mode, identity/translation/scale/rotation
transforms (including the non-uniform-scale normal check), and CRLF line
endings.

Two on-disk `.obj` assets were added under `Tests/Assets/` (`Triangle.obj`,
`Quad.obj`) and are loaded through `ImportObjFile()` using an absolute path
injected by `Tests/CMakeLists.txt` via `TESTS_ASSET_DIR`, so the test does
not depend on the process's working directory (the CWD-relative fragility
noted for shader loading in `architecture.md` was not reintroduced here).
Both are then run through the M3 `Core::IntersectBruteForce()` reference to
satisfy the "can be processed by CPU brute-force traversal" acceptance
criterion literally, not just structurally.

Not done, by design: no integration with `AppLayer`, GPU buffers, or the
BVH. `App/Shaders/RayTracing.glsl` and `App/Source/AppLayer.cpp` were not
touched; sphere rendering is unaffected because nothing in its code path
changed.

---

# M5 — CPU BVH Construction

## Objective

Implement the BVH construction algorithm.

### Implement

- BVH node
- Primitive references
- Primitive bounds
- Centroid bounds
- Split-axis selection
- Median partition
- Configurable leaf size
- Degenerate split handling
- Flattened representation

### Construction Strategy

Use:

    Binary BVH
    Centroid median split
    Largest centroid-extent axis
    Configurable leaf size
    Default leaf size = 4

### Acceptance Criteria

The BVH:

- Contains every triangle exactly once.
- Has valid parent/child bounds.
- Has a root containing all geometry.
- Terminates on degenerate inputs.
- Produces deterministic results for identical input.

BVH statistics can be printed or inspected.

### Status: COMPLETE

Implemented as `Core::BVH` in `Core/Source/Core/Renderer/BVH.h` / `.cpp`,
alongside `Core::BVHPrimitive`, `Core::BVHNode`, and `Core::BVHStats`.

Representation: a single pass builds directly into two flat arrays
(`std::vector<BVHNode>`, `std::vector<BVHPrimitive>`) instead of a
pointer-based tree that is flattened afterwards. A node's array slot is
reserved *before* its children are built and filled in by index (never by a
held reference, since recursive calls can reallocate the vector) once both
children exist, so the result is a deterministic pre-order layout with the
root at index 0 (`nodes[0]` root, `nodes[1]` left child, ... matching the
example in `specs/bvh.md`). This sidesteps "flattening" as a distinct step:
there is never a pointer tree to convert.

`BVHNode` is a plain struct of an `AABB` and 4 `uint32_t`s -- no pointers, no
OpenGL types -- already in a shape suitable for a later GPU upload (M8).
`PrimitiveCount > 0` distinguishes leaf from internal; this is unambiguous
because a BVH over zero triangles produces *no* root node at all
(`BVH::IsEmpty()`), so a leaf's `PrimitiveCount` is always >= 1 in practice
and never needs to double as an "is internal" sentinel.

Construction follows `specs/bvh.md`'s 7 steps exactly, with two explicit
safety floors beyond the spec's literal wording, both documented in
`BVH.h`/`BVH.cpp`:

- The leaf condition is `count <= leafSize || count <= 1`. The `count <= 1`
  floor exists so a configured leaf size of 0 cannot be asked to split a
  single primitive into two non-empty halves; leaf size 0 is otherwise
  honoured as-is (not clamped to 1), tested directly.
- Centroid-median partitioning (`std::nth_element`, splitting the *count* in
  half) structurally cannot produce an empty range, unlike a
  midpoint/threshold-based partition -- "both resulting ranges contain at
  least one primitive" therefore follows directly from `count > 1`, with no
  possibility of a "failed partition". The degenerate-centroid-extent check
  (all centroids numerically coincident) is consequently not required for
  *termination*, only for tree *quality*: it stops construction from
  building several pointless extra levels once a split can no longer
  separate anything spatially. This reasoning is recorded as a comment next
  to the check in `BVH.cpp` so the distinction isn't lost.

Split axis selection matches the specified tie-break exactly: X wins ties
with Y; whichever of those wins ties with Z. Verified directly (not just
inferred) by 5 tests that place two single-point degenerate triangles so
that the axis choice is externally observable via which triangle ends up in
the left child.

Statistics (`BVHStats`) are computed once, immediately after construction,
by an iterative (explicit-stack) depth-first walk of the finished node
array -- not threaded through the recursive build -- so `BVHNode` doesn't
need to carry its own depth. This also previews the iterative-traversal
style `specs/bvh.md` asks for in M6.

Validated by the `Tests` target, now 95 cases (18 new). Beyond the direct
per-feature tests (empty/single/oversized leaf size, configurable leaf
size, default-leaf-size-is-4, axis selection and its tie-breaks, degenerate
coincident centroids, degenerate zero-area triangles, leaf size 0,
determinism), most tests additionally run every constructed BVH through a
shared `CheckBVHInvariants()` helper that independently walks the tree and
asserts, in one place:

- every primitive-array slot is referenced by exactly one leaf, and every
  triangle id `0..count-1` appears in the primitive array exactly once
  (`specs/bvh.md` "Every triangle must occur in exactly one leaf"),
- every internal node's bounds contain both children's bounds,
- every leaf's bounds contain the *actual* mesh triangle geometry (not just
  the cached primitive bounds, to catch a stale-cache bug),
- the root bounds directly contain every triangle (not only transitively),
- leaf count == internal count + 1 (a structural property of any binary
  tree where every internal node has exactly two children), and
- `BVH::GetStats()` matches an independently computed walk field-by-field.

This was run over a 6x6 coplanar grid, a 500-triangle randomized mesh (fixed
seed), a 200-triangle randomized mesh at 6 different leaf sizes, and the
degenerate cases above. A separate, ad hoc stress run (not part of the
committed suite) built a BVH over 200,000 random triangles in ~50 ms with a
balanced tree (max depth 16, leaf sizes 3-4), confirming the construction
also behaves reasonably at scale.

Determinism is verified by building the same mesh/leaf-size twice and
comparing every node and primitive field-by-field, plus the stats.

Not done, by design: no CPU or GPU traversal (M6/M8), no GPU buffers or
upload, no `AppLayer`/shader integration, no benchmarking. `Core::BVH`
compiles with no OpenGL include path available, confirming it has no GL
dependency.

---

# M6 — CPU BVH Traversal

## Objective

Implement and validate BVH traversal before moving to the GPU.

### Implement

Iterative CPU traversal.

The traversal should:

1. Test the root.
2. Traverse intersected nodes.
3. Test triangles in leaves.
4. Track the closest hit.
5. Respect `tMin` and `tMax`.

### Validation

Generate deterministic test rays.

For every ray:

    bruteForceResult = bruteForce(ray)
    bvhResult = bvh(ray)

Compare:

- Hit/miss
- Triangle ID
- Intersection distance
- Relevant surface data

### Acceptance Criteria

CPU BVH and CPU brute-force traversal produce equivalent results within the defined floating-point tolerance.

This milestone must pass before GPU BVH work begins.

### Status: COMPLETE

Implemented as `Core::IntersectBVH(bvh, mesh, ray, tMin, tMax, record)` in
`Core/Source/Core/Renderer/TriangleTraversal.h` / `.cpp`, alongside
`IntersectBruteForce`. M3's documentation had already anticipated this exact
placement ("M6 adds an equivalent BVH traversal that cannot be a `Mesh`
member. Keeping both in one header places the reference and the accelerated
path side by side."), so no new header was introduced. `Core::BVH` itself
(construction, storage, statistics) is unchanged from M5.

Traversal algorithm (iterative, explicit stack of `uint32_t` node indices,
`std::vector`-backed):

1. `bvh.IsEmpty()` short-circuits to a miss (no root exists at all).
2. The root index is pushed once; the root's AABB test happens as the first
   iteration of the general loop below, rather than as a special case.
3. Loop: pop a node index. Re-test that node's AABB against `[tMin,
   closest]`, where `closest` starts at `tMax` and shrinks every time a
   nearer triangle hit is found (identical shrinking-`tMax` technique to
   `IntersectBruteForce` and to `hitWorld()` in
   `App/Shaders/RayTracing.glsl`). This re-test is required even for a node
   already bounds-checked before being pushed, since a sibling subtree
   visited in between may have shrunk `closest` and made the node's box no
   longer reachable.
4. Leaf: scan `[FirstPrimitive, FirstPrimitive + PrimitiveCount)` in
   `BVH::GetPrimitives()`, calling `Triangle::Intersect` (the same
   implementation `IntersectBruteForce` uses -- no separate GPU-style
   intersection routine was written) with `closest` as `tMax`, updating
   `record`/`closest`/`TriangleId` on every strict improvement.
5. Internal: test both children's AABBs immediately (rather than deferring
   entirely to step 3), so a child that clearly misses is never pushed at
   all, and so each child's entry distance (`AABB::Intersects`'s `tEnter`
   out-parameter) is available for ordering.

Node-ordering decision: the farther child (by entry distance) is pushed
first and the nearer child second, so the nearer child is popped and
traversed first (the stack is LIFO). This is a pure ordering choice with no
effect on correctness -- step 3's re-test still protects every node
regardless of push order -- but visiting the nearer child first lets a
genuine nearby hit shrink `closest` before the farther subtree is even
looked at, pruning more of it. A tied entry distance resolves to the left
child as "near," consistent with the left-biased centroid tie-break already
used by `BVH::BuildRecursive`. This ordering previews the "closest AABB
first" strategy `specs/bvh.md`'s GPU traversal (M8) will also want, without
requiring it here since correctness, not performance, is this milestone's
goal.

The traversal reuses `Triangle::Intersect` directly (no duplicated
intersection math) and never includes an OpenGL header, matching
`specs/architecture.md`'s "CPU geometry must not depend on OpenGL" rule;
`Core::BVH`, `Core::Mesh`, and `TriangleTraversal.*` remain buildable and
testable without a GL context.

Tie-break fix (post-M6 review): the paragraph above ("visits the lower-id
side first") was checked against a concrete counterexample and found to be
wrong. Two large, *non-coincident* triangles can cross a ray at a
bit-identical `t` while their centroids sit far apart, so a small-leaf-size
BVH places them in different leaves under different subtrees. Traversal
order between those subtrees is driven by AABB entry distance (near/far
child ordering) and by which side of a centroid-median split each triangle
landed on -- neither of which is related to `TriangleId`. A hand-built
counterexample confirmed this empirically:  `IntersectBruteForce` returned
the lower id (as documented above), while the pre-fix `IntersectBVH`
returned the higher id, because `Triangle::Intersect`'s own `t >= tMax`
rejection made the second, exactly-tied triangle indistinguishable from a
miss once the first had set `closest`.

Fix, in `IntersectBVH`'s leaf loop only: probe each triangle with `tMax =
std::nextafter(closest, +inf)` once a hit already exists, instead of
`closest` unmodified. Since there is no representable `float` strictly
between `closest` and `nextafter(closest, +inf)`, this admits exactly the
candidates with `t <= closest` -- never anything actually farther -- so the
function's external `tMin`/`tMax` contract is unchanged; it only creates the
opportunity to *see* an exact tie instead of having it silently rejected.
Acceptance is then decided explicitly: `candidate.T < closest` accepts
unconditionally; `candidate.T == closest && triangleId < record.TriangleId`
accepts the tie; anything else (including a widened-probe candidate that is
neither) is rejected. This reproduces `IntersectBruteForce`'s tie rule as an
explicit, order-independent comparison instead of relying on traversal
order to happen to agree with it.

`Triangle::Intersect`, the BVH's node/leaf visiting order, the AABB pruning
using `closest`, and the near/far child push ordering are all unchanged --
the fix is entirely local to the accept/reject decision at the point a leaf
triangle's candidate hit is evaluated. `IntersectBruteForce` needed no
change: its ascending-id scan order already implements the same rule as a
side effect.

Regression test:
`BVHTraversalResolvesCrossSubtreeExactTieToTheLowerTriangleId`
reconstructs the counterexample (two large triangles crossing the same ray
at a verified bit-identical `t`, plus filler triangles forcing an 8-triangle
mesh to split so the pair lands in different leaves) at leaf sizes 1 and 2,
and in both a "lower id naturally visited first" and a mirrored "lower id
naturally visited last" geometry variant, asserting `IntersectBVH` always
returns id 0 in agreement with `IntersectBruteForce`. This test was
confirmed to fail (8 checks) against the pre-fix traversal and pass against
the fixed one, and the original hand-built counterexample was re-run
directly and now agrees (`id=0` from both functions at leaf sizes 1 and 2).

Validated by the `Tests` target, now 114 cases (19 new relative to M5, in
`Tests/Source/BVHTraversalTests.cpp`, registered in `Tests/CMakeLists.txt`).
Methodology: every test funnels through a shared `CompareBVHToBruteForce()`
helper that runs `IntersectBVH` and `IntersectBruteForce` on the identical
mesh/BVH/ray/`tMin`/`tMax` and asserts, per `specs/bvh.md`'s "Correctness
Requirement" and this milestone's validation list:

- hit vs. miss agree,
- `TriangleId` agrees,
- `T` agrees within `1e-5`,
- `Position`, `Normal`, `U`, `V`, and `FrontFace` all agree.

Specific coverage, matching this milestone's required list:

- **Empty BVH**: `BVHTraversalOnEmptyMeshMisses`.
- **Single triangle**: `BVHTraversalFindsASingleTriangle`,
  `BVHTraversalMissesASingleTriangleWhenRayIsOffset`.
- **Rays that miss the root**: `BVHTraversalRayMissesTheRootAABB`,
  `BVHTraversalRayPointingAwayFromRootMisses` (a 200-triangle BVH, checked
  directly against `bvh.GetBounds().Intersects()` so the test cannot pass
  vacuously if the geometry generator changes).
- **Rays that hit internal nodes but miss every triangle**:
  `BVHTraversalRayHitsInternalNodeBoundsButMissesEveryTriangle`, using a
  sparse grid with leaf size 1 (forcing internal nodes) and a ray aimed at
  the gap between cells, with an explicit sanity check that the ray does
  hit the root bounds first.
- **Closest triangle must win**:
  `BVHTraversalReturnsTheClosestHitAmongManyStackedTriangles` (5 stacked
  triangles at leaf sizes 1/2/4),
  `BVHTraversalReturnsTheClosestHitWhenNearestIsBuiltLast` (reversed
  build/traversal order), `BVHTraversalClosestHitIsIndependentOfLeafSize`
  (300 random triangles at 5 leaf sizes against one brute-force reference).
- **`tMin`/`tMax`**: `BVHTraversalRespectsTMin`, `BVHTraversalRespectsTMax`,
  `BVHTraversalTMinTMaxAreExclusiveBoundaries` (hit exactly at `tMin` or
  `tMax` is rejected by both paths, matching `Triangle::Intersect`'s `t <=
  tMin || t >= tMax` contract).
- **Degenerate/edge cases already covered by the geometry layer**:
  `BVHTraversalSkipsDegenerateTriangles` (collinear triangle shadowing a
  valid one), `BVHTraversalMixOfDegenerateAndValidTrianglesMatchesBruteForce`
  (10 valid + 20 zero-area triangles, swept from both sides),
  `BVHTraversalResolvesCoincidentTrianglesConsistentlyWithBruteForce`,
  `BVHTraversalReportsBackFacingHitsLikeBruteForce`.
- **Broad deterministic fuzz cross-check**:
  `BVHTraversalMatchesBruteForceOverManyDeterministicRays` fires 1500 fixed-
  seed rays (inward from a sphere around 400 random triangles, with varied
  `tMin` and an occasional finite `tMax`) through `CompareBVHToBruteForce`
  at 6 different leaf sizes (9000 ray comparisons total), with a minimum-
  hit-count assertion per leaf size guarding against a vacuous pass.

All of these were also run and passed against the pre-existing M0-M5 test
cases: the full suite (114 cases) passes, confirming M0-M5 behavior is
unaffected.

Not done, by design: no GPU traversal, shader changes, GPU buffers,
benchmarking, or `AppLayer` integration (M7/M8). `Core::BVH`'s construction
code, `Core::Mesh`, and `IntersectBruteForce` were not modified; only
`TriangleTraversal.h`/`.cpp` gained the new function (plus the exact-tie fix
in `.cpp` and a `BVH.h` include in `.h`).

---

# M7 — GPU Triangle Rendering

## Objective

Add triangle geometry to the existing GPU path tracer.

### Implement

- GPU triangle buffer
- GPU triangle intersection
- GPU brute-force triangle traversal
- Integration into `hitWorld()`

### Requirements

The existing path tracer should remain responsible for:

- Ray generation
- Scattering
- Attenuation
- Sky
- Accumulation

The new code should primarily extend the intersection stage.

### Acceptance Criteria

The application can render:

- A simple triangle.
- A multi-triangle mesh.
- An imported model.

Triangle rendering must work using brute-force traversal.

Existing sphere rendering must still work.

### Status: COMPLETE

Implemented across `Core::Renderer` (buffer abstraction), `App/Shaders/RayTracing.glsl`
(GPU intersection), and `App/Source/AppLayer.*` (scene/upload orchestration),
per the responsibility split in `specs/architecture.md`. No new `Core`
CPU-geometry types or files were added or changed: `Triangle.h`, `Mesh.h/.cpp`,
`AABB.h`, `BVH.h/.cpp`, `TriangleTraversal.h/.cpp`, and `ModelImporter.h/.cpp`
are byte-for-byte unchanged from M6, and still compile with no OpenGL include
path available (checked directly, see "Build/Test Results" below).

**GPU buffer abstraction** (`Core::Renderer`, `Renderer.h`/`Renderer.cpp`):
added `StorageBuffer` (a bare `GLuint` handle) plus four free functions --
`CreateStorageBuffer`, `UploadStorageBuffer`, `BindStorageBuffer`,
`DestroyStorageBuffer` -- alongside the existing `CreateTexture` etc., exactly
matching `specs/gpu-integration.md`'s "GPU Buffer Abstraction" (create,
upload/replace, bind to a binding point, destroy; nothing else). It has no
concept of triangles, spheres, or BVH nodes; `AppLayer` alone decides what
bytes go into a `StorageBuffer` and which binding point it uses.

**GPU triangle representation** (`App/Source/AppLayer.cpp`, file-scope
`AppGPUTriangle`): three `glm::vec3` members, each followed by an explicit
4-byte pad field (`V0/_Pad0, V1/_Pad1, V2/_Pad2`). This is deliberately *not*
`Core::Triangle` (which stays CPU-only and 36 bytes, per `Triangle.h`'s
existing `static_assert`). Positions only -- no normals/UVs/material indices
-- since the geometric normal is derived from the edges on both the CPU
(`Triangle::Normal()`) and GPU (`hitTriangle()`) sides and nothing else is
currently read, per `specs/gpu-integration.md`'s "avoid uploading attributes
that are not currently required."

Documented layout (also in a comment block directly above the struct):
- CPU: `sizeof(AppGPUTriangle) == 48`, `offsetof(V0/V1/V2) == 0/16/32`. Each
  `glm::vec3` has no compiler-inserted padding of its own
  (`alignof(glm::vec3) == 4`), so the three `(vec3, float)` pairs pack
  contiguously with no *extra* compiler padding beyond the explicit pad
  fields -- this is not assumed, it is checked by four `static_assert`s
  immediately after the struct definition.
- GLSL (`App/Shaders/RayTracing.glsl`, `struct Triangle` inside the `std430`
  `TriangleBuffer` block): `vec3 v0/v1/v2` at offsets 0/16/32, struct size 48,
  array stride 48. A GLSL `vec3`'s base alignment is 16 bytes in *both*
  `std140` and `std430` (that rule is not `std140`-specific, unlike the
  scalar/`vec2` array-stride rounding `specs/gpu-integration.md` calls out),
  which is exactly why the CPU struct needs the explicit pad floats instead
  of `Core::Triangle`'s tightly packed 36 bytes.
- Serialisation: `BuildGPUTriangles(const Core::Mesh&)` explicitly copies
  `V0`/`V1`/`V2` out of each `Core::Triangle` (via `Mesh::GetTriangle`) into a
  new `AppGPUTriangle`; there is no `memcpy` of `Core::Triangle` anywhere.
- Buffer type / binding: `GL_SHADER_STORAGE_BUFFER`, `std430`, binding index
  **1**. The existing sphere SSBO (`std140`, binding 0, `hitSphere()`) is
  completely untouched, per `specs/gpu-integration.md` ("do not modify the
  existing sphere SSBO binding") and `specs/architecture.md` ("new SSBOs
  start at binding 1").

**Shader integration** (`App/Shaders/RayTracing.glsl`): added `struct
Triangle`, the `std430 TriangleBuffer` SSBO, a `u_TriangleCount` uniform,
`hitTriangle()` (Möller-Trumbore, double sided, same `t <= tMin || t >= tMax`
rejection and front-face convention as `hitSphere()` and as
`Core::Triangle::Intersect`, using the same `1e-8` determinant epsilon
documented in `Triangle.h`), and `hitTriangles()` (the brute-force scan: pass
the running `closest` distance as the per-triangle `tMax`, exactly mirroring
`Core::IntersectBruteForce` and the existing sphere loop in `hitWorld()`).
`hitWorld()` now runs the existing sphere loop *then* `hitTriangles()` over
the same shrinking `closest`/`rec`, so a triangle can only overwrite `rec` if
it is strictly nearer than every sphere (and vice versa) -- one closest-hit
winner across both primitive kinds. `trace()` was not changed at all: it
still only calls `hitWorld()` and has no idea a triangle exists.

**`AppLayer` orchestration** (`AppLayer.h`/`.cpp`): added a `Core::Mesh
m_TriangleMesh` and `Renderer::StorageBuffer m_TriangleBuffer` alongside the
existing sphere members (`Core::Mesh` was already OpenGL-independent and
required no changes to become an `AppLayer` member). The constructor builds
one CPU mesh via `BuildTriangleScene()` (see "Validation scene" below),
converts it with `BuildGPUTriangles()`, and uploads it once via
`Renderer::CreateStorageBuffer` -- matching `specs/gpu-integration.md`'s "GPU
Data Lifecycle" through "Upload triangle data" (BVH build/upload is M8 and
was not added). `OnRender()` binds the triangle buffer
(`Renderer::BindStorageBuffer(m_TriangleBuffer, 1)`) and sets
`u_TriangleCount` every frame, alongside the pre-existing sphere buffer bind
and `u_SphereCount` uniform; the destructor calls
`Renderer::DestroyStorageBuffer`. The mesh is built once in the constructor
and never rebuilt per frame, and nothing here touches `m_FrameIndex` or the
accumulation texture, so progressive accumulation is unaffected -- there is
no new per-frame state that would need to reset it (there is also no
traversal-mode toggle yet, so `specs/gpu-integration.md`'s "changing
traversal mode must reset accumulation" does not yet apply; see "Not done"
below).

**Validation scene** (`App/Source/AppLayer.cpp` `BuildTriangleScene()`,
`App/Assets/Tetrahedron.obj`): combines two triangle objects into the one
uploaded mesh, satisfying all three acceptance-criteria bullets in a single
running scene:
- A hand-authored quad (2 triangles, built directly through the `Core::Mesh`
  constructor -- no importer involved), placed to the left of the existing
  spheres. Its first triangle alone demonstrates "a simple triangle"; the
  full quad demonstrates "a multi-triangle mesh."
- A tetrahedron (4 triangles, 4 vertices) imported from the new
  `App/Assets/Tetrahedron.obj` via the existing (unmodified)
  `Core::ImportObjFile()`, translated and scaled into place to the right of
  the spheres, demonstrating "an imported model." A failed import is logged
  to `stderr` and skipped rather than crashing the application.

A small file-scope `AppendMesh()` helper concatenates a source mesh's
vertices/indices (with an index offset) onto the combined arrays; this is
plain `std::vector` concatenation, not a new geometry abstraction, so it does
not conflict with `specs/architecture.md`'s "no generic geometry framework."

**Manual rendering validation**: ran the built `App` against a real GL
context (AMD Radeon Graphics via Mesa 26.0.3, GL 4.6 core) from the required
`App/` working directory. A temporary, non-committed `glReadPixels` capture
(removed again immediately after use; not part of the final diff) confirmed
visually that: the two existing spheres render exactly as before; the
hand-authored quad renders as a flat parallelogram; the imported tetrahedron
renders as a solid triangular shape; and all four objects (2 spheres + 2
triangle meshes) are lit, shadowed, and accumulated by the same unmodified
path tracer in one image. This confirms brute-force GPU triangle
intersection is being exercised (there is no other code path that could have
produced the quad/tetrahedron shapes) and that sphere rendering is not
broken.

**Build/test results**: full project build succeeds
(`Core`, `App`, `Tests`, plus `glad`). `Tests` still reports 114/114 (unchanged
from M6 -- see M6's entry for the full breakdown); it was re-run after these
changes and every case still passes, since no `Core` file it links against
was modified. `Tests` links only `Core`, never `App`, so it cannot and does
not exercise the new GPU code; `Core`'s CPU geometry/BVH files were
independently confirmed (by inspection and by the fact that `Tests` itself
builds and links) to still contain no OpenGL headers or GL/GLSL-typed
symbols.

**Not done, by design** (explicitly out of scope for this milestone, per the
prompt and `specs/milestones.md` M8):
- No GPU BVH construction, upload, or traversal. `hitTriangles()` is a flat
  O(`u_TriangleCount`) scan over every triangle for every ray -- the GPU
  correctness reference `specs/gpu-integration.md` describes, and nothing
  more.
- No traversal-mode uniform/toggle (`u_TraversalMode`) yet: with only one GPU
  triangle traversal mode implemented, there is nothing to switch between.
  This is `specs/gpu-integration.md`'s "Traversal Mode" section, explicitly
  introduced alongside GPU BVH in M8.
- No benchmarking infrastructure, no GPU timing, no CSV export (M9).
- No per-triangle materials, UVs, or normals; the shading model is exactly
  the diffuse/Lambertian scatter that already existed, applied identically to
  sphere and triangle hits.
- `Core::Sphere`'s `std140` buffer, binding 0, and `hitSphere()` are
  byte-for-byte unchanged.

---

# M8 — GPU BVH

## Objective

Add BVH traversal to the compute shader.

### Implement

- GPU BVH node representation
- GPU BVH buffer
- BVH upload
- Iterative compute-shader traversal
- Traversal mode selection

### Traversal Modes

The renderer must support:

    BruteForce
    BVH

### Requirements

The BVH should:

- Traverse AABBs.
- Traverse leaf triangles.
- Track closest hit.
- Respect `tMin` and `tMax`.
- Use an explicit traversal stack.
- Handle stack capacity safely.

### Acceptance Criteria

The same scene can be rendered with:

    BruteForce

and:

    BVH

The resulting geometry intersections must be equivalent.

The existing sphere renderer must remain functional.

### Status: COMPLETE

Implemented across `App/Source/AppLayer.h`/`.cpp` (GPU BVH serialization,
buffer/uniform orchestration, traversal-mode toggle) and
`App/Shaders/RayTracing.glsl` (GPU BVH node type, ray-AABB test, iterative
traversal, `hitWorld()` dispatch). Per `specs/bvh.md` and this milestone's
"Implement" list, **no BVH construction algorithm was added on the GPU or
duplicated on the CPU**: `Core::BVH` (M5) is unchanged and is the only
construction path. `Core/Source/Core/Renderer/*` is byte-for-byte unchanged
from M7 -- confirmed by the `Tests` target still linking and passing without
any `Core` file being touched (see "Build/Test Results" below) -- so `Core`
remains fully OpenGL-independent, per `specs/architecture.md`.

**GPU BVH representation** (`App/Source/AppLayer.cpp`, file-scope
`AppGPUBVHNode`): a serialization of `Core::BVHNode`
(`Core/Source/Core/Renderer/BVH.h`), analogous to M7's `AppGPUTriangle`, but
*not* built the same way. `Core::BVHNode` is `AABB Bounds` (two `glm::vec3`)
followed by four `uint32_t` (`FirstPrimitive`, `PrimitiveCount`, `LeftChild`,
`RightChild`); only `Bounds`, the leaf primitive range, and the two child
indices are uploaded, matching this milestone's "only upload information
actually required for traversal." No `Centroid` field exists on `BVHNode`
(only on `BVHPrimitive`, which is CPU-construction-only and not uploaded at
all).

Layout derivation, verified by `static_assert(sizeof(...))` /
`static_assert(offsetof(...))` immediately after the struct definition (not
assumed, per `specs/gpu-integration.md` "Static validation"):

- CPU: `Core::BVHNode` is 40 bytes, tightly packed, 4-byte aligned -- CPU-only,
  not uploaded directly (same reasoning as `Core::Triangle`).
- GLSL (`App/Shaders/RayTracing.glsl`, `struct BVHNode` inside the `std430`
  `BVHNodeBuffer`): `vec3 boundsMin` at offset 0, `vec3 boundsMax` at offset
  16 (both base-aligned to 16, as with M7's `Triangle`), then `uint
  firstPrimitive` at offset **28** (not 32): a scalar immediately after a
  `vec3` only needs 4-byte alignment, unlike a `vec3` immediately after
  another `vec3`, which needs 16-byte alignment. `primitiveCount`/
  `leftChild`/`rightChild` follow at 32/36/40. Raw size 44, rounded up to 48
  (a multiple of the struct's largest member alignment, 16, from the two
  `vec3`s) for the array stride.
- This is a materially different padding shape than M7's `AppGPUTriangle`
  (three consecutive `vec3`s, where *every* member needs 16-byte alignment).
  Blindly reusing "pad every `vec3` to 16 bytes" here would place
  `FirstPrimitive` at CPU offset 32 while the GPU reads offset 28 -- a
  documented anti-pattern in `AppLayer.cpp`'s comment block, not just an
  abstract warning. The offset arithmetic was independently verified with a
  standalone `sizeof`/`offsetof` probe program before being encoded into the
  `static_assert`s, and again by `g++` compiling those `static_assert`s.
  `AppGPUBVHNode` is therefore: `vec3 BoundsMin` + an explicit `float _Pad0`
  (aligns `BoundsMax` to 16), `vec3 BoundsMax` (no trailing pad -- the next
  member only needs 4-byte alignment and already gets it), the four
  `uint32_t` fields, and a trailing `uint32_t _Pad1` (44 -> 48, matching the
  GLSL array stride). `sizeof(AppGPUBVHNode) == 48`.
- Serialisation: `BuildGPUBVHNodes(const Core::BVH&)` explicitly copies each
  field out of every `Core::BVHNode` (via `BVH::GetNodes()`); nothing is
  `memcpy`'d from `Core::BVHNode`.
- Buffer type / binding: `GL_SHADER_STORAGE_BUFFER`, `std430`, binding index
  **2** (binding 0 = sphere/`std140`, binding 1 = triangle/`std430`, both
  unchanged from M6/M7; `specs/architecture.md`/`specs/gpu-integration.md`
  "new SSBOs start at binding 1" -- the triangle buffer already claimed 1, so
  the BVH buffers continue at 2 and 3).

**GPU BVH primitive-reference buffer** (binding **3**): the flattened
`Core::BVH::GetPrimitives()` array, reduced to just each slot's `TriangleId`
(`BuildGPUBVHPrimitiveIds()`), per this milestone's and `specs/bvh.md`'s "use
the existing flattened primitive-reference array and triangle IDs" --
`BVHPrimitive::Bounds`/`Centroid` are CPU-construction-only and are not
uploaded. GLSL side is a plain `uint bvhPrimitiveTriangleIds[]` in a
`std430` block: `specs/gpu-integration.md`'s "Layout qualifier" section calls
out exactly this case (`std140` would round a scalar array's stride up to 16
bytes and silently corrupt a 4-byte-per-element CPU array); `std430` keeps
the stride at 4 bytes, so `BuildGPUBVHPrimitiveIds()` uploads a plain
`std::vector<uint32_t>` directly, with no wrapper struct.

A BVH leaf's primitive range indexes into `bvhPrimitiveTriangleIds[]`, whose
values in turn index the *existing, unmodified* `triangles[]` SSBO (binding
1, M7). **No triangle geometry is duplicated into the BVH buffers or
re-uploaded**, satisfying this milestone's "BVH primitives should reference
the existing triangle buffer by TriangleId."

**Traversal-mode mechanism** (`specs/gpu-integration.md` "Traversal Mode"):
a `TraversalMode { BruteForce = 0, BVH = 1 }` enum owned by `AppLayer`
(`AppLayer.h`), defaulting to `BruteForce` so the M7 baseline is unchanged
until explicitly switched. `OnRender()` sets `uniform int u_TraversalMode`
every frame (alongside the pre-existing uniforms), and
`RayTracing.glsl`'s `hitWorld()` dispatches to either the M7 brute-force
`hitTriangles()` or the new `hitTrianglesBVH()` based on that uniform --
`trace()` is unchanged and still has no idea which traversal mode (or even
which primitive kind) produced its `HitRecord`. Selected at runtime: pressing
`B` (`AppLayer::OnKeyPressed`, wired into `AppLayer::OnEvent` alongside the
pre-existing `EditorCameraController::OnEvent` call) toggles the mode and
sets `m_FrameIndex = 0`, per `specs/gpu-integration.md`'s explicit
requirement that a mode change must reset accumulation (otherwise two
different images blend together). This mirrors the already-existing
camera-triggered reset (`m_Camera.WasUpdated()`) one-for-one, rather than
introducing a new reset mechanism.

**GPU BVH traversal** (`App/Shaders/RayTracing.glsl`, `hitTrianglesBVH()`,
plus a `hitAABB()` helper): an iterative, explicit-stack mirror of
`Core::IntersectBVH` (`TriangleTraversal.cpp`, M6), not a re-derivation:

1. `hitAABB()` is `Core::AABB::Intersects()` translated line-for-line (same
   per-axis ternary slab narrowing, not a vectorized `min`/`max`), so it
   shares the same degenerate-axis behaviour: a zero ray-direction component
   with the origin exactly on that slab plane produces `0 * inf = NaN`,
   which fails both `>`/`<` comparisons and leaves that axis's interval
   unchanged (documented as "safe/conservative" on the CPU side).
2. `hitTrianglesBVH()` pushes the root index (0, matching
   `Core::BVH::RootIndex`), then loops: pop a node, re-test its AABB against
   the *current* `closest` (required even for a node already tested before
   being pushed, since a sibling subtree visited in between may have shrunk
   `closest`), and on a leaf (`primitiveCount > 0`) scans
   `[firstPrimitive, firstPrimitive + primitiveCount)` through
   `bvhPrimitiveTriangleIds[]` into the existing `hitTriangle()` -- the exact
   same GPU Möller-Trumbore function `hitTriangles()` already uses, per this
   milestone's explicit requirement. On an internal node
   (`primitiveCount == 0`), both children's AABBs are tested immediately (so
   a missed child is never pushed), and the **farther** child (by AABB entry
   distance `tEnter`) is pushed first so the **nearer** child is popped and
   visited first (LIFO stack) -- identical near/far ordering and left-biased
   tie-break to `Core::IntersectBVH`.
3. `closest`/`hitAnything`/`rec` are the same `inout` triple `hitTriangles()`
   already uses, so both traversal modes share one closest-hit contract:
   `hitWorld()`'s prior sphere loop and either triangle path only ever
   overwrite `rec` on a strict improvement.

**Stack strategy**: a fixed-size local array, `int stack[64]`, with an
explicit `stackSize` cursor -- no recursion (GLSL has no usable call stack
for this). 64 is derived, not guessed: `Core::BVH::BuildRecursive` always
partitions a range by primitive *count* at the median
(`std::nth_element`, `specs/bvh.md` "Split at Median"), never by a spatial
threshold that could fail to shrink the range, so every internal node's two
children receive at most `ceil(count/2)` primitives regardless of scene
geometry. That halving bounds tree depth by `ceil(log2(triangleCount))` plus
a small constant for the leaf-size floor. The traversal shape used here (push
both children, but only one sibling is ever left waiting on the stack per
level, since the other is popped and descended into immediately) needs stack
capacity equal to only the tree's *depth*, not twice it -- so 64 covers a
perfectly balanced BVH over more than 2^60 triangles, far beyond anything
this project can build, upload, or render. Every push is guarded by
`if (stackSize < BVH_STACK_SIZE)`; a push that would overflow is simply
dropped rather than overwriting another slot or indexing out of bounds --
per this milestone's "handle stack capacity safely," a hypothetical overflow
degrades to an incomplete (conservative) traversal, never undefined
behaviour or GPU memory corruption. This cannot be triggered by any BVH this
project actually builds, per the depth bound above.

**Known, documented limitation**: `Core::IntersectBVH`'s M6 exact-tie fix
(probing with `nextafter(closest, +inf)` so two triangles at a bit-identical
distance resolve to the lower `TriangleId`, independent of traversal order)
is *not* reproduced on the GPU. Per this milestone's explicit instruction
("do not modify the CPU implementation just to accommodate GPU traversal")
and its absence from `specs/gpu-integration.md`'s "GPU BVH Traversal"
section, both GPU paths keep the simpler M7 behaviour instead: a subsequent
triangle at `t == closest` is rejected by `hitTriangle()`'s own `t >= tMax`
check, so an exact tie resolves to whichever of the two tied triangles is
visited first by that path's traversal order -- consistent between GPU
brute-force and GPU BVH's *own* internal tie behaviour is not guaranteed to
match `Core::IntersectBVH`'s id-based rule in the specific bit-identical-tie
corner case. This has no observed effect on any scene rendered during
validation (see below): every brute-force/BVH image pair compared bit-exact.

**Build/Test Results**: a full clean configure + build (`rm -rf build`,
reconfigure, rebuild `glad`/`Core`/`App`/`Tests`) succeeds with no warnings
introduced. `Tests` (linking only `Core`, never `App`) still reports **114 /
114** passing, unchanged from M6/M7 -- expected, since no `Core` file was
touched. `App` was run against a real OpenGL context (AMD Radeon Graphics /
Mesa 26.0.3, GL 4.6 core, the same environment as M7) with no shader compile
or link errors and no GL debug-callback errors.

**Manual rendering validation** (temporary, non-committed `glReadPixels`-based
capture hooks, added and removed the same way M7 describes -- not part of the
final diff): rendered the existing M7 validation scene (2 spheres, the
hand-authored quad, the imported tetrahedron) to a 60-frame-accumulated PPM
once with `u_TraversalMode = BruteForce` and once with `u_TraversalMode =
BVH`, same camera, same frame count. The two images are **bit-for-bit
identical** (zero max channel difference over all 800x600 pixels). This was
repeated three more times to specifically exercise this milestone's required
scenarios beyond what the default 6-triangle/leaf-size-4 scene alone
constructs as a single leaf:

- **Multiple BVH levels / near-far child ordering**: rebuilt the same
  6-triangle scene with leaf size 1 (forcing every triangle into its own
  leaf, several internal levels) -- brute-force vs. BVH still bit-exact, and
  identical to the default-leaf-size run, confirming the geometric result is
  independent of tree shape.
- **Many triangles / deep tree / rays hitting one or both children / rays
  missing the root**: temporarily scattered 500 additional small, randomly
  placed, deterministically seeded triangles around the existing objects
  (506 triangles total, a visibly much deeper/wider BVH) and re-rendered;
  still bit-exact between modes. The sky region of every rendered frame
  (every pixel that shows only sky colour, no geometry) is itself a "ray
  misses the root" case exercised on every single render in this milestone,
  since a full-screen path tracer always casts rays that miss all geometry.
- **Traversal-mode switch + accumulation reset, end-to-end**: simulated a
  runtime switch (the same state change `OnKeyPressed` performs) from
  BruteForce to BVH mid-run (after 5 accumulated frames), then accumulated
  30 more frames post-switch and compared against a clean BVH-only 30-frame
  run with no prior mode. The two are bit-for-bit identical, proving the
  reset actually clears the accumulation buffer's contribution rather than
  blending two modes' images together, and that progressive accumulation
  continues correctly (same per-pixel/per-frame RNG sequence) after a
  mid-run reset.
- Nearest-hit selection, mixed spheres + triangles, and the imported
  tetrahedron are exercised by the base validation scene itself (the sphere
  partially occludes the quad/tetrahedron in every one of the renders above).

**Not done, by design** (out of scope per this milestone and
`specs/milestones.md` M9): no GPU BVH construction or refitting (`Core::BVH`,
unmodified, is the only construction path); no SAH; no dynamic BVHs; no
benchmarking, GPU timing, or CSV export; no Nsight/RenderDoc integration; no
unrelated renderer refactoring. `Core::Sphere`'s `std140` buffer, binding 0,
`hitSphere()`, and the M7 triangle buffer/binding/`hitTriangle()` are
byte-for-byte unchanged.

---

# M9 — Benchmarking

## Objective

Measure the performance impact of BVH traversal.

### Implement

- Benchmark scenes
- Traversal mode selection
- Warm-up
- Repeated measurements
- GPU timing
- BVH build timing
- Upload timing
- CSV export

### Benchmark Configurations

Test multiple triangle counts:

    ~100
    ~1,000
    ~10,000
    ~100,000
    Large real-world model

### Acceptance Criteria

Every benchmark configuration can be executed using:

    BruteForce

and:

    BVH

with identical rendering parameters.

Results are saved in a machine-readable format.

### Status: COMPLETE

Implemented as a new, separate `Benchmark` executable target
(`Benchmark/CMakeLists.txt`, `Benchmark/Source/*`), added to the top-level
`CMakeLists.txt` alongside `Core`/`App`/`Tests` -- the same "separate target"
pattern `Tests` already uses, per `specs/architecture.md`'s repository
structure. `Core/`, `App/Source/AppLayer.*`, and `App/Shaders/RayTracing.glsl`
are **byte-for-byte unchanged** from M8: this milestone required no
modification to any existing traversal algorithm, GPU buffer layout, or
shader code to expose it for measurement. This was a deliberate choice (see
"Decisions requiring review" below) made to keep the interactive
application's already-reviewed M7/M8 code path at zero regression risk.

#### Why a separate executable

Running a benchmark is always an explicit, deliberate action
(`./Benchmark ...`) that cannot be triggered by the interactive `App` simply
loading a large model, directly satisfying this milestone's "no accidental
catastrophic interactive runs" requirement. `Benchmark` does not use
`Core::Application`/`Core::Window`/`Core::Layer`: it creates its own hidden
(`GLFW_VISIBLE = false`) OpenGL 4.6 core context (`Benchmark/Source/
GpuContext.h`/`.cpp`) purely to drive the existing compute shader and
`Core::Renderer::StorageBuffer`/`Texture` abstractions -- no windowing, input,
or event machinery is needed for a benchmark tool.

#### Architecture

    Benchmark/Source/
        Main.cpp             CLI parsing, orchestration, summary printing
        BenchmarkResult.h     One shared CSV row schema for CPU and GPU rows
        Stats.h / .cpp        mean / min / max / sample stddev over N samples
        CsvWriter.h / .cpp    RFC4180-style CSV writer (quotes fields
                              containing commas -- GL_RENDERER strings do)
        Scenes.h / .cpp       Synthetic mesh generator + real-model loading
        RayGeneration.h / .cpp  Deterministic CPU test-ray generator
        CpuBenchmark.h / .cpp   CPU BruteForce vs BVH harness
        GpuContext.h / .cpp     Hidden GLFW/OpenGL context
        GpuUpload.h / .cpp      GPU triangle/BVH upload structs (see below)
        CameraFraming.h / .cpp  Deterministic camera framing for any mesh
        GpuBenchmark.h / .cpp   GPU BruteForce vs BVH harness

`CpuBenchmark` calls `Core::IntersectBruteForce`/`Core::IntersectBVH` (M3/M6)
directly. `GpuBenchmark` drives the existing compiled compute shader
(`App/Shaders/RayTracing.glsl`, compiled once via the existing
`Renderer::CreateComputeShader`) exactly as `AppLayer` does, toggling the
existing `u_TraversalMode` uniform (M8) between frames. No traversal
algorithm is duplicated or reimplemented; both harnesses are thin measurement
wrappers around the existing M3/M6/M7/M8 implementations.

#### Decisions requiring review

- **`GpuUpload.h`/`.cpp` duplicates, rather than shares, `AppLayer.cpp`'s GPU
  upload structs.** `AppLayer.cpp` defines `AppGPUTriangle`/`AppGPUBVHNode`
  and their serialization functions in an anonymous namespace (M7/M8).
  `GpuBenchmark` needs the identical layout to build the same SSBOs. Sharing
  a header would have required editing `AppLayer.cpp` to use it; duplicating
  it keeps `AppLayer.cpp` completely untouched. Both copies carry the same
  `static_assert`-verified offset/size documentation, so a future layout
  change to one without the other fails to compile rather than silently
  corrupting GPU data -- but they are still two hand-synchronized copies.
  Revisit if this drifts, or fold both into a shared header at that point.
- **The shader's hard-coded max path depth (`trace()`'s `for (int i = 0; i <
  50; i++)`) was not turned into a uniform.** `specs/benchmarking.md` asks
  for hardcoded rendering parameters to be "made explicit and recorded
  rather than assumed"; this was satisfied by recording the constant `50` on
  every GPU CSV row (`max_path_depth`) rather than by adding a new uniform to
  `RayTracing.glsl`, again to keep the shader file untouched. If a future
  milestone wants to sweep max path depth, this uniform would need adding.
- **GPU brute force is not attempted at all above a configured triangle
  ceiling** (`GpuBenchmarkConfig::MaxBruteForceTriangles`, default 20,000 for
  the full run, 5,000 for `--quick`), decided *before* any GPU work is
  submitted. This is a simple, static, documented cutoff rather than a
  dynamic cost model. It was picked empirically on this project's
  development hardware (see "Validation" below) with headroom; a much faster
  or much slower GPU would want a different value. This is why the vendored
  Bunny (69,630 triangles) and Armadillo (212,574 triangles) models never run
  a GPU brute-force pass in the default configuration -- exactly the
  "computationally impractical, handled explicitly" case this milestone's
  acceptance criteria describe.
- **A bounded per-iteration wait (`GpuBenchmarkConfig::MaxWaitPerIteration`,
  default 15s) polls for GPU completion instead of blocking.** This is a
  defense-in-depth safety net on top of the ceiling above (in case an
  unexpectedly slow driver/hardware combination is hit regardless of
  triangle count); it was manually verified to trigger correctly (see
  "Validation"). It bounds *this process's* wait, not the GPU work itself --
  already-submitted GPU commands cannot be cancelled from the client side.
  Since `Benchmark` is a separate, short-lived, non-interactive process, this
  is sufficient to satisfy "cannot hang indefinitely."
- **Camera framing is automatic** (`CameraFraming.h`/`.cpp`): a fixed
  three-quarter viewing angle plus a distance derived from each mesh's own
  bounding-sphere radius, so every scene (from 100 synthetic triangles to the
  212,574-triangle Armadillo) is framed without any per-scene hand-tuning.
  This matters specifically for BVH timing: unlike brute force (which always
  tests every triangle regardless of visibility), BVH cost depends on which
  nodes rays actually pass near, so a scene that isn't actually in view
  would silently under-test the BVH path.
- **`u_SphereCount` is fixed at 0 for every GPU measurement.** Only triangle
  traversal is being measured/compared (per `specs/benchmarking.md`'s "the
  benchmark must change only the traversal strategy"); spheres are an
  orthogonal, unrelated primitive type and are not part of this comparison.
- **GPU render timing uses a single fixed frame index (0) for every
  iteration, in both modes.** `RayTracing.glsl`'s per-pixel RNG seed depends
  only on pixel coordinates and `u_FrameIndex`, so this makes every warm-up
  and measured dispatch -- BruteForce or BVH -- render literally the same set
  of camera/scatter rays, which is what makes a pixel-for-pixel BruteForce
  vs. BVH comparison meaningful (see below) and keeps the measurement
  reproducible run-to-run. It does not exercise progressive multi-frame
  accumulation (that path is unchanged and still covered by the interactive
  application and M7/M8's manual validation).

#### Measured quantities and how they map to CSV columns

One CSV (`benchmark_results.csv`, one row per scene x traversal-mode x
processor) is produced per run, with a shared schema for CPU and GPU rows
(`BenchmarkResult.h`). Fields that do not apply to a row (e.g. resolution on
a CPU row, BVH stats on a BruteForce row) are written as the literal string
`NA`, never `0`, so a missing measurement can never be misread as a real
zero.

Timing categories (`specs/benchmarking.md` "Measurements", kept distinct
throughout, never merged into a combined number without saying so):

- `model_load_ms` -- CPU wall-clock time to import (real models, via the
  unmodified `Core::ImportObjFile`) or deterministically generate (synthetic
  scenes) the mesh. Excluded from every other timing column.
- `bvh_build_ms` -- CPU rows: `Core::BVH` construction time alone (M5), timed
  before ray generation's results are used and before any traversal
  warm-up/measurement. GPU rows: CPU BVH construction *and* its conversion
  into the GPU upload arrays (`GpuUpload::BuildGPUBVHNodes`/
  `BuildGPUBVHPrimitiveIds`) combined and explicitly labelled as such --
  this is `specs/benchmarking.md`'s "GPU BVH construction/serialization
  time" category, distinct from CPU-only `bvh_build_ms` on CPU rows and from
  the upload columns below. `NA` on BruteForce rows (no BVH is built for
  that mode).
- `gpu_triangle_upload_ms` / `gpu_bvh_node_upload_ms` /
  `gpu_bvh_primitive_upload_ms` -- GPU rows only. Each wraps exactly one
  `Renderer::CreateStorageBuffer` call with `glFinish()` immediately after,
  so the measured interval covers the upload actually completing on the GPU,
  not just the CPU-side driver call returning (see "GPU timing methodology"
  below). The triangle upload is measured once per scene and recorded on
  both the BruteForce and BVH rows (both modes read the same triangle
  buffer); the two BVH-buffer uploads are recorded only on the BVH row.
- `measurement_mean_ms` / `_min_ms` / `_max_ms` / `_stddev_ms` -- the primary
  measurement for the row: one full sweep over every CPU test ray (CPU rows),
  or one dispatched frame's GPU-completed render time (GPU rows). Sample
  standard deviation (Bessel-corrected, `Stats.cpp`), `0` when fewer than 2
  samples were collected (only possible after a `timeout`, see below).
- `node_count` / `leaf_count` / `max_depth` / `min_leaf_size` /
  `max_leaf_size` / `average_leaf_size` -- `Core::BVH::GetStats()` (M5),
  copied verbatim onto BVH rows only.
- `correctness_checked` / `correctness_passed` / `mismatch_count` /
  `tested_ray_count` -- CPU rows: whether `Core::IntersectBruteForce` and
  `Core::IntersectBVH` were run on the identical ray set and compared
  (always `true` for CPU rows), whether every comparison agreed, and how
  many/how many rays were tested. See "CPU correctness methodology" below.
- `gpu_pixel_check_performed` / `gpu_pixel_check_passed` /
  `gpu_pixel_max_abs_diff` / `gpu_pixel_mismatch_count` -- GPU rows: whether
  a full accumulation-texture readback comparison between the GPU BruteForce
  and GPU BVH renders of the same scene/camera/frame was performed (only
  possible when brute force was not skipped/timed out for that scene), and
  its result. See "GPU correctness methodology" below.
- `status` / `status_reason` -- `ok`, `skipped` (GPU brute force above the
  triangle ceiling), `timeout` (a bounded GPU-completion wait expired), or
  `error` (e.g. a model failed to import). Never silently absent: a
  skipped/timed-out row is still written, with a human-readable reason.
- Reproducibility columns (`specs/benchmarking.md` "Reproducibility
  Fields"): `traversal_mode`, `triangle_count`, `leaf_size`,
  `resolution_width`/`_height`, `samples_per_pixel`, `max_path_depth`,
  `iterations`, `warmup_iterations`, `gl_vendor`/`gl_renderer`/`gl_version`
  (GPU rows), `run_timestamp_utc`.

#### CPU benchmark methodology

Per scene: (1) generate a deterministic ray set from the mesh's bounds
(`RayGeneration.h`/`.cpp`, seeded, excluded from all timing); (2) build the
BVH once, timed; (3) run one full, untimed correctness pass comparing
`Core::IntersectBruteForce` against `Core::IntersectBVH` on every ray
(hit/miss, `TriangleId`, `T`, `Position`, `Normal`, `FrontFace`, each within
an absolute+relative tolerance), independently of and before any performance
measurement (`specs/benchmarking.md`: "Correctness must be verified
independently from performance"); (4) warm up each traversal path with
several full, discarded sweeps over the same ray set; (5) time N further
full sweeps per path, accumulating each hit's `T` into a checksum so the
compiler cannot eliminate the loop as dead code. `tMin`/`tMax` match the
project's existing `0.001` / `FLT_MAX` convention (`RayTracing.glsl`'s
`trace()`, `Tests/Source/BVHTraversalTests.cpp`).

#### GPU benchmark methodology

Per scene, using one hidden GL context and the one compiled compute shader
program for the whole run: upload the triangle buffer (timed); build +
serialize + upload the BVH (each timed separately, as above); create a
dedicated accumulation texture at the configured resolution; then, per
traversal mode: set `u_TraversalMode`, `u_SphereCount = 0`,
`u_TriangleCount`, `u_BVHNodeCount`, `u_SamplesPerPixel`, `u_FrameIndex = 0`,
and the framed camera uniforms; run several discarded warm-up dispatches;
then dispatch N measured frames, each wrapped in a `GL_TIME_ELAPSED` query
object. Every query's result is retrieved by *polling*
`GL_QUERY_RESULT_AVAILABLE` in a bounded loop (never a single blocking
`glGetQueryObjectui64v` call), so the process can never wait longer than the
configured budget for one dispatch (`specs/benchmarking.md`: "account for
the asynchronous nature of GPU execution... do not rely exclusively on CPU
wall-clock time"; this milestone's "cannot hang indefinitely" requirement).
GPU brute force is skipped outright (no GPU submission at all) when the
scene's triangle count exceeds a configured ceiling.

**GPU correctness methodology**: when both the BruteForce and BVH dispatch
sequences for a scene complete without a timeout, the accumulation texture
is read back (`glGetTextureImage`, after `glFinish()`) for each mode and the
two images are compared pixel-by-pixel (`GpuBenchmark.cpp`'s
`ComparePixels`), reporting the maximum absolute per-channel difference and
the count of channels exceeding a `1e-4` tolerance. Because `u_FrameIndex` is
fixed at 0 for every dispatch (see "Decisions requiring review" above), both
modes render the identical set of rays, so this is a meaningful equivalence
check, not just "both produced *an* image" -- directly exercising `specs/
gpu-integration.md`'s "GPU Validation" requirement ("GPU brute-force vs GPU
BVH must produce equivalent intersections and visually equivalent rendered
output").

#### Benchmark configurations

`Scenes.h`/`.cpp`'s default scene list (`--quick` uses a smaller subset --
see below):

    Synthetic_100      100 triangles      (generated, seed 1001)
    Synthetic_1000     1,000 triangles    (generated, seed 1002)
    Synthetic_10000    10,000 triangles   (generated, seed 1003)
    Synthetic_100000   100,000 triangles  (generated, seed 1004)
    Bunny              69,630 triangles   (App/Assets/bunny.obj)
    Armadillo          212,574 triangles  (App/Assets/armadillo.obj)

Synthetic scenes are independent, randomly placed, non-degenerate triangles
generated by a seeded `std::mt19937` (`Scenes::GenerateSyntheticMesh`); the
scattering volume scales with the cube root of the triangle count so
density stays roughly constant across scales. Bunny and Armadillo are the
two real-world OBJ models already vendored under `App/Assets/` (imported via
the unmodified `Core::ImportObjFile`), satisfying this milestone's "medium"
and "large" real-world mesh requirement and the "Stanford Bunny/Armadillo"
example named in `specs/milestones.md`'s M9 prompt; a Dragon model was not
added, since Bunny + Armadillo already span two clearly distinct real-world
scales and the importer places no format-specific restriction that would
make a third model qualitatively different to add later.

CPU defaults: 3,000 test rays, 3 warm-up sweeps, 12 measured sweeps, BVH leaf
size 4 (`Core::BVH::DefaultLeafSize`). GPU defaults: 320x240, 1 sample per
pixel, 3 warm-up frames, 10 measured frames, leaf size 4, GPU brute-force
ceiling 20,000 triangles, 15s max wait per dispatch. `--quick` (see below)
reduces every one of these for fast smoke-testing; it is explicitly
documented as not producing report-quality numbers.

#### Running it

    cmake --build build --target Benchmark
    ./build/Benchmark/Benchmark                 # full run, CPU + GPU
    ./build/Benchmark/Benchmark --quick          # fast smoke test, all scenes reduced
    ./build/Benchmark/Benchmark --skip-gpu       # CPU only (no OpenGL context needed)
    ./build/Benchmark/Benchmark --skip-cpu       # GPU only
    ./build/Benchmark/Benchmark --output <path>  # override the output CSV path

`Benchmark` resolves `App/Shaders/RayTracing.glsl` and `App/Assets/*.obj` via
an absolute path baked in at configure time (`BENCHMARK_APP_DIR`), so --
unlike `App` -- it can be run from any working directory. Default output is
`Benchmark/Results/benchmark_results.csv` (overwritten each run); a specific
run's file is what M10 analysis should treat as its input.

#### Validation performed

- **Clean build**: `rm -rf build`, reconfigure, full rebuild of `glad`,
  `Core`, `App`, `Tests`, and the new `Benchmark` target succeeds with no
  warnings introduced.
- **Full existing test suite**: `Tests` still reports **114 / 114** passing,
  unchanged from M8 -- expected, since `Core` was not modified at all.
- **CPU brute-force / BVH correctness unchanged**: exercised directly by
  every CPU benchmark row's own correctness check (see above), which cross-
  validates `Core::IntersectBruteForce` against `Core::IntersectBVH` on top
  of (not instead of) the pre-existing `Tests` suite coverage. All CPU rows
  reported `correctness_passed = true` (`mismatch_count = 0`) across every
  scene size, from 100 synthetic triangles through the 212,574-triangle
  Armadillo, on the full (non-`--quick`) run described below.
- **GPU brute-force / BVH still render correctly**: the interactive `App`
  was rebuilt and run against a real OpenGL context (AMD Radeon Graphics /
  Mesa 26.0.3, GL 4.6 core) and stayed responsive (no crash, no GL debug
  errors) -- unsurprising since `AppLayer.cpp`/`RayTracing.glsl` were not
  touched, but confirmed directly rather than assumed. Independently, every
  GPU benchmark row where both modes completed reported a pixel-exact
  cross-check (`gpu_pixel_max_abs_diff = 0.0`, `gpu_pixel_mismatch_count =
  0`) between the GPU BruteForce and GPU BVH renders of the same scene.
- **Reproducibility**: `--quick --skip-gpu` was run twice to independent
  output files; every deterministic field (triangle count, BVH node/leaf/
  depth/leaf-size statistics, mismatch count, correctness pass/fail) was
  identical byte-for-byte between the two runs' CSVs, while timing columns
  varied only by ordinary measurement noise. The GPU pixel cross-check
  (`gpu_pixel_max_abs_diff = 0.0`) is itself a reproducibility check: it
  requires the same camera/ray/RNG sequence to reproduce bit-identical
  results twice within a single run (BruteForce vs. BVH), which it did.
- **Small and large meshes**: the default (non-`--quick`) scene list spans
  100 to 212,574 triangles (see "Benchmark configurations" above); both ends
  were exercised in the same run (see below).
- **No accidental catastrophic interactive run**: `App` was not modified in
  any way, so loading any model through it behaves exactly as it did after
  M8. Running a benchmark requires explicitly invoking the separate
  `Benchmark` executable.
- **Timeout safety net actually triggers**: `GpuBenchmarkConfig::
  MaxWaitPerIteration` was temporarily set to `0` and the GPU `--quick`
  benchmark re-run. Every GPU dispatch immediately reported `status =
  timeout` with a clear reason, the process still completed (well under a
  second, no hang) and still wrote a valid CSV, and the setting was then
  reverted to its production value (`15000` ms) and the project rebuilt
  before any other validation. This confirms the bounded-polling design
  (not a blocking wait) actually prevents an indefinite hang rather than
  only being reasoned about on paper.
- **A representative full run** (`./build/Benchmark/Benchmark`, no flags, on
  this development machine: AMD Radeon Graphics / renoir / Mesa 26.0.3, GL
  4.6 core) completed end-to-end with **24 rows: 21 `ok`, 3 `skipped`
  (Synthetic_100000, Bunny, and Armadillo's GPU BruteForce rows, all for the
  documented ceiling reason above), 0 `timeout`, 0 `error`**, and zero
  correctness or pixel-check failures. Its output
  (`Benchmark/Results/benchmark_results.csv`) is committed as the initial
  M10 input. Representative figures from that specific run (full numbers,
  units, and every column are in the CSV -- these are quoted here only to
  demonstrate the tool produced sane, non-invented output, not as this
  milestone's performance conclusions, which are explicitly out of scope
  per "Scope boundary" below):
    - CPU, Armadillo (212,574 triangles): BruteForce mean 12,185.9 ms vs. BVH
      mean 3.549 ms per full 3,000-ray sweep (n=12 each).
    - CPU, Synthetic_100 (100 triangles): BruteForce mean 5.11 ms vs. BVH
      mean 0.94 ms (n=12 each) -- a far smaller gap than Armadillo's,
      consistent with BVH overhead mattering more at small N (measured, not
      assumed; see "Scope boundary").
    - GPU, Synthetic_10000 (10,000 triangles, 320x240, 1 spp): BruteForce
      mean 106.1 ms vs. BVH mean 1.77 ms per completed frame (n=10 each).
    - GPU BVH successfully rendered Armadillo (212,574 triangles) in mean
      5.98 ms per frame, at a scale where GPU BruteForce was skipped
      entirely by the ceiling above.

#### Limitations

- The GPU brute-force triangle ceiling and per-iteration wait budget are
  fixed, hand-picked constants tuned for the development machine's GPU, not
  derived from a runtime cost model; a very different GPU may want different
  values (see "Decisions requiring review").
- GPU timing measures one dispatched frame's completed compute-shader
  execution time via `GL_TIME_ELAPSED`, which is the entire `hitWorld()` +
  path-tracing pipeline for that frame (sphere loop, triangle traversal,
  scattering, sky, accumulation write) -- it is not an isolated measurement
  of *only* the triangle-traversal instructions in isolation from the rest
  of the shader. `u_SphereCount = 0` removes the sphere loop's contribution,
  and the same fixed-ray, fixed-bounce-count setup is used for both
  BruteForce and BVH, so the *difference* between the two modes' render
  times is attributable to the traversal strategy, but neither row's
  absolute number is "pure" BVH/brute-force traversal time in isolation.
  This is consistent with `specs/benchmarking.md`'s own framing ("The
  primary rendering metric should be GPU execution time... measured under
  identical rendering conditions").
- The shader's maximum path depth (50, hardcoded) is not exposed as a
  runtime uniform (see "Decisions requiring review"); it is recorded, not
  swept.
- `GpuUpload.h`/`.cpp` is a documented, hand-synchronized duplicate of
  `AppLayer.cpp`'s equivalent structs, not a shared header (see "Decisions
  requiring review").
- Synthetic scenes are independent (non-connected) random triangles, not a
  connected mesh; this is sufficient for triangle-count scaling
  measurements but is not representative of a real model's spatial
  coherence at a given triangle count.
- Only two real-world models (Bunny, Armadillo) are included by default; a
  third (e.g. Dragon) was not added, since the importer places no
  format-specific limitation that would make adding one later difficult.

#### Scope boundary (explicitly not done, per this milestone)

No SAH, no alternative BVH construction algorithms, no BVH refitting, no
dynamic BVHs, no GPU BVH construction (the CPU `Core::BVH`, unmodified, is
the only construction path; `GpuBenchmark` only serializes and uploads its
already-built result), no Nsight/RenderDoc integration, and -- per this
milestone's explicit instruction -- no final performance interpretation or
conclusions about BVH effectiveness. The bulleted figures under "Validation
performed" above are quoted only to demonstrate the tool executed
successfully and produced sane output; interpreting *why* the numbers look
the way they do, and drawing conclusions from them, is M10's job.

---

# M10 — BVH Analysis

## Objective

Analyze the generated BVHs and their performance.

### Collect

- Triangle count
- Node count
- Leaf count
- Maximum depth
- Average leaf size
- Build time
- Upload time
- Render time
- Total relevant time

### Analyze

- Brute-force scaling
- BVH scaling
- Construction overhead
- Upload overhead
- Effect of triangle count
- Effect of leaf size
- Effect of scene structure
- Synthetic vs real-world models

### Acceptance Criteria

The project contains enough reproducible data to support a university report/presentation.

The conclusions must distinguish measured results from theoretical expectations.

### Status: COMPLETE

This is an analysis milestone: it reads and interprets the M9 dataset
(`Benchmark/Results/benchmark_results.csv`) and produces no new rendering,
traversal, or BVH construction code. `Core/`, `App/`, `Tests/`, and
`Benchmark/` are byte-for-byte unchanged from M9. `Tests` was not re-run as
part of M10 for this reason (M9's own "114 / 114" figure is unaffected by an
analysis-only change and was not re-derived); it would need to be re-run
only if a future milestone touches `Core`.

**Dataset was re-run and re-analyzed once, on different GPU hardware.** The
initial version of this analysis used a representative M9 run on the
original development machine (AMD Radeon Graphics, `radeonsi`/`renoir`,
Mesa). `Benchmark/Results/benchmark_results.csv` was subsequently
regenerated by re-running `./build/Benchmark/Benchmark` on a machine with an
NVIDIA GeForce RTX 3050 Ti Laptop GPU (`run_timestamp_utc
=2026-09-20T22:04:47Z`); this is the CSV analyzed by the current
`Analysis/M10-BVH-Analysis.md`. Per explicit instruction, the AMD-hosted
numbers are **not** mixed with the NVIDIA-hosted numbers anywhere in the
analysis: every GPU table/plot/figure in the current document comes
exclusively from the NVIDIA run. CPU figures were also updated to this same
(single) latest run, since it is the one committed CSV and it contains its
own fresh CPU rows alongside the fresh GPU rows -- there is no CPU-only
historical CSV kept separately. Where the AMD-hosted and NVIDIA-hosted runs'
*conclusions* differ (see below), the difference is reported explicitly as a
measured, hardware-dependent observation rather than silently overwritten.

Implemented as a new top-level `Analysis/` directory, mirroring
`Benchmark/`'s `Source/`/`Results/` split:

    Analysis/
        M10-BVH-Analysis.md        The written analysis (all required sections)
        Source/
            generate_analysis.py    Reproducible, dependency-free table/plot generator
            svgplot.py              Small stdlib-only SVG chart renderer
        Results/
            tables/*.csv            6 derived-metric tables
            plots/*.svg             7 plots

**Why a Python script rather than manually computed numbers**: every table
and every plot in `Analysis/M10-BVH-Analysis.md` is generated by
`python3 Analysis/Source/generate_analysis.py` directly from the committed
CSV -- re-running it reproduces byte-identical tables from the same input.
This satisfies "prefer reproducible generated analysis over manually entered
values" without adding a project dependency: `generate_analysis.py` and
`svgplot.py` use only the Python standard library (`csv`, `math`,
`dataclasses`), the same "small, dependency-free tool for a narrow, one-off
scope" reasoning `specs/milestones.md` M4 used to justify a hand-written OBJ
parser instead of a third-party model-loading library. `matplotlib`/
`pandas` are not available in this environment (no working `pip`); rather
than adding a new project dependency to work around that, `svgplot.py`
renders the handful of line/scatter charts this milestone needs (linear or
log10 axes, multiple labelled series, dashed "theoretical reference" lines
visually distinct from solid "measured" lines) as plain SVG text.

**Analysis performed** (full detail and every number in
`Analysis/M10-BVH-Analysis.md`; summarized here):

- CPU BruteForce vs BVH compared quantitatively (absolute difference,
  speedup ratio, relative reduction, coefficient of variation) for all 6
  scenes -- every CPU row in the dataset is `status=ok`, so all 6 pairs are
  valid comparisons.
- GPU BruteForce vs BVH compared the same way, but only for the 3 scenes
  where GPU BruteForce actually completed (`Synthetic_100`,
  `Synthetic_1000`, `Synthetic_10000`); the 3 scenes where GPU BruteForce
  was skipped (`Synthetic_100000`, `Bunny`, `Armadillo`, all above the
  20,000-triangle ceiling) are reported in a separate table with no speedup
  or timing value computed against a "0" placeholder.
- BVH structural statistics (node/leaf count, max depth, leaf-size
  min/max/average) related to a theoretical median-split reference: measured
  max depth matches `ceil(log2(N/leafSize))` exactly for all 6 scenes;
  measured leaf count vs the theoretical minimum `ceil(N/leafSize)` ranges
  from 1.02x to 1.54x and is explicitly shown to be non-monotonic in N.
- BVH construction time analyzed separately from GPU upload time and from
  render time; a derived (not separately measured) frame-count amortization
  table shows one-time build+upload overhead falls under 10% of cumulative
  render time within at most 387 frames, and under 1% within at most 4,248
  frames, for every scene in the dataset (larger frame counts than the
  AMD-hosted run, because per-frame render time dropped more on the faster
  NVIDIA GPU than the largely CPU/PCIe-bound build+upload overhead did).
- The scene/ray-structure effect (BVH traversal time depends on more than
  triangle count alone) is **strongly and consistently reproduced on the
  CPU** across both the AMD-hosted and NVIDIA-hosted runs: the two
  real-world, spatially-coherent meshes (Bunny, Armadillo) traverse markedly
  faster than scattered synthetic scenes of comparable or much larger
  triangle count, with similar magnitude in both runs. On the GPU, the
  effect is hardware-dependent: it was measured for both real-world/
  synthetic pairs on the AMD iGPU, but on the NVIDIA discrete GPU it is only
  measured for the smaller pair (Bunny vs. `Synthetic_10000`) and is absent
  for the larger pair (Armadillo vs. `Synthetic_100000`, which instead
  scales roughly proportional to N on this GPU). This difference between
  the two GPU runs is reported explicitly in the analysis rather than
  resolved in favor of either, and is not fully explained by the current
  BVH statistics (no per-node visit-count instrumentation exists) -- flagged
  as a limitation and as something to consider instrumenting before/at M11
  if a more precise explanation is wanted.
- CPU correctness evidence (18,000 ray comparisons across 6 scenes, 0
  mismatches) and GPU correctness evidence (bit-exact pixel cross-checks,
  max abs diff 0.0, for the 3 scenes with a GPU BruteForce reference) are
  summarized in a section kept separate from every performance number, per
  `specs/bvh.md`'s "a faster result is not evidence that the BVH is
  correct." The bit-exact GPU pixel-check result is reproduced on both the
  AMD and NVIDIA GPUs.
- BVH structural statistics (node count, leaf count, max depth, leaf-size
  min/max/average) are byte-identical between the AMD-hosted and
  NVIDIA-hosted runs, confirming `Core::BVH` construction is deterministic
  and hardware-independent -- only the *timings* differ between runs.
- Measurement noise (coefficient of variation) is called out explicitly
  where it is large enough to matter: CPU BVH at the three largest-N scenes
  (Bunny, `Synthetic_100000`, Armadillo, ~12.7%-14.3% CoV) is the most
  notable source of variability in the current (NVIDIA-hosted) dataset. GPU
  noise is substantially lower on the NVIDIA GPU than it was on the AMD
  iGPU (no GPU row exceeds ~4.8% CoV here, versus up to ~70% previously for
  `Synthetic_100`'s BVH row) -- flagged as a hardware-dependent noise
  difference, not just restated as a fixed threshold.
- Explicit methodological limitations documented (single run analyzed and
  what is/isn't known about CPU hardware identity across runs, GPU/driver
  dependence and the hardware change between runs, the GPU brute-force
  ceiling as a hand-picked constant that was not re-tuned for the new GPU,
  fixed rendering configuration, whole-frame GPU timing rather than isolated
  traversal timing, median-split-not-SAH construction, the static-scene
  amortization assumption, the M8-documented GPU/CPU exact-tie semantic
  difference, non-connected synthetic geometry, and small sample counts at
  the noisiest rows).

**No genuine benchmark correctness bug was found.** Every row's own
correctness/pixel-check fields were already consistent (CPU: 0 mismatches on
all 6 scenes; GPU: bit-exact on all 3 scenes with a valid brute-force
reference), so no change to `Benchmark/`, `Core/`, or `App/` was required or
made. Per this milestone's "no algorithm changes" scope: the BVH
construction algorithm, GPU shader, CPU traversal, and interactive renderer
were not touched.

**Not done, by design**: no M11 cleanup, no final thesis-style conclusion,
no new benchmark run (this analysis is derived entirely from the single M9
CSV already committed at `Benchmark/Results/benchmark_results.csv`), no
per-node BVH visit-count instrumentation (identified above as something a
future milestone could add if a more precise explanation of the
scene-structure effect is wanted, but not added here since it would require
changing the traversal code this milestone is not permitted to touch).

---

# M11 — Final Cleanup and Documentation

## Objective

Prepare the project for final demonstration and submission.

### Tasks

- Remove temporary debugging code.
- Remove unused code.
- Verify sphere rendering.
- Verify triangle rendering.
- Verify model importing.
- Verify brute-force mode.
- Verify BVH mode.
- Verify benchmark mode.
- Ensure specifications match the implementation.
- Document important implementation decisions.
- Document known limitations.

### Final Acceptance Criteria

The project can demonstrate the complete pipeline:

    3D Model
        |
        v
    Mesh
        |
        v
    Triangles
        |
        v
    BVH Construction
        |
        v
    GPU Upload
        |
        v
    Path Tracing
        |
        +------------+
        |            |
    Brute Force     BVH
        |            |
        +-----+------+
              |
              v
        Rendered Image

The project can also produce reproducible measurements comparing the two traversal approaches.

### Status: COMPLETE

Finalization only: no new rendering, BVH, or benchmark features. The committed
dataset `Benchmark/Results/benchmark_results.csv` was not regenerated.

**Documentation**

- Added root `README.md`: project purpose, milestone progression, CPU/GPU
  split, SSBO bindings/layout, build/run/test/benchmark/analysis commands,
  hybrid-GPU NVIDIA offload, and a short pointer to
  `Analysis/M10-BVH-Analysis.md` instead of copied tables.
- Updated `specs/architecture.md` to the implemented four-target layout
  (`Core` / `App` / `Tests` / `Benchmark` + `Analysis/`), current GPU
  bindings, and data-flow diagram. Benchmark orchestration is documented as
  the `Benchmark` target, not `App`.
- Renamed `specs/AGETS.md` → `specs/AGENTS.md` (filename typo) and updated
  it from the spheres-only “two modules” description.
- Updated `specs/project.md`, `specs/gpu-integration.md`, and `specs/bvh.md`
  to the implemented state, including mermaid diagrams for overall data flow,
  CPU BVH traversal, and GPU buffer layout/traversal.
- Consistency-only edits in `Analysis/M10-BVH-Analysis.md`: status line,
  `architecture.md` → `gpu-integration.md` for “GPU Data Lifecycle”,
  `AGETS.md` → `AGENTS.md`, and the broken §9 “should be reviewed before
  M11” cross-reference. No numerical results, tables, plots, or conclusions
  were changed.

**Cleanup**

- Removed stale “GPU BVH is out of scope / will later be validated”
  comments and unused includes (`AppLayer.h` duplicate/`fwd` headers;
  unused `GLUtils.h` / `<print>` in `Renderer.cpp`).
- Replaced the `GLUtils.cpp` `TODO: Custom filters` leftover with a
  description of the existing MEDIUM/HIGH severity filter.
- Left unused original `Renderer` helpers (`LoadTexture`, framebuffer blit,
  shader reload) and the specified `UploadStorageBuffer` API in place: they
  are the existing engine surface, not M-stage leftovers.
- Left `AppLayer` / `GpuUpload` upload-struct duplication in place
  (documented M9 decision; both copies still `static_assert` the same
  layout).

**Validation**

- Clean `rm -rf build && cmake -S . -B build && cmake --build build`:
  `glad`, `Core`, `App`, `Tests`, `Benchmark` all built.
- `Tests` / `ctest`: **114 / 114** passed.
- Interactive `App` launched from `App/` with no shader/GL errors:
  default context AMD Radeon Graphics (Mesa 26.0.3); NVIDIA path
  (`__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`)
  reported `NVIDIA GeForce RTX 3050 Ti Laptop GPU`, driver
  `4.6.0 NVIDIA 595.71.05` — the same GPU as the committed CSV.
- Benchmark reproduction path verified without writing the committed
  file: `--help`; `--quick --skip-gpu --output /tmp/...`; NVIDIA
  `--quick --skip-cpu --output /tmp/...` (Bunny GPU BruteForce skipped
  by the ceiling, as documented).
- `python3 Analysis/Source/generate_analysis.py --output-dir /tmp/...`
  regenerated the six tables and seven plots from the committed CSV.

**Remaining issues worth knowing, not blocking M11**

- `App` still resolves shaders/assets from the working directory.
- Hybrid machines default to the iGPU unless PRIME offload is set.
- `GpuUpload` remains a hand-synchronized copy of `AppLayer` upload
  structs.
- No per-ray node-visit instrumentation (would be a feature change).
- CMake prints a CMP0072 OpenGL GLVND policy warning; it does not
  affect the build.

Do not start another feature milestone after M11.
