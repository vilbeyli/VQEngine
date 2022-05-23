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

RWBuffer<uint> g_ray_counter      : register(u0);
RWBuffer<uint> g_intersect_args   : register(u1);

[numthreads(1, 1, 1)]
void CSMain() {
    { // Prepare intersection args
        uint ray_count = g_ray_counter[0];
        
        g_intersect_args[0] = (ray_count + 63) / 64;
        g_intersect_args[1] = 1;
        g_intersect_args[2] = 1;

        g_ray_counter[0] = 0;
        g_ray_counter[1] = ray_count;
    }
    { // Prepare denoiser args
        uint tile_count = g_ray_counter[2];
    
        g_intersect_args[3] = tile_count;
        g_intersect_args[4] = 1;
        g_intersect_args[5] = 1;

        g_ray_counter[2] = 0;
        g_ray_counter[3] = tile_count;
    }
}