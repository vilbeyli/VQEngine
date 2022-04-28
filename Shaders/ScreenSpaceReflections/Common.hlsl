/**********************************************************************
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/

static const float g_roughness_sigma_min = 0.01f;
static const float g_roughness_sigma_max = 0.02f;
static const float g_depth_sigma = 0.02f;

[[vk::binding(0, 0)]] cbuffer Constants : register(b0) {
    float4x4 g_inv_view_proj;
    float4x4 g_proj;
    float4x4 g_inv_proj;
    float4x4 g_view;
    float4x4 g_inv_view;
    float4x4 g_prev_view_proj;
    uint2 g_buffer_dimensions;
    float2 g_inv_buffer_dimensions;
    float g_temporal_stability_factor;
    float g_depth_buffer_thickness;
    float g_roughness_threshold;
    float g_temporal_variance_threshold;
    uint g_frame_index;
    uint g_max_traversal_intersections;
    uint g_min_traversal_occupancy;
    uint g_most_detailed_mip;
    uint g_samples_per_quad;
    uint g_temporal_variance_guided_tracing_enabled;
};

//=== Common functions of the SssrSample ===

uint PackFloat16(min16float2 v) {
    uint2 p = f32tof16(float2(v));
    return p.x | (p.y << 16);
}

min16float2 UnpackFloat16(uint a) {
    float2 tmp = f16tof32(
        uint2(a & 0xFFFF, a >> 16));
    return min16float2(tmp);
}

uint PackRayCoords(uint2 ray_coord, bool copy_horizontal, bool copy_vertical, bool copy_diagonal) {
    uint ray_x_15bit = ray_coord.x & 0b111111111111111;
    uint ray_y_14bit = ray_coord.y & 0b11111111111111;
    uint copy_horizontal_1bit = copy_horizontal ? 1 : 0;
    uint copy_vertical_1bit = copy_vertical ? 1 : 0;
    uint copy_diagonal_1bit = copy_diagonal ? 1 : 0;

    uint packed = (copy_diagonal_1bit << 31) | (copy_vertical_1bit << 30) | (copy_horizontal_1bit << 29) | (ray_y_14bit << 15) | (ray_x_15bit << 0);
    return packed;
}

void UnpackRayCoords(uint packed, out uint2 ray_coord, out bool copy_horizontal, out bool copy_vertical, out bool copy_diagonal) {
    ray_coord.x = (packed >> 0) & 0b111111111111111;
    ray_coord.y = (packed >> 15) & 0b11111111111111;
    copy_horizontal = (packed >> 29) & 0b1;
    copy_vertical = (packed >> 30) & 0b1;
    copy_diagonal = (packed >> 31) & 0b1;
}

// Transforms origin to uv space
// Mat must be able to transform origin from its current space into clip space.
float3 ProjectPosition(float3 origin, float4x4 mat) {
    float4 projected = mul(mat, float4(origin, 1));
    projected.xyz /= projected.w;
    projected.xy = 0.5 * projected.xy + 0.5;
    projected.y = (1 - projected.y);
    return projected.xyz;
}

// Origin and direction must be in the same space and mat must be able to transform from that space into clip space.
float3 ProjectDirection(float3 origin, float3 direction, float3 screen_space_origin, float4x4 mat) {
    float3 offsetted = ProjectPosition(origin + direction, mat);
    return offsetted - screen_space_origin;
}

// Mat must be able to transform origin from texture space to a linear space.
float3 InvProjectPosition(float3 coord, float4x4 mat) {
    coord.y = (1 - coord.y);
    coord.xy = 2 * coord.xy - 1;
    float4 projected = mul(mat, float4(coord, 1));
    projected.xyz /= projected.w;
    return projected.xyz;
}

//=== FFX_DNSR_Reflections_ override functions ===

bool FFX_DNSR_Reflections_IsGlossyReflection(float roughness) {
    return roughness < g_roughness_threshold;
}

bool FFX_DNSR_Reflections_IsMirrorReflection(float roughness) {
    return roughness < 0.0001;
}

float3 FFX_DNSR_Reflections_ScreenSpaceToViewSpace(float3 screen_uv_coord) {
    return InvProjectPosition(screen_uv_coord, g_inv_proj);
}

float3 FFX_DNSR_Reflections_ViewSpaceToWorldSpace(float4 view_space_coord) {
    return mul(g_inv_view, view_space_coord).xyz;
}

float3 FFX_DNSR_Reflections_WorldSpaceToScreenSpacePrevious(float3 world_space_pos) {
    return ProjectPosition(world_space_pos, g_prev_view_proj);
}

float FFX_DNSR_Reflections_GetLinearDepth(float2 uv, float depth) {
    const float3 view_space_pos = InvProjectPosition(float3(uv, depth), g_inv_proj);
    return abs(view_space_pos.z);
}

uint FFX_DNSR_Reflections_RoundedDivide(uint value, uint divisor) {
    return (value + divisor - 1) / divisor;
}

uint FFX_DNSR_Reflections_GetTileMetaDataIndex(uint2 pixel_pos, uint screen_width) {
    uint2 tile_index = uint2(pixel_pos.x / 8, pixel_pos.y / 8);
    uint flattened = tile_index.y * FFX_DNSR_Reflections_RoundedDivide(screen_width, 8) + tile_index.x;
    return flattened;
}