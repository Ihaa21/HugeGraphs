
struct radix_tree_particle
{
    vec2 Pos;
    float Degree;
    float Size;
};

struct bounds
{
    vec2 Min;
    vec2 Max;
};

#define RADIX_DESCRIPTOR_LAYOUT(set_id)                                 \
                                                                        \
    layout(set = set_id, binding = 0) uniform radix_tree_uniforms       \
    {                                                                   \
        uint NumNodes;                                                  \
    } RadixTreeUniforms;                                                \
                                                                        \
                                                                        \
                                                                        \
    layout(set = set_id, binding = 1) buffer morton_keys                \
    {                                                                   \
        uint MortonKeys[];                                              \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 2) buffer element_remapping          \
    {                                                                   \
        uint ElementReMapping[];                                        \
    };                                                                  \
                                                                        \
                                                                        \
                                                                        \
    layout(set = set_id, binding = 3) buffer radix_tree_nodes           \
    {                                                                   \
        ivec2 RadixNodeChildren[];                                      \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 4) buffer radix_tree_node_parents    \
    {                                                                   \
        int RadixNodeParents[];                                         \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 5) buffer radix_tree_particle_array  \
    {                                                                   \
        radix_tree_particle RadixTreeParticles[];                       \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 6) buffer radix_tree_atomics_array   \
    {                                                                   \
        uint RadixTreeAtomics[];                                        \
    };                                                                  \
                                                                        \
                                                                        \
                                                                        \
    layout(set = set_id, binding = 7) coherent buffer element_bounds_reduction \
    {                                                                   \
        bounds GlobalBoundsReductionArray[];                            \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 8) buffer global_bounds_counter_buffer \
    {                                                                   \
        uint GlobalBoundsWorkCounter;                                   \
        uint GlobalBoundsDoneCounter;                                   \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 9) buffer element_bounds             \
    {                                                                   \
        bounds ElementBounds;                                           \
    };                                                                  
