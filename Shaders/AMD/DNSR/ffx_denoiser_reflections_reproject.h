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

#ifndef FFX_DNSR_REFLECTIONS_REPROJECT
#define FFX_DNSR_REFLECTIONS_REPROJECT

#define FFX_DNSR_REFLECTIONS_ESTIMATES_LOCAL_NEIGHBORHOOD
#include "ffx_denoiser_reflections_common.h"

groupshared uint  g_ffx_dnsr_shared_0[16][16];
groupshared uint  g_ffx_dnsr_shared_1[16][16];

struct FFX_DNSR_Reflections_NeighborhoodSample {
    min16float3 radiance;
};

FFX_DNSR_Reflections_NeighborhoodSample FFX_DNSR_Reflections_LoadFromGroupSharedMemory(int2 idx) {
    uint2       packed_radiance          = uint2(g_ffx_dnsr_shared_0[idx.y][idx.x], g_ffx_dnsr_shared_1[idx.y][idx.x]);
    min16float4 unpacked_radiance        = FFX_DNSR_Reflections_UnpackFloat16_4(packed_radiance);

    FFX_DNSR_Reflections_NeighborhoodSample sample;
    sample.radiance = unpacked_radiance.xyz;
    return sample;
}

void FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2 group_thread_id, min16float3 radiance) {
    g_ffx_dnsr_shared_0[group_thread_id.y][group_thread_id.x]     = FFX_DNSR_Reflections_PackFloat16(radiance.xy);
    g_ffx_dnsr_shared_1[group_thread_id.y][group_thread_id.x]     = FFX_DNSR_Reflections_PackFloat16(radiance.zz);
}

void FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2 group_thread_id, min16float4 radiance_variance) {
    g_ffx_dnsr_shared_0[group_thread_id.y][group_thread_id.x]     = FFX_DNSR_Reflections_PackFloat16(radiance_variance.xy);
    g_ffx_dnsr_shared_1[group_thread_id.y][group_thread_id.x]     = FFX_DNSR_Reflections_PackFloat16(radiance_variance.zw);
}

void FFX_DNSR_Reflections_InitializeGroupSharedMemory(int2 dispatch_thread_id, int2 group_thread_id, int2 screen_size) {
    // Load 16x16 region into shared memory using 4 8x8 blocks.
    int2 offset[4] = {int2(0, 0), int2(8, 0), int2(0, 8), int2(8, 8)};

    // Intermediate storage registers to cache the result of all loads
    min16float3 radiance[4];

    // Start in the upper left corner of the 16x16 region.
    dispatch_thread_id -= 4;

    // First store all loads in registers
    for (int i = 0; i < 4; ++i) {
        radiance[i] = FFX_DNSR_Reflections_LoadRadiance(dispatch_thread_id + offset[i]);
    }

    // Then move all registers to groupshared memory
    for (int j = 0; j < 4; ++j) {
        FFX_DNSR_Reflections_StoreInGroupSharedMemory(group_thread_id + offset[j], radiance[j]); // X
    }
}

min16float4 FFX_DNSR_Reflections_LoadFromGroupSharedMemoryRaw(int2 idx) {
    uint2 packed_radiance = uint2(g_ffx_dnsr_shared_0[idx.y][idx.x], g_ffx_dnsr_shared_1[idx.y][idx.x]);
    return FFX_DNSR_Reflections_UnpackFloat16_4(packed_radiance);
}

min16float FFX_DNSR_Reflections_GetLuminanceWeight(min16float3 val) {
    min16float luma   = FFX_DNSR_Reflections_Luminance(val.xyz);
    min16float weight = max(exp(-luma * FFX_DNSR_REFLECTIONS_AVG_RADIANCE_LUMINANCE_WEIGHT), 1.0e-2);
    return weight;
}

float2 FFX_DNSR_Reflections_GetSurfaceReprojection(int2 dispatch_thread_id, float2 uv, float2 motion_vector) {
    // Reflector position reprojection
    float2 history_uv = uv - motion_vector;
    return history_uv;
}

float2 FFX_DNSR_Reflections_GetHitPositionReprojection(int2 dispatch_thread_id, float2 uv, float reflected_ray_length) {
    float  z              = FFX_DNSR_Reflections_LoadDepth(dispatch_thread_id);
    float3 view_space_ray = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(float3(uv, z));

    // We start out with reconstructing the ray length in view space.
    // This includes the portion from the camera to the reflecting surface as well as the portion from the surface to the hit position.
    float surface_depth = length(view_space_ray);
    float ray_length    = surface_depth + reflected_ray_length;

    // We then perform a parallax correction by shooting a ray
    // of the same length "straight through" the reflecting surface
    // and reprojecting the tip of that ray to the previous frame.
    view_space_ray /= surface_depth; // == normalize(view_space_ray)
    view_space_ray *= ray_length;
    float3 world_hit_position =
        FFX_DNSR_Reflections_ViewSpaceToWorldSpace(float4(view_space_ray, 1)); // This is the "fake" hit position if we would follow the ray straight through the surface.
    float3 prev_hit_position = FFX_DNSR_Reflections_WorldSpaceToScreenSpacePrevious(world_hit_position);
    float2 history_uv        = prev_hit_position.xy;
    return history_uv;
}

min16float FFX_DNSR_Reflections_GetDisocclusionFactor(min16float3 normal, min16float3 history_normal, float linear_depth, float history_linear_depth) {
    min16float factor = 1.0                                                            //
                        * exp(-abs(1.0 - max(0.0, dot(normal, history_normal))) * FFX_DNSR_REFLECTIONS_DISOCCLUSION_NORMAL_WEIGHT) //
                        * exp(-abs(history_linear_depth - linear_depth) / linear_depth * FFX_DNSR_REFLECTIONS_DISOCCLUSION_DEPTH_WEIGHT);
    return factor;
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

float dot2(float3 a) { return dot(a, a); }

void FFX_DNSR_Reflections_PickReprojection(int2            dispatch_thread_id,  //
                                           int2            group_thread_id,     //
                                           uint2           screen_size,         //
                                           min16float      roughness,           //
                                           min16float      ray_length,          //
                                           out min16float  disocclusion_factor, //
                                           out float2      reprojection_uv,     //
                                           out min16float3 reprojection) {

    FFX_DNSR_Reflections_Moments local_neighborhood = FFX_DNSR_Reflections_EstimateLocalNeighborhoodInGroup(group_thread_id);

    float2      uv     = float2(dispatch_thread_id.x + 0.5, dispatch_thread_id.y + 0.5) / screen_size;
    min16float3 normal = FFX_DNSR_Reflections_LoadWorldSpaceNormal(dispatch_thread_id);
    min16float3 history_normal;
    float       history_linear_depth;

    {
        const float2      motion_vector             = FFX_DNSR_Reflections_LoadMotionVector(dispatch_thread_id);
        const float2      surface_reprojection_uv   = FFX_DNSR_Reflections_GetSurfaceReprojection(dispatch_thread_id, uv, motion_vector);
        const float2      hit_reprojection_uv       = FFX_DNSR_Reflections_GetHitPositionReprojection(dispatch_thread_id, uv, ray_length);
        const min16float3 surface_normal            = FFX_DNSR_Reflections_SampleWorldSpaceNormalHistory(surface_reprojection_uv);
        const min16float3 hit_normal                = FFX_DNSR_Reflections_SampleWorldSpaceNormalHistory(hit_reprojection_uv);
        const min16float3 surface_history           = FFX_DNSR_Reflections_SampleRadianceHistory(surface_reprojection_uv);
        const min16float3 hit_history               = FFX_DNSR_Reflections_SampleRadianceHistory(hit_reprojection_uv);
        const float       hit_normal_similarity     = dot(normalize((float3)hit_normal), normalize((float3)normal));
        const float       surface_normal_similarity = dot(normalize((float3)surface_normal), normalize((float3)normal));
        const min16float  hit_roughness             = FFX_DNSR_Reflections_SampleRoughnessHistory(hit_reprojection_uv);
        const min16float  surface_roughness         = FFX_DNSR_Reflections_SampleRoughnessHistory(surface_reprojection_uv);

        // Choose reprojection uv based on similarity to the local neighborhood.
        if (hit_normal_similarity > FFX_DNSR_REFLECTIONS_REPROJECTION_NORMAL_SIMILARITY_THRESHOLD  // Candidate for mirror reflection parallax
            && hit_normal_similarity + 1.0e-3 > surface_normal_similarity                          //
            && abs(hit_roughness - roughness) < abs(surface_roughness - roughness) + 1.0e-3        //
        ) {
            history_normal                 = hit_normal;
            float hit_history_depth        = FFX_DNSR_Reflections_SampleDepthHistory(hit_reprojection_uv);
            float hit_history_linear_depth = FFX_DNSR_Reflections_GetLinearDepth(hit_reprojection_uv, hit_history_depth);
            history_linear_depth           = hit_history_linear_depth;
            reprojection_uv                = hit_reprojection_uv;
            reprojection                   = hit_history;
        } else {
            // Reject surface reprojection based on simple distance
            if (dot2(surface_history - local_neighborhood.mean) <
                FFX_DNSR_REFLECTIONS_REPROJECT_SURFACE_DISCARD_VARIANCE_WEIGHT * length(local_neighborhood.variance)) {
                history_normal                     = surface_normal;
                float surface_history_depth        = FFX_DNSR_Reflections_SampleDepthHistory(surface_reprojection_uv);
                float surface_history_linear_depth = FFX_DNSR_Reflections_GetLinearDepth(surface_reprojection_uv, surface_history_depth);
                history_linear_depth               = surface_history_linear_depth;
                reprojection_uv                    = surface_reprojection_uv;
                reprojection                       = surface_history;
            } else {
                disocclusion_factor = 0.0;
                return;
            }
        }
    }
    float depth        = FFX_DNSR_Reflections_LoadDepth(dispatch_thread_id);
    float linear_depth = FFX_DNSR_Reflections_GetLinearDepth(uv, depth);
    // Determine disocclusion factor based on history
    disocclusion_factor = FFX_DNSR_Reflections_GetDisocclusionFactor(normal, history_normal, linear_depth, history_linear_depth);

    if (disocclusion_factor > FFX_DNSR_REFLECTIONS_DISOCCLUSION_THRESHOLD) // Early out, good enough
        return;

    // Try to find the closest sample in the vicinity if we are not convinced of a disocclusion
    if (disocclusion_factor < FFX_DNSR_REFLECTIONS_DISOCCLUSION_THRESHOLD) {
        float2    closest_uv    = reprojection_uv;
        float2    dudv          = 1.0 / float2(screen_size);
        const int search_radius = 1;
        for (int y = -search_radius; y <= search_radius; y++) {
            for (int x = -search_radius; x <= search_radius; x++) {
                float2      uv                   = reprojection_uv + float2(x, y) * dudv;
                min16float3 history_normal       = FFX_DNSR_Reflections_SampleWorldSpaceNormalHistory(uv);
                float       history_depth        = FFX_DNSR_Reflections_SampleDepthHistory(uv);
                float       history_linear_depth = FFX_DNSR_Reflections_GetLinearDepth(uv, history_depth);
                min16float  weight               = FFX_DNSR_Reflections_GetDisocclusionFactor(normal, history_normal, linear_depth, history_linear_depth);
                if (weight > disocclusion_factor) {
                    disocclusion_factor = weight;
                    closest_uv          = uv;
                    reprojection_uv     = closest_uv;
                }
            }
        }
        reprojection = FFX_DNSR_Reflections_SampleRadianceHistory(reprojection_uv);
    }

    // Rare slow path - triggered only on the edges.
    // Try to get rid of potential leaks at bilinear interpolation level.
    if (disocclusion_factor < FFX_DNSR_REFLECTIONS_DISOCCLUSION_THRESHOLD) {
        // If we've got a discarded history, try to construct a better sample out of 2x2 interpolation neighborhood
        // Helps quite a bit on the edges in movement
        float       uvx                    = frac(float(screen_size.x) * reprojection_uv.x + 0.5);
        float       uvy                    = frac(float(screen_size.y) * reprojection_uv.y + 0.5);
        int2        reproject_texel_coords = int2(screen_size * reprojection_uv - 0.5);
        min16float3 reprojection00         = FFX_DNSR_Reflections_LoadRadianceHistory(reproject_texel_coords + int2(0, 0));
        min16float3 reprojection10         = FFX_DNSR_Reflections_LoadRadianceHistory(reproject_texel_coords + int2(1, 0));
        min16float3 reprojection01         = FFX_DNSR_Reflections_LoadRadianceHistory(reproject_texel_coords + int2(0, 1));
        min16float3 reprojection11         = FFX_DNSR_Reflections_LoadRadianceHistory(reproject_texel_coords + int2(1, 1));
        min16float3 normal00               = FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(reproject_texel_coords + int2(0, 0));
        min16float3 normal10               = FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(reproject_texel_coords + int2(1, 0));
        min16float3 normal01               = FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(reproject_texel_coords + int2(0, 1));
        min16float3 normal11               = FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(reproject_texel_coords + int2(1, 1));
        float       depth00                = FFX_DNSR_Reflections_GetLinearDepth(reprojection_uv, FFX_DNSR_Reflections_LoadDepthHistory(reproject_texel_coords + int2(0, 0)));
        float       depth10                = FFX_DNSR_Reflections_GetLinearDepth(reprojection_uv, FFX_DNSR_Reflections_LoadDepthHistory(reproject_texel_coords + int2(1, 0)));
        float       depth01                = FFX_DNSR_Reflections_GetLinearDepth(reprojection_uv, FFX_DNSR_Reflections_LoadDepthHistory(reproject_texel_coords + int2(0, 1)));
        float       depth11                = FFX_DNSR_Reflections_GetLinearDepth(reprojection_uv, FFX_DNSR_Reflections_LoadDepthHistory(reproject_texel_coords + int2(1, 1)));
        min16float4 w                      = 1.0;
        // Initialize with occlusion weights
        w.x = FFX_DNSR_Reflections_GetDisocclusionFactor(normal, normal00, linear_depth, depth00) > FFX_DNSR_REFLECTIONS_DISOCCLUSION_THRESHOLD / 2.0 ? 1.0 : 0.0;
        w.y = FFX_DNSR_Reflections_GetDisocclusionFactor(normal, normal10, linear_depth, depth10) > FFX_DNSR_REFLECTIONS_DISOCCLUSION_THRESHOLD / 2.0 ? 1.0 : 0.0;
        w.z = FFX_DNSR_Reflections_GetDisocclusionFactor(normal, normal01, linear_depth, depth01) > FFX_DNSR_REFLECTIONS_DISOCCLUSION_THRESHOLD / 2.0 ? 1.0 : 0.0;
        w.w = FFX_DNSR_Reflections_GetDisocclusionFactor(normal, normal11, linear_depth, depth11) > FFX_DNSR_REFLECTIONS_DISOCCLUSION_THRESHOLD / 2.0 ? 1.0 : 0.0;
        // And then mix in bilinear weights
        w.x           = w.x * (1.0 - uvx) * (1.0 - uvy);
        w.y           = w.y * (uvx) * (1.0 - uvy);
        w.z           = w.z * (1.0 - uvx) * (uvy);
        w.w           = w.w * (uvx) * (uvy);
        min16float ws = max(w.x + w.y + w.z + w.w, 1.0e-3);
        // normalize
        w /= ws;

        min16float3 history_normal;
        float       history_linear_depth;
        reprojection         = reprojection00 * w.x + reprojection10 * w.y + reprojection01 * w.z + reprojection11 * w.w;
        history_linear_depth = depth00 * w.x + depth10 * w.y + depth01 * w.z + depth11 * w.w;
        history_normal       = normal00 * w.x + normal10 * w.y + normal01 * w.z + normal11 * w.w;
        disocclusion_factor  = FFX_DNSR_Reflections_GetDisocclusionFactor(normal, history_normal, linear_depth, history_linear_depth);
    }
    disocclusion_factor = disocclusion_factor < FFX_DNSR_REFLECTIONS_DISOCCLUSION_THRESHOLD ? 0.0 : disocclusion_factor;
}

void FFX_DNSR_Reflections_Reproject(int2 dispatch_thread_id, int2 group_thread_id, uint2 screen_size, float temporal_stability_factor, int max_samples) {
    FFX_DNSR_Reflections_InitializeGroupSharedMemory(dispatch_thread_id, group_thread_id, screen_size);
    GroupMemoryBarrierWithGroupSync();

    group_thread_id += 4; // Center threads in groupshared memory

    min16float       variance    = 1.0;
    min16float       num_samples = 0.0;
    min16float       roughness   = FFX_DNSR_Reflections_LoadRoughness(dispatch_thread_id);
    float3           normal      = FFX_DNSR_Reflections_LoadWorldSpaceNormal(dispatch_thread_id);
    min16float3      radiance    = FFX_DNSR_Reflections_LoadRadiance(dispatch_thread_id);
    const min16float ray_length  = FFX_DNSR_Reflections_LoadRayLength(dispatch_thread_id);

    if (FFX_DNSR_Reflections_IsGlossyReflection(roughness)) {
        min16float  disocclusion_factor;
        float2      reprojection_uv;
        min16float3 reprojection;
        FFX_DNSR_Reflections_PickReprojection(/*in*/ dispatch_thread_id,
                                              /* in */ group_thread_id,
                                              /* in */ screen_size,
                                              /* in */ roughness,
                                              /* in */ ray_length,
                                              /* out */ disocclusion_factor,
                                              /* out */ reprojection_uv,
                                              /* out */ reprojection);
        if (all(reprojection_uv > 0.0) && all(reprojection_uv < 1.0)) {
            min16float prev_variance = FFX_DNSR_Reflections_SampleVarianceHistory(reprojection_uv);
            num_samples              = FFX_DNSR_Reflections_SampleNumSamplesHistory(reprojection_uv) * disocclusion_factor;
            min16float s_max_samples = max(8.0, max_samples * FFX_DNSR_REFLECTIONS_SAMPLES_FOR_ROUGHNESS(roughness));
            num_samples              = min(s_max_samples, num_samples + 1);
            min16float new_variance  = FFX_DNSR_Reflections_ComputeTemporalVariance(radiance.xyz, reprojection.xyz);
            if (disocclusion_factor < FFX_DNSR_REFLECTIONS_DISOCCLUSION_THRESHOLD) {
                FFX_DNSR_Reflections_StoreRadianceReprojected(dispatch_thread_id, (0.0).xxx);
                FFX_DNSR_Reflections_StoreVariance(dispatch_thread_id, 1.0);
                FFX_DNSR_Reflections_StoreNumSamples(dispatch_thread_id, 1.0);
            } else {
                min16float variance_mix = lerp(new_variance, prev_variance, 1.0 / num_samples);
                FFX_DNSR_Reflections_StoreRadianceReprojected(dispatch_thread_id, reprojection);
                FFX_DNSR_Reflections_StoreVariance(dispatch_thread_id, variance_mix);
                FFX_DNSR_Reflections_StoreNumSamples(dispatch_thread_id, num_samples);
                // Mix in reprojection for radiance mip computation 
                radiance = lerp(radiance, reprojection, 0.3);
            }
        } else {
            FFX_DNSR_Reflections_StoreRadianceReprojected(dispatch_thread_id, (0.0).xxx);
            FFX_DNSR_Reflections_StoreVariance(dispatch_thread_id, 1.0);
            FFX_DNSR_Reflections_StoreNumSamples(dispatch_thread_id, 1.0);
        }
    }
    
    // Downsample 8x8 -> 1 radiance using groupshared memory
    // Initialize groupshared array for downsampling
    min16float weight = FFX_DNSR_Reflections_GetLuminanceWeight(radiance.xyz);
    radiance.xyz *= weight;
    if (any(dispatch_thread_id >= screen_size) || any(isinf(radiance)) || any(isnan(radiance)) || weight > 1.0e3) {
        radiance = (0.0).xxxx;
        weight   = 0.0;
    }

    group_thread_id -= 4; // Center threads in groupshared memory

    FFX_DNSR_Reflections_StoreInGroupSharedMemory(group_thread_id, min16float4(radiance.xyz, weight));
    GroupMemoryBarrierWithGroupSync();

    for (int i = 2; i <= 8; i = i * 2) {
        int ox = group_thread_id.x * i;
        int oy = group_thread_id.y * i;
        int ix = group_thread_id.x * i + i / 2;
        int iy = group_thread_id.y * i + i / 2;
        if (ix < 8 && iy < 8) {
            min16float4 rad_weight00 = FFX_DNSR_Reflections_LoadFromGroupSharedMemoryRaw(int2(ox, oy));
            min16float4 rad_weight10 = FFX_DNSR_Reflections_LoadFromGroupSharedMemoryRaw(int2(ox, iy));
            min16float4 rad_weight01 = FFX_DNSR_Reflections_LoadFromGroupSharedMemoryRaw(int2(ix, oy));
            min16float4 rad_weight11 = FFX_DNSR_Reflections_LoadFromGroupSharedMemoryRaw(int2(ix, iy));
            min16float4 sum          = rad_weight00 + rad_weight01 + rad_weight10 + rad_weight11;
            FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2(ox, oy), sum);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (all(group_thread_id == 0)) {
        min16float4 sum          = FFX_DNSR_Reflections_LoadFromGroupSharedMemoryRaw(int2(0, 0));
        min16float  weight_acc   = max(sum.w, 1.0e-3);
        float3      radiance_avg = sum.xyz / weight_acc;
        FFX_DNSR_Reflections_StoreAverageRadiance(dispatch_thread_id.xy / 8, radiance_avg);
    }
}

#endif // FFX_DNSR_REFLECTIONS_REPROJECT