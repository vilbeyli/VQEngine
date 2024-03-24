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


//#define DOMAIN__TRIANGLE 1
//#define DOMAIN__QUAD 1
#define ENABLE_TESSELLATION_SHADERS defined(DOMAIN__TRIANGLE) || defined(DOMAIN__QUAD)


bool IsOutOfBounds(float3 p, float3 lo, float3 hi)
{
	return p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y || p.z < lo.z || p.z > hi.z;
}
bool IsPointOutOfFrustum(float4 PositionCS)
{
	float3 culling = PositionCS.xyz;
	float3 w = PositionCS.w;
	return IsOutOfBounds(culling, float3(-w), float3(w));
}
bool ShouldClipPatch(float4 p0PositionCS, float4 p1PositionCS, float4 p2PositionCS)
{
	bool bAllOutside = IsPointOutOfFrustum(p0PositionCS)
		&& IsPointOutOfFrustum(p1PositionCS)
		&& IsPointOutOfFrustum(p2PositionCS);
	
	return bAllOutside;
}


//---------------------------------------------------------------------------------------------------
//
// DATA
//
//---------------------------------------------------------------------------------------------------
struct VSInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float3 tangent  : TANGENT;
	float2 uv       : TEXCOORD0;
#if INSTANCED_DRAW
	uint instanceID : SV_InstanceID;
#endif
};

// control points
struct HSInput 
{
	float4 Position : POSITION;
	float4 ClipPosition : POSITION2;
	float3 Normal : NORMAL;
	float3 Tangent : TANGENT;
	float2 uv0 : TEXCOORD0;
#if INSTANCED_DRAW
	uint instanceID : INSTANCEID;
#endif
};
struct HSOutput
{
	float4 vPosition : POSITION;
	float4 ClipPosition : POSITION2;
	float3 vNormal : NORMAL;
	float3 vTangent : TANGENT;
	float2 uv0 : TEXCOORD0;
#if INSTANCED_DRAW
	uint instanceID : INSTANCEID;
#endif
};

// HS patch constants
struct HSOutputTriPatchConstants
{
	float EdgeTessFactor[3] : SV_TessFactor;
	float InsideTessFactor : SV_InsideTessFactor;
};
struct HSOutputQuadPatchConstants
{
	float EdgeTessFactor[4] : SV_TessFactor;
	float InsideTessFactor[2] : SV_InsideTessFactor;
};




//---------------------------------------------------------------------------------------------------
//
// RESOURCE BINDING
//
//---------------------------------------------------------------------------------------------------
cbuffer CBTessellation : register(b3) { TessellationParams tess; }
#if 0
cbuffer CBPerFrame     : register(b0) { PerFrameData cbPerFrame; }
cbuffer CBPerView      : register(b1) { PerViewData cbPerView; }
cbuffer CBPerObject    : register(b2) { PerObjectData cbPerObject; }

SamplerState LinearSampler        : register(s0);
SamplerState PointSampler         : register(s1);
SamplerState AnisoSampler         : register(s2);
SamplerState ClampedLinearSampler : register(s3);

Texture2D texDiffuse        : register(t0);
Texture2D texNormals        : register(t1);

Texture2D texHeightmap      : register(t8); // VS or PS
#endif

//---------------------------------------------------------------------------------------------------
//
// KERNELS
//
//---------------------------------------------------------------------------------------------------
HSInput VSMain_Tess(VSInput vertex)
{
	HSInput o;
	o.Position = float4(vertex.position, 1.0f);
#if INSTANCED_DRAW
	o.ClipPosition = mul(cbPerObject.matWorldViewProj[vertex.instanceID], o.Position);
	o.instanceID = vertex.instanceID;
#else
	o.ClipPosition = mul(cbPerObject.matWorldViewProj, o.Position);
#endif
	o.Normal = vertex.normal;
	o.Tangent = vertex.tangent;
	o.uv0 = vertex.uv;
	return o;
}

#if ENABLE_TESSELLATION_SHADERS
// Sources:
// Cem Yuksel / Interactive Graphics 18 - Tessellation Shaders: https://www.youtube.com/watch?v=OqRMNrvu6TE
// https://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-8-adding-tessellation/
// ----------------------------------------------------------------------------------------------------------------
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-tessellation
// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics#system-value-semantics

#ifdef DOMAIN__TRIANGLE
	#define NUM_CONTROL_POINTS 3
	#define HSOutputPatchConstants HSOutputTriPatchConstants
#elif defined(DOMAIN__QUAD)
	#define NUM_CONTROL_POINTS 4
	#define HSOutputPatchConstants HSOutputQuadPatchConstants
#endif // DOMAIN__

HSOutputPatchConstants CalcHSPatchConstants(InputPatch<HSInput, NUM_CONTROL_POINTS> patch, uint PatchID : SV_PrimitiveID)
{
	float ClipMask = 1.0f; // TODO: enable clipping
	//float ClipMask = ShouldClipPatch(patch[0].ClipPosition, patch[1].ClipPosition, patch[2].ClipPosition) ? 0.0f : 1.0f;
	//if(!trrn.bCullPatches) ClipMask = 1.0f;
	
	HSOutputPatchConstants c;
#ifdef DOMAIN__TRIANGLE
	c.EdgeTessFactor[0] = tess.TriEdgeTessFactor.x * ClipMask;
	c.EdgeTessFactor[1] = tess.TriEdgeTessFactor.y * ClipMask;
	c.EdgeTessFactor[2] = tess.TriEdgeTessFactor.z * ClipMask;
	c.InsideTessFactor = tess.TriInnerTessFactor;
#elif defined(DOMAIN__QUAD)
	c.EdgeTessFactor[0] = tess.QuadEdgeTessFactor.x * ClipMask;
	c.EdgeTessFactor[1] = tess.QuadEdgeTessFactor.y * ClipMask;
	c.EdgeTessFactor[2] = tess.QuadEdgeTessFactor.z * ClipMask;
	c.EdgeTessFactor[3] = tess.QuadEdgeTessFactor.w * ClipMask;
	c.InsideTessFactor[0] = tess.QuadInsideFactor.x;
	c.InsideTessFactor[1] = tess.QuadInsideFactor.y;
#endif
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

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-domain
// Domain: tri, quad, or isoline
#if DOMAIN__QUAD
[domain("quad")] 
#elif DOMAIN__TRIANGLE
[domain("tri")] 
#endif

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-partitioning
// Partitioning: integer, fractional_even, fractional_odd, pow2
#if PARTITIONING__INT
[partitioning("integer")]
#elif PARTITIONING__EVEN
[partitioning("fractional_even")]
#elif PARTITIONING__ODD
[partitioning("fractional_odd")]
#elif PARTITIONING__POW2
[partitioning("pow2")]
#endif

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-outputtopology
// Output topology: point, line, triangle_cw, triangle_ccw
#if OUTTOPO__POINT
[outputtopology("point")] 
#elif OUTTOPO__LINE
[outputtopology("line")] 
#elif OUTTOPO__TRI_CW
[outputtopology("triangle_cw")] 
#elif OUTTOPO__TRI_CCW
[outputtopology("triangle_ccw")] 
#endif

[outputcontrolpoints(NUM_CONTROL_POINTS)]
[patchconstantfunc("CalcHSPatchConstants")]
HSOutput HSMain(
	InputPatch<HSInput, NUM_CONTROL_POINTS> ip,
	uint i : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID
)
{
	HSOutput o;
	o.vPosition = ip[i].Position;
	o.vNormal = ip[i].Normal;
	o.vTangent = ip[i].Tangent;
	o.uv0 = ip[i].uv0;
	o.instanceID = ip[i].instanceID;
	return o;
}


//
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-domain-shader-design
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-domain-shader-create
//
#define INTERPOLATE3_ATTRIBUTE(attr, bary)\
	attr * bary.x +\
	attr * bary.y +\
	attr * bary.z

#define INTERPOLATE2_ATTRIBUTE(attr, bary)\
	attr * bary.x +\
	attr * bary.y

#define INTERPOLATE3_PATCH_ATTRIBUTE(attr, bary)\
	patch[0].attr * bary.x +\
	patch[1].attr * bary.y +\
	patch[2].attr * bary.z

#define INTERPOLATE2_PATCH_ATTRIBUTE(attr, bary)\
	patch[0].attr * bary.x +\
	patch[1].attr * bary.y

#if DOMAIN__TRIANGLE
#define INTERPOLATE_PATCH_ATTRIBUTE(attr, bary) INTERPOLATE3_PATCH_ATTRIBUTE(attr, bary)
#elif DOMAIN__QUAD
#define INTERPOLATE_PATCH_ATTRIBUTE(attr, bary) INTERPOLATE2_PATCH_ATTRIBUTE(attr, bary)
#endif

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sv-domainlocation
#if DOMAIN__QUAD
[domain("quad")]
#else
[domain("tri")]
#endif
PSInput DSMain(
	HSOutputPatchConstants In,
	const OutputPatch<HSOutput, NUM_CONTROL_POINTS> patch,
#if DOMAIN__QUAD
	float2 bary               : SV_DomainLocation,
	float EdgeTessFactor[4]   : SV_TessFactor,
	float InsideTessFactor[2] : SV_InsideTessFactor
#elif DOMAIN__TRIANGLE
	float3 bary               : SV_DomainLocation,
	float EdgeTessFactor[3]   : SV_TessFactor,
	float InsideTessFactor    : SV_InsideTessFactor
#endif // DOMAIN__
)
{
	float2 uv = INTERPOLATE_PATCH_ATTRIBUTE(uv0, bary);
	//uv = uv * cbPerObject.materialData.uvScaleOffset.xy + cbPerObject.materialData.uvScaleOffset.zw;
	
	float3 vPosition = INTERPOLATE_PATCH_ATTRIBUTE(vPosition, bary);
	
#if 0
	// generate normals and tangents
	float3 tangent   = patch[1].vPosition- patch[0].vPosition;
	float3 bitangent = patch[2].vPosition - patch[0].vPosition;
	float3 vNormal = normalize(cross(tangent, bitangent));
	float3 vTangent = float3(1,0,0);//normalize(tangent - dot(tangent, vNormal) * vNormal);
#else
	float3 vNormal = INTERPOLATE_PATCH_ATTRIBUTE(vNormal, bary);
	float3 vTangent = INTERPOLATE_PATCH_ATTRIBUTE(vTangent, bary);
#endif


	return TransformVertex(
	#if INSTANCED_DRAW
		patch[0].instanceID,
	#endif
		vPosition,
		vNormal,
		vTangent,
		uv
	);
}

#endif // ENABLE_TESSELLATION_SHADERS
