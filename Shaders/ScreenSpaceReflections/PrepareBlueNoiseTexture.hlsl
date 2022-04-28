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

Buffer<uint> g_sobol_buffer                                           : register(t0);
Buffer<uint> g_ranking_tile_buffer                                    : register(t1);
Buffer<uint> g_scrambling_tile_buffer                                 : register(t2);

RWTexture2D<float2> g_blue_noise_texture                              : register(u0);

#define GOLDEN_RATIO                       1.61803398875f

// Blue Noise Sampler by Eric Heitz. Returns a value in the range [0, 1].
float SampleRandomNumber(uint pixel_i, uint pixel_j, uint sample_index, uint sample_dimension) {
    // Wrap arguments
    pixel_i = pixel_i & 127u;
    pixel_j = pixel_j & 127u;
    sample_index = sample_index & 255u;
    sample_dimension = sample_dimension & 255u;

    // xor index based on optimized ranking
    const uint ranked_sample_index = sample_index ^ g_ranking_tile_buffer[sample_dimension + (pixel_i + pixel_j * 128u) * 8u];

    // Fetch value in sequence
    uint value = g_sobol_buffer[sample_dimension + ranked_sample_index * 256u];

    // If the dimension is optimized, xor sequence value based on optimized scrambling
    value = value ^ g_scrambling_tile_buffer[(sample_dimension % 8u) + (pixel_i + pixel_j * 128u) * 8u];

    // Convert to float and return
    return (value + 0.5f) / 256.0f;
}

float2 SampleRandomVector2D(uint2 pixel) {
    float2 u = float2(
        fmod(SampleRandomNumber(pixel.x, pixel.y, 0, 0u) + (g_frame_index & 0xFFu) * GOLDEN_RATIO, 1.0f),
        fmod(SampleRandomNumber(pixel.x, pixel.y, 0, 1u) + (g_frame_index & 0xFFu) * GOLDEN_RATIO, 1.0f));
    return u;
}

[numthreads(8, 8, 1)]
void CSMain(uint2 dispatch_thread_id : SV_DispatchThreadID) {
    g_blue_noise_texture[dispatch_thread_id] = SampleRandomVector2D(dispatch_thread_id);
}