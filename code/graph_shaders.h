
struct graph_node_edges
{
    uint StartConnections;
    uint EndConnections;
};

struct graph_node_draw
{
    vec3 Color;
    float Scale;
};

struct edge
{
    uint OtherNodeId;
    float Weight;
};

struct global_move_reduction
{
    float Swing;
    float Traction;
};

#define GRAPH_DESCRIPTOR_LAYOUT(set_id)                                 \
                                                                        \
    layout(set = set_id, binding = 0) uniform graph_globals             \
    {                                                                   \
        mat4 VPTransform;                                               \
        vec2 ViewPort;                                                  \
        float FrameTime;                                                \
        uint NumNodes;                                                  \
                                                                        \
        float AttractionMultiplier;                                     \
        float AttractionWeightPower;                                    \
        float RepulsionMultiplier;                                      \
        float RepulsionSoftner;                                         \
        float GravityMultiplier;                                        \
        uint StrongGravityEnabled;                                      \
                                                                        \
        float CellDim;                                                  \
        float WorldRadius;                                              \
        uint NumCellsDim;                                               \
                                                                        \
        uint NumThreadGroupsCalcNodeBounds;                             \
        uint NumThreadGroupsGlobalSpeed;                                \
    } GraphGlobals;                                                     \
                                                                        \
    layout(set = set_id, binding = 1) buffer graph_node_position_array  \
    {                                                                   \
        vec2 NodePositionArray[];                                       \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 2) buffer graph_node_degree_array    \
    {                                                                   \
        float NodeDegreeArray[];                                        \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 3) buffer graph_node_cell_id_array   \
    {                                                                   \
        uint NodeCellIdArray[];                                         \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 4) buffer graph_node_force_array     \
    {                                                                   \
        vec2 NodeForceArray[];                                          \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 5) buffer graph_prev_node_force_array \
    {                                                                   \
        vec2 NodePrevForceArray[];                                      \
    };                                                                  \
                                                                        \
                                                                        \
                                                                        \
    layout(set = set_id, binding = 6) buffer graph_node_edge_array      \
    {                                                                   \
        graph_node_edges NodeEdgeArray[];                               \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 7) buffer graph_edge_array           \
    {                                                                   \
        edge EdgeArray[];                                               \
    };                                                                  \
                                                                        \
                                                                        \
                                                                        \
    layout(set = set_id, binding = 8) buffer graph_node_draw_array      \
    {                                                                   \
        graph_node_draw NodeDrawArray[];                                \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 9) buffer graph_edge_index_array     \
    {                                                                   \
        uint DrawEdgeIndexArray[];                                      \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 10) buffer graph_edge_color_array    \
    {                                                                   \
        uint DrawEdgeColorArray[];                                      \
    };                                                                  \
                                                                        \
                                                                        \
                                                                        \
    layout(set = set_id, binding = 11) buffer global_speed_data         \
    {                                                                   \
        float SpeedEfficiency;                                          \
        float Speed;                                                    \
        float JitterToleranceConstant;                                  \
        float MaxJitterTolerance;                                       \
    } GlobalMove;                                                       \
                                                                        \
    layout(set = set_id, binding = 12) coherent buffer global_move_reductions \
    {                                                                   \
        global_move_reduction GlobalMoveReductionArray[];               \
    };                                                                  \
                                                                        \
    layout(set = set_id, binding = 13) buffer global_move_counter_buffer \
    {                                                                   \
        uint GlobalMoveWorkCounter;                                     \
        uint GlobalMoveDoneCounter;                                     \
    };                                                                  \

