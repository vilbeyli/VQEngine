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
#include "../BRDF.hlsl"

Texture2D<float4> g_lit_scene                                         : register(t0);
Texture2D<float> g_depth_buffer_hierarchy                             : register(t1);
Texture2D<float4> g_normal                                            : register(t2);
Texture2D<float> g_roughness                                          : register(t3);
Texture2D<float2> g_blue_noise_texture                                : register(t4);
Buffer<uint> g_ray_list                                               : register(t5);

SamplerState g_environment_map_sampler                                : register(s0);
TextureCube g_environment_map                                         : register(t0, space1);
Texture2D texBRDFIntegrationLUT                                       : register(t1, space1);

RWTexture2D<float4> g_intersection_output                             : register(u0);
RWBuffer<uint> g_ray_counter                                          : register(u1);

#define M_PI                               3.14159265358979f

float3 FFX_SSSR_LoadWorldSpaceNormal(int2 pixel_coordinate) {
    return normalize(2 * g_normal.Load(int3(pixel_coordinate, 0)).xyz - 1);
}

float FFX_SSSR_LoadDepth(int2 pixel_coordinate, int mip) {
    return g_depth_buffer_hierarchy.Load(int3(pixel_coordinate, mip));
}

float3 FFX_SSSR_ScreenSpaceToViewSpace(float3 screen_space_position) {
    return InvProjectPosition(screen_space_position, g_inv_proj);
}

float3 ScreenSpaceToWorldSpace(float3 screen_space_position) {
    return InvProjectPosition(screen_space_position, g_inv_view_proj);
}

// http://jcgt.org/published/0007/04/01/paper.pdf by Eric Heitz
// Input Ve: view direction
// Input alpha_x, alpha_y: roughness parameters
// Input U1, U2: uniform random numbers
// Output Ne: normal sampled with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z
float3 SampleGGXVNDF(float3 Ve, float alpha_x, float alpha_y, float U1, float U2) {
    // Section 3.2: transforming the view direction to the hemisphere configuration
    float3 Vh = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1, 0, 0);
    float3 T2 = cross(Vh, T1);
    // Section 4.2: parameterization of the projected area
    float r = sqrt(U1);
    float phi = 2.0 * M_PI * U2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
    // Section 4.3: reprojection onto hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    // Section 3.4: transforming the normal back to the ellipsoid configuration
    float3 Ne = normalize(float3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
    return Ne;
}

float3 Sample_GGX_VNDF_Ellipsoid(float3 Ve, float alpha_x, float alpha_y, float U1, float U2) {
    return SampleGGXVNDF(Ve, alpha_x, alpha_y, U1, U2);
}

float3 Sample_GGX_VNDF_Hemisphere(float3 Ve, float alpha, float U1, float U2) {
    return Sample_GGX_VNDF_Ellipsoid(Ve, alpha, alpha, U1, U2);
}

float3x3 CreateTBN(float3 N) {
    float3 U;
    if (abs(N.z) > 0.0) {
        float k = sqrt(N.y * N.y + N.z * N.z);
        U.x = 0.0; U.y = -N.z / k; U.z = N.y / k;
    }
    else {
        float k = sqrt(N.x * N.x + N.y * N.y);
        U.x = N.y / k; U.y = -N.x / k; U.z = 0.0;
    }

    float3x3 TBN;
    TBN[0] = U;
    TBN[1] = cross(N, U);
    TBN[2] = N;
    return transpose(TBN);
}

float2 SampleRandomVector2D(uint2 pixel) {
    return g_blue_noise_texture.Load(int3(pixel.xy % 128, 0));
}

float3 SampleReflectionVector(float3 view_direction, float3 normal, float roughness, int2 dispatch_thread_id) {
    float3x3 tbn_transform = CreateTBN(normal);
    float3 view_direction_tbn = mul(-view_direction, tbn_transform);

    float2 u = SampleRandomVector2D(dispatch_thread_id);
    
    float3 sampled_normal_tbn = Sample_GGX_VNDF_Hemisphere(view_direction_tbn, roughness, u.x, u.y);
    #ifdef PERFECT_REFLECTIONS
        sampled_normal_tbn = float3(0, 0, 1); // Overwrite normal sample to produce perfect reflection.
    #endif
    
    float3 reflected_direction_tbn = reflect(-view_direction_tbn, sampled_normal_tbn);

    // Transform reflected_direction back to the initial space.
    float3x3 inv_tbn_transform = transpose(tbn_transform);
    return mul(reflected_direction_tbn, inv_tbn_transform);
}

float3 SampleEnvironmentMap(float3 direction, float NdotV, float roughness) {
    float3 preFilteredSpecular = g_environment_map.SampleLevel(g_environment_map_sampler, direction, 0).xyz;
    float2 F0ScaleBias = texBRDFIntegrationLUT.SampleLevel(g_environment_map_sampler, float2(NdotV, roughness), 0).rg;
    return EnvironmentBRDF(NdotV, roughness, 1.0f, float3(0, 0, 0), float3(0, 0, 0), preFilteredSpecular, F0ScaleBias);
}

bool IsMirrorReflection(float roughness) {
    return roughness < 0.041;
}

#include "../AMDFidelityFX/SSSR/ffx_sssr.h"

[numthreads(8, 8, 1)]
void CSMain(uint group_index : SV_GroupIndex, uint group_id : SV_GroupID) {
    
    uint ray_index = group_id * 64 + group_index;
    if (ray_index >= g_ray_counter[1]) return;
    uint packed_coords = g_ray_list[ray_index];
    
    int2 coords;
    bool copy_horizontal;
    bool copy_vertical;
    bool copy_diagonal;
    UnpackRayCoords(packed_coords, coords, copy_horizontal, copy_vertical, copy_diagonal);

    const uint2 screen_size = g_buffer_dimensions;

    float2 uv = (coords + 0.5) * g_inv_buffer_dimensions;

    float3 world_space_normal = FFX_SSSR_LoadWorldSpaceNormal(coords);
    float roughness = g_roughness.Load(int3(coords, 0));
    bool is_mirror = IsMirrorReflection(roughness);

    int most_detailed_mip = is_mirror ? 0 : g_most_detailed_mip;
    float2 mip_resolution = FFX_SSSR_GetMipResolution(screen_size, most_detailed_mip);
    float z = FFX_SSSR_LoadDepth(uv * mip_resolution, most_detailed_mip);

    float3 screen_uv_space_ray_origin = float3(uv, z);
    float3 view_space_ray = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
    float3 view_space_ray_direction = normalize(view_space_ray);

    float3 view_space_surface_normal = mul(g_view, float4(world_space_normal, 0)).xyz;
    float3 view_space_reflected_direction = SampleReflectionVector(view_space_ray_direction, view_space_surface_normal, roughness, coords);
    float3 screen_space_ray_direction = ProjectDirection(view_space_ray, view_space_reflected_direction, screen_uv_space_ray_origin, g_proj);
    
    //====SSSR====
    bool valid_hit = false;
    float3 hit = FFX_SSSR_HierarchicalRaymarch(screen_uv_space_ray_origin, screen_space_ray_direction, is_mirror, screen_size, most_detailed_mip, g_min_traversal_occupancy, g_max_traversal_intersections, valid_hit);

    float3 world_space_origin   = ScreenSpaceToWorldSpace(screen_uv_space_ray_origin);
    float3 world_space_hit      = ScreenSpaceToWorldSpace(hit);
    float3 world_space_ray      = world_space_hit - world_space_origin.xyz;

    float confidence = valid_hit ? FFX_SSSR_ValidateHit(hit, uv, world_space_ray, screen_size, g_depth_buffer_thickness) : 0;
    float world_ray_length = max(0, length(world_space_ray));

    float3 reflection_radiance = 0;
    if (confidence > 0) {
        // Found an intersection with the depth buffer -> We can lookup the color from lit scene.
        reflection_radiance = g_lit_scene.Load(int3(screen_size * hit.xy, 0)).xyz;  
    }

    // Sample environment map.
    float3 world_space_reflected_direction = mul(g_inv_view, float4(view_space_reflected_direction, 0)).xyz;
    float3 environment_lookup = SampleEnvironmentMap(world_space_reflected_direction, saturate(dot(view_space_surface_normal, view_space_ray_direction)), roughness);
    reflection_radiance = lerp(environment_lookup, reflection_radiance, confidence);

    float4 new_sample = float4(reflection_radiance, world_ray_length);
    g_intersection_output[coords] = new_sample;

    uint2 copy_target = coords ^ 0b1; // Flip last bit to find the mirrored coords along the x and y axis within a quad.
    if (copy_horizontal) {
        uint2 copy_coords = uint2(copy_target.x, coords.y);
        g_intersection_output[copy_coords] = new_sample;
    }
    if (copy_vertical) {
        uint2 copy_coords = uint2(coords.x, copy_target.y);
        g_intersection_output[copy_coords] = new_sample;
    }
    if (copy_diagonal) {
        uint2 copy_coords = copy_target;
        g_intersection_output[copy_coords] = new_sample;
    }
}