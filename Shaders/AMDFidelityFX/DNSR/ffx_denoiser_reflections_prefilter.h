
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

#ifndef FFX_DNSR_REFLECTIONS_PREFILTER
#define FFX_DNSR_REFLECTIONS_PREFILTER

#include "ffx_denoiser_reflections_common.h"

groupshared uint  g_ffx_dnsr_shared_0[16][16];
groupshared uint  g_ffx_dnsr_shared_1[16][16];
groupshared uint  g_ffx_dnsr_shared_2[16][16];
groupshared uint  g_ffx_dnsr_shared_3[16][16];
groupshared float g_ffx_dnsr_shared_depth[16][16];

struct FFX_DNSR_Reflections_NeighborhoodSample {
    min16float3 radiance;
    min16float  variance;
    min16float3 normal;
    float       depth;
};

FFX_DNSR_Reflections_NeighborhoodSample FFX_DNSR_Reflections_LoadFromGroupSharedMemory(int2 idx) {
    uint2       packed_radiance          = uint2(g_ffx_dnsr_shared_0[idx.y][idx.x], g_ffx_dnsr_shared_1[idx.y][idx.x]);
    min16float4 unpacked_radiance        = FFX_DNSR_Reflections_UnpackFloat16_4(packed_radiance);
    uint2       packed_normal_variance   = uint2(g_ffx_dnsr_shared_2[idx.y][idx.x], g_ffx_dnsr_shared_3[idx.y][idx.x]);
    min16float4 unpacked_normal_variance = FFX_DNSR_Reflections_UnpackFloat16_4(packed_normal_variance);

    FFX_DNSR_Reflections_NeighborhoodSample sample;
    sample.radiance = unpacked_radiance.xyz;
    sample.normal   = unpacked_normal_variance.xyz;
    sample.variance = unpacked_normal_variance.w;
    sample.depth    = g_ffx_dnsr_shared_depth[idx.y][idx.x];
    return sample;
}

void FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2 group_thread_id, min16float3 radiance, min16float variance, min16float3 normal, float depth) {
    g_ffx_dnsr_shared_0[group_thread_id.y][group_thread_id.x]     = FFX_DNSR_Reflections_PackFloat16(radiance.xy);
    g_ffx_dnsr_shared_1[group_thread_id.y][group_thread_id.x]     = FFX_DNSR_Reflections_PackFloat16(radiance.zz);
    g_ffx_dnsr_shared_2[group_thread_id.y][group_thread_id.x]     = FFX_DNSR_Reflections_PackFloat16(normal.xy);
    g_ffx_dnsr_shared_3[group_thread_id.y][group_thread_id.x]     = FFX_DNSR_Reflections_PackFloat16(min16float2(normal.z, variance));
    g_ffx_dnsr_shared_depth[group_thread_id.y][group_thread_id.x] = depth;
}

void FFX_DNSR_Reflections_InitializeGroupSharedMemory(int2 dispatch_thread_id, int2 group_thread_id, int2 screen_size) {
    // Load 16x16 region into shared memory using 4 8x8 blocks.
    int2 offset[4] = {int2(0, 0), int2(8, 0), int2(0, 8), int2(8, 8)};

    // Intermediate storage registers to cache the result of all loads
    min16float3 radiance[4];
    min16float  variance[4];
    min16float3 normal[4];
    float       depth[4];

    // Start in the upper left corner of the 16x16 region.
    dispatch_thread_id -= 4;

    // First store all loads in registers
    for (int i = 0; i < 4; ++i) {
        FFX_DNSR_Reflections_LoadNeighborhood(dispatch_thread_id + offset[i], radiance[i], variance[i], normal[i], depth[i], screen_size);
    }

    // Then move all registers to groupshared memory
    for (int j = 0; j < 4; ++j) {
        FFX_DNSR_Reflections_StoreInGroupSharedMemory(group_thread_id + offset[j], radiance[j], variance[j], normal[j], depth[j]); // X
    }
}

min16float FFX_DNSR_Reflections_GetEdgeStoppingNormalWeight(min16float3 normal_p, min16float3 normal_q) {
    return pow(max(dot(normal_p, normal_q), 0.0), FFX_DNSR_REFLECTIONS_PREFILTER_NORMAL_SIGMA);
}

min16float FFX_DNSR_Reflections_GetEdgeStoppingDepthWeight(float center_depth, float neighbor_depth) {
    return exp(-abs(center_depth - neighbor_depth) * center_depth * FFX_DNSR_REFLECTIONS_PREFILTER_DEPTH_SIGMA);
}

min16float FFX_DNSR_Reflections_GetRadianceWeight(min16float3 center_radiance, min16float3 neighbor_radiance, min16float variance) {
    return max(exp(-(FFX_DNSR_REFLECTIONS_RADIANCE_WEIGHT_BIAS + variance * FFX_DNSR_REFLECTIONS_RADIANCE_WEIGHT_VARIANCE_K)
                    * length(center_radiance - neighbor_radiance.xyz))
            , 1.0e-2);
}

void FFX_DNSR_Reflections_Resolve(int2 group_thread_id, min16float3 avg_radiance, FFX_DNSR_Reflections_NeighborhoodSample center,
                                  out min16float3 resolved_radiance, out min16float resolved_variance) {
    // Initial weight is important to remove fireflies.
    // That removes quite a bit of energy but makes everything much more stable.
    min16float  accumulated_weight   = FFX_DNSR_Reflections_GetRadianceWeight(avg_radiance, center.radiance.xyz, center.variance);
    min16float3 accumulated_radiance = center.radiance.xyz * accumulated_weight;
    min16float  accumulated_variance = center.variance * accumulated_weight * accumulated_weight;
    // First 15 numbers of Halton(2,3) streteched to [-3,3]. Skipping the center, as we already have that in center_radiance and center_variance.
    const uint sample_count     = 15;
    const int2 sample_offsets[] = {int2(0, 1),  int2(-2, 1),  int2(2, -3), int2(-3, 0),  int2(1, 2), int2(-1, -2), int2(3, 0), int2(-3, 3),
                                   int2(0, -3), int2(-1, -1), int2(2, 1),  int2(-2, -2), int2(1, 0), int2(0, 2),   int2(3, -1)};
    min16float variance_weight = max(FFX_DNSR_REFLECTIONS_PREFILTER_VARIANCE_BIAS,
                                     1.0 - exp(-(center.variance * FFX_DNSR_REFLECTIONS_PREFILTER_VARIANCE_WEIGHT))
                                    );
    for (int i = 0; i < sample_count; ++i) {
        int2                                    new_idx  = group_thread_id + sample_offsets[i];
        FFX_DNSR_Reflections_NeighborhoodSample neighbor = FFX_DNSR_Reflections_LoadFromGroupSharedMemory(new_idx);

        min16float weight = 1.0;
        weight *= FFX_DNSR_Reflections_GetEdgeStoppingNormalWeight(float3(center.normal), float3(neighbor.normal));
        weight *= FFX_DNSR_Reflections_GetEdgeStoppingDepthWeight(center.depth, neighbor.depth);
        weight *= FFX_DNSR_Reflections_GetRadianceWeight(avg_radiance, neighbor.radiance.xyz, center.variance);
        weight *= variance_weight;

        // Accumulate all contributions.
        accumulated_weight += weight;
        accumulated_radiance += weight * neighbor.radiance.xyz;
        accumulated_variance += weight * weight * neighbor.variance;
    }

    accumulated_radiance /= accumulated_weight;
    accumulated_variance /= (accumulated_weight * accumulated_weight);
    resolved_radiance = accumulated_radiance;
    resolved_variance = accumulated_variance;
}

void FFX_DNSR_Reflections_Prefilter(int2 dispatch_thread_id, int2 group_thread_id, uint2 screen_size) {
    min16float center_roughness = FFX_DNSR_Reflections_LoadRoughness(dispatch_thread_id);
    FFX_DNSR_Reflections_InitializeGroupSharedMemory(dispatch_thread_id, group_thread_id, screen_size);
    GroupMemoryBarrierWithGroupSync();

    group_thread_id += 4; // Center threads in groupshared memory

    FFX_DNSR_Reflections_NeighborhoodSample center = FFX_DNSR_Reflections_LoadFromGroupSharedMemory(group_thread_id);

    min16float3 resolved_radiance = center.radiance;
    min16float  resolved_variance = center.variance;

    // Check if we have to denoise or if a simple copy is enough
    bool needs_denoiser = center.variance > 0.0 && FFX_DNSR_Reflections_IsGlossyReflection(center_roughness) && !FFX_DNSR_Reflections_IsMirrorReflection(center_roughness);
    if (needs_denoiser) {
        float2      uv8          = (float2(dispatch_thread_id.xy) + (0.5).xx) / FFX_DNSR_Reflections_RoundUp8(screen_size);
        min16float3 avg_radiance = FFX_DNSR_Reflections_SampleAverageRadiance(uv8);
        FFX_DNSR_Reflections_Resolve(group_thread_id, avg_radiance, center, resolved_radiance, resolved_variance);
    }

    FFX_DNSR_Reflections_StorePrefilteredReflections(dispatch_thread_id, resolved_radiance, resolved_variance);
}

#endif // FFX_DNSR_REFLECTIONS_PREFILTER