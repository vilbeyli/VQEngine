// Modifications Copyright  2020. Advanced Micro Devices, Inc. All Rights Reserved.

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


#include "ffx_cacao_defines.h"

#define SSAO_ENABLE_NORMAL_WORLD_TO_VIEW_CONVERSION 1

#define INTELSSAO_MAIN_DISK_SAMPLE_COUNT (32)

struct CACAOConstants
{
	float2                  DepthUnpackConsts;
	float2                  CameraTanHalfFOV;

	float2                  NDCToViewMul;
	float2                  NDCToViewAdd;

	float2                  DepthBufferUVToViewMul;
	float2                  DepthBufferUVToViewAdd;

	float                   EffectRadius;                           // world (viewspace) maximum size of the shadow
	float                   EffectShadowStrength;                   // global strength of the effect (0 - 5)
	float                   EffectShadowPow;
	float                   EffectShadowClamp;

	float                   EffectFadeOutMul;                       // effect fade out from distance (ex. 25)
	float                   EffectFadeOutAdd;                       // effect fade out to distance   (ex. 100)
	float                   EffectHorizonAngleThreshold;            // limit errors on slopes and caused by insufficient geometry tessellation (0.05 to 0.5)
	float                   EffectSamplingRadiusNearLimitRec;          // if viewspace pixel closer than this, don't enlarge shadow sampling radius anymore (makes no sense to grow beyond some distance, not enough samples to cover everything, so just limit the shadow growth; could be SSAOSettingsFadeOutFrom * 0.1 or less)

	float                   DepthPrecisionOffsetMod;
	float                   NegRecEffectRadius;                     // -1.0 / EffectRadius
	float                   LoadCounterAvgDiv;                      // 1.0 / ( halfDepthMip[SSAO_DEPTH_MIP_LEVELS-1].sizeX * halfDepthMip[SSAO_DEPTH_MIP_LEVELS-1].sizeY )
	float                   AdaptiveSampleCountLimit;

	float                   InvSharpness;
	int                     PassIndex;
	float                   BilateralSigmaSquared;
	float                   BilateralSimilarityDistanceSigma;

	float4                  PatternRotScaleMatrices[5];

	float                   NormalsUnpackMul;
	float                   NormalsUnpackAdd;
	float                   DetailAOStrength;
	float                   Dummy0;

	float2                  SSAOBufferDimensions;
	float2                  SSAOBufferInverseDimensions;

	float2                  DepthBufferDimensions;
	float2                  DepthBufferInverseDimensions;

	int2                    DepthBufferOffset;
	float2                  PerPassFullResUVOffset;

	float2                  OutputBufferDimensions;
	float2                  OutputBufferInverseDimensions;

	float2                  ImportanceMapDimensions;
	float2                  ImportanceMapInverseDimensions;

	float2                  DeinterleavedDepthBufferDimensions;
	float2                  DeinterleavedDepthBufferInverseDimensions;

	float2                  DeinterleavedDepthBufferOffset;
	float2                  DeinterleavedDepthBufferNormalisedOffset;

#if SSAO_ENABLE_NORMAL_WORLD_TO_VIEW_CONVERSION
	float4x4                NormalsWorldToViewspaceMatrix;
#endif
};

static const float4 g_samplePatternMain[INTELSSAO_MAIN_DISK_SAMPLE_COUNT] =
{
	 0.78488064,  0.56661671,  1.500000, -0.126083,     0.26022232, -0.29575172,  1.500000, -1.064030,     0.10459357,  0.08372527,  1.110000, -2.730563,    -0.68286800,  0.04963045,  1.090000, -0.498827,
	-0.13570161, -0.64190155,  1.250000, -0.532765,    -0.26193795, -0.08205118,  0.670000, -1.783245,    -0.61177456,  0.66664219,  0.710000, -0.044234,     0.43675563,  0.25119025,  0.610000, -1.167283,
	 0.07884444,  0.86618668,  0.640000, -0.459002,    -0.12790935, -0.29869005,  0.600000, -1.729424,    -0.04031125,  0.02413622,  0.600000, -4.792042,     0.16201244, -0.52851415,  0.790000, -1.067055,
	-0.70991218,  0.47301072,  0.640000, -0.335236,     0.03277707, -0.22349690,  0.600000, -1.982384,     0.68921727,  0.36800742,  0.630000, -0.266718,     0.29251814,  0.37775412,  0.610000, -1.422520,
	-0.12224089,  0.96582592,  0.600000, -0.426142,     0.11071457, -0.16131058,  0.600000, -2.165947,     0.46562141, -0.59747696,  0.600000, -0.189760,    -0.51548797,  0.11804193,  0.600000, -1.246800,
	 0.89141309, -0.42090443,  0.600000,  0.028192,    -0.32402530, -0.01591529,  0.600000, -1.543018,     0.60771245,  0.41635221,  0.600000, -0.605411,     0.02379565, -0.08239821,  0.600000, -3.809046,
	 0.48951152, -0.23657045,  0.600000, -1.189011,    -0.17611565, -0.81696892,  0.600000, -0.513724,    -0.33930185, -0.20732205,  0.600000, -1.698047,    -0.91974425,  0.05403209,  0.600000,  0.062246,
	-0.15064627, -0.14949332,  0.600000, -1.896062,     0.53180975, -0.35210401,  0.600000, -0.758838,     0.41487166,  0.81442589,  0.600000, -0.505648,    -0.24106961, -0.32721516,  0.600000, -1.665244
};

#define SSAO_MAX_TAPS (32)
#define SSAO_MAX_REF_TAPS (512)
#define SSAO_ADAPTIVE_TAP_BASE_COUNT (5)
#define SSAO_ADAPTIVE_TAP_FLEXIBLE_COUNT (SSAO_MAX_TAPS - SSAO_ADAPTIVE_TAP_BASE_COUNT)
#define SSAO_DEPTH_MIP_LEVELS (4)

// these values can be changed (up to SSAO_MAX_TAPS) with no changes required elsewhere; values for 4th and 5th preset are ignored but array needed to avoid compilation errors
// the actual number of texture samples is two times this value (each "tap" has two symmetrical depth texture samples)
static const uint g_numTaps[5] = { 3, 5, 12, 0, 0 };


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Optional parts that can be enabled for a required quality preset level and above (0 == Low, 1 == Medium, 2 == High, 3 == Highest/Adaptive, 4 == reference/unused )
// Each has its own cost. To disable just set to 5 or above.
//
// (experimental) tilts the disk (although only half of the samples!) towards surface normal; this helps with effect uniformity between objects but reduces effect distance and has other side-effects
#define SSAO_TILT_SAMPLES_ENABLE_AT_QUALITY_PRESET                      (99)        // to disable simply set to 99 or similar
#define SSAO_TILT_SAMPLES_AMOUNT                                        (0.4)
//
#define SSAO_HALOING_REDUCTION_ENABLE_AT_QUALITY_PRESET                 (1)         // to disable simply set to 99 or similar
#define SSAO_HALOING_REDUCTION_AMOUNT                                   (0.6)       // values from 0.0 - 1.0, 1.0 means max weighting (will cause artifacts, 0.8 is more reasonable)
//
#define SSAO_NORMAL_BASED_EDGES_ENABLE_AT_QUALITY_PRESET                (2) //2        // to disable simply set to 99 or similar
#define SSAO_NORMAL_BASED_EDGES_DOT_THRESHOLD                           (0.5)       // use 0-0.1 for super-sharp normal-based edges
//
#define SSAO_DETAIL_AO_ENABLE_AT_QUALITY_PRESET                         (1) //1         // whether to use DetailAOStrength; to disable simply set to 99 or similar
//
#define SSAO_DEPTH_MIPS_ENABLE_AT_QUALITY_PRESET                        (2)         // !!warning!! the MIP generation on the C++ side will be enabled on quality preset 2 regardless of this value, so if changing here, change the C++ side too
#define SSAO_DEPTH_MIPS_GLOBAL_OFFSET                                   (-4.3)      // best noise/quality/performance tradeoff, found empirically
//
// !!warning!! the edge handling is hard-coded to 'disabled' on quality level 0, and enabled above, on the C++ side; while toggling it here will work for 
// testing purposes, it will not yield performance gains (or correct results)
#define SSAO_DEPTH_BASED_EDGES_ENABLE_AT_QUALITY_PRESET                 (1)     
//
#define SSAO_REDUCE_RADIUS_NEAR_SCREEN_BORDER_ENABLE_AT_QUALITY_PRESET  (99)        // 99 means disabled; only helpful if artifacts at the edges caused by lack of out of screen depth data are not acceptable with the depth sampler in either clamp or mirror modes
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SamplerState              g_PointClampSampler        : register(s0);   // corresponds to SSAO_SAMPLERS_SLOT0
SamplerState              g_PointMirrorSampler       : register(s1);   // corresponds to SSAO_SAMPLERS_SLOT2
SamplerState              g_LinearClampSampler       : register(s2);   // corresponds to SSAO_SAMPLERS_SLOT1
SamplerState              g_ViewspaceDepthTapSampler : register(s3);   // corresponds to SSAO_SAMPLERS_SLOT3
SamplerState              g_ZeroTextureSampler       : register(s4);

cbuffer SSAOConstantsBuffer                          : register(b0)    // corresponds to SSAO_CONSTANTS_BUFFERSLOT
{
	CACAOConstants        g_CACAOConsts;
}


RWTexture1D<uint> g_ClearLoadCounterInput : register(u0);
[numthreads(1, 1, 1)]
void CSClearLoadCounter()
{
	g_ClearLoadCounterInput[0] = 0;
}

// packing/unpacking for edges; 2 bits per edge mean 4 gradient values (0, 0.33, 0.66, 1) for smoother transitions!
float PackEdges(float4 edgesLRTB)
{
	//    int4 edgesLRTBi = int4( saturate( edgesLRTB ) * 3.0 + 0.5 );
	//    return ( (edgesLRTBi.x << 6) + (edgesLRTBi.y << 4) + (edgesLRTBi.z << 2) + (edgesLRTBi.w << 0) ) / 255.0;

		// optimized, should be same as above
	edgesLRTB = round(saturate(edgesLRTB) * 3.05);
	return dot(edgesLRTB, float4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

float4 UnpackEdges(float _packedVal)
{
	uint packedVal = (uint)(_packedVal * 255.5);
	float4 edgesLRTB;
	edgesLRTB.x = float((packedVal >> 6) & 0x03) / 3.0;          // there's really no need for mask (as it's an 8 bit input) but I'll leave it in so it doesn't cause any trouble in the future
	edgesLRTB.y = float((packedVal >> 4) & 0x03) / 3.0;
	edgesLRTB.z = float((packedVal >> 2) & 0x03) / 3.0;
	edgesLRTB.w = float((packedVal >> 0) & 0x03) / 3.0;

	return saturate(edgesLRTB + g_CACAOConsts.InvSharpness);
}

float ScreenSpaceToViewSpaceDepth(float screenDepth)
{
	float depthLinearizeMul = g_CACAOConsts.DepthUnpackConsts.x;
	float depthLinearizeAdd = g_CACAOConsts.DepthUnpackConsts.y;

	// Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"

	// Set your depthLinearizeMul and depthLinearizeAdd to:
	// depthLinearizeMul = ( cameraClipFar * cameraClipNear) / ( cameraClipFar - cameraClipNear );
	// depthLinearizeAdd = cameraClipFar / ( cameraClipFar - cameraClipNear );

	return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

float4 ScreenSpaceToViewSpaceDepth(float4 screenDepth)
{
	float depthLinearizeMul = g_CACAOConsts.DepthUnpackConsts.x;
	float depthLinearizeAdd = g_CACAOConsts.DepthUnpackConsts.y;

	// Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"

	// Set your depthLinearizeMul and depthLinearizeAdd to:
	// depthLinearizeMul = ( cameraClipFar * cameraClipNear) / ( cameraClipFar - cameraClipNear );
	// depthLinearizeAdd = cameraClipFar / ( cameraClipFar - cameraClipNear );

	return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

float4 CalculateEdges(const float centerZ, const float leftZ, const float rightZ, const float topZ, const float bottomZ)
{
	// slope-sensitive depth-based edge detection
	float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - centerZ;
	float4 edgesLRTBSlopeAdjusted = edgesLRTB + edgesLRTB.yxwz;
	edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));
	return saturate((1.3 - edgesLRTB / (centerZ * 0.040)));

	// cheaper version but has artifacts
	// edgesLRTB = abs( float4( leftZ, rightZ, topZ, bottomZ ) - centerZ; );
	// return saturate( ( 1.3 - edgesLRTB / (pixZ * 0.06 + 0.1) ) );
}

float3 NDCToViewspace(float2 pos, float viewspaceDepth)
{
	float3 ret;

	ret.xy = (g_CACAOConsts.NDCToViewMul * pos.xy + g_CACAOConsts.NDCToViewAdd) * viewspaceDepth;

	ret.z = viewspaceDepth;

	return ret;
}

float3 DepthBufferUVToViewspace(float2 pos, float viewspaceDepth)
{
	float3 ret;
	ret.xy = (g_CACAOConsts.DepthBufferUVToViewMul * pos.xy + g_CACAOConsts.DepthBufferUVToViewAdd) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float3 CalculateNormal(const float4 edgesLRTB, float3 pixCenterPos, float3 pixLPos, float3 pixRPos, float3 pixTPos, float3 pixBPos)
{
	// Get this pixel's viewspace normal
	float4 acceptedNormals = float4(edgesLRTB.x*edgesLRTB.z, edgesLRTB.z*edgesLRTB.y, edgesLRTB.y*edgesLRTB.w, edgesLRTB.w*edgesLRTB.x);

	pixLPos = normalize(pixLPos - pixCenterPos);
	pixRPos = normalize(pixRPos - pixCenterPos);
	pixTPos = normalize(pixTPos - pixCenterPos);
	pixBPos = normalize(pixBPos - pixCenterPos);

	float3 pixelNormal = float3(0, 0, -0.0005);
	pixelNormal += (acceptedNormals.x) * cross(pixLPos, pixTPos);
	pixelNormal += (acceptedNormals.y) * cross(pixTPos, pixRPos);
	pixelNormal += (acceptedNormals.z) * cross(pixRPos, pixBPos);
	pixelNormal += (acceptedNormals.w) * cross(pixBPos, pixLPos);
	pixelNormal = normalize(pixelNormal);

	return pixelNormal;
}


// ================================================================================
// Blur stuff

Texture2DArray<float2>    g_BlurInput  : register(t0);
RWTexture2DArray<float2>  g_BlurOutput : register(u0);

void AddSample(float ssaoValue, float edgeValue, inout float sum, inout float sumWeight)
{
	float weight = edgeValue;

	sum += (weight * ssaoValue);
	sumWeight += weight;
}

float2 SampleBlurredWide(float2 inPos, float2 coord)
{
	float3 fullCoord = float3(coord, 0.0f);
	float2 vC = g_BlurInput.SampleLevel(g_PointMirrorSampler, fullCoord, 0.0, int2(0, 0)).xy;
	float2 vL = g_BlurInput.SampleLevel(g_PointMirrorSampler, fullCoord, 0.0, int2(-2, 0)).xy;
	float2 vT = g_BlurInput.SampleLevel(g_PointMirrorSampler, fullCoord, 0.0, int2(0, -2)).xy;
	float2 vR = g_BlurInput.SampleLevel(g_PointMirrorSampler, fullCoord, 0.0, int2(2, 0)).xy;
	float2 vB = g_BlurInput.SampleLevel(g_PointMirrorSampler, fullCoord, 0.0, int2(0, 2)).xy;

	float packedEdges = vC.y;
	float4 edgesLRTB = UnpackEdges(packedEdges);
	edgesLRTB.x *= UnpackEdges(vL.y).y;
	edgesLRTB.z *= UnpackEdges(vT.y).w;
	edgesLRTB.y *= UnpackEdges(vR.y).x;
	edgesLRTB.w *= UnpackEdges(vB.y).z;

	float ssaoValue = vC.x;
	float ssaoValueL = vL.x;
	float ssaoValueT = vT.x;
	float ssaoValueR = vR.x;
	float ssaoValueB = vB.x;

	float sumWeight = 0.8f;
	float sum = ssaoValue * sumWeight;

	AddSample(ssaoValueL, edgesLRTB.x, sum, sumWeight);
	AddSample(ssaoValueR, edgesLRTB.y, sum, sumWeight);
	AddSample(ssaoValueT, edgesLRTB.z, sum, sumWeight);
	AddSample(ssaoValueB, edgesLRTB.w, sum, sumWeight);

	float ssaoAvg = sum / sumWeight;

	ssaoValue = ssaoAvg; //min( ssaoValue, ssaoAvg ) * 0.2 + ssaoAvg * 0.8;

	return float2(ssaoValue, packedEdges);
}

uint PackFloat16(min16float2 v)
{
	uint2 p = f32tof16(float2(v));
	return p.x | (p.y << 16);
}

min16float2 UnpackFloat16(uint a)
{
	float2 tmp = f16tof32(uint2(a & 0xFFFF, a >> 16));
	return min16float2(tmp);
}

// all in one, SIMD in yo SIMD dawg, shader
#define TILE_WIDTH  4
#define TILE_HEIGHT 3
#define HALF_TILE_WIDTH (TILE_WIDTH / 2)
#define QUARTER_TILE_WIDTH (TILE_WIDTH / 4)

#define ARRAY_WIDTH  (HALF_TILE_WIDTH  * BLUR_WIDTH  + 2)
#define ARRAY_HEIGHT (TILE_HEIGHT * BLUR_HEIGHT + 2)

#define ITERS 4

groupshared uint s_BlurF16Front_4[ARRAY_WIDTH][ARRAY_HEIGHT];
groupshared uint s_BlurF16Back_4[ARRAY_WIDTH][ARRAY_HEIGHT];

struct Edges_4
{
	min16float4 left;
	min16float4 right;
	min16float4 top;
	min16float4 bottom;
};

Edges_4 UnpackEdgesFloat16_4(min16float4 _packedVal)
{
	uint4 packedVal = (uint4)(_packedVal * 255.5);
	Edges_4 result;
	result.left   = min16float4(saturate(min16float4((packedVal >> 6) & 0x03) / 3.0 + g_CACAOConsts.InvSharpness));
	result.right  = min16float4(saturate(min16float4((packedVal >> 4) & 0x03) / 3.0 + g_CACAOConsts.InvSharpness));
	result.top    = min16float4(saturate(min16float4((packedVal >> 2) & 0x03) / 3.0 + g_CACAOConsts.InvSharpness));
	result.bottom = min16float4(saturate(min16float4((packedVal >> 0) & 0x03) / 3.0 + g_CACAOConsts.InvSharpness));

	return result;
}

min16float4 CalcBlurredSampleF16_4(min16float4 packedEdges, min16float4 centre, min16float4 left, min16float4 right, min16float4 top, min16float4 bottom)
{
	min16float4 sum = centre * min16float(0.5f);
	min16float4 weight = min16float4(0.5f, 0.5f, 0.5f, 0.5f);
	Edges_4 edges = UnpackEdgesFloat16_4(packedEdges);

	sum += left * edges.left;
	weight += edges.left;
	sum += right * edges.right;
	weight += edges.right;
	sum += top * edges.top;
	weight += edges.top;
	sum += bottom * edges.bottom;
	weight += edges.bottom;

	return sum / weight;
}

void LDSEdgeSensitiveBlur(const uint blurPasses, const uint2 tid, const uint2 gid)
{
	int2 imageCoord = gid * (int2(TILE_WIDTH * BLUR_WIDTH, TILE_HEIGHT * BLUR_HEIGHT) - (2*blurPasses)) + int2(TILE_WIDTH, TILE_HEIGHT) * tid - blurPasses;
	int2 bufferCoord = int2(HALF_TILE_WIDTH, TILE_HEIGHT) * tid + 1;

	// todo -- replace this with gathers.
	min16float4 packedEdges[QUARTER_TILE_WIDTH][TILE_HEIGHT];
	{
		float2 input[TILE_WIDTH][TILE_HEIGHT];
		int y;
		[unroll]
		for (y = 0; y < TILE_HEIGHT; ++y)
		{
			[unroll]
			for (int x = 0; x < TILE_WIDTH; ++x)
			{
				input[x][y] = g_BlurInput.SampleLevel(g_PointMirrorSampler, float3((imageCoord + int2(x, y) + 0.5f) * g_CACAOConsts.SSAOBufferInverseDimensions, 0.0f), 0).xy;
			}
		}
		[unroll]
		for (y = 0; y < TILE_HEIGHT; ++y)
		{
			[unroll]
			for (int x = 0; x < QUARTER_TILE_WIDTH; ++x)
			{
				min16float2 ssaoVals = min16float2(input[4 * x + 0][y].x, input[4 * x + 1][y].x);
				s_BlurF16Front_4[bufferCoord.x + 2*x + 0][bufferCoord.y + y] = PackFloat16(ssaoVals);
				ssaoVals = min16float2(input[4 * x + 2][y].x, input[4 * x + 3][y].x);
				s_BlurF16Front_4[bufferCoord.x + 2*x + 1][bufferCoord.y + y] = PackFloat16(ssaoVals);
				// min16float2 ssaoVals = min16float2(1, 1);
				packedEdges[x][y] = min16float4(input[4 * x + 0][y].y, input[4 * x + 1][y].y, input[4 * x + 2][y].y, input[4 * x + 3][y].y);
			}
		}
	}

	GroupMemoryBarrierWithGroupSync();

	[unroll]
	for (uint i = 0; i < (blurPasses + 1) / 2; ++i)
	{
		[unroll]
		for (int y = 0; y < TILE_HEIGHT; ++y)
		{
			[unroll]
			for (int x = 0; x < QUARTER_TILE_WIDTH; ++x)
			{
				int2 c = bufferCoord + int2(2*x, y);
				min16float4 centre = min16float4(UnpackFloat16(s_BlurF16Front_4[c.x + 0][c.y + 0]), UnpackFloat16(s_BlurF16Front_4[c.x + 1][c.y + 0]));
				min16float4 top    = min16float4(UnpackFloat16(s_BlurF16Front_4[c.x + 0][c.y - 1]), UnpackFloat16(s_BlurF16Front_4[c.x + 1][c.y - 1]));
				min16float4 bottom = min16float4(UnpackFloat16(s_BlurF16Front_4[c.x + 0][c.y + 1]), UnpackFloat16(s_BlurF16Front_4[c.x + 1][c.y + 1]));

				min16float2 tmp = UnpackFloat16(s_BlurF16Front_4[c.x - 1][c.y + 0]);
				min16float4 left = min16float4(tmp.y, centre.xyz);
				tmp = UnpackFloat16(s_BlurF16Front_4[c.x + 2][c.y + 0]);
				min16float4 right = min16float4(centre.yzw, tmp.x);

				min16float4 tmp_4 = CalcBlurredSampleF16_4(packedEdges[x][y], centre, left, right, top, bottom);
				s_BlurF16Back_4[c.x + 0][c.y] = PackFloat16(tmp_4.xy);
				s_BlurF16Back_4[c.x + 1][c.y] = PackFloat16(tmp_4.zw);
			}
		}
		GroupMemoryBarrierWithGroupSync();

		if (2 * i + 1 < blurPasses)
		{
			[unroll]
			for (int y = 0; y < TILE_HEIGHT; ++y)
			{
				[unroll]
				for (int x = 0; x < QUARTER_TILE_WIDTH; ++x)
				{
					int2 c = bufferCoord + int2(2 * x, y);
					min16float4 centre = min16float4(UnpackFloat16(s_BlurF16Back_4[c.x + 0][c.y + 0]), UnpackFloat16(s_BlurF16Back_4[c.x + 1][c.y + 0]));
					min16float4 top    = min16float4(UnpackFloat16(s_BlurF16Back_4[c.x + 0][c.y - 1]), UnpackFloat16(s_BlurF16Back_4[c.x + 1][c.y - 1]));
					min16float4 bottom = min16float4(UnpackFloat16(s_BlurF16Back_4[c.x + 0][c.y + 1]), UnpackFloat16(s_BlurF16Back_4[c.x + 1][c.y + 1]));

					min16float2 tmp = UnpackFloat16(s_BlurF16Back_4[c.x - 1][c.y + 0]);
					min16float4 left = min16float4(tmp.y, centre.xyz);
					tmp = UnpackFloat16(s_BlurF16Back_4[c.x + 2][c.y + 0]);
					min16float4 right = min16float4(centre.yzw, tmp.x);

					min16float4 tmp_4 = CalcBlurredSampleF16_4(packedEdges[x][y], centre, left, right, top, bottom);
					s_BlurF16Front_4[c.x + 0][c.y] = PackFloat16(tmp_4.xy);
					s_BlurF16Front_4[c.x + 1][c.y] = PackFloat16(tmp_4.zw);
				}
			}
			GroupMemoryBarrierWithGroupSync();
		}
	}

	[unroll]
	for (uint y = 0; y < TILE_HEIGHT; ++y)
	{
		uint outputY = TILE_HEIGHT * tid.y + y;
		if (blurPasses <= outputY && outputY < TILE_HEIGHT * BLUR_HEIGHT - blurPasses)
		{
			[unroll]
			for (int x = 0; x < QUARTER_TILE_WIDTH; ++x)
			{
				uint outputX = TILE_WIDTH * tid.x + 4 * x;

				min16float4 ssaoVal;
				if (blurPasses % 2 == 0)
				{
					ssaoVal = min16float4(UnpackFloat16(s_BlurF16Front_4[bufferCoord.x + x][bufferCoord.y + y]), UnpackFloat16(s_BlurF16Front_4[bufferCoord.x + x + 1][bufferCoord.y + y]));
				}
				else
				{
					ssaoVal = min16float4(UnpackFloat16(s_BlurF16Back_4[bufferCoord.x + x][bufferCoord.y + y]), UnpackFloat16(s_BlurF16Back_4[bufferCoord.x + x + 1][bufferCoord.y + y]));
				}

				if (blurPasses <= outputX && outputX < TILE_WIDTH * BLUR_WIDTH - blurPasses)
				{
					g_BlurOutput[int3(imageCoord + int2(4 * x, y), 0)] = float2(ssaoVal.x, packedEdges[x][y].x);
				}
				outputX += 1;
				if (blurPasses <= outputX && outputX < TILE_WIDTH * BLUR_WIDTH - blurPasses)
				{
					g_BlurOutput[int3(imageCoord + int2(4 * x + 1, y), 0)] = float2(ssaoVal.y, packedEdges[x][y].y);
				}
				outputX += 1;
				if (blurPasses <= outputX && outputX < TILE_WIDTH * BLUR_WIDTH - blurPasses)
				{
					g_BlurOutput[int3(imageCoord + int2(4 * x + 2, y), 0)] = float2(ssaoVal.z, packedEdges[x][y].z);
				}
				outputX += 1;
				if (blurPasses <= outputX && outputX < TILE_WIDTH * BLUR_WIDTH - blurPasses)
				{
					g_BlurOutput[int3(imageCoord + int2(4 * x + 3, y), 0)] = float2(ssaoVal.w, packedEdges[x][y].w);
				}
			}
		}
	}
}

[numthreads(BLUR_WIDTH, BLUR_HEIGHT, 1)]
void CSEdgeSensitiveBlur1(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	LDSEdgeSensitiveBlur(1, tid, gid);
}

[numthreads(BLUR_WIDTH, BLUR_HEIGHT, 1)]
void CSEdgeSensitiveBlur2(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	LDSEdgeSensitiveBlur(2, tid, gid);
}

[numthreads(BLUR_WIDTH, BLUR_HEIGHT, 1)]
void CSEdgeSensitiveBlur3(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	LDSEdgeSensitiveBlur(3, tid, gid);
}

[numthreads(BLUR_WIDTH, BLUR_HEIGHT, 1)]
void CSEdgeSensitiveBlur4(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	LDSEdgeSensitiveBlur(4, tid, gid);
}

[numthreads(BLUR_WIDTH, BLUR_HEIGHT, 1)]
void CSEdgeSensitiveBlur5(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	LDSEdgeSensitiveBlur(5, tid, gid);
}

[numthreads(BLUR_WIDTH, BLUR_HEIGHT, 1)]
void CSEdgeSensitiveBlur6(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	LDSEdgeSensitiveBlur(6, tid, gid);
}

[numthreads(BLUR_WIDTH, BLUR_HEIGHT, 1)]
void CSEdgeSensitiveBlur7(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	LDSEdgeSensitiveBlur(7, tid, gid);
}


[numthreads(BLUR_WIDTH, BLUR_HEIGHT, 1)]
void CSEdgeSensitiveBlur8(uint2 tid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	LDSEdgeSensitiveBlur(8, tid, gid);
}


#undef TILE_WIDTH
#undef TILE_HEIGHT
#undef ARRAY_WIDTH
#undef ARRAY_HEIGHT
#undef ITERS



// =======================================================================================================
// SSAO stuff

Texture2DArray<float>    g_ViewspaceDepthSource      : register(t0);
Texture1D<uint>          g_LoadCounter               : register(t2);
Texture2D<float>         g_ImportanceMap             : register(t3);
Texture2DArray           g_FinalSSAO                 : register(t4);
Texture1D<float>         g_ZeroTexture               : register(t5);
Texture2DArray           g_deinterlacedNormals       : register(t6);

RWTexture2DArray<float2> g_SSAOOutput                : register(u0);


// calculate effect radius and fit our screen sampling pattern inside it
void CalculateRadiusParameters(const float pixCenterLength, const float2 pixelDirRBViewspaceSizeAtCenterZ, out float pixLookupRadiusMod, out float effectRadius, out float falloffCalcMulSq)
{
	effectRadius = g_CACAOConsts.EffectRadius;

	// leaving this out for performance reasons: use something similar if radius needs to scale based on distance
	//effectRadius *= pow( pixCenterLength, g_CACAOConsts.RadiusDistanceScalingFunctionPow);

	// when too close, on-screen sampling disk will grow beyond screen size; limit this to avoid closeup temporal artifacts
	const float tooCloseLimitMod = saturate(pixCenterLength * g_CACAOConsts.EffectSamplingRadiusNearLimitRec) * 0.8 + 0.2;

	effectRadius *= tooCloseLimitMod;

	// 0.85 is to reduce the radius to allow for more samples on a slope to still stay within influence
	pixLookupRadiusMod = (0.85 * effectRadius) / pixelDirRBViewspaceSizeAtCenterZ.x;

	// used to calculate falloff (both for AO samples and per-sample weights)
	falloffCalcMulSq = -1.0f / (effectRadius*effectRadius);
}


float3 DecodeNormal(float3 encodedNormal)
{
	float3 normal = encodedNormal * g_CACAOConsts.NormalsUnpackMul.xxx + g_CACAOConsts.NormalsUnpackAdd.xxx;

#if SSAO_ENABLE_NORMAL_WORLD_TO_VIEW_CONVERSION
	normal = mul(normal, (float3x3)g_CACAOConsts.NormalsWorldToViewspaceMatrix).xyz;
#endif

	// normal = normalize( normal );    // normalize adds around 2.5% cost on High settings but makes little (PSNR 66.7) visual difference when normals are as in the sample (stored in R8G8B8A8_UNORM,
	//                                  // decoded in the shader), however it will likely be required if using different encoding/decoding or the inputs are not normalized, etc.

	return normal;
}

// all vectors in viewspace
float CalculatePixelObscurance(float3 pixelNormal, float3 hitDelta, float falloffCalcMulSq)
{
	float lengthSq = dot(hitDelta, hitDelta);
	float NdotD = dot(pixelNormal, hitDelta) / sqrt(lengthSq);

	float falloffMult = max(0.0, lengthSq * falloffCalcMulSq + 1.0);

	return max(0, NdotD - g_CACAOConsts.EffectHorizonAngleThreshold) * falloffMult;
}

void SSAOTapInner(const int qualityLevel, inout float obscuranceSum, inout float weightSum, const float2 samplingUV, const float mipLevel, const float3 pixCenterPos, const float3 negViewspaceDir, float3 pixelNormal, const float falloffCalcMulSq, const float weightMod, const int dbgTapIndex)
{
	// get depth at sample
	float viewspaceSampleZ = g_ViewspaceDepthSource.SampleLevel(g_ViewspaceDepthTapSampler, float3(samplingUV.xy, 0.0f), mipLevel).x; // * g_CACAOConsts.MaxViewspaceDepth;

	// convert to viewspace
	// float3 hitPos = NDCToViewspace(samplingUV.xy, viewspaceSampleZ).xyz;
	float3 hitPos = DepthBufferUVToViewspace(samplingUV.xy, viewspaceSampleZ).xyz;
	float3 hitDelta = hitPos - pixCenterPos;

	float obscurance = CalculatePixelObscurance(pixelNormal, hitDelta, falloffCalcMulSq);
	float weight = 1.0;

	if (qualityLevel >= SSAO_HALOING_REDUCTION_ENABLE_AT_QUALITY_PRESET)
	{
		//float reduct = max( 0, dot( hitDelta, negViewspaceDir ) );
		float reduct = max(0, -hitDelta.z); // cheaper, less correct version
		reduct = saturate(reduct * g_CACAOConsts.NegRecEffectRadius + 2.0); // saturate( 2.0 - reduct / g_CACAOConsts.EffectRadius );
		weight = SSAO_HALOING_REDUCTION_AMOUNT * reduct + (1.0 - SSAO_HALOING_REDUCTION_AMOUNT);
	}
	weight *= weightMod;
	obscuranceSum += obscurance * weight;
	weightSum += weight;
}

void SSAOTap(const int qualityLevel, inout float obscuranceSum, inout float weightSum, const int tapIndex, const float2x2 rotScale, const float3 pixCenterPos, const float3 negViewspaceDir, float3 pixelNormal, const float2 normalizedScreenPos, const float2 depthBufferUV, const float mipOffset, const float falloffCalcMulSq, float weightMod, float2 normXY, float normXYLength)
{
	float2  sampleOffset;
	float   samplePow2Len;

	// patterns
	{
		float4 newSample = g_samplePatternMain[tapIndex];
		sampleOffset = mul(rotScale, newSample.xy);
		samplePow2Len = newSample.w;                      // precalculated, same as: samplePow2Len = log2( length( newSample.xy ) );
		weightMod *= newSample.z;
	}

	// snap to pixel center (more correct obscurance math, avoids artifacts)
	sampleOffset = round(sampleOffset);

	// calculate MIP based on the sample distance from the centre, similar to as described 
	// in http://graphics.cs.williams.edu/papers/SAOHPG12/.
	float mipLevel = (qualityLevel < SSAO_DEPTH_MIPS_ENABLE_AT_QUALITY_PRESET) ? (0) : (samplePow2Len + mipOffset);

	float2 samplingUV = sampleOffset * g_CACAOConsts.DeinterleavedDepthBufferInverseDimensions + depthBufferUV;

	SSAOTapInner(qualityLevel, obscuranceSum, weightSum, samplingUV, mipLevel, pixCenterPos, negViewspaceDir, pixelNormal, falloffCalcMulSq, weightMod, tapIndex * 2);

	// for the second tap, just use the mirrored offset
	float2 sampleOffsetMirroredUV = -sampleOffset;

	// tilt the second set of samples so that the disk is effectively rotated by the normal
	// effective at removing one set of artifacts, but too expensive for lower quality settings
	if (qualityLevel >= SSAO_TILT_SAMPLES_ENABLE_AT_QUALITY_PRESET)
	{
		float dotNorm = dot(sampleOffsetMirroredUV, normXY);
		sampleOffsetMirroredUV -= dotNorm * normXYLength * normXY;
		sampleOffsetMirroredUV = round(sampleOffsetMirroredUV);
	}

	// snap to pixel center (more correct obscurance math, avoids artifacts)
	float2 samplingMirroredUV = sampleOffsetMirroredUV * g_CACAOConsts.DeinterleavedDepthBufferInverseDimensions + depthBufferUV;

	SSAOTapInner(qualityLevel, obscuranceSum, weightSum, samplingMirroredUV, mipLevel, pixCenterPos, negViewspaceDir, pixelNormal, falloffCalcMulSq, weightMod, tapIndex * 2 + 1);
}

struct SSAOHits
{
	float3 hits[2];
	float weightMod;
};

SSAOHits SSAOGetHits(const int qualityLevel, const float2 depthBufferUV, const int tapIndex, const float mipOffset, const float2x2 rotScale, const float4 newSample)
{
	SSAOHits result;

	float2  sampleOffset;
	float   samplePow2Len;

	// patterns
	{
		// float4 newSample = g_samplePatternMain[tapIndex];
		sampleOffset = mul(rotScale, newSample.xy);
		samplePow2Len = newSample.w;                      // precalculated, same as: samplePow2Len = log2( length( newSample.xy ) );
		result.weightMod = newSample.z;
	}

	// snap to pixel center (more correct obscurance math, avoids artifacts)
	sampleOffset = round(sampleOffset) * g_CACAOConsts.DeinterleavedDepthBufferInverseDimensions;

	float mipLevel = (qualityLevel < SSAO_DEPTH_MIPS_ENABLE_AT_QUALITY_PRESET) ? (0) : (samplePow2Len + mipOffset);

	float2 sampleUV = depthBufferUV + sampleOffset;
	result.hits[0] = float3(sampleUV, g_ViewspaceDepthSource.SampleLevel(g_ViewspaceDepthTapSampler, float3(sampleUV, 0.0f), mipLevel).x);

	sampleUV = depthBufferUV - sampleOffset;
	result.hits[1] = float3(sampleUV, g_ViewspaceDepthSource.SampleLevel(g_ViewspaceDepthTapSampler, float3(sampleUV, 0.0f), mipLevel).x);

	return result;
}

struct SSAOSampleData
{
	float2 uvOffset;
	float mipLevel;
	float weightMod;
};

SSAOSampleData SSAOGetSampleData(const int qualityLevel, const float2x2 rotScale, const float4 newSample, const float mipOffset)
{
	float2  sampleOffset = mul(rotScale, newSample.xy);
	sampleOffset = round(sampleOffset) * g_CACAOConsts.DeinterleavedDepthBufferInverseDimensions;

	float samplePow2Len = newSample.w;
	float mipLevel = (qualityLevel < SSAO_DEPTH_MIPS_ENABLE_AT_QUALITY_PRESET) ? (0) : (samplePow2Len + mipOffset);

	SSAOSampleData result;

	result.uvOffset = sampleOffset;
	result.mipLevel = mipLevel;
	result.weightMod = newSample.z;

	return result;
}

SSAOHits SSAOGetHits2(SSAOSampleData data, const float2 depthBufferUV)
{
	SSAOHits result;
	result.weightMod = data.weightMod;
	float2 sampleUV = depthBufferUV + data.uvOffset;
	result.hits[0] = float3(sampleUV, g_ViewspaceDepthSource.SampleLevel(g_ViewspaceDepthTapSampler, float3(sampleUV, 0.0f), data.mipLevel).x);
	sampleUV = depthBufferUV - data.uvOffset;
	result.hits[1] = float3(sampleUV, g_ViewspaceDepthSource.SampleLevel(g_ViewspaceDepthTapSampler, float3(sampleUV, 0.0f), data.mipLevel).x);
	return result;
}

void SSAOAddHits(const int qualityLevel, const float3 pixCenterPos, const float3 pixelNormal, const float falloffCalcMulSq, inout float weightSum, inout float obscuranceSum, SSAOHits hits)
{
	float weight = hits.weightMod;
	[unroll]
	for (int hitIndex = 0; hitIndex < 2; ++hitIndex)
	{
		float3 hit = hits.hits[hitIndex];
		float3 hitPos = DepthBufferUVToViewspace(hit.xy, hit.z);
		float3 hitDelta = hitPos - pixCenterPos;

		float obscurance = CalculatePixelObscurance(pixelNormal, hitDelta, falloffCalcMulSq);

		if (qualityLevel >= SSAO_HALOING_REDUCTION_ENABLE_AT_QUALITY_PRESET)
		{
			//float reduct = max( 0, dot( hitDelta, negViewspaceDir ) );
			float reduct = max(0, -hitDelta.z); // cheaper, less correct version
			reduct = saturate(reduct * g_CACAOConsts.NegRecEffectRadius + 2.0); // saturate( 2.0 - reduct / g_CACAOConsts.EffectRadius );
			weight = SSAO_HALOING_REDUCTION_AMOUNT * reduct + (1.0 - SSAO_HALOING_REDUCTION_AMOUNT);
		}
		obscuranceSum += obscurance * weight;
		weightSum += weight;
	}
}

void SSAOTap2(const int qualityLevel, inout float obscuranceSum, inout float weightSum, const int tapIndex, const float2x2 rotScale, const float3 pixCenterPos, const float3 negViewspaceDir, float3 pixelNormal, const float2 normalizedScreenPos, const float mipOffset, const float falloffCalcMulSq, float weightMod, float2 normXY, float normXYLength)
{
	float4 newSample = g_samplePatternMain[tapIndex];
	SSAOSampleData data = SSAOGetSampleData(qualityLevel, rotScale, newSample, mipOffset);
	SSAOHits hits = SSAOGetHits2(data, normalizedScreenPos);
	SSAOAddHits(qualityLevel, pixCenterPos, pixelNormal, falloffCalcMulSq, weightSum, obscuranceSum, hits);
}


void GenerateSSAOShadowsInternal(out float outShadowTerm, out float4 outEdges, out float outWeight, const float2 SVPos/*, const float2 normalizedScreenPos*/, uniform int qualityLevel, bool adaptiveBase)
{
	float2 SVPosRounded = trunc(SVPos);
	uint2 SVPosui = uint2(SVPosRounded); //same as uint2( SVPos )

	const int numberOfTaps = (adaptiveBase) ? (SSAO_ADAPTIVE_TAP_BASE_COUNT) : (g_numTaps[qualityLevel]);
	float pixZ, pixLZ, pixTZ, pixRZ, pixBZ;

	float2 depthBufferUV = (SVPos + 0.5f) * g_CACAOConsts.DeinterleavedDepthBufferInverseDimensions + g_CACAOConsts.DeinterleavedDepthBufferNormalisedOffset;
	float4 valuesUL = g_ViewspaceDepthSource.GatherRed(g_PointMirrorSampler, float3(depthBufferUV, 0.0f), int2(-1, -1));
	float4 valuesBR = g_ViewspaceDepthSource.GatherRed(g_PointMirrorSampler, float3(depthBufferUV, 0.0f));

	// get this pixel's viewspace depth
	pixZ = valuesUL.y; //float pixZ = g_ViewspaceDepthSource.SampleLevel( g_PointMirrorSampler, float3(normalizedScreenPos, 0.0f), 0.0 ).x; // * g_CACAOConsts.MaxViewspaceDepth;

	// get left right top bottom neighbouring pixels for edge detection (gets compiled out on qualityLevel == 0)
	pixLZ = valuesUL.x;
	pixTZ = valuesUL.z;
	pixRZ = valuesBR.z;
	pixBZ = valuesBR.x;

	// float2 normalizedScreenPos = SVPosRounded * g_CACAOConsts.Viewport2xPixelSize + g_CACAOConsts.Viewport2xPixelSize_x_025;
	float2 normalizedScreenPos = (SVPosRounded + 0.5f) * g_CACAOConsts.SSAOBufferInverseDimensions;
	float3 pixCenterPos = NDCToViewspace(normalizedScreenPos, pixZ); // g

	// Load this pixel's viewspace normal
	// uint2 fullResCoord = 2 * (SVPosui * 2 + g_CACAOConsts.PerPassFullResCoordOffset.xy);
	int3 normalCoord = int3(SVPosui, g_CACAOConsts.PassIndex);
	float3 pixelNormal = g_deinterlacedNormals[normalCoord].xyz;

	// optimized approximation of:  float2 pixelDirRBViewspaceSizeAtCenterZ = NDCToViewspace( normalizedScreenPos.xy + g_CACAOConsts._ViewportPixelSize.xy, pixCenterPos.z ).xy - pixCenterPos.xy;
	// const float2 pixelDirRBViewspaceSizeAtCenterZ = pixCenterPos.z * g_CACAOConsts.NDCToViewMul * g_CACAOConsts.Viewport2xPixelSize;
	const float2 pixelDirRBViewspaceSizeAtCenterZ = pixCenterPos.z * g_CACAOConsts.NDCToViewMul * g_CACAOConsts.SSAOBufferInverseDimensions;

	float pixLookupRadiusMod;
	float falloffCalcMulSq;

	// calculate effect radius and fit our screen sampling pattern inside it
	float effectViewspaceRadius;
	CalculateRadiusParameters(length(pixCenterPos), pixelDirRBViewspaceSizeAtCenterZ, pixLookupRadiusMod, effectViewspaceRadius, falloffCalcMulSq);

	// calculate samples rotation/scaling
	float2x2 rotScale;
	{
		// reduce effect radius near the screen edges slightly; ideally, one would render a larger depth buffer (5% on each side) instead
		if (!adaptiveBase && (qualityLevel >= SSAO_REDUCE_RADIUS_NEAR_SCREEN_BORDER_ENABLE_AT_QUALITY_PRESET))
		{
			float nearScreenBorder = min(min(depthBufferUV.x, 1.0 - depthBufferUV.x), min(depthBufferUV.y, 1.0 - depthBufferUV.y));
			nearScreenBorder = saturate(10.0 * nearScreenBorder + 0.6);
			pixLookupRadiusMod *= nearScreenBorder;
		}

		// load & update pseudo-random rotation matrix
		uint pseudoRandomIndex = uint(SVPosRounded.y * 2 + SVPosRounded.x) % 5;
		float4 rs = g_CACAOConsts.PatternRotScaleMatrices[pseudoRandomIndex];
		rotScale = float2x2(rs.x * pixLookupRadiusMod, rs.y * pixLookupRadiusMod, rs.z * pixLookupRadiusMod, rs.w * pixLookupRadiusMod);
	}

	// the main obscurance & sample weight storage
	float obscuranceSum = 0.0;
	float weightSum = 0.0;

	// edge mask for between this and left/right/top/bottom neighbour pixels - not used in quality level 0 so initialize to "no edge" (1 is no edge, 0 is edge)
	float4 edgesLRTB = float4(1.0, 1.0, 1.0, 1.0);

	// Move center pixel slightly towards camera to avoid imprecision artifacts due to using of 16bit depth buffer; a lot smaller offsets needed when using 32bit floats
	pixCenterPos *= g_CACAOConsts.DepthPrecisionOffsetMod;

	if (!adaptiveBase && (qualityLevel >= SSAO_DEPTH_BASED_EDGES_ENABLE_AT_QUALITY_PRESET))
	{
		edgesLRTB = CalculateEdges(pixZ, pixLZ, pixRZ, pixTZ, pixBZ);
	}

	// adds a more high definition sharp effect, which gets blurred out (reuses left/right/top/bottom samples that we used for edge detection)
	if (!adaptiveBase && (qualityLevel >= SSAO_DETAIL_AO_ENABLE_AT_QUALITY_PRESET))
	{
		// disable in case of quality level 4 (reference)
		if (qualityLevel != 4)
		{
			//approximate neighbouring pixels positions (actually just deltas or "positions - pixCenterPos" )
			float3 viewspaceDirZNormalized = float3(pixCenterPos.xy / pixCenterPos.zz, 1.0);

			// very close approximation of: float3 pixLPos  = NDCToViewspace( normalizedScreenPos + float2( -g_CACAOConsts.HalfViewportPixelSize.x, 0.0 ), pixLZ ).xyz - pixCenterPos.xyz;
			float3 pixLDelta = float3(-pixelDirRBViewspaceSizeAtCenterZ.x, 0.0, 0.0) + viewspaceDirZNormalized * (pixLZ - pixCenterPos.z);
			// very close approximation of: float3 pixRPos  = NDCToViewspace( normalizedScreenPos + float2( +g_CACAOConsts.HalfViewportPixelSize.x, 0.0 ), pixRZ ).xyz - pixCenterPos.xyz;
			float3 pixRDelta = float3(+pixelDirRBViewspaceSizeAtCenterZ.x, 0.0, 0.0) + viewspaceDirZNormalized * (pixRZ - pixCenterPos.z);
			// very close approximation of: float3 pixTPos  = NDCToViewspace( normalizedScreenPos + float2( 0.0, -g_CACAOConsts.HalfViewportPixelSize.y ), pixTZ ).xyz - pixCenterPos.xyz;
			float3 pixTDelta = float3(0.0, -pixelDirRBViewspaceSizeAtCenterZ.y, 0.0) + viewspaceDirZNormalized * (pixTZ - pixCenterPos.z);
			// very close approximation of: float3 pixBPos  = NDCToViewspace( normalizedScreenPos + float2( 0.0, +g_CACAOConsts.HalfViewportPixelSize.y ), pixBZ ).xyz - pixCenterPos.xyz;
			float3 pixBDelta = float3(0.0, +pixelDirRBViewspaceSizeAtCenterZ.y, 0.0) + viewspaceDirZNormalized * (pixBZ - pixCenterPos.z);

			const float rangeReductionConst = 4.0f;                         // this is to avoid various artifacts
			const float modifiedFalloffCalcMulSq = rangeReductionConst * falloffCalcMulSq;

			float4 additionalObscurance;
			additionalObscurance.x = CalculatePixelObscurance(pixelNormal, pixLDelta, modifiedFalloffCalcMulSq);
			additionalObscurance.y = CalculatePixelObscurance(pixelNormal, pixRDelta, modifiedFalloffCalcMulSq);
			additionalObscurance.z = CalculatePixelObscurance(pixelNormal, pixTDelta, modifiedFalloffCalcMulSq);
			additionalObscurance.w = CalculatePixelObscurance(pixelNormal, pixBDelta, modifiedFalloffCalcMulSq);

			obscuranceSum += g_CACAOConsts.DetailAOStrength * dot(additionalObscurance, edgesLRTB);
		}
	}

	// Sharp normals also create edges - but this adds to the cost as well
	if (!adaptiveBase && (qualityLevel >= SSAO_NORMAL_BASED_EDGES_ENABLE_AT_QUALITY_PRESET))
	{
		float3 neighbourNormalL = g_deinterlacedNormals[normalCoord + int3(-1, +0, 0)].xyz;
		float3 neighbourNormalR = g_deinterlacedNormals[normalCoord + int3(+1, +0, 0)].xyz;
		float3 neighbourNormalT = g_deinterlacedNormals[normalCoord + int3(+0, -1, 0)].xyz;
		float3 neighbourNormalB = g_deinterlacedNormals[normalCoord + int3(+0, +1, 0)].xyz;
		

		const float dotThreshold = SSAO_NORMAL_BASED_EDGES_DOT_THRESHOLD;

		float4 normalEdgesLRTB;
		normalEdgesLRTB.x = saturate((dot(pixelNormal, neighbourNormalL) + dotThreshold));
		normalEdgesLRTB.y = saturate((dot(pixelNormal, neighbourNormalR) + dotThreshold));
		normalEdgesLRTB.z = saturate((dot(pixelNormal, neighbourNormalT) + dotThreshold));
		normalEdgesLRTB.w = saturate((dot(pixelNormal, neighbourNormalB) + dotThreshold));

		//#define SSAO_SMOOTHEN_NORMALS // fixes some aliasing artifacts but kills a lot of high detail and adds to the cost - not worth it probably but feel free to play with it
#ifdef SSAO_SMOOTHEN_NORMALS
		//neighbourNormalL  = LoadNormal( fullResCoord, int2( -1,  0 ) );
		//neighbourNormalR  = LoadNormal( fullResCoord, int2(  1,  0 ) );
		//neighbourNormalT  = LoadNormal( fullResCoord, int2(  0, -1 ) );
		//neighbourNormalB  = LoadNormal( fullResCoord, int2(  0,  1 ) );
		pixelNormal += neighbourNormalL * edgesLRTB.x + neighbourNormalR * edgesLRTB.y + neighbourNormalT * edgesLRTB.z + neighbourNormalB * edgesLRTB.w;
		pixelNormal = normalize(pixelNormal);
#endif

		edgesLRTB *= normalEdgesLRTB;
	}



	const float globalMipOffset = SSAO_DEPTH_MIPS_GLOBAL_OFFSET;
	float mipOffset = (qualityLevel < SSAO_DEPTH_MIPS_ENABLE_AT_QUALITY_PRESET) ? (0) : (log2(pixLookupRadiusMod) + globalMipOffset);

	// Used to tilt the second set of samples so that the disk is effectively rotated by the normal
	// effective at removing one set of artifacts, but too expensive for lower quality settings
	float2 normXY = float2(pixelNormal.x, pixelNormal.y);
	float normXYLength = length(normXY);
	normXY /= float2(normXYLength, -normXYLength);
	normXYLength *= SSAO_TILT_SAMPLES_AMOUNT;

	const float3 negViewspaceDir = -normalize(pixCenterPos);

	// standard, non-adaptive approach
	if ((qualityLevel != 3) || adaptiveBase)
	{
		//SSAOHits prevHits = SSAOGetHits(qualityLevel, normalizedScreenPos, 0, mipOffset);

#if 0
		float4 newSample = g_samplePatternMain[0];
		// float zero = g_ZeroTexture.SampleLevel(g_PointClampSampler, float2(0.5f, 0.5f), 0);
		SSAOSampleData data = SSAOGetSampleData(qualityLevel, rotScale, newSample, mipOffset);
		SSAOHits hits = SSAOGetHits2(data, depthBufferUV);
		newSample = g_samplePatternMain[1];
		// newSample.x += zero;
		data = SSAOGetSampleData(qualityLevel, rotScale, newSample, mipOffset);

		[unroll]
		for (int i = 0; i < numberOfTaps - 1; ++i)
		{
			// zero = g_ZeroTexture.SampleLevel(g_PointClampSampler, float2(0.5f + zero, 0.5f), 0);
			SSAOHits nextHits = SSAOGetHits2(data, depthBufferUV);
			// hits.hits[0].x += zero;
			newSample = g_samplePatternMain[i + 2];

			SSAOAddHits(qualityLevel, pixCenterPos, pixelNormal, falloffCalcMulSq, weightSum, obscuranceSum, hits);
			SSAOSampleData nextData = SSAOGetSampleData(qualityLevel, rotScale, newSample, mipOffset);
			hits = nextHits;
			data = nextData;
		}

		// last loop iteration
		{
			SSAOAddHits(qualityLevel, pixCenterPos, pixelNormal, falloffCalcMulSq, weightSum, obscuranceSum, hits);
		}
#else

		[unroll]
		for (int i = 0; i < numberOfTaps; i++)
		{
			SSAOTap(qualityLevel, obscuranceSum, weightSum, i, rotScale, pixCenterPos, negViewspaceDir, pixelNormal, normalizedScreenPos, depthBufferUV, mipOffset, falloffCalcMulSq, 1.0, normXY, normXYLength);
			// SSAOHits hits = SSAOGetHits(qualityLevel, normalizedScreenPos, i, mipOffset, rotScale);
			// SSAOAddHits(qualityLevel, pixCenterPos, pixelNormal, 1.0f, falloffCalcMulSq, weightSum, obscuranceSum, hits);
		}

#endif
	}
	else // if( qualityLevel == 3 ) adaptive approach
	{
		// add new ones if needed
		float2 fullResUV = normalizedScreenPos + g_CACAOConsts.PerPassFullResUVOffset.xy;
		float importance = g_ImportanceMap.SampleLevel(g_LinearClampSampler, fullResUV, 0.0).x;

		// this is to normalize SSAO_DETAIL_AO_AMOUNT across all pixel regardless of importance
		obscuranceSum *= (SSAO_ADAPTIVE_TAP_BASE_COUNT / (float)SSAO_MAX_TAPS) + (importance * SSAO_ADAPTIVE_TAP_FLEXIBLE_COUNT / (float)SSAO_MAX_TAPS);

		// load existing base values
		float2 baseValues = g_FinalSSAO.Load(int4(SVPosui, g_CACAOConsts.PassIndex, 0)).xy;
		weightSum += baseValues.y * (float)(SSAO_ADAPTIVE_TAP_BASE_COUNT * 4.0);
		obscuranceSum += (baseValues.x) * weightSum;

		// increase importance around edges
		float edgeCount = dot(1.0 - edgesLRTB, float4(1.0, 1.0, 1.0, 1.0));

		float avgTotalImportance = (float)g_LoadCounter[0] * g_CACAOConsts.LoadCounterAvgDiv;

		float importanceLimiter = saturate(g_CACAOConsts.AdaptiveSampleCountLimit / avgTotalImportance);
		importance *= importanceLimiter;

		float additionalSampleCountFlt = SSAO_ADAPTIVE_TAP_FLEXIBLE_COUNT * importance;

		additionalSampleCountFlt += 1.5;
		uint additionalSamples = uint(additionalSampleCountFlt);
		uint additionalSamplesTo = min(SSAO_MAX_TAPS, additionalSamples + SSAO_ADAPTIVE_TAP_BASE_COUNT);

		// sample loop
		{
			float4 newSample = g_samplePatternMain[SSAO_ADAPTIVE_TAP_BASE_COUNT];
			SSAOSampleData data = SSAOGetSampleData(qualityLevel, rotScale, newSample, mipOffset);
			SSAOHits hits = SSAOGetHits2(data, depthBufferUV);
			newSample = g_samplePatternMain[SSAO_ADAPTIVE_TAP_BASE_COUNT + 1];

			for (uint i = SSAO_ADAPTIVE_TAP_BASE_COUNT; i < additionalSamplesTo - 1; i++)
			{
				data = SSAOGetSampleData(qualityLevel, rotScale, newSample, mipOffset);
				newSample = g_samplePatternMain[i + 2];
				SSAOHits nextHits = SSAOGetHits2(data, depthBufferUV);

				// float zero = g_ZeroTexture.SampleLevel(g_ZeroTextureSampler, (float)i, 0.0f);
				// hits.weightMod += zero;

				SSAOAddHits(qualityLevel, pixCenterPos, pixelNormal, falloffCalcMulSq, weightSum, obscuranceSum, hits);
				hits = nextHits;
			}

			// last loop iteration
			{
				SSAOAddHits(qualityLevel, pixCenterPos, pixelNormal, falloffCalcMulSq, weightSum, obscuranceSum, hits);
			}
		}
	}

	// early out for adaptive base - just output weight (used for the next pass)
	if (adaptiveBase)
	{
		float obscurance = obscuranceSum / weightSum;

		outShadowTerm = obscurance;
		outEdges = 0;
		outWeight = weightSum;
		return;
	}

	// calculate weighted average
	float obscurance = obscuranceSum / weightSum;

	// calculate fadeout (1 close, gradient, 0 far)
	float fadeOut = saturate(pixCenterPos.z * g_CACAOConsts.EffectFadeOutMul + g_CACAOConsts.EffectFadeOutAdd);

	// Reduce the SSAO shadowing if we're on the edge to remove artifacts on edges (we don't care for the lower quality one)
	if (!adaptiveBase && (qualityLevel >= SSAO_DEPTH_BASED_EDGES_ENABLE_AT_QUALITY_PRESET))
	{
		// float edgeCount = dot( 1.0-edgesLRTB, float4( 1.0, 1.0, 1.0, 1.0 ) );

		// when there's more than 2 opposite edges, start fading out the occlusion to reduce aliasing artifacts
		float edgeFadeoutFactor = saturate((1.0 - edgesLRTB.x - edgesLRTB.y) * 0.35) + saturate((1.0 - edgesLRTB.z - edgesLRTB.w) * 0.35);

		// (experimental) if you want to reduce the effect next to any edge
		// edgeFadeoutFactor += 0.1 * saturate( dot( 1 - edgesLRTB, float4( 1, 1, 1, 1 ) ) );

		fadeOut *= saturate(1.0 - edgeFadeoutFactor);
	}

	// same as a bove, but a lot more conservative version
	// fadeOut *= saturate( dot( edgesLRTB, float4( 0.9, 0.9, 0.9, 0.9 ) ) - 2.6 );

	// strength
	obscurance = g_CACAOConsts.EffectShadowStrength * obscurance;

	// clamp
	obscurance = min(obscurance, g_CACAOConsts.EffectShadowClamp);

	// fadeout
	obscurance *= fadeOut;

	// conceptually switch to occlusion with the meaning being visibility (grows with visibility, occlusion == 1 implies full visibility), 
	// to be in line with what is more commonly used.
	float occlusion = 1.0 - obscurance;

	// modify the gradient
	// note: this cannot be moved to a later pass because of loss of precision after storing in the render target
	occlusion = pow(saturate(occlusion), g_CACAOConsts.EffectShadowPow);

	// outputs!
	outShadowTerm = occlusion;    // Our final 'occlusion' term (0 means fully occluded, 1 means fully lit)
	outEdges = edgesLRTB;    // These are used to prevent blurring across edges, 1 means no edge, 0 means edge, 0.5 means half way there, etc.
	outWeight = weightSum;
}

[numthreads(GENERATE_WIDTH, GENERATE_HEIGHT, 1)]
void CSGenerateQ0(uint2 coord : SV_DispatchThreadID)
{
	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 0, false);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = PackEdges(float4(1, 1, 1, 1)); // no edges in low quality
	g_SSAOOutput[uint3(coord, 0)] = out0;
}

[numthreads(GENERATE_WIDTH, GENERATE_HEIGHT, 1)]
void CSGenerateQ1(uint2 coord : SV_DispatchThreadID)
{
	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 1, false);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = PackEdges(outEdges);
	g_SSAOOutput[uint3(coord, 0)] = out0;
}

[numthreads(GENERATE_WIDTH, GENERATE_HEIGHT, 1)]
void CSGenerateQ2(uint2 coord : SV_DispatchThreadID)
{
	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 2, false);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = PackEdges(outEdges);
	g_SSAOOutput[uint3(coord, 0)] = out0;
}

[numthreads(GENERATE_WIDTH, GENERATE_HEIGHT, 1)]
void CSGenerateQ3Base(uint2 coord : SV_DispatchThreadID)
{
	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 3, true);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = outWeight / ((float)SSAO_ADAPTIVE_TAP_BASE_COUNT * 4.0); //0.0; //frac(outWeight / 6.0);// / (float)(SSAO_MAX_TAPS * 4.0);
	g_SSAOOutput[uint3(coord, 0)] = out0;
}

[numthreads(GENERATE_WIDTH, GENERATE_HEIGHT, 1)]
void CSGenerateQ3(uint2 coord : SV_DispatchThreadID)
{

	float2 inPos = (float2)coord;
	float   outShadowTerm;
	float   outWeight;
	float4  outEdges;
	GenerateSSAOShadowsInternal(outShadowTerm, outEdges, outWeight, inPos.xy, 3, false);
	float2 out0;
	out0.x = outShadowTerm;
	out0.y = PackEdges(outEdges);
	g_SSAOOutput[uint3(coord, 0)] = out0;
}




// =======================================================
// Apply

Texture2DArray     g_ApplyFinalSSAO : register(t0);
RWTexture2D<float> g_ApplyOutput    : register(u0);


[numthreads(APPLY_WIDTH, APPLY_HEIGHT, 1)]
void CSApply(uint2 coord : SV_DispatchThreadID)
{
	float ao;
	float2 inPos = coord;
	uint2 pixPos = coord;
	uint2 pixPosHalf = pixPos / uint2(2, 2);

	// calculate index in the four deinterleaved source array texture
	int mx = (pixPos.x % 2);
	int my = (pixPos.y % 2);
	int ic = mx + my * 2;       // center index
	int ih = (1 - mx) + my * 2;   // neighbouring, horizontal
	int iv = mx + (1 - my) * 2;   // neighbouring, vertical
	int id = (1 - mx) + (1 - my) * 2; // diagonal

	float2 centerVal = g_ApplyFinalSSAO.Load(int4(pixPosHalf, ic, 0)).xy;

	ao = centerVal.x;

#if 1   // change to 0 if you want to disable last pass high-res blur (for debugging purposes, etc.)
	float4 edgesLRTB = UnpackEdges(centerVal.y);

	// return 1.0 - float4( edgesLRTB.x, edgesLRTB.y * 0.5 + edgesLRTB.w * 0.5, edgesLRTB.z, 0.0 ); // debug show edges

	// convert index shifts to sampling offsets
	float fmx = (float)mx;
	float fmy = (float)my;

	// in case of an edge, push sampling offsets away from the edge (towards pixel center)
	float fmxe = (edgesLRTB.y - edgesLRTB.x);
	float fmye = (edgesLRTB.w - edgesLRTB.z);

	// calculate final sampling offsets and sample using bilinear filter
	float2  uvH = (inPos.xy + float2(fmx + fmxe - 0.5, 0.5 - fmy)) * 0.5 * g_CACAOConsts.SSAOBufferInverseDimensions;
	float   aoH = g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(uvH, ih), 0).x;
	float2  uvV = (inPos.xy + float2(0.5 - fmx, fmy - 0.5 + fmye)) * 0.5 * g_CACAOConsts.SSAOBufferInverseDimensions;
	float   aoV = g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(uvV, iv), 0).x;
	float2  uvD = (inPos.xy + float2(fmx - 0.5 + fmxe, fmy - 0.5 + fmye)) * 0.5 * g_CACAOConsts.SSAOBufferInverseDimensions;
	float   aoD = g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(uvD, id), 0).x;

	// reduce weight for samples near edge - if the edge is on both sides, weight goes to 0
	float4 blendWeights;
	blendWeights.x = 1.0;
	blendWeights.y = (edgesLRTB.x + edgesLRTB.y) * 0.5;
	blendWeights.z = (edgesLRTB.z + edgesLRTB.w) * 0.5;
	blendWeights.w = (blendWeights.y + blendWeights.z) * 0.5;

	// calculate weighted average
	float blendWeightsSum = dot(blendWeights, float4(1.0, 1.0, 1.0, 1.0));
	ao = dot(float4(ao, aoH, aoV, aoD), blendWeights) / blendWeightsSum;
#endif

	g_ApplyOutput[coord] = ao.x;
}


// edge-ignorant blur & apply (for the lowest quality level 0)
[numthreads(APPLY_WIDTH, APPLY_HEIGHT, 1)]
void CSNonSmartApply(uint2 tid : SV_DispatchThreadID)
{
	float2 inUV = float2(tid) * g_CACAOConsts.OutputBufferInverseDimensions;
	float a = g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(inUV.xy, 0), 0.0).x;
	float b = g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(inUV.xy, 1), 0.0).x;
	float c = g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(inUV.xy, 2), 0.0).x;
	float d = g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(inUV.xy, 3), 0.0).x;
	float avg = (a + b + c + d) * 0.25;
	g_ApplyOutput[tid] = avg;
}

// edge-ignorant blur & apply, skipping half pixels in checkerboard pattern (for the Lowest quality level 0 and Settings::SkipHalfPixelsOnLowQualityLevel == true )
[numthreads(APPLY_WIDTH, APPLY_HEIGHT, 1)]
void CSNonSmartHalfApply(uint2 tid : SV_DispatchThreadID)
{
	float2 inUV = float2(tid) * g_CACAOConsts.OutputBufferInverseDimensions;
	float a = g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(inUV.xy, 0), 0.0).x;
	float d = g_ApplyFinalSSAO.SampleLevel(g_LinearClampSampler, float3(inUV.xy, 3), 0.0).x;
	float avg = (a + d) * 0.5;
	g_ApplyOutput[tid] = avg;
}


// =============================================================================
// Prepare

Texture2D<float>          g_DepthIn    : register(t0);

groupshared uint s_PrepareMem[10][18];


min16float ScreenSpaceToViewSpaceDepth(min16float screenDepth)
{
	min16float depthLinearizeMul = min16float(g_CACAOConsts.DepthUnpackConsts.x);
	min16float depthLinearizeAdd = min16float(g_CACAOConsts.DepthUnpackConsts.y);

	// Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"

	// Set your depthLinearizeMul and depthLinearizeAdd to:
	// depthLinearizeMul = ( cameraClipFar * cameraClipNear) / ( cameraClipFar - cameraClipNear );
	// depthLinearizeAdd = cameraClipFar / ( cameraClipFar - cameraClipNear );

	return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

min16float4 ScreenSpaceToViewSpaceDepth4x(min16float4 screenDepths)
{
	min16float depthLinearizeMul = min16float(g_CACAOConsts.DepthUnpackConsts.x);
	min16float depthLinearizeAdd = min16float(g_CACAOConsts.DepthUnpackConsts.y);

	// Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"

	// Set your depthLinearizeMul and depthLinearizeAdd to:
	// depthLinearizeMul = ( cameraClipFar * cameraClipNear) / ( cameraClipFar - cameraClipNear );
	// depthLinearizeAdd = cameraClipFar / ( cameraClipFar - cameraClipNear );

	return depthLinearizeMul / (depthLinearizeAdd - screenDepths);
}

RWTexture2DArray<float> g_PrepareDepthsAndMips_OutMip0 : register(u0);
RWTexture2DArray<float> g_PrepareDepthsAndMips_OutMip1 : register(u1);
RWTexture2DArray<float> g_PrepareDepthsAndMips_OutMip2 : register(u2);
RWTexture2DArray<float> g_PrepareDepthsAndMips_OutMip3 : register(u3);

groupshared float s_PrepareDepthsAndMipsBuffer[4][8][8];

float MipSmartAverage(float4 depths)
{
	float closest = min(min(depths.x, depths.y), min(depths.z, depths.w));
	float falloffCalcMulSq = -1.0f / g_CACAOConsts.EffectRadius * g_CACAOConsts.EffectRadius;
	float4 dists = depths - closest.xxxx;
	float4 weights = saturate(dists * dists * falloffCalcMulSq + 1.0);
	return dot(weights, depths) / dot(weights, float4(1.0, 1.0, 1.0, 1.0));
}

min16float MipSmartAverage_16(min16float4 depths)
{
	min16float closest = min(min(depths.x, depths.y), min(depths.z, depths.w));
	min16float falloffCalcMulSq = min16float(-1.0f / g_CACAOConsts.EffectRadius * g_CACAOConsts.EffectRadius);
	min16float4 dists = depths - closest.xxxx;
	min16float4 weights = saturate(dists * dists * falloffCalcMulSq + 1.0);
	return dot(weights, depths) / dot(weights, min16float4(1.0, 1.0, 1.0, 1.0));
}

void PrepareDepthsAndMips(float4 samples, uint2 outputCoord, uint2 gtid)
{
	samples = ScreenSpaceToViewSpaceDepth(samples);

	s_PrepareDepthsAndMipsBuffer[0][gtid.x][gtid.y] = samples.w;
	s_PrepareDepthsAndMipsBuffer[1][gtid.x][gtid.y] = samples.z;
	s_PrepareDepthsAndMipsBuffer[2][gtid.x][gtid.y] = samples.x;
	s_PrepareDepthsAndMipsBuffer[3][gtid.x][gtid.y] = samples.y;

	g_PrepareDepthsAndMips_OutMip0[int3(outputCoord.x, outputCoord.y, 0)] = samples.w;
	g_PrepareDepthsAndMips_OutMip0[int3(outputCoord.x, outputCoord.y, 1)] = samples.z;
	g_PrepareDepthsAndMips_OutMip0[int3(outputCoord.x, outputCoord.y, 2)] = samples.x;
	g_PrepareDepthsAndMips_OutMip0[int3(outputCoord.x, outputCoord.y, 3)] = samples.y;

	uint depthArrayIndex = 2 * (gtid.y % 2) + (gtid.x % 2);
	uint2 depthArrayOffset = int2(gtid.x % 2, gtid.y % 2);
	int2 bufferCoord = int2(gtid) - int2(depthArrayOffset);

	outputCoord /= 2;
	GroupMemoryBarrierWithGroupSync();

	// if (stillAlive) <-- all threads alive here
	{
		float sample_00 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 0];
		float sample_01 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 1];
		float sample_10 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 1][bufferCoord.y + 0];
		float sample_11 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 1][bufferCoord.y + 1];

		float avg = MipSmartAverage(float4(sample_00, sample_01, sample_10, sample_11));
		g_PrepareDepthsAndMips_OutMip1[int3(outputCoord.x, outputCoord.y, depthArrayIndex)] = avg;
		s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x][bufferCoord.y] = avg;
	}

	bool stillAlive = gtid.x % 4 == depthArrayOffset.x && gtid.y % 4 == depthArrayOffset.y;

	outputCoord /= 2;
	GroupMemoryBarrierWithGroupSync();

	if (stillAlive)
	{
		float sample_00 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 0];
		float sample_01 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 2];
		float sample_10 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 2][bufferCoord.y + 0];
		float sample_11 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 2][bufferCoord.y + 2];

		float avg = MipSmartAverage(float4(sample_00, sample_01, sample_10, sample_11));
		g_PrepareDepthsAndMips_OutMip2[int3(outputCoord.x, outputCoord.y, depthArrayIndex)] = avg;
		s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x][bufferCoord.y] = avg;
	}

	stillAlive = gtid.x % 8 == depthArrayOffset.x && depthArrayOffset.y % 8 == depthArrayOffset.y;

	outputCoord /= 2;
	GroupMemoryBarrierWithGroupSync();

	if (stillAlive)
	{
		float sample_00 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 0];
		float sample_01 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 0][bufferCoord.y + 4];
		float sample_10 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 4][bufferCoord.y + 0];
		float sample_11 = s_PrepareDepthsAndMipsBuffer[depthArrayIndex][bufferCoord.x + 4][bufferCoord.y + 4];

		float avg = MipSmartAverage(float4(sample_00, sample_01, sample_10, sample_11));
		g_PrepareDepthsAndMips_OutMip3[int3(outputCoord.x, outputCoord.y, depthArrayIndex)] = avg;
	}
}

[numthreads(PREPARE_DEPTHS_AND_MIPS_WIDTH, PREPARE_DEPTHS_AND_MIPS_HEIGHT, 1)]
void CSPrepareDownsampledDepthsAndMips(uint2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
	int2 depthBufferCoord = 4 * tid.xy;
	int2 outputCoord = tid;

	float2 uv = (float2(depthBufferCoord)+0.5f) * g_CACAOConsts.DepthBufferInverseDimensions;
	float4 samples;
#if 1
	samples.x = g_DepthIn.SampleLevel(g_PointClampSampler, uv, 0, int2(0, 2));
	samples.y = g_DepthIn.SampleLevel(g_PointClampSampler, uv, 0, int2(2, 2));
	samples.z = g_DepthIn.SampleLevel(g_PointClampSampler, uv, 0, int2(2, 0));
	samples.w = g_DepthIn.SampleLevel(g_PointClampSampler, uv, 0, int2(0, 0));
#else
	samples.x = g_DepthIn[depthBufferCoord + uint2(0, 2)];
	samples.y = g_DepthIn[depthBufferCoord + uint2(2, 2)];
	samples.z = g_DepthIn[depthBufferCoord + uint2(2, 0)];
	samples.w = g_DepthIn[depthBufferCoord + uint2(0, 0)];
#endif

	PrepareDepthsAndMips(samples, outputCoord, gtid);
}

[numthreads(PREPARE_DEPTHS_AND_MIPS_WIDTH, PREPARE_DEPTHS_AND_MIPS_HEIGHT, 1)]
void CSPrepareNativeDepthsAndMips(uint2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
	int2 depthBufferCoord = 2 * tid.xy;
	int2 outputCoord = tid;

	float2 uv = (float2(depthBufferCoord)+0.5f) * g_CACAOConsts.DepthBufferInverseDimensions;
	float4 samples = g_DepthIn.GatherRed(g_PointClampSampler, uv);

	PrepareDepthsAndMips(samples, outputCoord, gtid);
}

RWTexture2DArray<float> g_PrepareDepthsOut : register(u0);

void PrepareDepths(float4 samples, uint2 tid)
{
	samples = ScreenSpaceToViewSpaceDepth(samples);
	g_PrepareDepthsOut[int3(tid.x, tid.y, 0)] = samples.w;
	g_PrepareDepthsOut[int3(tid.x, tid.y, 1)] = samples.z;
	g_PrepareDepthsOut[int3(tid.x, tid.y, 2)] = samples.x;
	g_PrepareDepthsOut[int3(tid.x, tid.y, 3)] = samples.y;
}

[numthreads(PREPARE_DEPTHS_WIDTH, PREPARE_DEPTHS_HEIGHT, 1)]
void CSPrepareDownsampledDepths(uint2 tid : SV_DispatchThreadID)
{
	int2 depthBufferCoord = 4 * tid.xy;

	float2 uv = (float2(depthBufferCoord)+0.5f) * g_CACAOConsts.DepthBufferInverseDimensions;
	float4 samples;
	samples.x = g_DepthIn.SampleLevel(g_PointClampSampler, uv, 0, int2(0, 2));
	samples.y = g_DepthIn.SampleLevel(g_PointClampSampler, uv, 0, int2(2, 2));
	samples.z = g_DepthIn.SampleLevel(g_PointClampSampler, uv, 0, int2(2, 0));
	samples.w = g_DepthIn.SampleLevel(g_PointClampSampler, uv, 0, int2(0, 0));
	
	PrepareDepths(samples, tid);
}

[numthreads(PREPARE_DEPTHS_WIDTH, PREPARE_DEPTHS_HEIGHT, 1)]
void CSPrepareNativeDepths(uint2 tid : SV_DispatchThreadID)
{
	int2 depthBufferCoord = 2 * tid.xy;

	float2 uv = (float2(depthBufferCoord)+0.5f) * g_CACAOConsts.DepthBufferInverseDimensions;
	float4 samples = g_DepthIn.GatherRed(g_PointClampSampler, uv);

	PrepareDepths(samples, tid);
}

[numthreads(PREPARE_DEPTHS_HALF_WIDTH, PREPARE_DEPTHS_HALF_HEIGHT, 1)]
void CSPrepareDownsampledDepthsHalf(uint2 tid : SV_DispatchThreadID)
{
	float sample_00 = g_DepthIn.Load(int3(4 * tid.x + 0, 4 * tid.y + 0, 0));
	float sample_11 = g_DepthIn.Load(int3(4 * tid.x + 2, 4 * tid.y + 2, 0));
	sample_00 = ScreenSpaceToViewSpaceDepth(sample_00);
	sample_11 = ScreenSpaceToViewSpaceDepth(sample_11);
	g_PrepareDepthsOut[int3(tid.x, tid.y, 0)] = sample_00;
	g_PrepareDepthsOut[int3(tid.x, tid.y, 3)] = sample_11;
}

[numthreads(PREPARE_DEPTHS_HALF_WIDTH, PREPARE_DEPTHS_HALF_HEIGHT, 1)]
void CSPrepareNativeDepthsHalf(uint2 tid : SV_DispatchThreadID)
{
	float sample_00 = g_DepthIn.Load(int3(2 * tid.x + 0, 2 * tid.y + 0, 0));
	float sample_11 = g_DepthIn.Load(int3(2 * tid.x + 1, 2 * tid.y + 1, 0));
	sample_00 = ScreenSpaceToViewSpaceDepth(sample_00);
	sample_11 = ScreenSpaceToViewSpaceDepth(sample_11);
	g_PrepareDepthsOut[int3(tid.x, tid.y, 0)] = sample_00;
	g_PrepareDepthsOut[int3(tid.x, tid.y, 3)] = sample_11;
}

groupshared float s_PrepareDepthsNormalsAndMipsBuffer[18][18];

RWTexture2DArray<float4> g_PrepareNormals_NormalOut : register(u0);

struct PrepareNormalsInputDepths
{
	float depth_10;
	float depth_20;

	float depth_01;
	float depth_11;
	float depth_21;
	float depth_31;

	float depth_02;
	float depth_12;
	float depth_22;
	float depth_32;

	float depth_13;
	float depth_23;
};

void PrepareNormals(PrepareNormalsInputDepths depths, float2 uv, float2 pixelSize, int2 normalCoord)
{
	float3 p_10 = NDCToViewspace(uv + float2(+0.0f, -1.0f) * pixelSize, depths.depth_10);
	float3 p_20 = NDCToViewspace(uv + float2(+1.0f, -1.0f) * pixelSize, depths.depth_20);

	float3 p_01 = NDCToViewspace(uv + float2(-1.0f, +0.0f) * pixelSize, depths.depth_01);
	float3 p_11 = NDCToViewspace(uv + float2(+0.0f, +0.0f) * pixelSize, depths.depth_11);
	float3 p_21 = NDCToViewspace(uv + float2(+1.0f, +0.0f) * pixelSize, depths.depth_21);
	float3 p_31 = NDCToViewspace(uv + float2(+2.0f, +0.0f) * pixelSize, depths.depth_31);

	float3 p_02 = NDCToViewspace(uv + float2(-1.0f, +1.0f) * pixelSize, depths.depth_02);
	float3 p_12 = NDCToViewspace(uv + float2(+0.0f, +1.0f) * pixelSize, depths.depth_12);
	float3 p_22 = NDCToViewspace(uv + float2(+1.0f, +1.0f) * pixelSize, depths.depth_22);
	float3 p_32 = NDCToViewspace(uv + float2(+2.0f, +1.0f) * pixelSize, depths.depth_32);

	float3 p_13 = NDCToViewspace(uv + float2(+0.0f, +2.0f) * pixelSize, depths.depth_13);
	float3 p_23 = NDCToViewspace(uv + float2(+1.0f, +2.0f) * pixelSize, depths.depth_23);

	float4 edges_11 = CalculateEdges(p_11.z, p_01.z, p_21.z, p_10.z, p_12.z);
	float4 edges_21 = CalculateEdges(p_21.z, p_11.z, p_31.z, p_20.z, p_22.z);
	float4 edges_12 = CalculateEdges(p_12.z, p_02.z, p_22.z, p_11.z, p_13.z);
	float4 edges_22 = CalculateEdges(p_22.z, p_12.z, p_32.z, p_21.z, p_23.z);

	float3 norm_11 = CalculateNormal(edges_11, p_11, p_01, p_21, p_10, p_12);
	float3 norm_21 = CalculateNormal(edges_21, p_21, p_11, p_31, p_20, p_22);
	float3 norm_12 = CalculateNormal(edges_12, p_12, p_02, p_22, p_11, p_13);
	float3 norm_22 = CalculateNormal(edges_22, p_22, p_12, p_32, p_21, p_23);

	g_PrepareNormals_NormalOut[int3(normalCoord, 0)] = float4(norm_11, 1.0f);
	g_PrepareNormals_NormalOut[int3(normalCoord, 1)] = float4(norm_21, 1.0f);
	g_PrepareNormals_NormalOut[int3(normalCoord, 2)] = float4(norm_12, 1.0f);
	g_PrepareNormals_NormalOut[int3(normalCoord, 3)] = float4(norm_22, 1.0f);
}

[numthreads(PREPARE_NORMALS_WIDTH, PREPARE_NORMALS_HEIGHT, 1)]
void CSPrepareDownsampledNormals(int2 tid : SV_DispatchThreadID)
{
	int2 depthCoord = 4 * tid + g_CACAOConsts.DepthBufferOffset;

	PrepareNormalsInputDepths depths;

	depths.depth_10 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+0, -2)));
	depths.depth_20 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+2, -2)));

	depths.depth_01 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(-2, +0)));
	depths.depth_11 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+0, +0)));
	depths.depth_21 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+2, +0)));
	depths.depth_31 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+4, +0)));

	depths.depth_02 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(-2, +2)));
	depths.depth_12 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+0, +2)));
	depths.depth_22 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+2, +2)));
	depths.depth_32 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+4, +2)));

	depths.depth_13 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+0, +4)));
	depths.depth_23 = ScreenSpaceToViewSpaceDepth(g_DepthIn.Load(int3(depthCoord, 0), int2(+2, +4)));

	float2 pixelSize = 2.0f * g_CACAOConsts.OutputBufferInverseDimensions; // 2.0f * g_CACAOConsts.DepthBufferInverseDimensions;
	float2 uv = (float2(4 * tid) + 0.5f) * g_CACAOConsts.OutputBufferInverseDimensions; // * g_CACAOConsts.SSAOBufferInverseDimensions;

	PrepareNormals(depths, uv, pixelSize, tid);
}

[numthreads(PREPARE_NORMALS_WIDTH, PREPARE_NORMALS_HEIGHT, 1)]
void CSPrepareNativeNormals(int2 tid : SV_DispatchThreadID)
{
	int2 depthCoord = 2 * tid + g_CACAOConsts.DepthBufferOffset;
	float2 depthBufferUV = (float2(depthCoord)-0.5f) * g_CACAOConsts.DepthBufferInverseDimensions;
	float4 samples_00 = g_DepthIn.GatherRed(g_PointClampSampler, depthBufferUV, int2(0, 0));
	float4 samples_10 = g_DepthIn.GatherRed(g_PointClampSampler, depthBufferUV, int2(2, 0));
	float4 samples_01 = g_DepthIn.GatherRed(g_PointClampSampler, depthBufferUV, int2(0, 2));
	float4 samples_11 = g_DepthIn.GatherRed(g_PointClampSampler, depthBufferUV, int2(2, 2));

	PrepareNormalsInputDepths depths;

	depths.depth_10 = ScreenSpaceToViewSpaceDepth(samples_00.z);
	depths.depth_20 = ScreenSpaceToViewSpaceDepth(samples_10.w);

	depths.depth_01 = ScreenSpaceToViewSpaceDepth(samples_00.x);
	depths.depth_11 = ScreenSpaceToViewSpaceDepth(samples_00.y);
	depths.depth_21 = ScreenSpaceToViewSpaceDepth(samples_10.x);
	depths.depth_31 = ScreenSpaceToViewSpaceDepth(samples_10.y);

	depths.depth_02 = ScreenSpaceToViewSpaceDepth(samples_01.w);
	depths.depth_12 = ScreenSpaceToViewSpaceDepth(samples_01.z);
	depths.depth_22 = ScreenSpaceToViewSpaceDepth(samples_11.w);
	depths.depth_32 = ScreenSpaceToViewSpaceDepth(samples_11.z);

	depths.depth_13 = ScreenSpaceToViewSpaceDepth(samples_01.y);
	depths.depth_23 = ScreenSpaceToViewSpaceDepth(samples_11.x);

	// use unused samples to make sure compiler doesn't overlap memory and put a sync
	// between loads
	float epsilon = (samples_00.w + samples_10.z + samples_01.x + samples_11.y) * 1e-20f;

	float2 pixelSize = g_CACAOConsts.OutputBufferInverseDimensions;
	float2 uv = (float2(2 * tid) + 0.5f + epsilon) * g_CACAOConsts.OutputBufferInverseDimensions;

	PrepareNormals(depths, uv, pixelSize, tid);
}

Texture2D<float4>        g_PrepareNormalsFromNormalsInput  : register(t0);
RWTexture2DArray<float4> g_PrepareNormalsFromNormalsOutput : register(u0);

float3 PrepareNormalsFromInputNormalsLoadNormal(int2 pos)
{
	float3 encodedNormal = g_PrepareNormalsFromNormalsInput.SampleLevel(g_PointClampSampler, (float2(pos)+0.5f) * g_CACAOConsts.OutputBufferInverseDimensions, 0).xyz;
	return DecodeNormal(encodedNormal);
}

[numthreads(PREPARE_NORMALS_FROM_INPUT_NORMALS_WIDTH, PREPARE_NORMALS_FROM_INPUT_NORMALS_HEIGHT, 1)]
void CSPrepareDownsampledNormalsFromInputNormals(int2 tid : SV_DispatchThreadID)
{
	int2 baseCoord = 4 * tid;
	g_PrepareNormalsFromNormalsOutput[uint3(tid, 0)] = float4(PrepareNormalsFromInputNormalsLoadNormal(baseCoord + int2(0, 0)), 1.0f);
	g_PrepareNormalsFromNormalsOutput[uint3(tid, 1)] = float4(PrepareNormalsFromInputNormalsLoadNormal(baseCoord + int2(2, 0)), 1.0f);
	g_PrepareNormalsFromNormalsOutput[uint3(tid, 2)] = float4(PrepareNormalsFromInputNormalsLoadNormal(baseCoord + int2(0, 2)), 1.0f);
	g_PrepareNormalsFromNormalsOutput[uint3(tid, 3)] = float4(PrepareNormalsFromInputNormalsLoadNormal(baseCoord + int2(2, 2)), 1.0f);
}

[numthreads(PREPARE_NORMALS_FROM_INPUT_NORMALS_WIDTH, PREPARE_NORMALS_FROM_INPUT_NORMALS_HEIGHT, 1)]
void CSPrepareNativeNormalsFromInputNormals(int2 tid : SV_DispatchThreadID)
{
	int2 baseCoord = 2 * tid;
	g_PrepareNormalsFromNormalsOutput[uint3(tid, 0)] = float4(PrepareNormalsFromInputNormalsLoadNormal(baseCoord + int2(0, 0)), 1.0f);
	g_PrepareNormalsFromNormalsOutput[uint3(tid, 1)] = float4(PrepareNormalsFromInputNormalsLoadNormal(baseCoord + int2(1, 0)), 1.0f);
	g_PrepareNormalsFromNormalsOutput[uint3(tid, 2)] = float4(PrepareNormalsFromInputNormalsLoadNormal(baseCoord + int2(0, 1)), 1.0f);
	g_PrepareNormalsFromNormalsOutput[uint3(tid, 3)] = float4(PrepareNormalsFromInputNormalsLoadNormal(baseCoord + int2(1, 1)), 1.0f);
}

// ======================================================================================
// importance map stuff

Texture2DArray<float2> g_ImportanceFinalSSAO : register(t0);
RWTexture2D<float>     g_ImportanceOut       : register(u0);

[numthreads(IMPORTANCE_MAP_WIDTH, IMPORTANCE_MAP_HEIGHT, 1)]
void CSGenerateImportanceMap(uint2 tid : SV_DispatchThreadID)
{
	uint2 basePos = tid * 2;

	float2 baseUV = (float2(basePos)+float2(0.5f, 0.5f)) * g_CACAOConsts.SSAOBufferInverseDimensions;

	float avg = 0.0;
	float minV = 1.0;
	float maxV = 0.0;
	[unroll]
	for (int i = 0; i < 4; i++)
	{
		float4 vals = g_ImportanceFinalSSAO.GatherRed(g_PointClampSampler, float3(baseUV, i));

		// apply the same modifications that would have been applied in the main shader
		vals = g_CACAOConsts.EffectShadowStrength * vals;

		vals = 1 - vals;

		vals = pow(saturate(vals), g_CACAOConsts.EffectShadowPow);

		avg += dot(float4(vals.x, vals.y, vals.z, vals.w), float4(1.0 / 16.0, 1.0 / 16.0, 1.0 / 16.0, 1.0 / 16.0));

		maxV = max(maxV, max(max(vals.x, vals.y), max(vals.z, vals.w)));
		minV = min(minV, min(min(vals.x, vals.y), min(vals.z, vals.w)));
	}

	float minMaxDiff = maxV - minV;

	g_ImportanceOut[tid] = pow(saturate(minMaxDiff * 2.0), 0.8);
}

Texture2D<float>   g_ImportanceAIn  : register(t0);
RWTexture2D<float> g_ImportanceAOut : register(u0);

static const float cSmoothenImportance = 1.0;

[numthreads(IMPORTANCE_MAP_A_WIDTH, IMPORTANCE_MAP_A_HEIGHT, 1)]
void CSPostprocessImportanceMapA(uint2 tid : SV_DispatchThreadID)
{
	float2 uv = (float2(tid)+0.5f) * g_CACAOConsts.ImportanceMapInverseDimensions;

	float centre = g_ImportanceAIn.SampleLevel(g_LinearClampSampler, uv, 0.0).x;
	//return centre;

	float2 halfPixel = 0.5f * g_CACAOConsts.ImportanceMapInverseDimensions;

	float4 vals;
	vals.x = g_ImportanceAIn.SampleLevel(g_LinearClampSampler, uv + float2(-halfPixel.x * 3, -halfPixel.y), 0.0).x;
	vals.y = g_ImportanceAIn.SampleLevel(g_LinearClampSampler, uv + float2(+halfPixel.x, -halfPixel.y * 3), 0.0).x;
	vals.z = g_ImportanceAIn.SampleLevel(g_LinearClampSampler, uv + float2(+halfPixel.x * 3, +halfPixel.y), 0.0).x;
	vals.w = g_ImportanceAIn.SampleLevel(g_LinearClampSampler, uv + float2(-halfPixel.x, +halfPixel.y * 3), 0.0).x;

	float avgVal = dot(vals, float4(0.25, 0.25, 0.25, 0.25));
	vals.xy = max(vals.xy, vals.zw);
	float maxVal = max(centre, max(vals.x, vals.y));

	g_ImportanceAOut[tid] = lerp(maxVal, avgVal, cSmoothenImportance);
}

Texture2D<float>   g_ImportanceBIn          : register(t0);
RWTexture2D<float> g_ImportanceBOut         : register(u0);
RWTexture1D<uint>  g_ImportanceBLoadCounter : register(u1);

[numthreads(IMPORTANCE_MAP_B_WIDTH, IMPORTANCE_MAP_B_HEIGHT, 1)]
void CSPostprocessImportanceMapB(uint2 tid : SV_DispatchThreadID)
{
	float2 uv = (float2(tid)+0.5f) * g_CACAOConsts.ImportanceMapInverseDimensions;

	float centre = g_ImportanceBIn.SampleLevel(g_LinearClampSampler, uv, 0.0).x;
	//return centre;

	float2 halfPixel = 0.5f * g_CACAOConsts.ImportanceMapInverseDimensions;

	float4 vals;
	vals.x = g_ImportanceBIn.SampleLevel(g_LinearClampSampler, uv + float2(-halfPixel.x, -halfPixel.y * 3), 0.0).x;
	vals.y = g_ImportanceBIn.SampleLevel(g_LinearClampSampler, uv + float2(+halfPixel.x * 3, -halfPixel.y), 0.0).x;
	vals.z = g_ImportanceBIn.SampleLevel(g_LinearClampSampler, uv + float2(+halfPixel.x, +halfPixel.y * 3), 0.0).x;
	vals.w = g_ImportanceBIn.SampleLevel(g_LinearClampSampler, uv + float2(-halfPixel.x * 3, +halfPixel.y), 0.0).x;

	float avgVal = dot(vals, float4(0.25, 0.25, 0.25, 0.25));
	vals.xy = max(vals.xy, vals.zw);
	float maxVal = max(centre, max(vals.x, vals.y));

	float retVal = lerp(maxVal, avgVal, cSmoothenImportance);
	g_ImportanceBOut[tid] = retVal;

	// sum the average; to avoid overflowing we assume max AO resolution is not bigger than 16384x16384; so quarter res (used here) will be 4096x4096, which leaves us with 8 bits per pixel 
	uint sum = (uint)(saturate(retVal) * 255.0 + 0.5);

	// save every 9th to avoid InterlockedAdd congestion - since we're blurring, this is good enough; compensated by multiplying LoadCounterAvgDiv by 9
	if (((tid.x % 3) + (tid.y % 3)) == 0)
	{
		InterlockedAdd(g_ImportanceBLoadCounter[0], sum);
	}
}

// ============================================================================================
// bilateral upscale

RWTexture2D<float>     g_BilateralUpscaleOutput            : register(u0);

Texture2DArray<float2> g_BilateralUpscaleInput             : register(t0);

Texture2D<float>       g_BilateralUpscaleDepth             : register(t1);
Texture2DArray<float>  g_BilateralUpscaleDownscaledDepth   : register(t3);


uint DoublePackFloat16(float v)
{
	uint2 p = f32tof16(float2(v, v));
	return p.x | (p.y << 16);
}

#define BILATERAL_UPSCALE_BUFFER_WIDTH  (BILATERAL_UPSCALE_WIDTH  + 4)
#define BILATERAL_UPSCALE_BUFFER_HEIGHT (BILATERAL_UPSCALE_HEIGHT + 4 + 4)

struct BilateralBufferVal
{
	// float depth;
	// float ssaoVal;
	uint packedDepths;
	uint packedSsaoVals;
};

groupshared BilateralBufferVal s_BilateralUpscaleBuffer[BILATERAL_UPSCALE_BUFFER_WIDTH][BILATERAL_UPSCALE_BUFFER_HEIGHT];

void BilateralUpscaleNxN(int2 tid, uint2 gtid, uint2 gid, const int width, const int height)
{
	// fill in group shared buffer
	{
		uint threadNum = (gtid.y * BILATERAL_UPSCALE_WIDTH + gtid.x) * 3;
		uint2 bufferCoord = uint2(threadNum % BILATERAL_UPSCALE_BUFFER_WIDTH, threadNum / BILATERAL_UPSCALE_BUFFER_WIDTH);
		uint2 imageCoord = (gid * uint2(BILATERAL_UPSCALE_WIDTH, BILATERAL_UPSCALE_HEIGHT)) + bufferCoord - 2;

		for (int i = 0; i < 3; ++i)
		{
			// uint2 depthBufferCoord = imageCoord + 2 * g_CACAOConsts.DeinterleavedDepthBufferOffset;
			// uint3 depthArrayBufferCoord = uint3(depthBufferCoord / 2, 2 * (depthBufferCoord.y % 2) + depthBufferCoord.x % 2);
			uint3 ssaoArrayBufferCoord = uint3(imageCoord / 2, 2 * (imageCoord.y % 2) + imageCoord.x % 2);
			uint3 depthArrayBufferCoord = ssaoArrayBufferCoord + uint3(g_CACAOConsts.DeinterleavedDepthBufferOffset, 0);
			++imageCoord.x;

			BilateralBufferVal bufferVal;

			float depth = g_BilateralUpscaleDownscaledDepth[depthArrayBufferCoord];
			float ssaoVal = g_BilateralUpscaleInput.SampleLevel(g_PointClampSampler, float3((float2(ssaoArrayBufferCoord.xy) + 0.5f) * g_CACAOConsts.SSAOBufferInverseDimensions, ssaoArrayBufferCoord.z), 0).x;

			bufferVal.packedDepths = DoublePackFloat16(depth);
			bufferVal.packedSsaoVals = DoublePackFloat16(ssaoVal);

			s_BilateralUpscaleBuffer[bufferCoord.x + i][bufferCoord.y] = bufferVal;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	float depths[4];
	// load depths
	{
		int2 fullBufferCoord = 2 * tid;
		int2 fullDepthBufferCoord = fullBufferCoord + g_CACAOConsts.DepthBufferOffset;

		depths[0] = ScreenSpaceToViewSpaceDepth(g_BilateralUpscaleDepth[fullDepthBufferCoord + int2(0, 0)]);
		depths[1] = ScreenSpaceToViewSpaceDepth(g_BilateralUpscaleDepth[fullDepthBufferCoord + int2(1, 0)]);
		depths[2] = ScreenSpaceToViewSpaceDepth(g_BilateralUpscaleDepth[fullDepthBufferCoord + int2(0, 1)]);
		depths[3] = ScreenSpaceToViewSpaceDepth(g_BilateralUpscaleDepth[fullDepthBufferCoord + int2(1, 1)]);
	}
	min16float4 packedDepths = min16float4(depths[0], depths[1], depths[2], depths[3]);

	float totals[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float totalWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float2 pps[] = { float2(0.0f, 0.0f), float2(0.5f, 0.0f), float2(0.0f, 0.5f), float2(0.5f, 0.5f) };

	min16float4 packedTotals = min16float4(0.0f, 0.0f, 0.0f, 0.0f);
	min16float4 packedTotalWeights = min16float4(0.0f, 0.0f, 0.0f, 0.0f);

	int2 baseBufferCoord = gtid + int2(width, height);

	float distanceSigma = g_CACAOConsts.BilateralSimilarityDistanceSigma;
	min16float2 packedDistSigma = min16float2(1.0f / distanceSigma, 1.0f / distanceSigma);
	float sigma = g_CACAOConsts.BilateralSigmaSquared;
	min16float2 packedSigma = min16float2(1.0f / sigma, 1.0f / sigma);

	for (int x = -width; x <= width; ++x)
	{
		for (int y = -height; y <= height; ++y)
		{
			int2 bufferCoord = baseBufferCoord + int2(x, y);

			BilateralBufferVal bufferVal = s_BilateralUpscaleBuffer[bufferCoord.x][bufferCoord.y];

			min16float2 u = min16float2(x, x) + min16float2(0.0f, 0.5f);
			min16float2 v1 = min16float2(y, y) + min16float2(0.0f, 0.0f);
			min16float2 v2 = min16float2(y, y) + min16float2(0.5f, 0.5f);
			u = u * u;
			v1 = v1 * v1;
			v2 = v2 * v2;

			min16float2 dist1 = u + v1;
			min16float2 dist2 = u + v2;

			min16float2 wx1 = exp(-dist1 * packedSigma);
			min16float2 wx2 = exp(-dist2 * packedSigma);

			min16float2 bufferPackedDepths = UnpackFloat16(bufferVal.packedDepths);

#if 0
			min16float2 diff1 = abs(packedDepths.xy - bufferPackedDepths);
			min16float2 diff2 = abs(packedDepths.zw - bufferPackedDepths);
#else
			min16float2 diff1 = packedDepths.xy - bufferPackedDepths;
			min16float2 diff2 = packedDepths.zw - bufferPackedDepths;
			diff1 *= diff1;
			diff2 *= diff2;
#endif

			min16float2 wy1 = exp(-diff1 * packedDistSigma);
			min16float2 wy2 = exp(-diff2 * packedDistSigma);

			min16float2 weight1 = wx1 * wy1;
			min16float2 weight2 = wx2 * wy2;

			min16float2 packedSsaoVals = UnpackFloat16(bufferVal.packedSsaoVals);
			packedTotals.xy += packedSsaoVals * weight1;
			packedTotals.zw += packedSsaoVals * weight2;
			packedTotalWeights.xy += weight1;
			packedTotalWeights.zw += weight2;
		}
	}

	uint2 outputCoord = 2 * tid;
	min16float4 outputValues = packedTotals / packedTotalWeights;
	g_BilateralUpscaleOutput[outputCoord + int2(0, 0)] = outputValues.x; // totals[0] / totalWeights[0];
	g_BilateralUpscaleOutput[outputCoord + int2(1, 0)] = outputValues.y; // totals[1] / totalWeights[1];
	g_BilateralUpscaleOutput[outputCoord + int2(0, 1)] = outputValues.z; // totals[2] / totalWeights[2];
	g_BilateralUpscaleOutput[outputCoord + int2(1, 1)] = outputValues.w; // totals[3] / totalWeights[3];
}

[numthreads(BILATERAL_UPSCALE_WIDTH, BILATERAL_UPSCALE_HEIGHT, 1)]
void CSUpscaleBilateral5x5(int2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	BilateralUpscaleNxN(tid, gtid, gid, 2, 2);
}

[numthreads(BILATERAL_UPSCALE_WIDTH, BILATERAL_UPSCALE_HEIGHT, 1)]
void CSUpscaleBilateral7x7(int2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	BilateralUpscaleNxN(tid, gtid, gid, 3, 3);
}

[numthreads(BILATERAL_UPSCALE_WIDTH, BILATERAL_UPSCALE_HEIGHT, 1)]
void CSUpscaleBilateral5x5Half(int2 tid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID, uint2 gid : SV_GroupID)
{
	const int width = 2, height = 2;

	// fill in group shared buffer
	{
		uint threadNum = (gtid.y * BILATERAL_UPSCALE_WIDTH + gtid.x) * 3;
		uint2 bufferCoord = uint2(threadNum % BILATERAL_UPSCALE_BUFFER_WIDTH, threadNum / BILATERAL_UPSCALE_BUFFER_WIDTH);
		uint2 imageCoord = (gid * uint2(BILATERAL_UPSCALE_WIDTH, BILATERAL_UPSCALE_HEIGHT)) + bufferCoord - 2;

		for (int i = 0; i < 3; ++i)
		{
			// uint2 depthBufferCoord = imageCoord + g_CACAOConsts.DeinterleavedDepthBufferOffset;
			// uint3 depthArrayBufferCoord = uint3(depthBufferCoord / 2, 2 * (depthBufferCoord.y % 2) + depthBufferCoord.x % 2);
			uint idx = (imageCoord.y % 2) * 3;
			uint3 ssaoArrayBufferCoord = uint3(imageCoord / 2, idx);
			uint3 depthArrayBufferCoord = ssaoArrayBufferCoord + uint3(g_CACAOConsts.DeinterleavedDepthBufferOffset, 0);
			++imageCoord.x;

			BilateralBufferVal bufferVal;

			float depth = g_BilateralUpscaleDownscaledDepth[depthArrayBufferCoord];
			// float ssaoVal = g_BilateralUpscaleInput.SampleLevel(g_PointClampSampler, float3((float2(ssaoArrayBufferCoord.xy) + 0.5f) * g_CACAOConsts.HalfViewportPixelSize, ssaoArrayBufferCoord.z), 0);
			float ssaoVal = g_BilateralUpscaleInput.SampleLevel(g_PointClampSampler, float3((float2(ssaoArrayBufferCoord.xy) + 0.5f) * g_CACAOConsts.SSAOBufferInverseDimensions, ssaoArrayBufferCoord.z), 0).x;

			bufferVal.packedDepths = DoublePackFloat16(depth);
			bufferVal.packedSsaoVals = DoublePackFloat16(ssaoVal);

			s_BilateralUpscaleBuffer[bufferCoord.x + i][bufferCoord.y] = bufferVal;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	float depths[4];
	// load depths
	{
		int2 fullBufferCoord = 2 * tid;
		int2 fullDepthBufferCoord = fullBufferCoord + g_CACAOConsts.DepthBufferOffset;

		depths[0] = ScreenSpaceToViewSpaceDepth(g_BilateralUpscaleDepth[fullDepthBufferCoord + int2(0, 0)]);
		depths[1] = ScreenSpaceToViewSpaceDepth(g_BilateralUpscaleDepth[fullDepthBufferCoord + int2(1, 0)]);
		depths[2] = ScreenSpaceToViewSpaceDepth(g_BilateralUpscaleDepth[fullDepthBufferCoord + int2(0, 1)]);
		depths[3] = ScreenSpaceToViewSpaceDepth(g_BilateralUpscaleDepth[fullDepthBufferCoord + int2(1, 1)]);
	}
	min16float4 packedDepths = min16float4(depths[0], depths[1], depths[2], depths[3]);

	float totals[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float totalWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float2 pps[] = { float2(0.0f, 0.0f), float2(0.5f, 0.0f), float2(0.0f, 0.5f), float2(0.5f, 0.5f) };

	min16float4 packedTotals = min16float4(0.0f, 0.0f, 0.0f, 0.0f);
	min16float4 packedTotalWeights = min16float4(0.0f, 0.0f, 0.0f, 0.0f);

	int2 baseBufferCoord = gtid + int2(width, height);

	float distanceSigma = g_CACAOConsts.BilateralSimilarityDistanceSigma;
	min16float2 packedDistSigma = min16float2(1.0f / distanceSigma, 1.0f / distanceSigma);
	float sigma = g_CACAOConsts.BilateralSigmaSquared;
	min16float2 packedSigma = min16float2(1.0f / sigma, 1.0f / sigma);

	for (int x = -width; x <= width; ++x)
	{
		for (int y = -height; y <= height; ++y)
		{
			int2 bufferCoord = baseBufferCoord + int2(x, y);

			BilateralBufferVal bufferVal = s_BilateralUpscaleBuffer[bufferCoord.x][bufferCoord.y];

			min16float2 u = min16float2(x, x) + min16float2(0.0f, 0.5f);
			min16float2 v1 = min16float2(y, y) + min16float2(0.0f, 0.0f);
			min16float2 v2 = min16float2(y, y) + min16float2(0.5f, 0.5f);
			u = u * u;
			v1 = v1 * v1;
			v2 = v2 * v2;

			min16float2 dist1 = u + v1;
			min16float2 dist2 = u + v2;

			min16float2 wx1 = exp(-dist1 * packedSigma);
			min16float2 wx2 = exp(-dist2 * packedSigma);

			min16float2 bufferPackedDepths = UnpackFloat16(bufferVal.packedDepths);

#if 0
			min16float2 diff1 = abs(packedDepths.xy - bufferPackedDepths);
			min16float2 diff2 = abs(packedDepths.zw - bufferPackedDepths);
#else
			min16float2 diff1 = packedDepths.xy - bufferPackedDepths;
			min16float2 diff2 = packedDepths.zw - bufferPackedDepths;
			diff1 *= diff1;
			diff2 *= diff2;
#endif

			min16float2 wy1 = exp(-diff1 * packedDistSigma);
			min16float2 wy2 = exp(-diff2 * packedDistSigma);

			min16float2 weight1 = wx1 * wy1;
			min16float2 weight2 = wx2 * wy2;

			min16float2 packedSsaoVals = UnpackFloat16(bufferVal.packedSsaoVals);
			packedTotals.xy += packedSsaoVals * weight1;
			packedTotals.zw += packedSsaoVals * weight2;
			packedTotalWeights.xy += weight1;
			packedTotalWeights.zw += weight2;
		}
	}

	uint2 outputCoord = 2 * tid;
	min16float4 outputValues = packedTotals / packedTotalWeights;
	g_BilateralUpscaleOutput[outputCoord + int2(0, 0)] = outputValues.x; // totals[0] / totalWeights[0];
	g_BilateralUpscaleOutput[outputCoord + int2(1, 0)] = outputValues.y; // totals[1] / totalWeights[1];
	g_BilateralUpscaleOutput[outputCoord + int2(0, 1)] = outputValues.z; // totals[2] / totalWeights[2];
	g_BilateralUpscaleOutput[outputCoord + int2(1, 1)] = outputValues.w; // totals[3] / totalWeights[3];
}


#undef BILATERAL_UPSCALE_BUFFER_WIDTH
#undef BILATERAL_UPSCALE_BUFFER_HEIGHT
