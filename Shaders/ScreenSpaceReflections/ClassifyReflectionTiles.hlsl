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

#include "Common.hlsl"
#include "../AMDFidelityFX/DNSR/ffx_denoiser_reflections_common.h"

Texture2D<float4> g_roughness                         : register(t0);
Texture2D<float> g_depth_buffer                       : register(t1);
Texture2D<float> g_variance_history                   : register(t2);
Texture2D<float4> g_normal                            : register(t3);

SamplerState g_environment_map_sampler                : register(s0);

RWBuffer<uint> g_ray_list                             : register(u0);
globallycoherent RWBuffer<uint> g_ray_counter         : register(u1);
RWTexture2D<float4> g_intersection_output             : register(u2);
RWTexture2D<float> g_extracted_roughness              : register(u3);
RWBuffer<uint> g_denoiser_tile_list                   : register(u4);

TextureCube g_environment_map                         : register(t0, space1);

void IncrementRayCounter(uint value, out uint original_value) {
    InterlockedAdd(g_ray_counter[0], value, original_value);
}

void IncrementDenoiserTileCounter(out uint original_value) {
    InterlockedAdd(g_ray_counter[2], 1, original_value);
}

void StoreRay(int index, uint2 ray_coord, bool copy_horizontal, bool copy_vertical, bool copy_diagonal) {
    g_ray_list[index] = PackRayCoords(ray_coord, copy_horizontal, copy_vertical, copy_diagonal); // Store out pixel to trace
}

void StoreDenoiserTile(int index, uint2 tile_coord) {
    g_denoiser_tile_list[index] = ((tile_coord.y & 0xffffu) << 16) | ((tile_coord.x & 0xffffu) << 0); // Store out pixel to trace
}

bool IsReflectiveSurface(int2 pixel_coordinate, float roughness)
{
    const float far_plane = 1.0f; // g_depth_buffer is NDC, and Cauldron does not use reverse Z. Thus the far plane is at 1 in NDC.
    return g_depth_buffer[pixel_coordinate] < far_plane;
}

bool IsBaseRay(uint2 dispatch_thread_id, uint samples_per_quad) {
    switch (samples_per_quad) {
    case 1:
        return ((dispatch_thread_id.x & 1) | (dispatch_thread_id.y & 1)) == 0; // Deactivates 3 out of 4 rays
    case 2:
        return (dispatch_thread_id.x & 1) == (dispatch_thread_id.y & 1); // Deactivates 2 out of 4 rays. Keeps diagonal.
    default: // case 4:
        return true;
    }
}

groupshared uint g_TileCount;

float3 SampleEnvironmentMap(uint2 dispatch_thread_id, float roughness, float mip_count) {
    float2 uv = (dispatch_thread_id + 0.5) * g_inv_buffer_dimensions;
    float3 world_space_normal = normalize(2.0 * g_normal.Load(int3(dispatch_thread_id, 0)).xyz - 1.0);
    float  z = g_depth_buffer.Load(int3(dispatch_thread_id, 0));
    float3 screen_uv_space_ray_origin = float3(uv, z);
    float3 view_space_ray = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
    float3 view_space_ray_direction = normalize(view_space_ray);
    float3 view_space_surface_normal = mul(g_view, float4(world_space_normal, 0)).xyz;
    float3 view_space_reflected_direction = reflect(view_space_ray_direction, view_space_surface_normal);
    float3 world_space_reflected_direction = mul(g_inv_view, float4(view_space_reflected_direction, 0)).xyz;

    return g_environment_map.SampleLevel(g_environment_map_sampler, world_space_reflected_direction, roughness * (mip_count - 1)).xyz;
}

void ClassifyTiles(uint2 dispatch_thread_id, uint2 group_thread_id, float roughness) {
    g_TileCount = 0;

    bool is_first_lane_of_wave = WaveIsFirstLane();

    // First we figure out on a per thread basis if we need to shoot a reflection ray.
    // Disable offscreen pixels
    bool needs_ray = !(dispatch_thread_id.x >= g_buffer_dimensions.x || dispatch_thread_id.y >= g_buffer_dimensions.y);

    // Dont shoot a ray on very rough surfaces.
    bool is_reflective_surface = IsReflectiveSurface(dispatch_thread_id, roughness);
    bool is_glossy_reflection = FFX_DNSR_Reflections_IsGlossyReflection(roughness);
    needs_ray = needs_ray && is_glossy_reflection && is_reflective_surface;

    // Also we dont need to run the denoiser on mirror reflections.
    bool needs_denoiser = needs_ray && !FFX_DNSR_Reflections_IsMirrorReflection(roughness);

    // Decide which ray to keep
    bool is_base_ray = IsBaseRay(dispatch_thread_id, g_samples_per_quad);
    needs_ray = needs_ray && (!needs_denoiser || is_base_ray); // Make sure to not deactivate mirror reflection rays.

    if (g_temporal_variance_guided_tracing_enabled && needs_denoiser && !needs_ray) {
        bool has_temporal_variance = g_variance_history.Load(int3(dispatch_thread_id, 0)) > g_temporal_variance_threshold;
        needs_ray = needs_ray || has_temporal_variance;
    }

    GroupMemoryBarrierWithGroupSync(); // Wait until g_TileCount is cleared - allow some computations before and after

    // Now we know for each thread if it needs to shoot a ray and wether or not a denoiser pass has to run on this pixel.

    if (is_glossy_reflection && is_reflective_surface) InterlockedAdd(g_TileCount, 1);

    // Next we have to figure out for which pixels that ray is creating the values for. Thus, if we have to copy its value horizontal, vertical or across.
    bool require_copy = !needs_ray && needs_denoiser; // Our pixel only requires a copy if we want to run a denoiser on it but don't want to shoot a ray for it.
    bool copy_horizontal = (g_samples_per_quad != 4) && is_base_ray && WaveReadLaneAt(require_copy, WaveGetLaneIndex() ^ 0b01); // QuadReadAcrossX
    bool copy_vertical = (g_samples_per_quad == 1) && is_base_ray && WaveReadLaneAt(require_copy, WaveGetLaneIndex() ^ 0b10); // QuadReadAcrossY
    bool copy_diagonal = (g_samples_per_quad == 1) && is_base_ray && WaveReadLaneAt(require_copy, WaveGetLaneIndex() ^ 0b11); // QuadReadAcrossDiagonal

    // Thus, we need to compact the rays and append them all at once to the ray list.
    uint local_ray_index_in_wave = WavePrefixCountBits(needs_ray);
    uint wave_ray_count = WaveActiveCountBits(needs_ray);
    uint base_ray_index;
    if (is_first_lane_of_wave) {
        IncrementRayCounter(wave_ray_count, base_ray_index);
    }
    base_ray_index = WaveReadLaneFirst(base_ray_index);
    if (needs_ray) {
        int ray_index = base_ray_index + local_ray_index_in_wave;
        StoreRay(ray_index, dispatch_thread_id, copy_horizontal, copy_vertical, copy_diagonal);
    }

    float4 intersection_output = 0;
    if (is_reflective_surface && !is_glossy_reflection)
    {
        // Fall back to environment map without preparing a ray
        intersection_output.xyz = SampleEnvironmentMap(dispatch_thread_id, roughness, g_env_map_mip_count);
    }
    g_intersection_output[dispatch_thread_id] = intersection_output;

    GroupMemoryBarrierWithGroupSync(); // Wait until g_TileCount

    if (all(group_thread_id == 0) && g_TileCount > 0) {
        uint tile_offset;
        IncrementDenoiserTileCounter(tile_offset);
        StoreDenoiserTile(tile_offset, dispatch_thread_id.xy);
    }
}


[numthreads(8, 8, 1)]
void CSMain(uint2 group_id : SV_GroupID, uint group_index : SV_GroupIndex) {
    uint2 group_thread_id = FFX_DNSR_Reflections_RemapLane8x8(group_index); // Remap lanes to ensure four neighboring lanes are arranged in a quad pattern
    uint2 dispatch_thread_id = group_id * 8 + group_thread_id;

    float roughness = g_roughness.Load(int3(dispatch_thread_id, 0)).w;

    ClassifyTiles(dispatch_thread_id, group_thread_id, roughness);

    // Extract only the channel containing the roughness to avoid loading all 4 channels in the follow up passes.
    g_extracted_roughness[dispatch_thread_id] = roughness;
}