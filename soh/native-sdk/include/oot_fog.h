#ifndef LINKSPAN_OOT_FOG_H
#define LINKSPAN_OOT_FOG_H

#include "shiplua/native/ship_native_abi.h"

#define LINKSPAN_OOT_FOG_SERVICE "linkspan.oot.fog"
#define LINKSPAN_OOT_FOG_VERSION 1u
#define LINKSPAN_OOT_FOG_VERSION_2 2u
#define LINKSPAN_OOT_FOG_MAX_SIDE 32u
#define LINKSPAN_OOT_FOG_FLOW_PARTICLES 1u
#define LINKSPAN_OOT_FOG_FLOW_BLOCKED_EDGES 2u
#define LINKSPAN_OOT_FOG_FLOW_VORTICES 4u
#define LINKSPAN_OOT_FOG_FLOW_LIFT 8u
/* A provider can translate the XZ bounds within the same generation. The host seeds the new
   grid from the submitted snapshot and keeps particles inside the shared area. */
#define LINKSPAN_OOT_FOG_FLOW_DOMAIN_SHIFT 16u
/* Sample the current play state's emissive point lights when rendering the volume. */
#define LINKSPAN_OOT_FOG_FLOW_LOCAL_LIGHTS 32u
#define LINKSPAN_OOT_FOG_FLOW_STACKED_SUPPORTS 64u
/* Blend distant fog with the current scene's atmospheric fog color. */
#define LINKSPAN_OOT_FOG_FLOW_ATMOSPHERE 128u
#define LINKSPAN_OOT_FOG_FLOW_VERTICAL_COMPUTE 256u
/* Render the submitted CPU field with the V2 volume and optional GPU particles. */
#define LINKSPAN_OOT_FOG_FLOW_CPU_FIELD 512u
#define LINKSPAN_OOT_FOG_MAX_RAISED_FRACTION 0.70f
#define LINKSPAN_OOT_FOG_VORTEX_REST_DENSITY 0.60f
#define LINKSPAN_OOT_FOG_EDGE_NEGATIVE_X 1u
#define LINKSPAN_OOT_FOG_EDGE_POSITIVE_X 2u
#define LINKSPAN_OOT_FOG_EDGE_NEGATIVE_Z 4u
#define LINKSPAN_OOT_FOG_EDGE_POSITIVE_Z 8u

typedef struct ShipOotFogCellV1 {
    float floor_y;
    float density;
} ShipOotFogCellV1;

typedef struct ShipOotFogGridV1 {
    uint32_t size;
    uint32_t width, depth;
    float x_min, x_max, z_min, z_max;
    float layer_height, falloff, extinction, edge_feather;
    float color[3];
    const ShipOotFogCellV1* cells; /* row-major [z][x], normalized density in [0,1] */
} ShipOotFogGridV1;

typedef struct ShipOotFogV1 {
    uint32_t size;
    /* Game thread, inside the world draw hook. First valid submission per
       frame wins. The host copies the cells and consumes them before the HUD.
       UNSUPPORTED means the caller can use its geometry fallback. */
    ShipNativeStatus (SHIP_NATIVE_CALL* submit)(void* play_state, const ShipOotFogGridV1* grid);
} ShipOotFogV1;

typedef struct ShipOotFogImpulseV2 {
    float density_scale;
    float velocity_x, velocity_z;
    float min_x, max_x, min_z, max_z;
} ShipOotFogImpulseV2;

typedef struct ShipOotFogFlowCellV2 {
    float velocity_x, velocity_z; /* final CPU state used to seed a discontinuity */
    uint32_t open;
    ShipOotFogImpulseV2 before_step;
    ShipOotFogImpulseV2 after_step;
} ShipOotFogFlowCellV2;

typedef struct ShipOotFogVerticalCell {
    float raised_density;
    float vertical_speed;
    float before_step_lift;
} ShipOotFogVerticalCell;

typedef struct ShipOotFogFlowV2 {
    uint32_t size;
    uint32_t generation; /* change on reset or when switching simulation modes */
    uint32_t tick;       /* one increment per simulated tick, identical on redraw */
    float step_seconds;
    const ShipOotFogFlowCellV2* cells; /* same dimensions and order as the grid */
    uint32_t flags; /* optional tail; zero selects the simulation without particle detail */
    /* Optional row-major masks. One bit per neighbor, with matching bits on both sides of an edge. */
    const uint8_t* blocked_edges;
    /* Optional row-major fraction of the column density carried in the raised band.
       Read only with LINKSPAN_OOT_FOG_FLOW_LIFT. Zero is entirely ground fog. */
    const float* raised_fraction;
    /* Optional upper support per column, in the same order as the base grid.
       Zero density means the column has only its base support. */
    const ShipOotFogCellV1* upper_cells;
    /* Optional raised fractions for upper_cells. Read when both features are enabled. */
    const float* upper_raised_fraction;
    /* Final base-band seed state and one tick of vertical forcing, in grid order.
       The CPU fractions remain available to hosts without vertical compute. */
    const ShipOotFogVerticalCell* vertical_cells;
} ShipOotFogFlowV2;

typedef struct ShipOotFogV2 {
    uint32_t size;
    /* The grid is the final CPU snapshot. With consecutive ticks the host can
       advance its GPU state using the two impulse batches. All inputs are copied. */
    ShipNativeStatus (SHIP_NATIVE_CALL* submit_flow)(void* play_state, const ShipOotFogGridV1* grid,
                                                    const ShipOotFogFlowV2* flow);
    uint32_t features; /* optional capability bits for extended flow submissions */
} ShipOotFogV2;

#endif
