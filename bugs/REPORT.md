# Vcc / Shady SPIR-V backend: issues found compiling kd3

Environment: LLVM 23.1.5, Vcc built from `main` (`dd2787a`) plus the
`llvm23-support` fix, RADV / Radeon Vega 8, `spirv-val` from spirv-tools
1.4.363.

The reproducers below are C++ shaders compiled with:

```sh
vcc --target spirv -std=c++23 -O0 --no-dynamic-scheduling -o out.spv repro.cpp
```

Issue B was found while compiling the kd3 kd-tree query code
(`include/kd3/query.hpp`), a non-trivial but ordinary C++ compute kernel.

Three further behaviours were suspected during that work (identical-type
globals being merged, write-only globals being dropped from the push
constants, and nested plain-array structs losing their explicit layout). None
of them could be reproduced in isolation against this revision, so they are
not reported here; they were most likely misdiagnoses of the issues below.

---

## A. Descriptor globals are emitted without `Block` — **fixed in this branch**

Every `StorageBuffer`/`Uniform` global typed as a struct was emitted without
`OpDecorate ... Block`, so `spirv-val` rejects the module:

```
VUID-StandaloneSpirv-Uniform-06807:
StorageBuffer OpVariable has illegal type
```

`bugs/ssbo-block/repro.cpp` reproduces it in 12 lines.

Cause: `ShdStructFlagBlock` is only set by `spirv_lower_entrypoint_args.c`
(entry-point argument blocks) and by the SPIR-V *frontend*. Globals that the
user declares with `descriptor_set()`/`descriptor_binding()` never get the
flag.

Fix: `src/backend/spirv/emit_spv.c`, in `spv_emit_decl`, decorate the pointee
struct when the storage class is `StorageBuffer`/`Uniform`:

```c
const Type* pointee = shd_get_unqualified_type(decl->type);
if (pointee->tag == PtrType_TAG) pointee = pointee->payload.ptr_type.pointed_type;
if (pointee->tag == StructType_TAG && !(pointee->payload.struct_type.flags & ShdStructFlagBlock)) {
    spvb_decorate(emitter->file_builder, spv_emit_type(emitter, pointee), SpvDecorationBlock, 0, NULL);
    ((Node*) pointee)->payload.struct_type.flags |= ShdStructFlagBlock;  // latch: duplicates are invalid
}
```

Notes for a proper fix:

* the flag is latched on the shared type node because the same declaration can
  be emitted more than once (see issue C); mutating a hash-consed type is not
  ideal, threading the flag through type construction would be cleaner;
* `decl->type` is a *qualified* pointer type, hence `shd_get_unqualified_type`;
* array-typed descriptor globals (e.g. `float g[]`) still need a wrapping
  struct: Vulkan requires the pointee to be a `Block` **struct**;
* after this fix `spirv-val --target-env vulkan1.2` accepts the reproducer.

---

## B. `-O1`/`-O2` miscompiles the kernel; `-O0` is correct

Same source, same data: `-O0` returns exact results, `-O1`/`-O2` return wrong
distances (and `NotFound` for most queries). It is reproducible with a 32-point
tree and 8 queries.

Bisected with `-mllvm -opt-bisect-limit=N`:

| limit | result |
|---|---|
| <= 122 | correct |
| >= 123 | wrong |

With `limit=122`, the first pass *not* run is printed as
`BISECT: NOT running pass (123) instcombine on main`, so the trigger is
**InstCombine on the entry function**.

What that pass changes in `main` (LLVM IR diff at the boundary):

* `icmp uge i32 %qi, 8` -> `icmp ugt i32 %qi, 7` (equivalent);
* the `g_results` dummy load/store pair is eliminated (`x = x`);
* `nonnull` attributes are added to call arguments and the local view.

The transformed comparisons lower correctly in isolation
(`uge` -> `OpUGreaterThanEqual`, `ugt` -> `OpUGreaterThan`), so the failure is
in how the *combination* is lowered, not in the comparison itself.

Tried and does not help: `-fno-vectorize`, `-fno-slp-vectorize`,
`-fno-strict-aliasing`, `-fno-inline`, `-fno-unroll-loops`, calling the
`always_inline` variant of the query.

Two observations that matter for prioritisation:

* the miscompiled `-O1` build is also **~10x slower** (0.28 vs 3.1 Mq/s), so it
  is not a silent fast-and-wrong case;
* stopping the pipeline before the bad pass (correct) is *slower* than plain
  `-O0` (1.9 vs 3.1 Mq/s), so simply fixing this pass may not recover the
  performance gap. The kernel that reaches ~33 Mq/s is the equivalent GLSL
  shader compiled by glslc, which uses descriptor SSBOs rather than physical
  pointers.

---

## C. Push-constant member order is compiler-chosen and unstable

For a shader that reads six device pointers declared as globals, the
`__main_args` push-constant struct member order is **not** the declaration
order, and it changes when the body changes (e.g. adding an early `return`
reorders it). The host must therefore parse the module to bind the buffers
correctly; hard-coding the order silently corrupts results.

Suggested: emit globals in a deterministic order (declaration / first-use),
or document the order explicitly.

---
