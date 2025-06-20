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

#ifndef FFX_DNSR_REFLECTIONS_RESOLVE_TEMPORAL
#define FFX_DNSR_REFLECTIONS_RESOLVE_TEMPORAL

#define FFX_DNSR_REFLECTIONS_ESTIMATES_LOCAL_NEIGHBORHOOD
#include "ffx_denoiser_reflections_common.h"

groupshared uint g_ffx_dnsr_shared_0[16][16];
groupshared uint g_ffx_dnsr_shared_1[16][16];

struct FFX_DNSR_Reflections_NeighborhoodSample {
    min16float3 radiance;
};

FFX_DNSR_Reflections_NeighborhoodSample FFX_DNSR_Reflections_LoadFromGroupSharedMemory(int2 idx) {
    uint2       packed_radiance   = uint2(g_ffx_dnsr_shared_0[idx.y][idx.x], g_ffx_dnsr_shared_1[idx.y][idx.x]);
    min16float3 unpacked_radiance = FFX_DNSR_Reflections_UnpackFloat16_4(packed_radiance).xyz;

    FFX_DNSR_Reflections_NeighborhoodSample sample;
    sample.radiance = unpacked_radiance;
    return sample;
}

struct FFX_DNSR_Reflections_Moments {
    min16float3 mean;
    min16float3 variance;
};

FFX_DNSR_Reflections_Moments FFX_DNSR_Reflections_EstimateLocalNeighborhoodInGroup(int2 group_thread_id) {
    FFX_DNSR_Reflections_Moments estimate;
    estimate.mean                 = 0;
    estimate.variance             = 0;
    min16float accumulated_weight = 0;
    for (int j = -FFX_DNSR_REFLECTIONS_LOCAL_NEIGHBORHOOD_RADIUS; j <= FFX_DNSR_REFLECTIONS_LOCAL_NEIGHBORHOOD_RADIUS; ++j) {
        for (int i = -FFX_DNSR_REFLECTIONS_LOCAL_NEIGHBORHOOD_RADIUS; i <= FFX_DNSR_REFLECTIONS_LOCAL_NEIGHBORHOOD_RADIUS; ++i) {
            int2        new_idx  = group_thread_id + int2(i, j);
            min16float3 radiance = FFX_DNSR_Reflections_LoadFromGroupSharedMemory(new_idx).radiance;
            min16float  weight   = FFX_DNSR_Reflections_LocalNeighborhoodKernelWeight(i) * FFX_DNSR_Reflections_LocalNeighborhoodKernelWeight(j);
            accumulated_weight  += weight;
            estimate.mean       += radiance * weight;
            estimate.variance   += radiance * radiance * weight;
        }
    }
    estimate.mean     /= accumulated_weight;
    estimate.variance /= accumulated_weight;

    estimate.variance = abs(estimate.variance - estimate.mean * estimate.mean);
    return estimate;
}

void FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2 group_thread_id, min16float3 radiance) {
    g_ffx_dnsr_shared_0[group_thread_id.y][group_thread_id.x] = FFX_DNSR_Reflections_PackFloat16(radiance.xy);
    g_ffx_dnsr_shared_1[group_thread_id.y][group_thread_id.x] = FFX_DNSR_Reflections_PackFloat16(radiance.zz);
}

void FFX_DNSR_Reflections_LoadNeighborhood(int2 pixel_coordinate, out min16float3 radiance) { radiance = FFX_DNSR_Reflections_LoadRadiance(pixel_coordinate); }

void FFX_DNSR_Reflections_InitializeGroupSharedMemory(int2 dispatch_thread_id, int2 group_thread_id, int2 screen_size) {
    // Load 16x16 region into shared memory using 4 8x8 blocks.
    int2 offset[4] = {int2(0, 0), int2(8, 0), int2(0, 8), int2(8, 8)};

    // Intermediate storage registers to cache the result of all loads
    min16float3 radiance[4];

    // Start in the upper left corner of the 16x16 region.
    dispatch_thread_id -= 4;

    // First store all loads in registers
    for (int i = 0; i < 4; ++i) {
        FFX_DNSR_Reflections_LoadNeighborhood(dispatch_thread_id + offset[i], radiance[i]);
    }

    // Then move all registers to groupshared memory
    for (int j = 0; j < 4; ++j) {
        FFX_DNSR_Reflections_StoreInGroupSharedMemory(group_thread_id + offset[j], radiance[j]);
    }
}

void FFX_DNSR_Reflections_ResolveTemporal(int2 dispatch_thread_id, int2 group_thread_id, uint2 screen_size, float2 inv_screen_size, float history_clip_weight) {
    FFX_DNSR_Reflections_InitializeGroupSharedMemory(dispatch_thread_id, group_thread_id, screen_size);
    GroupMemoryBarrierWithGroupSync();

    group_thread_id += 4; // Center threads in groupshared memory

    FFX_DNSR_Reflections_NeighborhoodSample center       = FFX_DNSR_Reflections_LoadFromGroupSharedMemory(group_thread_id);
    min16float3                             new_signal   = center.radiance;
    min16float                              roughness    = FFX_DNSR_Reflections_LoadRoughness(dispatch_thread_id);
    min16float                              new_variance = FFX_DNSR_Reflections_LoadVariance(dispatch_thread_id);
    
    if (FFX_DNSR_Reflections_IsGlossyReflection(roughness)) {
        min16float  num_samples  = FFX_DNSR_Reflections_LoadNumSamples(dispatch_thread_id);
        float2      uv8          = (float2(dispatch_thread_id.xy) + (0.5).xx) / FFX_DNSR_Reflections_RoundUp8(screen_size);
        min16float3 avg_radiance = FFX_DNSR_Reflections_SampleAverageRadiance(uv8);

        min16float3                  old_signal         = FFX_DNSR_Reflections_LoadRadianceReprojected(dispatch_thread_id);
        FFX_DNSR_Reflections_Moments local_neighborhood = FFX_DNSR_Reflections_EstimateLocalNeighborhoodInGroup(group_thread_id);
        // Clip history based on the curren local statistics
        min16float3                  color_std          = (sqrt(local_neighborhood.variance.xyz) + length(local_neighborhood.mean.xyz - avg_radiance)) * history_clip_weight * 1.4;
                            local_neighborhood.mean.xyz = lerp(local_neighborhood.mean.xyz, avg_radiance, 0.2);
        min16float3                  radiance_min       = local_neighborhood.mean.xyz - color_std;
        min16float3                  radiance_max       = local_neighborhood.mean.xyz + color_std;
        min16float3                  clipped_old_signal = FFX_DNSR_Reflections_ClipAABB(radiance_min, radiance_max, old_signal.xyz);
        min16float                   accumulation_speed = 1.0 / max(num_samples, 1.0);
        min16float                   weight             = (1.0 - accumulation_speed);
        // Blend with average for small sample count
        new_signal.xyz                                  = lerp(new_signal.xyz, avg_radiance, 1.0 / max(num_samples + 1.0f, 1.0));
        // Clip outliers
        {
            min16float3                  radiance_min       = avg_radiance.xyz - color_std * 1.0;
            min16float3                  radiance_max       = avg_radiance.xyz + color_std * 1.0;
            new_signal.xyz                                  = FFX_DNSR_Reflections_ClipAABB(radiance_min, radiance_max, new_signal.xyz);
        }
        // Blend with history
        new_signal                                      = lerp(new_signal, clipped_old_signal, weight);
        new_variance                                    = lerp(FFX_DNSR_Reflections_ComputeTemporalVariance(new_signal.xyz, clipped_old_signal.xyz), new_variance, weight);
        if (any(isinf(new_signal)) || any(isnan(new_signal)) || any(isinf(new_variance)) || any(isnan(new_variance))) {
            new_signal   = 0.0;
            new_variance = 0.0;
        }

    }
    FFX_DNSR_Reflections_StoreTemporalAccumulation(dispatch_thread_id, new_signal, new_variance);
}

#endif // FFX_DNSR_REFLECTIONS_RESOLVE_TEMPORAL