//	VQE
//	Copyright(C) 2024  - Volkan Ilbeyli
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
bool IsPointOutOfFrustum(float4 PositionCS, float Tolerance)
{
	float3 culling = PositionCS.xyz;
	float3 w = PositionCS.w;
	return IsOutOfBounds(culling, float3(-w - Tolerance), float3(w + Tolerance));
}

struct AABB
{
	float3 Center;
	float3 Extents;
};

bool IsAABBBehindPlane(AABB aabb, float4 Plane, const bool bNormalize)
{
	if(bNormalize)
	{
		float L = length(Plane.xyz);
		Plane /= L;
	}
	
	// N : get the absolute value of the plane normal, so all {x,y,z} are positive
	//     which aligns well with the extents vector which is all positive due to
	//     storing the distances of maxs and mins.
	float3 N = abs(Plane.xyz);
	
	// r : how far away is the furthest point along the plane normal
	float r = dot(N, aabb.Extents);
	// Intuition: see https://fgiesen.wordpress.com/2010/10/17/view-frustum-culling/
	
	// signed distance of the center point of AABB to the plane
	float sd = dot(float4(aabb.Center, 1.0f), Plane);
	
	return (sd + r) < 0.0f;
}

// TODO:
// Triplanar mapping: https://gamedevelopment.tutsplus.com/use-tri-planar-texture-mapping-for-better-terrain--gamedev-13821a
// LODs : https://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-9-dynamic-level-of-detail/
// Normal reconstruction: http://www.thetenthplanet.de/archives/1180

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

// control points (HS+DS)
struct TessellationControlPoint
{
	float4 LocalPosition : POSITION;
	float4 ClipPosition  : COLOR0;
	float3 WorldPosition : COLOR1;
	float3 Normal        : NORMAL;
	float3 Tangent       : TANGENT;
	float2 uv0           : TEXCOORD0;
#if INSTANCED_DRAW
	uint instanceID      : TEXCOORD1;
#endif
};

#if ENABLE_TESSELLATION_SHADERS
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

#ifdef DOMAIN__TRIANGLE
	#define NUM_CONTROL_POINTS 3
	#define HSOutputPatchConstants HSOutputTriPatchConstants
#elif defined(DOMAIN__QUAD)
	#define NUM_CONTROL_POINTS 4
	#define HSOutputPatchConstants HSOutputQuadPatchConstants
#endif // DOMAIN__

AABB BuildAABB(InputPatch<TessellationControlPoint, NUM_CONTROL_POINTS> patch)
{
	float3 Mins = +1000000.0f.xxx; // TODO: FLT_MAX
	float3 Maxs = -1000000.0f.xxx; // TODO: FLT_MIN
	[unroll]
	for (int iCP = 0; iCP < NUM_CONTROL_POINTS; ++iCP)
	{
		Mins = min(Mins, patch[iCP].WorldPosition);
		Maxs = max(Maxs, patch[iCP].WorldPosition);
	}

	AABB aabb;
	aabb.Center  = 0.5f * (Mins + Maxs);
	aabb.Extents = 0.5f * (Maxs - Mins);
	return aabb;
}

bool ShouldFrustumCullPatch(float4 FrustumPlanes[6], InputPatch<TessellationControlPoint, NUM_CONTROL_POINTS> patch)
{
	AABB aabb = BuildAABB(patch);
	
	[unroll]
	for (int iPlane = 0; iPlane < 6; ++iPlane)
	{
		if (IsAABBBehindPlane(aabb, FrustumPlanes[iPlane], false))
		{
			return true;
		}
	}
	
	return false;
}

bool ShouldCullFace(InputPatch<TessellationControlPoint, NUM_CONTROL_POINTS> patch)
{
	// TODO
	return false;
}

#endif



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
TessellationControlPoint VSMain_Tess(VSInput vertex)
{
	TessellationControlPoint o;
	o.LocalPosition = float4(vertex.position, 1.0f);
	float fHeightOffset = texHeightmap.SampleLevel(LinearSamplerTess, vertex.uv, 0).r * cbPerObject.materialData.displacement;
	float4 DisplacedLocalPosition = float4(o.LocalPosition.xyz + float3(0, fHeightOffset, 0), 1.0f);
#if INSTANCED_DRAW
	o.WorldPosition = mul(cbPerObject.matWorld[vertex.instanceID], DisplacedLocalPosition).xyz;
	o.ClipPosition = mul(cbPerObject.matWorldViewProj[vertex.instanceID], o.LocalPosition);
	o.instanceID = vertex.instanceID;
#else
	o.WorldPosition = mul(cbPerObject.matWorld, DisplacedLocalPosition).xyz;
	o.ClipPosition = mul(cbPerObject.matWorldViewProj, o.LocalPosition);
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
// https://bruop.github.io/frustum_culling/
// http://www.richardssoftware.net/2013/09/dynamic-terrain-rendering-with-slimdx.html
// https://fgiesen.wordpress.com/2010/10/17/view-frustum-culling/
// https://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-10-view-frustum-culling/
// ----------------------------------------------------------------------------------------------------------------
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-tessellation
// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics#system-value-semantics

HSOutputPatchConstants CalcHSPatchConstants(
	InputPatch<TessellationControlPoint, NUM_CONTROL_POINTS> patch, 
	uint PatchID : SV_PrimitiveID
)
{
	float CullMask = select(tess.bFrustumCull && ShouldFrustumCullPatch(cbPerView.WorldFrustumPlanes, patch), 0.0f, 1.0f);
	CullMask += select(tess.bFaceCull && ShouldCullFace(patch), 0.0f, 1.0f);
	CullMask = saturate(CullMask); //[0,N] -> [0,1]
	
	HSOutputPatchConstants c;

#ifdef DOMAIN__TRIANGLE
	c.EdgeTessFactor[0] = tess.TriEdgeTessFactor.x * CullMask;
	c.EdgeTessFactor[1] = tess.TriEdgeTessFactor.y * CullMask;
	c.EdgeTessFactor[2] = tess.TriEdgeTessFactor.z * CullMask;
	c.InsideTessFactor = tess.TriInnerTessFactor;
#elif defined(DOMAIN__QUAD)
	c.EdgeTessFactor[0] = tess.QuadEdgeTessFactor.x * CullMask;
	c.EdgeTessFactor[1] = tess.QuadEdgeTessFactor.y * CullMask;
	c.EdgeTessFactor[2] = tess.QuadEdgeTessFactor.z * CullMask;
	c.EdgeTessFactor[3] = tess.QuadEdgeTessFactor.w * CullMask;
	c.InsideTessFactor[0] = tess.QuadInsideFactor.x;
	c.InsideTessFactor[1] = tess.QuadInsideFactor.y;
#elif defined(DOMAIN__LINE)
	// TODO:
#endif

	return c;
}

// Hull Shader
//
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-hull-shader-design
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-hull-shader-create
//
// control point phase + patch-constant phase
// input : 1-32 control points 
// output: 1-32 control points  --> DS
//         patch constants      --> Tessellator + DS
//         tessellation factors --> Tessellator

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-domain
// Domain: tri, quad, or isoline
#if DOMAIN__QUAD
[domain("quad")] 
#elif DOMAIN__TRIANGLE
[domain("tri")] 
#elif DOMAIN__LINE
[domain("isoline")]
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
TessellationControlPoint HSMain(
	InputPatch<TessellationControlPoint, NUM_CONTROL_POINTS> ip,
	uint i       : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID
)
{
	TessellationControlPoint o;
	o.LocalPosition = ip[i].LocalPosition;
	o.WorldPosition = ip[i].WorldPosition;
	o.Normal = ip[i].Normal;
	o.Tangent = ip[i].Tangent;
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
	lerp(\
		lerp(patch[1].attr, patch[2].attr, bary.x),\
		lerp(patch[0].attr, patch[3].attr, bary.x),\
		bary.y)

#if DOMAIN__TRIANGLE
#define INTERPOLATE_PATCH_ATTRIBUTE(attr, bary) INTERPOLATE3_PATCH_ATTRIBUTE(attr, bary)
#elif DOMAIN__QUAD || DOMAIN__LINE
#define INTERPOLATE_PATCH_ATTRIBUTE(attr, bary) INTERPOLATE2_PATCH_ATTRIBUTE(attr, bary)
#endif

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sv-domainlocation
#if DOMAIN__QUAD
[domain("quad")]
#elif DOMAIN__TRIANGLE
[domain("tri")]
#elif DOMAIN__LINE
[domain("isoline")]
#endif
PSInput DSMain(
	HSOutputPatchConstants In,
	const OutputPatch<TessellationControlPoint, NUM_CONTROL_POINTS> patch,
#if DOMAIN__QUAD || DOMAIN__LINE
	float2 bary : SV_DomainLocation
#elif DOMAIN__TRIANGLE
	float3 bary : SV_DomainLocation
#endif // DOMAIN__
)
{
	float2 uv = INTERPOLATE_PATCH_ATTRIBUTE(uv0, bary);
	float2 uvScale = cbPerObject.materialData.uvScaleOffset.xy;
	float2 uvOffst = cbPerObject.materialData.uvScaleOffset.zw;
	float2 uvTiled = uv * uvScale + uvOffst;
	
	float3 vPosition = INTERPOLATE_PATCH_ATTRIBUTE(LocalPosition.xyz, bary);
	
#if DOMAIN__TRIANGLE && 1 // generate normals & tangents
	float3 tangent   = patch[1].LocalPosition.xyz - patch[0].LocalPosition.xyz;
	float3 bitangent = patch[2].LocalPosition.xyz - patch[0].LocalPosition.xyz;
	float3 vNormal = normalize(cross(tangent, bitangent));
	float3 vTangent = float3(1,0,0);//normalize(tangent - dot(tangent, vNormal) * vNormal);
#else // read normals & tangents from VB
	float3 vNormal = INTERPOLATE_PATCH_ATTRIBUTE(Normal, bary);
	float3 vTangent = INTERPOLATE_PATCH_ATTRIBUTE(Tangent, bary);
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
