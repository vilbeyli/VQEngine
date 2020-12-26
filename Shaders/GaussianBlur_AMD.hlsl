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

#ifndef USE_LDS
#define USE_LDS 1
#endif

#if USE_LDS

#ifndef LDS_COLUMN_MAJOR
#define LDS_COLUMN_MAJOR 0
#endif


#ifndef LDS_LOAD_ROW_FROM_SINGLE_THREAD
#define LDS_LOAD_ROW_FROM_SINGLE_THREAD 0
#endif

#if LDS_COLUMN_MAJOR
groupshared float3 CachedInputRGB_X[THREADGROUP_X][THREADGROUP_Y + KERNEL_RANGE_MINUS1 * 2];
groupshared float3 CachedInputRGB_Y[THREADGROUP_X + KERNEL_RANGE_MINUS1 * 2][THREADGROUP_Y];
#else 
groupshared half3 CachedInputRGB_X[THREADGROUP_X + KERNEL_RANGE_MINUS1 * 2][THREADGROUP_Y];
groupshared half3 CachedInputRGB_Y[THREADGROUP_X][THREADGROUP_Y + KERNEL_RANGE_MINUS1 * 2];
#endif

//
// e.g. KERNEL_DIMENSION == 9 -> KERNEL_RANGE == 5
//
// 
// pxCoord : [00, ImageSizeXY-(1,1)]
void InitializeLDS(int2 pxCoord, int2 imageSize, uint2 localTID, const bool HORIZONTAL_BLUR)
{
	float3 rgb = texColorInput[pxCoord];
	
	// handle image borders
	if(HORIZONTAL_BLUR)
	{
		// init LDS data with the pixel centers
		#if LDS_COLUMN_MAJOR
		#if LDS_LOAD_ROW_FROM_SINGLE_THREAD
		//if(localTID.x == 0)
		if (localTID.y == 0)
		{
			[unroll]
			for (int y = 0; y < THREADGROUP_Y; ++y)
				CachedInputRGB_X[y][localTID.x + KERNEL_RANGE_MINUS1] = texColorInput[int2(pxCoord.x, pxCoord.y + y)];
			//[unroll]
			//for (int x = 0; x < THREADGROUP_X; ++x)
			//	CachedInputRGB_X[x + KERNEL_RANGE_MINUS1][localTID.y] = texColorInput[int2(pxCoord.x + x, pxCoord.y)];
		}
		#else
		CachedInputRGB_X[localTID.y][localTID.x + KERNEL_RANGE_MINUS1] = rgb;
		#endif
		
		#else
		
		#if LDS_LOAD_ROW_FROM_SINGLE_THREAD
		#if 0
		if(localTID.y == 0)
		{
			[unroll] for (int y = 0; y < THREADGROUP_Y; ++y)
				CachedInputRGB_X[localTID.x + KERNEL_RANGE_MINUS1][y] = texColorInput[int2(pxCoord.x, pxCoord.y + y)];
		}
		#elif 0
		if(localTID.x == 0)
		{
			[unroll] for (int x = 0; x < THREADGROUP_X; ++x)
				CachedInputRGB_X[x + KERNEL_RANGE_MINUS1][localTID.y] = texColorInput[int2(pxCoord.x + x, pxCoord.y)];
		}
		#elif 1
		#define LOCAL_FETCH_GROUP_SIZE 2
		if (localTID.x % LOCAL_FETCH_GROUP_SIZE == 0 && localTID.y % LOCAL_FETCH_GROUP_SIZE == 0)
		{
			[unroll]for (int xOffset = 0; xOffset < LOCAL_FETCH_GROUP_SIZE; ++xOffset)
			[unroll]for (int yOffset = 0; yOffset < LOCAL_FETCH_GROUP_SIZE; ++yOffset)
				CachedInputRGB_X[KERNEL_RANGE_MINUS1 + localTID.x + xOffset][localTID.y + yOffset] = texColorInput[int2(pxCoord.x + xOffset, pxCoord.y + yOffset)];
		}
		#endif
		#else
		// each thread fills its own slot on LDS
		CachedInputRGB_X[localTID.x + KERNEL_RANGE_MINUS1][localTID.y] = rgb;
		#endif
		#endif
		
		// init LDS borders
		{
			const bool bOnLeftImageBorder        =  pxCoord.x == 0;
			const bool bOnRightImageBorder       =  pxCoord.x == (imageSize.x - 1);
			const bool bOnLeftThreadgroupBorder  = localTID.x == 0;
			const bool bOnRightThreadgroupBorder = localTID.x == (THREADGROUP_X - 1);
			

			
			// handle threadgroup borders: sample from the neighboring thread group location, but from global memory
			if (bOnLeftThreadgroupBorder && !bOnLeftImageBorder)
			{
				for (int LDS_index_x = 0; LDS_index_x < KERNEL_RANGE_MINUS1; ++LDS_index_x)
				{
					int2 sampleCoord = int2(pxCoord.x - (LDS_index_x+1), pxCoord.y);
					#if LDS_COLUMN_MAJOR
					CachedInputRGB_X[localTID.y][KERNEL_RANGE_MINUS1 - 1 - LDS_index_x] = texColorInput[sampleCoord].rgb;
					#else
					CachedInputRGB_X[KERNEL_RANGE_MINUS1 - 1 - LDS_index_x][localTID.y] = texColorInput[sampleCoord].rgb;
					#endif
				}
			} 
			else if (bOnRightThreadgroupBorder && !bOnRightImageBorder)
			{
				for (int LDS_index_x = 0; LDS_index_x < KERNEL_RANGE_MINUS1; ++LDS_index_x)
				{
					int2 sampleCoord = int2(pxCoord.x + (LDS_index_x+1), pxCoord.y);
					#if LDS_COLUMN_MAJOR
					CachedInputRGB_X[localTID.y][KERNEL_RANGE_MINUS1 + THREADGROUP_X + LDS_index_x] = texColorInput[sampleCoord].rgb;
					#else
					CachedInputRGB_X[KERNEL_RANGE_MINUS1 + THREADGROUP_X + LDS_index_x][localTID.y] = texColorInput[sampleCoord].rgb;
					#endif
				}
			}
			
			
			
			// handle image borders: fill with border color
			if (bOnLeftImageBorder) 
			{
				for (int LDS_index_x = 0; LDS_index_x < KERNEL_RANGE_MINUS1; ++LDS_index_x)
					#if LDS_COLUMN_MAJOR
					CachedInputRGB_X[localTID.y][LDS_index_x] = rgb;
					#else
					CachedInputRGB_X[LDS_index_x][localTID.y] = rgb;
					#endif
			}
			if (bOnRightImageBorder) // right border
			{
				for (int LDS_index_x = 0; LDS_index_x < KERNEL_RANGE_MINUS1; ++LDS_index_x)
					#if LDS_COLUMN_MAJOR
					CachedInputRGB_X[localTID.y][KERNEL_RANGE_MINUS1 + THREADGROUP_X + LDS_index_x] = rgb;
					#else
					CachedInputRGB_X[KERNEL_RANGE_MINUS1 + THREADGROUP_X + LDS_index_x][localTID.y] = rgb;
					#endif
			}
		}
	}
	else
	{
		// TODO
	}
	
	GroupMemoryBarrierWithGroupSync();
}
float3 SampleLDS(int2 sampleCoord, int2 pxCoord, int2 imageSize, uint2 localTID, const bool HORIZONTAL_BLUR)
{
	float3 color = 0.0f.xxx;
	
	// TODO: pxCoord -> LDS index
	
	if (HORIZONTAL_BLUR)
	{
		int kernelOffset = sampleCoord.x - pxCoord.x; // [-KERNEL_RANGE_MINUS1, KERNEL_RANGE_MINUS1] e.g. [-4, 4] for a 9-wide kernel
		#if LDS_COLUMN_MAJOR
		color = CachedInputRGB_X[localTID.y][localTID.x + KERNEL_RANGE_MINUS1 + kernelOffset];
		#else
		color = CachedInputRGB_X[localTID.x + KERNEL_RANGE_MINUS1 + kernelOffset][localTID.y];
		#endif
	}
	else
	{
		
	}
	
	return color;
}
#endif

#define A_HLSL 1
#define A_GPU  1
#include "ffx_a.h"

//
// ENTRY POINT
//
//[numthreads(THREADGROUP_X, THREADGROUP_Y, 1)]
[numthreads(64, 1, 1)]
void CSMain_X(
    uint3 LocalThreadId    : SV_GroupThreadID
  , uint3 WorkGroupId      : SV_GroupID
  , uint3 DispatchThreadID : SV_DispatchThreadID
) 
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

	
	AU2 px = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 3u, WorkGroupId.y << 3u);
	//int2 pxCoord = DispatchThreadID.xy;
	int2 pxCoord = px;
	
	// early out for out-of-image-bounds
	//if(pxCoord.x >= iImageSize.x || pxCoord.y >= iImageSize.y)
	//	return; 
	
	float4 InRGBA = texColorInput[pxCoord];
	//float3 OutRGB = InRGBA.brg;
	
	#if USE_LDS
	AU2 LocalTID = ARmp8x8(LocalThreadId.x);
	InitializeLDS(pxCoord, iImageSize, LocalTID.xy, true);
	#endif
	
	float3 OutRGB = 0.0.xxx;
	#if 0
	for (int sampleCoordx = pxCoord.x - KERNEL_RANGE_MINUS1; sampleCoordx < pxCoord.x + KERNEL_RANGE; ++sampleCoordx)
	{
		int2 sampleCoord = int2(sampleCoordx, pxCoord.y);
		int  kernelIndex = abs(sampleCoordx - pxCoord.x);
	#else
	[unroll]
	for (int kernelIt = 0; kernelIt < KERNEL_DIMENSION; ++kernelIt)
	{
		int kernelOffset = kernelIt - KERNEL_RANGE_MINUS1;
		int kernelIndex = abs(kernelOffset);
		int2 sampleCoord = int2(pxCoord.x + kernelOffset, pxCoord.y);
	#endif
		
#if USE_LDS
		float3 sampledColor = SampleLDS(sampleCoord, pxCoord, iImageSize, /*LocalThreadId.xy*/LocalTID.xy, true);
#else
		float3 sampledColor = texColorInput[sampleCoord];
#endif
		
		OutRGB += sampledColor * KERNEL_WEIGHTS[kernelIndex];
	}
	
	texColorOutput[pxCoord] = float4(OutRGB, 1.0f);
}


//[numthreads(8, 8, 1)] 
[numthreads(64, 1, 1)] 
void CSMain_Y(
    uint3 LocalThreadId    : SV_GroupThreadID
  , uint3 WorkGroupId      : SV_GroupID
  , uint3 DispatchThreadID : SV_DispatchThreadID
)
{
	AU2 px = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 3u, WorkGroupId.y << 3u);
	//int2 pxCoord = DispatchThreadID.xy;
	int2 pxCoord = px;
	
	// early out for out-of-image-bounds
	if (pxCoord.x >= iImageSize.x || pxCoord.y >= iImageSize.y)
		return;
	
	float4 InRGBA = texColorInput[pxCoord];
	//float3 OutRGB = InRGBA.gbr;
	float3 OutRGB = InRGBA.rgb;
	//float3 OutRGB = float3(LocalThreadId.xy / 7.0f.xx, 0.0f);
	//float3 OutRGB = float3(DispatchThreadID.xy / float2(iImageSize.x - 1, iImageSize.y - 1), 0.0f);
	
	//#if USE_LDS
	//InitializeLDS(pxCoord, InRGBA.rgb, iImageSize, false);
	//#endif
	
	//float3 OutRGB = 0.0.xxx;
	//for (int sampleCoordy = pxCoord.y - KERNEL_RANGE; sampleCoordy < pxCoord.y + KERNEL_RANGE; ++sampleCoordy)
	//{
	//	int2 sampleCoord = int2(pxCoord.x, sampleCoordy);
	//	int  kernelIndex = abs(sampleCoordy - pxCoord.y);
	//	OutRGB += texColorInput[sampleCoord] * KERNEL_WEIGHTS[kernelIndex];
	//}
	
	texColorOutput[pxCoord] = float4(OutRGB, InRGBA.a);
}