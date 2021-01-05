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


//
// GAUSSIAN BLUR 1D KERNELS  
//
//---------------------------------------------------------------------------------------------------------------------------------- 
// src: http://dev.theomader.com/gaussian-kernel-calculator/
// Sigma      : 1.7515
// Kernel Size: [9, 21]: odd numbers
//
// App should define e.g the following for 5x5 blur:
// #define KERNEL_DIMENSION 5 
#ifndef KERNEL_DIMENSION 
#define KERNEL_DIMENSION 21
#endif
//----------------------------------------------------------------------------------------------------------------------------------


// KERNEL_RANGE is this: center + half width of the kernel
//
// consider a blur kernel 5x5 - '*' indicates the center of the kernel
//
// x   x   x   x   x
// x   x   x   x   x
// x   x   x*  x   x
// x   x   x   x   x
// x   x   x   x   x
//
//
// separate into 1D kernels
//
// x   x   x*  x   x
//         ^-------^
//        KERNEL_RANGE
//
#ifndef KERNEL_RANGE
#define KERNEL_RANGE  ((KERNEL_DIMENSION - 1) / 2) + 1
#define KERNEL_RANGE_MINUS1 (KERNEL_RANGE-1)
#endif

//
// RESOURCE BINDING
//

Texture2D           texColorInput;
RWTexture2D<float4> texColorOutput;

cbuffer BlurParameters : register(b0)
{
	int2 iImageSize;
}


#define THREADGROUP_X 8
#define THREADGROUP_Y 8

float3 Convolve(float3 rgb, int iKernelIndex)
{
const float KERNEL_WEIGHTS[KERNEL_RANGE] =
//----------------------------------------------------------------------------------------------------------------------------------
#if KERNEL_RANGE == 2
{ 0.369459, 0.31527 };
#endif
#if KERNEL_RANGE == 3
{ 0.265458, 0.226523, 0.140748 };
#endif
#if KERNEL_RANGE == 4
{ 0.235473, 0.200936, 0.12485, 0.056477 };
#endif
#if KERNEL_RANGE == 5
#if USE_LEARNOPENGL_KERNEL
		{ 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
#else
		{ 0.22703, 0.193731, 0.120373, 0.054452, 0.017929 };
#endif
#endif
#if KERNEL_RANGE == 6
{ 0.225096, 0.192081, 0.119348, 0.053988, 0.017776, 0.004259 };
#endif
#if KERNEL_RANGE == 7
{ 0.224762, 0.191796, 0.119171, 0.053908, 0.01775, 0.004253, 0.000741 };
#endif
#if KERNEL_RANGE == 8
{ 0.22472, 0.19176, 0.119148, 0.053898, 0.017747, 0.004252, 0.000741, 0.000094 };
#endif
#if KERNEL_RANGE == 9
{ 0.224716, 0.191757, 0.119146, 0.053897, 0.017746, 0.004252, 0.000741, 0.000094, 0.000009 };
#endif
#if KERNEL_RANGE == 10
{ 0.224716, 0.191756, 0.119146, 0.053897, 0.017746, 0.004252, 0.000741, 0.000094, 0.000009, 0.000001 };
#endif
#if KERNEL_RANGE == 11
{ 0.224716, 0.191756, 0.119146, 0.053897, 0.017746, 0.004252, 0.000741, 0.000094, 0.000009, 0.000001, 0 };
#endif
//----------------------------------------------------------------------------------------------------------------------------------
	return rgb * KERNEL_WEIGHTS[iKernelIndex];
}

//
// ENTRY POINT
//
[numthreads(THREADGROUP_X, THREADGROUP_Y, 1)]
void CSMain_X(
    uint3 LocalThreadId    : SV_GroupThreadID
  , uint3 WorkGroupId      : SV_GroupID
  , uint3 DispatchThreadID : SV_DispatchThreadID
) 
{
	int2 pxCoord = DispatchThreadID.xy;
	
	// early out for out-of-image-bounds
	if(pxCoord.x >= iImageSize.x || pxCoord.y >= iImageSize.y)
		return; 
	
	float4 InRGBA = texColorInput[pxCoord];
	//float3 OutRGB = InRGBA.brg;
	
	float3 OutRGB = 0.0.xxx;

	[unroll]
	for (int kernelIt = 0; kernelIt < KERNEL_DIMENSION; ++kernelIt)
	{
		int kernelOffset = kernelIt - KERNEL_RANGE_MINUS1; // [-(KERNEL_RANGE-1), KERNEL_RANGE-1]
		int kernelIndex = abs(kernelOffset); // [0, KERNEL_RANGE]
		int2 sampleCoord = int2(pxCoord.x + kernelOffset, pxCoord.y);
		
		sampleCoord.x = clamp(sampleCoord.x, 0, iImageSize.x - 1);
		
		float3 sampledColor = texColorInput[sampleCoord];
		OutRGB += Convolve(sampledColor, kernelIndex);
	}
	
	texColorOutput[pxCoord] = float4(OutRGB, 1.0f);
}


[numthreads(8, 8, 1)]
void CSMain_Y(
    uint3 LocalThreadId    : SV_GroupThreadID
  , uint3 WorkGroupId      : SV_GroupID
  , uint3 DispatchThreadID : SV_DispatchThreadID
)
{
	int2 pxCoord = DispatchThreadID.xy;
	
	// early out for out-of-image-bounds
	if (pxCoord.x >= iImageSize.x || pxCoord.y >= iImageSize.y)
		return;
	
	float4 InRGBA = texColorInput[pxCoord];
	//float3 OutRGB = InRGBA.gbr;
	float3 OutRGB = 0.0f;
	//float3 OutRGB = float3(LocalThreadId.xy / 7.0f.xx, 0.0f);
	//float3 OutRGB = float3(DispatchThreadID.xy / float2(iImageSize.x - 1, iImageSize.y - 1), 0.0f);
	
	[unroll]
	for (int kernelIt = 0; kernelIt < KERNEL_DIMENSION; ++kernelIt) // [0, KERNEL_DIMENSION)
	{
		int kernelOffset = kernelIt - KERNEL_RANGE_MINUS1; // [-(KERNEL_RANGE-1), KERNEL_RANGE-1]
		int kernelIndex = abs(kernelOffset); // [0, KERNEL_RANGE]
		int2 sampleCoord = int2(pxCoord.x, pxCoord.y + kernelOffset);
		
		sampleCoord.y = clamp(sampleCoord.y, 0, iImageSize.y - 1);
		
		float3 sampledColor = texColorInput[sampleCoord];
		OutRGB += Convolve(sampledColor, kernelIndex);
	}
	
	texColorOutput[pxCoord] = float4(OutRGB, 1.0f);
}