# Benchmarking Specification

## Objective

Measure the effect of BVH acceleration on triangle intersection performance.

The primary comparison is:

    Brute-force triangle intersection
            vs
    BVH triangle traversal

## Benchmark Principle

The benchmark must change only the traversal strategy.

All other relevant rendering parameters must remain identical.

## Controlled Parameters

Each benchmark must specify:

- Scene
- Triangle count
- Resolution
- Samples per pixel
- Maximum path depth
- Camera position
- Camera orientation
- Rendering configuration
- GPU
- Driver/version where available
- BVH configuration

## Scene Complexity

Benchmark multiple triangle counts.

Initial target:

    ~100 triangles
    ~1,000 triangles
    ~10,000 triangles
    ~100,000 triangles
    Larger real-world model

Exact triangle counts may depend on available model assets.

The benchmark should include both:

- Synthetic/simple scenes
- Real-world imported models

## Benchmark Modes

Each scene should be benchmarked with:

    BruteForce

and:

    BVH

The same scene data must be used for both modes.

## Measurements

The benchmark must separate four timing categories and never merge them into a
single number:

1. Model loading time
2. BVH construction time
3. GPU upload time
4. GPU rendering time

The primary brute-force vs BVH comparison is **GPU rendering time**, measured
under identical rendering conditions. Model loading, BVH construction, and
upload are reported separately as overhead; they are not part of the traversal
comparison.

### BVH Construction Time

Measure CPU time required to construct the BVH.

This should be reported separately from rendering time.

### GPU Upload Time

Measure the time required to upload:

- Triangle data
- BVH nodes
- Primitive/index data

where practical.

### Rendering Time

Measure the actual rendering workload.

The primary rendering metric should be GPU execution time where reliable GPU timing is available.

Do not rely exclusively on CPU wall-clock time around asynchronous OpenGL calls.

## Total Time

If useful, also calculate:

    total_time =
        model_load_time
        + bvh_build_time
        + gpu_upload_time
        + render_time

However, total application startup cost must remain separate from steady-state rendering cost.

## Warm-Up

Before recording measurements:

1. Initialize the renderer.
2. Upload scene data.
3. Perform warm-up rendering.
4. Discard warm-up measurements.
5. Begin benchmark measurements.

This reduces the influence of initialization and shader-compilation effects.

## Repetitions

Each configuration should be measured multiple times.

Record each individual measurement.

Calculate at minimum:

- Mean
- Minimum
- Maximum

Standard deviation should be calculated where useful.

## GPU Timing

Use OpenGL GPU timing facilities where supported by the existing architecture.

The benchmark must account for the asynchronous nature of GPU execution.

Timing methodology should be documented.

## Reproducibility Fields

Every benchmark result must carry enough information to be reproduced. At
minimum:

- GPU / OpenGL renderer string (`GL_RENDERER`, `GL_VENDOR`, `GL_VERSION`)
- Scene / model identifier
- Triangle count
- Resolution
- Samples per pixel
- Bounce limit (maximum path depth)
- Traversal mode
- Timing methodology (what was measured and how)
- Number of benchmark samples / runs
- Number of discarded warm-up runs
- BVH configuration (leaf size)

Rendering parameters that are currently hardcoded in the shader, such as the
bounce limit and samples per pixel, must be made explicit and recorded rather
than assumed.

## Benchmark Output

Benchmark results should be exportable as CSV.

Example:

    scene,triangles,resolution,spp,mode,bvh_build_ms,gpu_upload_ms,render_ms

Additional useful fields may include:

    frame_index
    sample_count
    node_count
    leaf_count
    max_depth
    average_leaf_size

## BVH Statistics

Record:

- Triangle count
- Node count
- Leaf count
- Maximum depth
- Average leaf size
- Maximum leaf size
- Minimum leaf size

This allows the performance results to be connected to the structure of the generated BVH.

## Correctness Requirement

A performance measurement is valid only if:

    brute-force result == BVH result

for the tested scene.

Correctness must be verified independently from performance.

A faster result is not evidence that the BVH is correct.

## Analysis

The benchmark should allow analysis of:

1. How brute-force work scales with triangle count.
2. How BVH traversal changes the amount of geometry tested.
3. BVH construction overhead.
4. GPU upload overhead.
5. The effect of BVH size/depth.
6. The effect of different leaf sizes.
7. Differences between synthetic and real-world geometry.
8. The point at which BVH construction overhead becomes insignificant relative to rendering cost.

## Reporting

The final project report should distinguish:

- Measured results
- Theoretical complexity
- Implementation details
- Hardware-dependent observations
- Limitations

Do not assume a specific performance improvement before running the benchmark.

The benchmark should report observed behavior.
