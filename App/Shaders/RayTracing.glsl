#version 450 core

#define POS_INF 1e30
#define PI 3.14159265358979323846

// Möller-Trumbore determinant threshold, matching
// Core::TriangleIntersectionEpsilon (Core/Source/Core/Renderer/Triangle.h).
// One test rejects both rays parallel to the triangle plane and degenerate
// (zero-area) triangles, since both drive the determinant to zero.
#define TRIANGLE_EPSILON 1e-8

layout (local_size_x = 8, local_size_y = 8) in;

layout (rgba32f, binding = 0) uniform image2D u_AccumulationImage;

struct Sphere {
    vec3 center;
    float radius;
};

// GPU triangle representation (specs/gpu-integration.md "GPU Geometry").
// Positions only: enough to perform intersection (Möller-Trumbore) and derive
// the geometric surface normal (cross(v1 - v0, v2 - v0)), as required by
// specs/geometry.md. No normals/UVs/material indices are uploaded because
// nothing currently reads them.
//
// GLSL layout (std430, matches the CPU upload struct AppGPUTriangle in
// App/Source/AppLayer.cpp exactly -- see that file for the full CPU-side
// layout derivation):
//   vec3 v0;  // offset  0, base alignment 16
//   vec3 v1;  // offset 16, base alignment 16
//   vec3 v2;  // offset 32, base alignment 16
//   // struct size 48, array stride 48
struct Triangle {
    vec3 v0;
    vec3 v1;
    vec3 v2;
};

// GPU BVH node (specs/bvh.md, specs/gpu-integration.md "GPU BVH", M8). Mirrors
// the serialization of Core::BVHNode (Core/Source/Core/Renderer/BVH.h)
// performed by AppGPUBVHNode in App/Source/AppLayer.cpp -- see that file for
// the full CPU-side layout derivation.
//
// GLSL layout (std430, matches AppGPUBVHNode exactly):
//   vec3 boundsMin;      // offset  0, base alignment 16
//   vec3 boundsMax;      // offset 16, base alignment 16
//   uint firstPrimitive; // offset 28, base alignment 4 (scalar immediately
//   uint primitiveCount; // offset 32,  after a vec3 only needs 4-byte
//   uint leftChild;      // offset 36,  alignment, not 16 -- unlike a vec3
//   uint rightChild;     // offset 40,  immediately after another vec3)
//   // struct size 48 (rounded up to the vec3 base alignment of 16),
//   // array stride 48.
//
// primitiveCount > 0 means this node is a leaf: [firstPrimitive,
// firstPrimitive + primitiveCount) indexes bvhPrimitiveTriangleIds[], whose
// values in turn index the existing 'triangles[]' SSBO (binding 1) -- no
// triangle geometry is duplicated into the BVH buffers.
// primitiveCount == 0 means this node is internal: leftChild/rightChild are
// indices into bvhNodes[] itself. Identical convention to
// Core::BVHNode::IsLeaf().
struct BVHNode {
    vec3 boundsMin;
    vec3 boundsMax;
    uint firstPrimitive;
    uint primitiveCount;
    uint leftChild;
    uint rightChild;
};

struct Ray {
    vec3 origin;
    vec3 dir;
};

struct HitRecord {
    vec3 p;
    vec3 normal;
    float t;
    bool frontFace;
};

layout (std140, binding = 0) buffer SphereBuffer {
    Sphere spheres[];
};

// New SSBO per specs/gpu-integration.md ("New SSBOs prefer std430" / "new
// SSBOs start at binding 1"; binding 0 and the sphere buffer's std140 layout
// are unchanged). std430 does not round the vec3-array stride up beyond its
// natural 16-byte alignment the way std140 would for other element shapes,
// so the array stride here is exactly sizeof(AppGPUTriangle) = 48 bytes.
layout (std430, binding = 1) buffer TriangleBuffer {
    Triangle triangles[];
};

// M8 GPU BVH buffers (specs/gpu-integration.md "New SSBOs start at binding
// 1" -> the triangle buffer already claimed binding 1, so the BVH buffers
// continue at 2/3). Both std430, per specs/gpu-integration.md's "New SSBOs
// prefer std430".
layout (std430, binding = 2) buffer BVHNodeBuffer {
    BVHNode bvhNodes[];
};

// The flattened BVH primitive-reference array, reduced to just the
// TriangleId each slot refers to (specs/bvh.md "Use the existing flattened
// primitive-reference array and triangle IDs"). A plain 'uint' array: std430
// keeps its element stride at 4 bytes, matching the CPU uint32_t array
// exactly (specs/gpu-integration.md "Layout qualifier" -- std140 would round
// this up to 16 bytes per element and silently corrupt the data).
layout (std430, binding = 3) buffer BVHPrimitiveBuffer {
    uint bvhPrimitiveTriangleIds[];
};

uniform int u_SphereCount;
uniform int u_TriangleCount;
uniform int u_BVHNodeCount;

// Traversal mode (specs/gpu-integration.md "Traversal Mode"): 0 = BruteForce
// (M7 baseline, scans every triangle), 1 = BVH (M8). Selected by AppLayer at
// runtime (see AppLayer::OnKeyPressed) without recompiling the shader.
#define TRAVERSAL_MODE_BRUTE_FORCE 0
#define TRAVERSAL_MODE_BVH 1
uniform int u_TraversalMode;

uniform vec3 u_CameraPos;
uniform vec3 u_CameraForward;
uniform vec3 u_CameraUp;
uniform float u_CameraFocal;
uniform int u_SamplesPerPixel;
uniform uint u_FrameIndex;

// --- BETTER RANDOM NUMBER GENERATION (PCG Hash) ---
// This prevents the diagonal banding patterns
uint seed;
uint pcg_hash() {
    seed = seed * 747796405u + 2891336453u;
    uint word = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand() {
    return float(pcg_hash()) / float(0xFFFFFFFFu);
}

vec3 randomUnitVector() {
    float z = rand() * 2.0 - 1.0;
    float a = rand() * 2.0 * PI;
    float r = sqrt(1.0 - z * z);
    return vec3(r * cos(a), r * sin(a), z);
}

// --- RAY TRACING LOGIC ---

bool hitSphere(vec3 center, float radius, float tMin, float tMax, Ray r, out HitRecord rec) {
    vec3 oc = center - r.origin;
    float a = dot(r.dir, r.dir);
    float h = dot(r.dir, oc);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = h * h - a * c;

    if (discriminant < 0.0) return false;

    float sqrtd = sqrt(discriminant);
    float root = (h - sqrtd) / a;
    if (root <= tMin || root >= tMax) {
        root = (h + sqrtd) / a;
        if (root <= tMin || root >= tMax) return false;
    }

    rec.t = root;
    rec.p = r.origin + root * r.dir;
    vec3 outwardNormal = (rec.p - center) / radius;
    rec.frontFace = dot(r.dir, outwardNormal) < 0.0;
    rec.normal = rec.frontFace ? outwardNormal : -outwardNormal;
    return true;
}

// Ray-triangle intersection (Möller-Trumbore), double sided. This is the GPU
// counterpart of Core::Triangle::Intersect
// (Core/Source/Core/Renderer/Triangle.h) and follows the exact same
// tMin/tMax rejection (t <= tMin || t >= tMax) and front-face convention as
// hitSphere() above, so hitWorld() below can treat spheres and triangles
// identically (specs/geometry.md "Intersection Contract").
bool hitTriangle(vec3 v0, vec3 v1, vec3 v2, float tMin, float tMax, Ray r, out HitRecord rec) {
    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;

    vec3 pVec = cross(r.dir, edge2);
    float det = dot(edge1, pVec);

    if (abs(det) < TRIANGLE_EPSILON) return false;

    float invDet = 1.0 / det;

    vec3 tVec = r.origin - v0;
    float u = dot(tVec, pVec) * invDet;
    if (u < 0.0 || u > 1.0) return false;

    vec3 qVec = cross(tVec, edge1);
    float v = dot(r.dir, qVec) * invDet;
    if (v < 0.0 || u + v > 1.0) return false;

    float t = dot(edge2, qVec) * invDet;
    if (t <= tMin || t >= tMax) return false;

    vec3 outwardNormal = normalize(cross(edge1, edge2));

    rec.t = t;
    rec.p = r.origin + t * r.dir;
    rec.frontFace = dot(r.dir, outwardNormal) < 0.0;
    rec.normal = rec.frontFace ? outwardNormal : -outwardNormal;
    return true;
}

// Brute-force triangle traversal (specs/gpu-integration.md "Brute-Force GPU
// Traversal"): scan every triangle, passing the running closest distance as
// tMax so only strictly nearer hits are accepted. This is the GPU
// counterpart of Core::IntersectBruteForce
// (Core/Source/Core/Renderer/TriangleTraversal.cpp) and, like it, is the
// GPU correctness reference that hitTrianglesBVH() is compared against.
void hitTriangles(Ray r, float tMin, inout float closest, inout bool hitAnything, inout HitRecord rec) {
    HitRecord temp;
    for (int i = 0; i < u_TriangleCount; i++) {
        if (hitTriangle(triangles[i].v0, triangles[i].v1, triangles[i].v2, tMin, closest, r, temp)) {
            hitAnything = true;
            closest = temp.t;
            rec = temp;
        }
    }
}

// Ray-AABB slab test, mirroring Core::AABB::Intersects
// (Core/Source/Core/Renderer/AABB.h) member-for-member (same per-axis
// ternary narrowing, not a vectorized min/max) so the two share identical
// behaviour on the degenerate axis-aligned-ray case: a zero direction
// component with the origin exactly on that slab plane produces a NaN via
// 0 * inf, and NaN fails both '>'/'<' comparisons below, leaving the
// interval unchanged for that axis -- the same "treat as inside" behaviour
// documented on the CPU side.
bool hitAABB(vec3 boundsMin, vec3 boundsMax, Ray r, float tMinIn, float tMaxIn, out float tEnter) {
    float lo = tMinIn;
    float hi = tMaxIn;

    for (int axis = 0; axis < 3; axis++) {
        float invDir = 1.0 / r.dir[axis];

        float t0 = (boundsMin[axis] - r.origin[axis]) * invDir;
        float t1 = (boundsMax[axis] - r.origin[axis]) * invDir;

        if (invDir < 0.0) {
            float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }

        lo = t0 > lo ? t0 : lo;
        hi = t1 < hi ? t1 : hi;

        // Strict '<' keeps zero-thickness boxes intersectable, matching
        // AABB::Intersects.
        if (hi < lo) return false;
    }

    tEnter = lo;
    return true;
}

// Iterative GPU BVH traversal (specs/gpu-integration.md "GPU BVH Traversal",
// specs/bvh.md "CPU Traversal"), mirroring Core::IntersectBVH
// (Core/Source/Core/Renderer/TriangleTraversal.cpp) exactly:
//
//   push root
//   while stack not empty:
//       pop node
//       if node AABB misses ray (tested against the current closest hit):
//           continue
//       if leaf: test referenced triangles, update closest hit
//       else: test both children, push the farther child first so the
//             nearer child (by AABB entry distance) is popped and visited
//             first
//
// No recursion: GLSL has no call stack usable for this, so the traversal
// stack is an explicit fixed-size local array of node indices.
//
// Stack capacity: BVH_STACK_SIZE below. Core::BVH always splits a range by
// primitive *count* (std::nth_element at the median, specs/bvh.md "Split at
// Median"), never by a spatial threshold that could fail to shrink the
// range, so every internal node's two children each receive at most
// ceil(count/2) primitives. That halving bounds the tree depth by
// ceil(log2(triangleCount)) + a small constant for the leaf-size floor,
// regardless of scene geometry. An iterative DFS that always keeps
// descending into one child while leaving at most one sibling per level on
// the stack (the shape used below) therefore needs stack capacity equal to
// only the tree's depth, not 2x it. 64 covers a perfectly balanced BVH over
// more than 2^60 triangles -- far beyond any scene this project can build,
// upload, or render -- with headroom to spare, matching the reasoning
// already documented for the CPU traversal stack in TriangleTraversal.cpp.
//
// Stack overflow safety: if a push would exceed BVH_STACK_SIZE, that child
// is simply not pushed (dropped) rather than overwriting another slot. This
// cannot happen for any BVH this project actually builds (see above), but
// if it ever did, the result would be an incomplete (conservative) traversal
// -- never an out-of-bounds array write, undefined behaviour, or GPU memory
// corruption.
#define BVH_STACK_SIZE 64

void hitTrianglesBVH(Ray r, float tMin, inout float closest, inout bool hitAnything, inout HitRecord rec) {
    if (u_BVHNodeCount == 0) return;

    int stack[BVH_STACK_SIZE];
    int stackSize = 0;
    stack[stackSize++] = 0; // root index, matching Core::BVH::RootIndex

    HitRecord temp;

    while (stackSize > 0) {
        int nodeIndex = stack[--stackSize];
        BVHNode node = bvhNodes[nodeIndex];

        // Re-test this node's AABB against the *current* closest distance
        // even if it was already tested before being pushed (internal-node
        // branch below): a sibling subtree visited since then may have
        // shrunk 'closest' and made this node's box no longer reachable.
        // The root's first-ever test happens here too, so "test the root"
        // and "traverse intersected child nodes" share one code path.
        float tEnter;
        if (!hitAABB(node.boundsMin, node.boundsMax, r, tMin, closest, tEnter))
            continue;

        if (node.primitiveCount > 0) {
            // Leaf: test referenced triangles via the existing GPU
            // Möller-Trumbore implementation, exactly like hitTriangles()'s
            // brute-force scan but restricted to this leaf's primitive
            // range. bvhPrimitiveTriangleIds[] maps a primitive slot to a
            // TriangleId into the existing triangle buffer -- no triangle
            // geometry is duplicated here.
            uint end = node.firstPrimitive + node.primitiveCount;
            for (uint i = node.firstPrimitive; i < end; i++) {
                uint triangleId = bvhPrimitiveTriangleIds[i];
                Triangle tri = triangles[triangleId];
                if (hitTriangle(tri.v0, tri.v1, tri.v2, tMin, closest, r, temp)) {
                    hitAnything = true;
                    closest = temp.t;
                    rec = temp;
                }
            }
            continue;
        }

        // Internal node: test both children's AABBs now (rather than
        // deferring entirely to the pop-time test above) so a child that
        // clearly misses is never pushed, and so each child's entry
        // distance is available to order the push.
        BVHNode left = bvhNodes[node.leftChild];
        BVHNode right = bvhNodes[node.rightChild];

        float tEnterLeft;
        float tEnterRight;
        bool hitLeft = hitAABB(left.boundsMin, left.boundsMax, r, tMin, closest, tEnterLeft);
        bool hitRight = hitAABB(right.boundsMin, right.boundsMax, r, tMin, closest, tEnterRight);

        // Push the farther child first so the nearer child (by AABB entry
        // distance) is popped and visited first -- the stack is LIFO. This
        // is a pure ordering choice, not required for correctness (the
        // pop-time re-test above protects every node regardless of push
        // order), but lets a genuinely near hit shrink 'closest' before the
        // farther subtree is even looked at, pruning more of it. A tied
        // entry distance prefers the left child as "near", matching
        // Core::IntersectBVH's tie-break.
        if (hitLeft && hitRight) {
            if (tEnterLeft <= tEnterRight) {
                if (stackSize < BVH_STACK_SIZE) stack[stackSize++] = int(node.rightChild);
                if (stackSize < BVH_STACK_SIZE) stack[stackSize++] = int(node.leftChild);
            } else {
                if (stackSize < BVH_STACK_SIZE) stack[stackSize++] = int(node.leftChild);
                if (stackSize < BVH_STACK_SIZE) stack[stackSize++] = int(node.rightChild);
            }
        } else if (hitLeft) {
            if (stackSize < BVH_STACK_SIZE) stack[stackSize++] = int(node.leftChild);
        } else if (hitRight) {
            if (stackSize < BVH_STACK_SIZE) stack[stackSize++] = int(node.rightChild);
        }
    }
}

// Scene-intersection entry point (specs/gpu-integration.md "Existing Shader
// Interface"). trace() below does not need to know whether the closest hit
// came from a sphere or a triangle, nor which triangle traversal mode
// produced it: both write the same HitRecord shape.
bool hitWorld(Ray r, float tMin, float tMax, out HitRecord rec) {
    HitRecord temp;
    bool hitAnything = false;
    float closest = tMax;

    for (int i = 0; i < u_SphereCount; i++) {
        if (hitSphere(spheres[i].center, spheres[i].radius, tMin, closest, r, temp)) {
            hitAnything = true;
            closest = temp.t;
            rec = temp;
        }
    }

    // M8: traversal-mode dispatch (specs/gpu-integration.md "Traversal
    // Mode"). Both paths share the identical closest-hit contract: they
    // read/shrink the same 'closest' and only overwrite 'rec' on a strict
    // improvement, so switching modes cannot change how triangles interact
    // with the sphere loop above.
    if (u_TraversalMode == TRAVERSAL_MODE_BVH) {
        hitTrianglesBVH(r, tMin, closest, hitAnything, rec);
    } else {
        hitTriangles(r, tMin, closest, hitAnything, rec);
    }

    return hitAnything;
}

vec3 trace(Ray r) {
    vec3 attenuation = vec3(1.0);
    for (int i = 0; i < 50; i++) { // Max 50 bounces for performance
        HitRecord rec;
        if (hitWorld(r, 0.001, POS_INF, rec)) {
            // Standard Lambertian Diffusion: normal + random unit vector
            vec3 scatterDir = rec.normal + randomUnitVector();

            // Catch degenerate scatter direction
            if (length(scatterDir) < 0.0001) scatterDir = rec.normal;

            r = Ray(rec.p + rec.normal * 0.0001, normalize(scatterDir));
            attenuation *= 0.5; // Surface color
        } else {
            // Sky
            vec3 unitDir = normalize(r.dir);
            float t = 0.5 * (unitDir.y + 1.0);
            vec3 sky = (1.0 - t) * vec3(1.0) + t * vec3(0.5, 0.7, 1.0);
            return attenuation * sky;
        }
    }
    return vec3(0.0);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSizePx = imageSize(u_AccumulationImage);

    if (pixel.x >= imageSizePx.x || pixel.y >= imageSizePx.y) return;

    vec2 resolution = vec2(imageSizePx);

    // Initialize RNG seed based on pixel position and frame index so every
    // frame draws a fresh set of samples that refine the accumulated image
    seed = uint(pixel.x) * 1973u + uint(pixel.y) * 9277u + u_FrameIndex * 26699u + 1u;

    vec3 color = vec3(0.0);

    vec3 right = normalize(cross(u_CameraForward, u_CameraUp));
    vec3 trueUp = cross(right, u_CameraForward);

    for (int s = 0; s < u_SamplesPerPixel; s++) {
        // AA Jitter
        float px = (float(pixel.x) + rand()) / resolution.x;
        float py = (float(pixel.y) + rand()) / resolution.y;

        vec2 ndc = vec2(px, py) * 2.0 - 1.0;
        ndc.x *= resolution.x / resolution.y;

        vec3 dir = normalize(ndc.x * right + ndc.y * trueUp + u_CameraFocal * u_CameraForward);
        Ray r = Ray(u_CameraPos, dir);

        color += trace(r);
    }

    color /= float(u_SamplesPerPixel);

    // Accumulate the running mean in linear space; gamma is applied at present
    vec3 accumulated = color;
    if (u_FrameIndex > 0u) {
        vec3 previous = imageLoad(u_AccumulationImage, pixel).rgb;
        accumulated = mix(previous, color, 1.0 / float(u_FrameIndex + 1u));
    }

    imageStore(u_AccumulationImage, pixel, vec4(accumulated, 1.0));
}
