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

// Sources:
// Cem Yuksel / Interactive Graphics 18 - Tessellation Shaders: https://www.youtube.com/watch?v=OqRMNrvu6TE
// https://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-8-adding-tessellation/
// ----------------------------------------------------------------------------------------------------------------
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-tessellation
// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics#system-value-semantics
//
//  VS   // passthrough vertex shader
//  |
//  HS   // once per patch, input: control points for surface | output: geometry patch (quad/tri/line) control points
//  |
//  TS   // fixed function tessellation/sundivision, vertex transformation
//  |
//  DS   // per vertex on tessellated mesh, handles transformation etc 
//  |
//  GS   // 
//  |
//  PS
// ----------------------------------------------------------------------------------------------------------------

struct VSInput
{
	float3 vLocalPosition : POSITION;
	float3 vNormal : NORMAL;
	float3 vTangent : TANGENT;
	float2 uv0 : TEXCOORD0;
	uint InstanceID : SV_InstanceID;
};

struct HSInput // control point
{
	float4 vPosition : SV_POSITION;
	float3 vNormal   : NORMAL;
	float3 vTangent  : TANGENT;
};

struct HSOutput // control point
{
	float4 vPosition : POSITION0;
	float3 vNormal   : NORMAL;
	float3 vTangent  : TANGENT;
};

// HS patch constants
struct HSOutputTriPatchConstants
{
	float EdgeTessFactor[3] : SV_TessFactor;
	float InsideTessFactor  : SV_InsideTessFactor;
};
struct HSOutputQuadPatchConstants
{
	float EdgeTessFactor[4]   : SV_TessFactor;
	float InsideTessFactor[2] : SV_InsideTessFactor;
};

// ----------------------------------------------------------------------------------------------------------------

#define VQ_GPU 1
#include "TerrainConstantBufferData.h"
#include "Lighting.hlsl"

cbuffer CBPerFrame     : register(b0) { PerFrameData cbPerFrame; }
cbuffer CBPerView      : register(b1) { PerViewData cbPerView;   }
cbuffer CBTerrain      : register(b2) { TerrainParams trrn; }
cbuffer CBTessellation : register(b3) { TerrainTessellationParams tess; }

SamplerState LinearSampler : register(s0);
Texture2D     texHeightmap : register(t0);
Texture2D    texDiffusemap : register(t1);

// ----------------------------------------------------------------------------------------------------------------

struct PSInput
{
	float4 ClipPos : SV_Position;
	float3 WorldPos : COLOR;
	float3 Normal : NORMAL;
	float3 Tangent : TANGENT;
	float2 uv0 : TEXCOORD0;
};
PSInput VSMain_Heightmap(VSInput v)
{
	const float heightMapSample = texHeightmap.SampleLevel(LinearSampler, v.uv0, 0).x;
	const float heightOffset = trrn.fHeightScale * heightMapSample;
	const float3 positionWithHeightOffset = v.vLocalPosition + float3(0, heightOffset, 0);
	
	PSInput o;
	o.ClipPos = mul(trrn.worldViewProj, float4(positionWithHeightOffset, 1.0f));
	o.Normal = mul(trrn.matNormal, float4(v.vNormal, 0.0f)).xyz;
	o.Tangent;
	o.WorldPos = mul(trrn.world, float4(positionWithHeightOffset, 1.0f));
	o.uv0 = v.uv0;
	return o;
}

float4 PSMain_Heightmap(PSInput In) : SV_TARGET0
{
	const float3 VertNormalCS = normalize(In.Normal);
	
	//return float4(In.uv0, 0, 1);
	
	BRDF_Surface Surface = (BRDF_Surface) 0;
	Surface.diffuseColor = texDiffusemap.Sample(LinearSampler, In.uv0);
	
	Surface.specularColor = float3(1, 1, 1);
	//Surface.emissiveColor = HasEmissiveMap(TEX_CFG) ? Emissive * cbPerObject.materialData.emissiveColor : cbPerObject.materialData.emissiveColor;
	Surface.emissiveColor = Surface.diffuseColor;
	//Surface.emissiveIntensity = cbPerObject.materialData.emissiveIntensity;
	Surface.roughness = 1.0f;
	Surface.metalness = 0.0f;
	
	const float3 N = normalize(In.Normal);
	const float3 T = normalize(In.Tangent);
	//float3 Normal = texNormals.Sample(AnisoSampler, uv).rgb;
	//Surface.N = length(Normal) < 0.01 ? N : UnpackNormal(Normal, N, T);
	Surface.N = N;
	
	const float AmbientLight = 0.011f;
	float3 I_total = float3(AmbientLight, AmbientLight, AmbientLight);
	
	// lighting & surface parameters (World Space)
	const float3 P = In.WorldPos;
	const float3 V = normalize(cbPerView.CameraPosition - P);
	const float2 screenSpaceUV = In.ClipPos.xy / cbPerView.ScreenDimensions;
	for (int s = 0; s < cbPerFrame.Lights.numSpotLights; ++s)
	{
		I_total += CalculateSpotLightIllumination(cbPerFrame.Lights.spot_lights[s], Surface, P, V);
	}
	
	return float4(Surface.diffuseColor, 1);
	return float4(I_total, 1.0f);
	
	//return float4(VertNormalCS, 1);
	//return float4(0, 0.5, 0, 1);
}


#if 0
HSInput VSMain(VSInput v)
{
	HSInput o;
	o.vPosition = float4(v.vLocalPosition, 1.0f);
	o.vNormal = v.vNormal;
	o.vTangent = v.vTangent;
	return o;
}


#define NUM_CONTROL_POINTS 3
HSOutputTriPatchConstants CalcHSPatchConstants(InputPatch<HSInput, NUM_CONTROL_POINTS> ip, uint PatchID : SV_PrimitiveID)
{
	HSOutputTriPatchConstants c;
	c.EdgeTessFactor[0] = 4; // 
	c.EdgeTessFactor[1] = 4; // 
	c.EdgeTessFactor[2] = 4;
	c.InsideTessFactor  = 4;
	return c;
}

// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-hull-shader-design
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-hull-shader-create
//
// control point phase + patch-constant phase
// input : 1-32 control points 
// output: 1-32 control points  --> DS
//         patch constants      --> DS
//         tessellation factors --> DS + TS
//
[domain("tri")]                   // tri, quad, or isoline.
[partitioning("fractional_even")] // integer, fractional_even, fractional_odd, or pow2.
[outputtopology("triangle_cw")]   // point, line, triangle_cw, or triangle_ccw
[outputcontrolspoints(NUM_CONTROL_POINTS)]
[patchconstantfunc("CalcHSPatchConstants")]
HSOutput HSMain(
	InputPatch<HSInput, NUM_CONTROL_POINTS> ip,
	uint i : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID
)
{
	HSOutput o;
	o.vPosition = ip[i].vPosition;
	o.vNormal = ip[i].vNormal;
	o.vTangent = ip[i].vTangent;
	return o;
}


//
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-domain-shader-design
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-domain-shader-create
//
void DSMain()
{
	
}


float4 PSMain(PSInput In) : SV_TARGET0
{
	return float4(0.0f, 0.5f, 0.5f, 1.0f);
}
#endif