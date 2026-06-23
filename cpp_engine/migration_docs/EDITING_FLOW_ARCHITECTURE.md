# DFEE Native Editing Flow Architecture

This document defines the architectural principles that should govern DFEE's
live editing flow as the engine moves from Python to native C++ and later to
optional CUDA acceleration.

The goal is not only to make the engine fast in benchmarks. The goal is to make
the editing experience responsive, predictable, memory-safe, and visually
trustworthy while preserving a clear path to full-resolution export quality.

## Why This Matters

Photo editors do not feel fast merely because the language is C++ or because a
GPU is present. They feel fast because the engine is designed around bounded
work, bounded memory, partial recomputation, and clear separation between
interactive preview quality and final export quality.

For DFEE, this matters especially because:

- RAW files are large and expand dramatically after decode.
- the film-emulation pipeline contains several expensive full-image stages
- exports can require multiple full-resolution intermediate buffers
- users expect immediate preview feedback while dragging controls
- final export must remain stable and faithful even when preview shortcuts exist

## Core Principles

### 1. Bound work during interaction

A slider drag must not blindly trigger a full pipeline recomputation of every
stage at full precision and full resolution.

The engine should:

- recompute only the nodes invalidated by the changed parameter
- operate on the smallest image representation that preserves believable
  preview quality for the current view
- cancel stale work when newer user input arrives

This is the foundation of a responsive editor.

### 2. Bound memory explicitly

The engine must treat memory as a scheduled resource, not as an incidental side
effect of the current implementation.

That means:

- explicit cache budgets
- explicit export memory budgets
- early release of large temporaries
- no reliance on the operating system to rescue unsafe working-set growth

The recent native export memory-pressure guard is one immediate safety measure,
but the long-term architecture must reduce peak allocation rather than merely
detecting danger late.

### 3. Separate preview behavior from export behavior

Interactive preview and final export are related but distinct workloads.

Preview favors:

- low latency
- partial recomputation
- reduced working resolution where acceptable
- cancelability

Export favors:

- full precision
- deterministic output
- complete pipeline execution
- high memory discipline under large-image workloads

DFEE should not force preview and export to share one execution strategy merely
because they share the same visual pipeline.

### 4. Preserve visual trust

A fast preview that does not represent the final image closely enough is not a
good editing experience.

Preview shortcuts are acceptable only when:

- they are stable
- they do not introduce obvious false color or tonal drift
- they are progressively refined when necessary
- export still uses the authoritative full-quality path

## Architectural Pillars

### A. Tiled full-resolution processing

This is the highest-priority large-image architecture change still ahead of us.

The full export path should be redesigned so that memory scales with tile height
instead of full-frame intermediates wherever possible.

DFEE implications:

- full-resolution render should operate on tiles for stages that can be tiled
- blur-based stages must include halo/overlap regions so seams do not appear
- stages that are not naturally tileable must be isolated and minimized
- tile size should be chosen from explicit CPU RAM and future GPU VRAM budgets

Tiling will usually be slower than processing one full frame in memory, but it
is the correct fallback for production stability on limited-memory machines.

### B. Region-of-interest preview

When the user is zoomed into a part of the image, preview rendering should be
able to target that region first instead of always recomputing the whole frame.

DFEE implications:

- the viewport should request a preview ROI when zoomed
- ROI renders should reuse global analysis where valid
- local stages such as grain, local contrast, and halation should support ROI
  evaluation with edge padding where needed
- whole-image re-render can still happen lazily behind the focused ROI update

This is one of the strongest levers for improving perceived responsiveness.

### C. Resolution ladders

The engine should maintain distinct working resolutions for different moments in
the editing flow.

Recommended ladder:

- thumbnail / browser preview
- fit-to-screen interactive preview
- zoomed ROI preview
- full-resolution export

DFEE implications:

- expensive stages such as bloom, halation, grain, and local contrast should be
  designed to operate sensibly on preview resolutions
- preview caches should be keyed by both selected file and working resolution
- export analysis may remain bounded to a smaller working view when the derived
  masks can be safely reconstructed for full resolution

This allows the editor to stay responsive without lying about final output.

### D. Graph-based invalidation

The native engine should evolve toward an explicit dependency graph instead of a
single monolithic "render everything again" mentality.

Example invalidation behavior:

- exposure or white balance changes should not invalidate export DPI metadata
- grain-size changes should invalidate grain but not RAW decode
- JPEG quality changes should invalidate write-out but not the render pipeline
- print-stock toggles should invalidate only the dependent downstream stages

DFEE implications:

- represent render stages and derived artifacts as explicit nodes
- cache node outputs with versioned keys
- invalidate only dependent descendants when parameters change

This is the key to scaling from a functional renderer to an efficient editor.

### E. Cancelable asynchronous preview jobs

The UI must never wait for an old render request that has already become
obsolete.

DFEE implications:

- preview requests should carry job IDs or revision counters
- the backend should drop stale work when a newer request supersedes it
- the frontend should display only the newest completed render for the current
  edit state
- rapid slider drags should be coalesced rather than flooding the engine with
  work that the user no longer needs

This matters even before a future native desktop UI arrives. The current
React/FastAPI stack still benefits from the same discipline.

### F. Cache discipline and memory budgets

Caches are necessary, but undisciplined caches simply move instability around.

DFEE should keep only caches with strong reuse value, such as:

- decoded RAW state
- preview-scale RGB
- preview analysis outputs
- export analysis outputs
- reusable blur pyramids
- deterministic grain fields

DFEE should avoid:

- retaining multiple full-resolution intermediates longer than needed
- treating preview and export caches as unbounded
- keeping caches alive after a file switch when they are no longer reusable

Each major cache should have:

- an ownership boundary
- an invalidation rule
- a size estimate
- a budget or eviction strategy

### G. Effect-specific efficient algorithms

Some stages are too expensive to remain naive full-frame implementations.

DFEE should continue following this rule:

- bloom and halation should use multiscale or separable strategies, not giant
  literal full-resolution kernels
- grain should use deterministic procedural or cached field generation
- local contrast should avoid repeated redundant blur/mask passes
- encoder/write-out should not require unnecessary whole-image duplication

This is where much of the practical "editor feels instant" gain comes from.

### H. CPU/GPU transfer discipline

When CUDA is added, the engine should not oscillate data between host and device
unnecessarily.

DFEE CUDA design should prefer:

- keeping intermediate stage data on the device as long as practical
- batching transfers rather than issuing many small copies
- pinned host memory only where measurement proves it helps
- overlapping transfer and compute only when the staging model truly benefits
- tiled scheduling that respects VRAM limits

CUDA can accelerate a bad architecture, but it cannot rescue one that is still
structured around wasteful full-frame copies and host-device ping-pong.

### I. Quality ladders for interaction

Some modules should expose different operational modes for:

- slider drag
- short idle after drag
- final export

Example policy:

- during drag: cheaper approximation
- after drag settles: refine preview
- on export: authoritative quality

This should be used carefully and only where visual trust remains high.

### J. Instrumentation as architecture

Performance work should rely on measured stages rather than wall-clock guesses.

DFEE should keep measuring:

- total preview latency
- total export latency
- decode time
- analysis time
- per-stage render timings
- cache hit/miss behavior
- memory estimates and rejection counts
- later, CUDA transfer and kernel timings

If a stage cannot be measured, it cannot be managed confidently.

## Practical DFEE Direction

The next editing-flow architecture priorities should be:

1. tiled full-resolution export design
2. row-streamed PNG/TIFF writing for lower encoder memory pressure
3. ROI-aware preview rendering
4. explicit render-graph invalidation model
5. cancelable preview job scheduling
6. cache budgets and eviction policy
7. CUDA scheduling only after the CPU memory and invalidation model are sound

## Non-Goals

This document does not require:

- changing the current film look
- changing the public FastAPI request shape immediately
- forcing every stage to be tileable before progress can continue
- introducing speculative approximations without parity or UX justification

## Relationship To Other Documents

- [PLAN.md](d:/Codebases/DFEE/cpp_engine/migration_docs/PLAN.md):
  overall migration roadmap
- [PERFORMANCE_METHOD.md](d:/Codebases/DFEE/cpp_engine/migration_docs/PERFORMANCE_METHOD.md):
  measurement and optimization discipline
- [TASKLIST.md](d:/Codebases/DFEE/cpp_engine/migration_docs/TASKLIST.md):
  concrete tracked implementation work
- [BUG_TRACKER.md](d:/Codebases/DFEE/cpp_engine/migration_docs/BUG_TRACKER.md):
  current risks and defects, including large-image export memory scaling

## Sources That Informed These Principles

These are reference models, not copy targets:

- darktable memory and tiling guidance
- OpenImageIO tile-cache design
- GEGL data-flow/non-destructive processing model
- NVIDIA CUDA best-practices guidance on transfer minimization and pinned memory

The DFEE implementation should borrow the architectural lessons, but keep its
own renderer behavior and code structure aligned with this repo's needs.
