// Descriptor (SSBO) globals are emitted without OpDecorate ... Block.
//
//   vcc --target spirv -std=c++23 -O0 --no-dynamic-scheduling -o repro.spv repro.cpp
//   spirv-val --target-env vulkan1.2 repro.spv
//
// Before the fix: VUID-StandaloneSpirv-Uniform-06807 "missing Block decoration".
// After:          validates.
//
// Note: the globals must be *definitions*; clang does not emit
// llvm.global.annotations for `extern` declarations, so the descriptor
// annotations would be silently lost.
#define compute_shader __attribute__((annotate("shady::entry_point::Compute")))
#define local_size(x, y, z) __attribute__((annotate("shady::workgroup_size::" #x "::" #y "::" #z)))
#define descriptor_set(i) __attribute__((annotate("shady::descriptor_set::" #i)))
#define descriptor_binding(i) __attribute__((annotate("shady::descriptor_binding::" #i)))

struct Buf { float v[64]; };

descriptor_set(0) descriptor_binding(0)
__attribute__((address_space(394))) Buf g_in = {};

descriptor_set(0) descriptor_binding(1)
__attribute__((address_space(394))) Buf g_out = {};

compute_shader local_size(64, 1, 1)
extern "C" void main() {
    unsigned i = 0;
    g_out.v[i] = g_in.v[i] * 2.0f;
}
