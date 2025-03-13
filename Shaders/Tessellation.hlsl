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


// TESSELLATION
// ==================================================================================================================================
//
// Sources:
// - Academy
//   - Real-Time Rendering Techniques w/ HW Tessellation: https://techmatt.github.io/pdfs/realtimeRendering.pdf
//   - Cem Yuksel / Interactive Graphics 18 - Tessellation Shaders: https://www.youtube.com/watch?v=OqRMNrvu6TE
// - Projects/Blogs
//   - https://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-8-adding-tessellation/
//   - https://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-10-view-frustum-culling/
//   - https://bruop.github.io/frustum_culling/
//   - http://www.richardssoftware.net/2013/09/dynamic-terrain-rendering-with-slimdx.html
//   - https://fgiesen.wordpress.com/2010/10/17/view-frustum-culling/
// ----------------------------------------------------------------------------------------------------------------
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-tessellation
// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics#system-value-semantics
//
// TODO:
// vertex packing: https://www.youtube.com/watch?v=5zlfJW2VGLM
// Triplanar mapping: https://gamedevelopment.tutsplus.com/use-tri-planar-texture-mapping-for-better-terrain--gamedev-13821a
// LODs : https://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-9-dynamic-level-of-detail/
// Normal reconstruction: http://www.thetenthplanet.de/archives/1180
// ==================================================================================================================================


//#define DOMAIN__TRIANGLE 1
//#define DOMAIN__QUAD 1
//#define DOMAIN__LINE 1
#define ENABLE_TESSELLATION_SHADERS defined(DOMAIN__TRIANGLE) || defined(DOMAIN__QUAD) || defined(DOMAIN__LINE)


//---------------------------------------------------------------------------------------------------
//
// UTILS
//
//---------------------------------------------------------------------------------------------------
bool IsOutOfBounds(float3 p, float3 lo, float3 hi)
{
	return p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y || p.z <= 0 || p.z > hi.z;
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

struct Frustum
{
	float4 Planes[6];
};

bool IsAABBBehindPlane(AABB aabb, float4 Plane, const bool bNormalize, float fTolerance)
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
	
	return (sd + r) < (0.0f + fTolerance);
}

bool ShouldFrustumCullTriangle(float4 FrustumPlanes[6], float3 V0WorldPos, float3 V1WorldPos, float3 V2WorldPos, float fTolerance)
{
	float3 Mins = +1000000.0f.xxx; // TODO: FLT_MAX
	float3 Maxs = -1000000.0f.xxx; // TODO: FLT_MIN
	AABB aabb;
	Mins = min(V0WorldPos, min(V1WorldPos, V2WorldPos));
	Maxs = max(V0WorldPos, max(V1WorldPos, V2WorldPos));
	aabb.Center = 0.5f * (Mins + Maxs);
	aabb.Extents = 0.5f * (Maxs - Mins);
	
	[unroll]
	for (int iPlane = 0; iPlane < 6; ++iPlane)
	{
		if (IsAABBBehindPlane(aabb, FrustumPlanes[iPlane], false, fTolerance))
		{
			return true;
		}
	}
	
	return false;
}

bool ShouldCullBackFace(float4 ClipPos0, float4 ClipPos1, float4 ClipPos2, float fTolerance)
{
	float3 NDC0 = ClipPos0.xyz / ClipPos0.w;
	float3 NDC1 = ClipPos1.xyz / ClipPos1.w;
	float3 NDC2 = ClipPos2.xyz / ClipPos2.w;
	float3 NormalNDC = cross(NDC1 - NDC0, NDC2 - NDC0);
	return NormalNDC.z > fTolerance;
}

//---------------------------------------------------------------------------------------------------
//
// DATA
//
//---------------------------------------------------------------------------------------------------
#if DOMAIN__TRIANGLE || DOMAIN__LINE
#define CONTROL_POINT_TANGENT_DATA 1
#endif
#if DOMAIN__TRIANGLE 
#define CONTROL_POINT_NORMAL_DATA  1
#endif

struct VSInput_Tess
{
	float3 position           : POSITION;
#if CONTROL_POINT_NORMAL_DATA // for tri
	float3 normal             : NORMAL;
#endif
#if CONTROL_POINT_TANGENT_DATA // for tri + lines
	float3 tangent            : TANGENT;
#endif
	float2 uv                 : TEXCOORD0;
#if INSTANCED_DRAW
	uint instanceID           : SV_InstanceID;
#endif
};

struct HSInput
{
	float3 LocalSpacePosition : POSITION0;
	float3 WorldSpacePosition : POSITION1;
	float2 uv                 : TEXCOORD0;
#if CONTROL_POINT_NORMAL_DATA
	float3 LocalSpaceNormal   : NORMAL;
#endif
#if CONTROL_POINT_TANGENT_DATA
	float3 LocalSpaceTangent  : TANGENT;
#endif
	
#if INSTANCED_DRAW
	uint instanceID           : TEXCOORD1;
#endif
};

struct DSInput
{
	float3 LocalSpacePosition : POSITION0;
	float2 uv                 : TEXCOORD0;

#if HS_OUTPUT_WORLD_SPACE_POSITIONS // WIP: HS coarse culling
	float3 WorldSpacePosition : POSITION1;
#endif	
#if CONTROL_POINT_NORMAL_DATA
	float3 LocalSpaceNormal   : NORMAL;
#endif
#if CONTROL_POINT_TANGENT_DATA
	float3 LocalSpaceTangent  : TANGENT;
#endif
};

#if ENABLE_TESSELLATION_SHADERS
// HS patch constants
struct HSOutputTriPatchConstants
{
	float EdgeTessFactor[3] : SV_TessFactor;
	float InsideTessFactor  : SV_InsideTessFactor;
#if INSTANCED_DRAW
	uint instanceID         : TEXCOORD1;
#endif
};
struct HSOutputQuadPatchConstants
{
	float EdgeTessFactor[4]   : SV_TessFactor;
	float InsideTessFactor[2] : SV_InsideTessFactor;
#if INSTANCED_DRAW
	uint instanceID         : TEXCOORD1;
#endif
};
struct HSOutputLinePatchConstants
{
	float EdgeTessFactor[2] : SV_TessFactor;
#if INSTANCED_DRAW
	uint instanceID         : TEXCOORD1;
#endif
};

#ifdef DOMAIN__TRIANGLE
	#define NUM_CONTROL_POINTS 3
	#define HSOutputPatchConstants HSOutputTriPatchConstants
#elif defined(DOMAIN__QUAD)
	#define NUM_CONTROL_POINTS 4
	#define HSOutputPatchConstants HSOutputQuadPatchConstants
#elif defined(DOMAIN__LINE)
	// control points should be provided by the user.
	#ifndef NUM_CONTROL_POINTS
	#define NUM_CONTROL_POINTS 2 
	#endif
	#define HSOutputPatchConstants HSOutputLinePatchConstants
#endif // DOMAIN__

#if 0 // WIP: HS coarse culling
AABB BuildAABB(InputPatch<HSInput, NUM_CONTROL_POINTS> patch)
{
	float3 Mins = +1000000.0f.xxx; // TODO: FLT_MAX
	float3 Maxs = -1000000.0f.xxx; // TODO: FLT_MIN
	[unroll]
	for (int iCP = 0; iCP < NUM_CONTROL_POINTS; ++iCP)
	{
		Mins = min(Mins, patch[iCP].WorldSpacePosition);
		Maxs = max(Maxs, patch[iCP].WorldSpacePosition);
	}

	AABB aabb;
	aabb.Center  = 0.5f * (Mins + Maxs);
	aabb.Extents = 0.5f * (Maxs - Mins);
	return aabb;
}

bool ShouldFrustumCullPatch(float4 FrustumPlanes[6], InputPatch<HSInput, NUM_CONTROL_POINTS> patch, float fTolerance)
{
	AABB aabb = BuildAABB(patch);
	
	//[unroll]
	for (int iPlane = 0; iPlane < 6; ++iPlane)
	{
		if (IsAABBBehindPlane(aabb, FrustumPlanes[iPlane], false, fTolerance))
		{
			return true;
		}
	}
	
	return false;
}

bool ShouldCullBackFace(InputPatch<TessellationControlPoint, NUM_CONTROL_POINTS> patch, float fTolerance)
{
	#if DOMAIN__TRIANGLE || DOMAIN__QUAD
	return ShouldCullBackFace(patch[0].ClipPosition, patch[2].ClipPosition, patch[2].ClipPosition, fTolerance);
	#else 
	// TODO: line
	return false;
	#endif
}
#endif// WIP: HS coarse culling

#endif // ENABLE_TESSELLATION_SHADERS



//---------------------------------------------------------------------------------------------------
//
// RESOURCE BINDING
//
//---------------------------------------------------------------------------------------------------
cbuffer CBTessellation : register(b3) { TessellationParams tess; } // TODO: move to stubs, get w/ function

//---------------------------------------------------------------------------------------------------
//
// VERTEX SHADER
//
//---------------------------------------------------------------------------------------------------
// includer of Tessellation.hlsl needs to define the following functions:
//   float3 CalcHeightOffset(float2 uv) { ... } // sample from heightmap, mul w/ max displacement factor
//   matrix GetWorldMatrix(uint InstanceID [optional]) { ... }
//   matrix GetWorldViewProjectionMatrix(uint InstanceID [optional]) { ... }
//   float2 GetUVScale()
//   float2 GetUVBias()
HSInput VSMain_Tess(VSInput_Tess vertex /*control point*/)
{
	float4 LocalSpacePosition = float4(vertex.position, 1.0f);
	float3 vHeightOffset = CalcHeightOffset(vertex.uv);
	float4 DisplacedLocalPosition = float4(LocalSpacePosition.xyz + vHeightOffset, 1.0f);

#if INSTANCED_DRAW
	matrix matW   = GetWorldMatrix(vertex.instanceID);
	matrix matWVP = GetWorldViewProjectionMatrix(vertex.instanceID);
#else
	matrix matW   = GetWorldMatrix();
	matrix matWVP = GetWorldViewProjectionMatrix();
#endif
	
	HSInput o;
	o.LocalSpacePosition = LocalSpacePosition.xyz;
	//o.ClipPosition = mul(matWVP, o.LocalPosition);
	o.WorldSpacePosition = mul(matW, LocalSpacePosition).xyz;
	o.uv = vertex.uv;
#if CONTROL_POINT_NORMAL_DATA
	o.LocalSpaceNormal = vertex.normal;
#endif
#if CONTROL_POINT_TANGENT_DATA
	o.LocalSpaceTangent = vertex.tangent;
#endif
#if INSTANCED_DRAW
	o.instanceID = vertex.instanceID;
#endif
	return o;
}

//---------------------------------------------------------------------------------------------------
//
// HULL SHADER
//
//---------------------------------------------------------------------------------------------------
float CalcTessFactor(float3 Point, float3 Eye, float fMinDist, float fMaxDist)
{
	float Distance = distance(Point, Eye);
	float s = saturate((Distance - fMinDist) / (fMaxDist - fMinDist));
	return pow(2, (lerp(6, 0, s))); // 2^6 = 64 max tess factor
}

#if ENABLE_TESSELLATION_SHADERS
HSOutputPatchConstants CalcHSPatchConstants(
	InputPatch<HSInput, NUM_CONTROL_POINTS> patch, 
	uint PatchID : SV_PrimitiveID
)
{	
	HSOutputPatchConstants c;

	// CULLING -------------------------------------------------------------------------------------
	// WIP: coarse culling on HS requries more work, disabled for now
	// WorldSpace AABB culling is incorrectly culling some patches when the camera is close
	// Also missing is the edge case of all 3 vertices of the triangle are outside frustum but its 
	// edges intersect the frustum (i.e. plane of triangle is visible within the plane)
	#if 0
	bool bCull = tess.IsFrustumCullingOn() && ShouldFrustumCullPatch(cbPerView.WorldFrustumPlanes, patch, tess.fHSFrustumCullEpsilon);
	if(!bCull)
		bCull = tess.IsFaceCullingOn() && ShouldCullBackFace(patch, tess.fHSFaceCullEpsilon);
	if(bCull)
	{
		#ifdef DOMAIN__TRIANGLE
		c.EdgeTessFactor[0] = 0;
		c.EdgeTessFactor[1] = 0;
		c.EdgeTessFactor[2] = 0;
		c.InsideTessFactor  = 0;
		#elif defined(DOMAIN__QUAD)
		c.EdgeTessFactor[0]   = 0;
		c.EdgeTessFactor[1]   = 0;
		c.EdgeTessFactor[2]   = 0;
		c.EdgeTessFactor[3]   = 0;
		c.InsideTessFactor[0] = 0;
		c.InsideTessFactor[1] = 0;
		#elif defined(DOMAIN__LINE)
		c.EdgeTessFactor[0]   = 0;
		c.EdgeTessFactor[1]   = 0;
		#endif // DOMAIN__

		return c;
	}
	#endif
	// ---------------------------------------------------------------------------------------------

	// Calculate Tessellation Factor
	float3 Eye = cbPerView.CameraPosition;

	float3 PatchCenter = float3(0,0,0);
	for (int i = 0; i < NUM_CONTROL_POINTS; ++i)
	{
		PatchCenter += patch[i].WorldSpacePosition;
	}
	PatchCenter /= NUM_CONTROL_POINTS;
	
	float fDTessMin = tess.fHSAdaptiveTessellationMinDist;
	float fDTessMax = tess.fHSAdaptiveTessellationMaxDist;
	
#ifdef DOMAIN__TRIANGLE
	
	if(tess.IsAdaptiveTessellationOn())
	{
		float3 e0 = 0.5f * (patch[1].WorldSpacePosition + patch[0].WorldSpacePosition);
		float3 e1 = 0.5f * (patch[2].WorldSpacePosition + patch[0].WorldSpacePosition);
		float3 e2 = 0.5f * (patch[2].WorldSpacePosition + patch[1].WorldSpacePosition);
	
		c.EdgeTessFactor[0] = CalcTessFactor(e0, Eye, fDTessMin, fDTessMax);
		c.EdgeTessFactor[1] = CalcTessFactor(e1, Eye, fDTessMin, fDTessMax);
		c.EdgeTessFactor[2] = CalcTessFactor(e2, Eye, fDTessMin, fDTessMax);
		c.InsideTessFactor  = CalcTessFactor(PatchCenter, Eye, fDTessMin, fDTessMax);
		return c;
	}
	
	c.EdgeTessFactor[0] = tess.TriEdgeTessFactor.x;
	c.EdgeTessFactor[1] = tess.TriEdgeTessFactor.y;
	c.EdgeTessFactor[2] = tess.TriEdgeTessFactor.z;
	c.InsideTessFactor  = tess.TriInnerTessFactor;
	
#elif defined(DOMAIN__QUAD)
	
	if(tess.IsAdaptiveTessellationOn())
	{
		float3 e0 = 0.5f * (patch[1].WorldSpacePosition + patch[0].WorldSpacePosition);
		float3 e1 = 0.5f * (patch[2].WorldSpacePosition + patch[1].WorldSpacePosition);
		float3 e2 = 0.5f * (patch[3].WorldSpacePosition + patch[2].WorldSpacePosition);
		float3 e3 = 0.5f * (patch[0].WorldSpacePosition + patch[3].WorldSpacePosition);
		float3 fCenter = CalcTessFactor(PatchCenter, Eye, fDTessMin, fDTessMax);

		c.EdgeTessFactor[0]   = CalcTessFactor(e0, Eye, fDTessMin, fDTessMax);
		c.EdgeTessFactor[1]   = CalcTessFactor(e1, Eye, fDTessMin, fDTessMax);
		c.EdgeTessFactor[2]   = CalcTessFactor(e2, Eye, fDTessMin, fDTessMax);
		c.EdgeTessFactor[3]   = CalcTessFactor(e3, Eye, fDTessMin, fDTessMax);
		c.InsideTessFactor[0] = fCenter;
		c.InsideTessFactor[1] = fCenter;
		return c;
	}
	
	c.EdgeTessFactor[0]   = tess.QuadEdgeTessFactor.x;
	c.EdgeTessFactor[1]   = tess.QuadEdgeTessFactor.y;
	c.EdgeTessFactor[2]   = tess.QuadEdgeTessFactor.z;
	c.EdgeTessFactor[3]   = tess.QuadEdgeTessFactor.w;
	c.InsideTessFactor[0] = tess.QuadInsideFactor.x;
	c.InsideTessFactor[1] = tess.QuadInsideFactor.y;
	
#elif defined(DOMAIN__LINE)
	c.EdgeTessFactor[0]   = 1.0f;
	c.EdgeTessFactor[1]   = 1.0f;
#endif

	
#if INSTANCED_DRAW
	c.instanceID = patch[0].instanceID;
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
//
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
DSInput HSMain(
	InputPatch<HSInput, NUM_CONTROL_POINTS> patch,
	uint i       : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID
)
{
	DSInput o;
	o.LocalSpacePosition = patch[i].LocalSpacePosition;
#if CONTROL_POINT_NORMAL_DATA
	o.LocalSpaceNormal = patch[i].LocalSpaceNormal;
#endif
#if CONTROL_POINT_TANGENT_DATA
	o.LocalSpaceTangent = patch[i].LocalSpaceTangent;
#endif
	o.uv = patch[i].uv;
	return o;
}


//---------------------------------------------------------------------------------------------------
//
// DOMAIN SHADER
//
//---------------------------------------------------------------------------------------------------
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


#define INTERPOLATE_TRI_PATCH_ATTRIBUTE(attr, bary)\
	patch[0].attr * bary.x +\
	patch[1].attr * bary.y +\
	patch[2].attr * bary.z
#define INTERPOLATE_QUAD_PATCH_ATTRIBUTE(attr, bary)\
	lerp(\
		lerp(patch[1].attr, patch[2].attr, bary.x),\
		lerp(patch[0].attr, patch[3].attr, bary.x),\
		bary.y)
#define INTERPOLATE_LINE_PATCH_ATTRIBUTE(attr, bary)\
	lerp(patch[0].attr, patch[1].attr, bary.x)

#if DOMAIN__TRIANGLE
#define INTERPOLATE_PATCH_ATTRIBUTE(attr, bary) INTERPOLATE_TRI_PATCH_ATTRIBUTE(attr, bary)
#elif DOMAIN__QUAD
#define INTERPOLATE_PATCH_ATTRIBUTE(attr, bary) INTERPOLATE_QUAD_PATCH_ATTRIBUTE(attr, bary)
#elif DOMAIN__LINE
#define INTERPOLATE_PATCH_ATTRIBUTE(attr, bary) INTERPOLATE_LINE_PATCH_ATTRIBUTE(attr, bary)
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
	const OutputPatch<DSInput, NUM_CONTROL_POINTS> patch,
#if DOMAIN__QUAD || DOMAIN__LINE
	float2 bary : SV_DomainLocation
#elif DOMAIN__TRIANGLE
	float3 bary : SV_DomainLocation
#endif // DOMAIN__
)
{
	float2 uv = INTERPOLATE_PATCH_ATTRIBUTE(uv, bary);
	float2 uvScale = GetUVScale();
	float2 uvOffst = GetUVOffset();
	float2 uvTiled = uv * uvScale + uvOffst;
	
	float3 vPosition = INTERPOLATE_PATCH_ATTRIBUTE(LocalSpacePosition.xyz, bary);
	
#if DOMAIN__TRIANGLE
	float3 vNormal = normalize(INTERPOLATE_PATCH_ATTRIBUTE(LocalSpaceNormal.xyz, bary));
	float3 vTangent = normalize(INTERPOLATE_PATCH_ATTRIBUTE(LocalSpaceTangent.xyz, bary));
#elif DOMAIN__QUAD
	float3 tangent   = patch[1].LocalSpacePosition.xyz - patch[0].LocalSpacePosition.xyz;
	float3 bitangent = patch[2].LocalSpacePosition.xyz - patch[0].LocalSpacePosition.xyz;
	float3 vNormal   = normalize(cross(tangent, bitangent));
	float3 vTangent  = normalize(tangent); //float3(1,0,0);//normalize(tangent - dot(tangent, vNormal) * vNormal);
#elif DOMAIN__LINE
	// TODO
	float3 tangent   = 0.0f.xxx;
	float3 bitangent = 0.0f.xxx;
	float3 vNormal = normalize(cross(tangent, bitangent));
	float3 vTangent = normalize(tangent); //float3(1,0,0);//normalize(tangent - dot(tangent, vNormal) * vNormal);
#endif

	// transform vertex
	PSInput TransformedVertex = TransformVertex(
	#if INSTANCED_DRAW
		In.instanceID,
	#endif
		vPosition,
		vNormal,
		vTangent,
		uv
	);

	return TransformedVertex;
}

#endif // ENABLE_TESSELLATION_SHADERS

//---------------------------------------------------------------------------------------------------
//
// GEOMETRY SHADER
//
//---------------------------------------------------------------------------------------------------
#if OUTTOPO__POINT
#define NUM_VERTS_PER_INPUT_PRIM     1
#elif OUTTOPO__LINE
#define NUM_VERTS_PER_INPUT_PRIM     2
#elif OUTTOPO__TRI_CW || OUTTOPO__TRI_CCW
#define NUM_VERTS_PER_INPUT_PRIM     3
#endif


#if ENABLE_GEOMETRY_SHADER

[maxvertexcount(NUM_VERTS_PER_INPUT_PRIM)]
void GSMain(
// input
#if OUTTOPO__POINT
	point PSInput Input[NUM_VERTS_PER_INPUT_PRIM]
#elif OUTTOPO__LINE
	line PSInput Input[NUM_VERTS_PER_INPUT_PRIM]
#elif OUTTOPO__TRI_CW || OUTTOPO__TRI_CCW
	triangle PSInput Input[NUM_VERTS_PER_INPUT_PRIM]
#endif

// output
#if OUTTOPO__TRI_CW || OUTTOPO__TRI_CCW
	, inout TriangleStream<PSInput> outTriStream
#elif OUTTOPO__LINE
	, inout LineStream<PSInput> outTriStream
#elif OUTTOPO__POINT
	, inout PointStream<PSInput> outTriStream
#endif
)
{	
	#if OUTTOPO__TRI_CW || OUTTOPO__TRI_CCW
	// cull triangle
	if(tess.IsFrustumCullingOn() && ShouldFrustumCullTriangle(cbPerView.WorldFrustumPlanes, Input[0].WorldSpacePosition, Input[1].WorldSpacePosition, Input[2].WorldSpacePosition, tess.fHSFrustumCullEpsilon))
		return;
	if (tess.IsFaceCullingOn() && ShouldCullBackFace(Input[0].position, Input[1].position, Input[2].position, tess.fHSFaceCullEpsilon)) // need a GSFaceCullEpsilon?
		return;
	#endif

	// fetch primitive data
	PSInput OutputVertex[NUM_VERTS_PER_INPUT_PRIM];
	[unroll] for (int iVert = 0; iVert < NUM_VERTS_PER_INPUT_PRIM; ++iVert)
	{
		OutputVertex[iVert] = Input[iVert];
	}
	
	// append to output
	[unroll] for (int iVert = 0; iVert < NUM_VERTS_PER_INPUT_PRIM; ++iVert)
	{
		outTriStream.Append(OutputVertex[iVert]);
	}
}

#endif // ENABLE_GEOMETRY_SHADER