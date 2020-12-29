//	VQE
//	Copyright(C) 2020  - Volkan Ilbeyli
//
//	This program is free software : you can redistribute it and / or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.If not, see <http://www.gnu.org/licenses/>.
//
//	Contact: volkanilbeyli@gmail.com

//==============================================================================================================================
//
// AMD FIDELITYFX 
//
//==============================================================================================================================
// LICENSE
// =======
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// -------
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// -------
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
// -------
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//------------------------------------------------------------------------------------------------------------------------------

#if 0
struct VSInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float3 tangent : TANGENT;
	float2 uv : TEXCOORD0;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 color : COLOR0;
};

cbuffer CBuffer : register(b0)
{
	float4x4 matModelViewProj;
	float3 color;
}
#endif


//--------------------------------------------------------------------------------------
//
// FidelityFX CONFIGURATION
//
//--------------------------------------------------------------------------------------
#define A_HLSL
#define A_GPU


#include "../Libs/FidelityFX/ffx_a.h"



//--------------------------------------------------------------------------------------
//
// FidelityFX CONTRAST ADAPTIVE SHARPENING (CAS)
//
//--------------------------------------------------------------------------------------
#define FFXCAS_CS 1 // TODO: provide with compilation params

#if FFXCAS_CS || FFXCAS_PS
//
// CAS App-side Defines
//
#ifndef FFXCAS_FP16
#define FFXCAS_FP16 0
#endif
#ifndef FFXCAS_NO_UPSCALING
#define FFXCAS_NO_UPSCALING 1
#endif 

//
// CAS Shader Resources
//
Texture2D           CASInputTexture  : register(t0);
RWTexture2D<float3> CASOutputTexture : register(u0);
cbuffer             CASConstants     : register(b0)
{
	uint4 CASConst0;
	uint4 CASConst1;
};

//
// CAS Defines & Callbacks
//
#if FFXCAS_FP16
	#define A_HALF 1
	#define CAS_PACKED_ONLY 1

	//
	// CAS Callbacks
	//
	AH3 CasLoadH(ASW2 p)
	{
	    return InputTexture.Load(ASU3(p, 0)).rgb;
	}
	
	// Lets you transform input from the load into a linear color space between 0 and 1. See ffx_cas.h
	// In this case, our input is already linear and between 0 and 1 so its empty
	void CasInputH(inout AH2 r, inout AH2 g, inout AH2 b) {}

#else // FP32

	AF3 CasLoad(ASU2 p)
	{
		return CASInputTexture.Load(int3(p, 0)).rgb;
	}

	// Lets you transform input from the load into a linear color space between 0 and 1. See ffx_cas.h
	// In this case, our input is already linear and between 0 and 1 so its empty
	void CasInput(inout AF1 r, inout AF1 g, inout AF1 b){}

#endif 



#include "../Libs/FidelityFX/CAS/ffx_cas.h"

//
// CAS Main
//
[numthreads(64, 1, 1)]
void CAS_CSMain(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID)
{
	// Do remapping of local xy in workgroup : 64x1 to 8x8 with rotated 2x2 pixel quads in quad linear.
    AU2 gxy = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);

	bool sharpenOnly;
#if FFXCAS_NO_UPSCALING
    sharpenOnly = true;
#else
	sharpenOnly = false;
#endif

	
#if FFXCAS_FP16
    
    // Filter.
    AH4 c0, c1;
    AH2 cR, cG, cB;
    
    CasFilterH(cR, cG, cB, gxy, const0, const1, sharpenOnly);
    CasDepack(c0, c1, cR, cG, cB);
    CASOutputTexture[ASU2(gxy)] = AF4(c0);
    CASOutputTexture[ASU2(gxy) + ASU2(8, 0)] = AF4(c1);
    gxy.y += 8u;
    
    CasFilterH(cR, cG, cB, gxy, const0, const1, sharpenOnly);
    CasDepack(c0, c1, cR, cG, cB);
    CASOutputTexture[ASU2(gxy)] = AF4(c0);
    CASOutputTexture[ASU2(gxy) + ASU2(8, 0)] = AF4(c1);
    
#else
    
    // Filter.
    AF3 c; // color
    
	CasFilter(c.r, c.g, c.b, gxy, CASConst0, CASConst1, sharpenOnly);
	CASOutputTexture[ASU2(gxy)] = AF4(c, 1);
	gxy.x += 8u;
    
	CasFilter(c.r, c.g, c.b, gxy, CASConst0, CASConst1, sharpenOnly);
	CASOutputTexture[ASU2(gxy)] = AF4(c, 1);
	gxy.y += 8u;
    
	CasFilter(c.r, c.g, c.b, gxy, CASConst0, CASConst1, sharpenOnly);
	CASOutputTexture[ASU2(gxy)] = AF4(c, 1);
	gxy.x -= 8u;
    
	CasFilter(c.r, c.g, c.b, gxy, CASConst0, CASConst1, sharpenOnly);
	CASOutputTexture[ASU2(gxy)] = AF4(c, 1);
    
#endif
#endif
}

//--------------------------------------------------------------------------------------
//
// FidelityFX - SINGLE PASS DOWNSAMPLER (SPD)
//
//--------------------------------------------------------------------------------------
#if FFXSPD_CS || FFXSPD_PS
//
// SPD Callbacks
//


#include "../Libs/FidelityFX/SPD/ffx_spd.h"
//
// SPD Main
//
[numthreads(64, 1, 1)]
void SPD_CSMain()
{
	
} 
#endif