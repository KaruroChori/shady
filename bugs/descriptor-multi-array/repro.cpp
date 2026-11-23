// Accessing a second array member of a descriptor (SSBO) struct fails with
//   "Address space ShaderStorageBufferObject is not physical in this arena"
// even though indexing a single array member is fine.
//
//   vcc --target spirv -std=c++23 -O0 --no-dynamic-scheduling -o repro.spv repro.cpp
//
// Reading only `coords` compiles; adding the `ids` read fails.
#define compute_shader __attribute__((annotate("shady::entry_point::Compute")))
#define local_size(x, y, z) __attribute__((annotate("shady::workgroup_size::" #x "::" #y "::" #z)))
#define descriptor_set(i) __attribute__((annotate("shady::descriptor_set::" #i)))
#define descriptor_binding(i) __attribute__((annotate("shady::descriptor_binding::" #i)))

typedef unsigned uvec3 __attribute__((ext_vector_type(3)));
__attribute__((annotate("shady::builtin::GlobalInvocationId")))
__attribute__((annotate("shady::io::389")))
uvec3 gl_GlobalInvocationID;

struct Bucket { float coords[3][16]; unsigned ids[16]; };
struct Buckets { Bucket v[4]; };

descriptor_set(0) descriptor_binding(0)
__attribute__((address_space(394))) Buckets g = {};

descriptor_set(0) descriptor_binding(1)
__attribute__((address_space(394))) float out[64] = {};

compute_shader local_size(64, 1, 1)
extern "C" void main() {
    unsigned t = gl_GlobalInvocationID.x;
    unsigned i = t % 4u;
    // Remove the second term to make it compile.
    out[t % 64u] = g.v[i].coords[t % 3u][t % 16u] + float(g.v[i].ids[t % 16u]);
}
