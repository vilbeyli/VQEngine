// Modifications Copyright © 2020. Advanced Micro Devices, Inc. All Rights Reserved.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016, Intel Corporation
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated 
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation 
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of 
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
// SOFTWARE.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File changes (yyyy-mm-dd)
// 2016-09-07: filip.strugar@intel.com: first commit
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Defines for constants common to both CACAO.cpp and CACAO.hlsl

// ====================================================================
// Prepare shader dimensions

#define PREPARE_DEPTHS_AND_MIPS_WIDTH  8
#define PREPARE_DEPTHS_AND_MIPS_HEIGHT 8

#define PREPARE_DEPTHS_WIDTH  8
#define PREPARE_DEPTHS_HEIGHT 8

#define PREPARE_DEPTHS_HALF_WIDTH  8
#define PREPARE_DEPTHS_HALF_HEIGHT 8

#define PREPARE_DEPTHS_NORMALS_AND_MIPS_WIDTH  8
#define PREPARE_DEPTHS_NORMALS_AND_MIPS_HEIGHT 8

#define PREPARE_DEPTHS_AND_NORMALS_WIDTH  8
#define PREPARE_DEPTHS_AND_NORMALS_HEIGHT 8

#define PREPARE_DEPTHS_AND_NORMALS_HALF_WIDTH  8
#define PREPARE_DEPTHS_AND_NORMALS_HALF_HEIGHT 8

#define PREPARE_NORMALS_WIDTH  8
#define PREPARE_NORMALS_HEIGHT 8

#define PREPARE_NORMALS_FROM_INPUT_NORMALS_WIDTH  8
#define PREPARE_NORMALS_FROM_INPUT_NORMALS_HEIGHT 8

// ====================================================================
// Generate SSAO shader dimensions

#define GENERATE_WIDTH  8
#define GENERATE_HEIGHT 8

// ====================================================================
// Importance map shader dimensions

#define IMPORTANCE_MAP_WIDTH  8
#define IMPORTANCE_MAP_HEIGHT 8

#define IMPORTANCE_MAP_A_WIDTH  8
#define IMPORTANCE_MAP_A_HEIGHT 8

#define IMPORTANCE_MAP_B_WIDTH  8
#define IMPORTANCE_MAP_B_HEIGHT 8

// ====================================================================
// Blur shader dimensions

#define BLUR_WIDTH  16
#define BLUR_HEIGHT 16

// ====================================================================
// Apply shader dimensions

#define APPLY_WIDTH  8
#define APPLY_HEIGHT 8

// ====================================================================
// Reinterleave shader dimensions

#define REINTERLEAVE_WIDTH  16
#define REINTERLEAVE_HEIGHT 8

// ====================================================================
// Upscale

#define UPSCALE_WIDTH 8
#define UPSCALE_HEIGHT 8

#define BILATERAL_UPSCALE_WIDTH  8
#define BILATERAL_UPSCALE_HEIGHT 8