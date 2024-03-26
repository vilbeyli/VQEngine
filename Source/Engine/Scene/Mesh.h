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
#pragma once

#include "../../Renderer/Renderer.h"
#include "../Culling.h"

#include <string>
#include <vector>
#include <limits>

#include <DirectXMath.h>

#define NOMINMAX
#undef max

enum EBuiltInMeshes
{
	TRIANGLE = 0,
	QUAD,
	CUBE,
	SPHERE,
	CYLINDER,
	CONE,
	GRID_SIMPLE_QUAD,
	GRID_DETAILED_QUAD0,
	GRID_DETAILED_QUAD1,
	GRID_DETAILED_QUAD2,
	
	NUM_BUILTIN_MESHES
};
namespace GeometryGenerator
{
	template<class TVertex, class TIndex = unsigned>
	struct GeometryData
	{
		static_assert(std::is_same<TIndex, unsigned>() || std::is_same<TIndex, unsigned short>()); // ensure UINT32 or UINT16 indices
		std::vector<std::vector<TVertex>>  LODVertices;
		std::vector<std::vector<TIndex> >  LODIndices;
		GeometryData(size_t NumLODs) : LODVertices(NumLODs), LODIndices(NumLODs) {}
		GeometryData() = delete;
	};

	template<class TVertex, class TIndex = unsigned>
	constexpr GeometryData<TVertex, TIndex> Triangle(float size);

	template<class TVertex, class TIndex = unsigned>
	constexpr GeometryData<TVertex, TIndex> Quad(float scale);

	template<class TVertex, class TIndex = unsigned>
	constexpr GeometryData<TVertex, TIndex> FullScreenQuad();

	template<class TVertex, class TIndex = unsigned>
	constexpr GeometryData<TVertex, TIndex> Cube();

	template<class TVertex, class TIndex = unsigned>
	constexpr GeometryData<TVertex, TIndex> Sphere(float radius = 1.0f, unsigned ringCount = 12, unsigned sliceCount = 12, int numLODLevels = 1);

	template<class TVertex, class TIndex = unsigned>
	constexpr GeometryData<TVertex, TIndex> Grid(float Width, float Depth, unsigned NumVertsX, unsigned NumVertsY, int NumLODLevels);

	template<class TVertex, class TIndex = unsigned>
	constexpr GeometryData<TVertex, TIndex> Cylinder(float height = 3.0f, float topRadius = 1.0f, float bottomRadius = 1.0f, unsigned sliceCount = 18, unsigned stackCount = 6, int numLODLevels = 1);

	template<class TVertex, class TIndex = unsigned>
	constexpr GeometryData<TVertex, TIndex> Cone(float height, float radius, unsigned sliceCount, int numLODLevels = 1);




	// --------------------------------------------------------------------------------------------------------------------------------------------
	// TEMPLATE DEFINITIONS
	// --------------------------------------------------------------------------------------------------------------------------------------------
	template<size_t LEN> // Helper function for setting values on a C-style array using an initializer list
	void SetFVec(float* p, const std::initializer_list<float>& values)
	{
		static_assert(LEN > 0 && LEN <= 4); // assume [vec1-vec4]
		for (size_t i = 0; i < LEN; ++i)
			p[i] = values.begin()[i];
	}



	//  Bitangent
	// 
	// ^  
	// |                  V1 (uv1)
	// |                  ^
	// |                 / \ 
	// |                /   \
	// |               /     \
	// |              /       \ 
	// |             /         \
	// |            /           \
	// |           /             \
	// |          /               \
	// |         /                 \
	// |    V0   ___________________ V2 
	// |   (uv0)                    (uv2)
	// ----------------------------------------->  Tangent
	template<class TVertex, class TIndex>
	constexpr GeometryData<TVertex, TIndex> Triangle(float size)
	{
		constexpr bool bHasTangents = std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasNormals = std::is_same<TVertex, FVertexWithNormal>() || std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasColor = std::is_same<TVertex, FVertexWithColor>() || std::is_same<TVertex, FVertexWithColorAndAlpha>();
		constexpr bool bHasAlpha = std::is_same<TVertex, FVertexWithColorAndAlpha>();

		constexpr size_t NUM_VERTS = 3;

		GeometryData<TVertex, TIndex> data(1);
		std::vector<TVertex>& v = data.LODVertices[0];
		v.resize(NUM_VERTS);

		// indices
		data.LODIndices[0] = { 0u, 1u, 2u };

		// position
		SetFVec<3>(v[0].position, { -size, -size, 0.0f });
		SetFVec<3>(v[1].position, { 0.0f,  size, 0.0f });
		SetFVec<3>(v[2].position, { size, -size, 0.0f });

		// uv
		SetFVec<2>(v[0].uv, { 0.0f, 1.0f });
		SetFVec<2>(v[1].uv, { 0.5f, 0.0f });
		SetFVec<2>(v[2].uv, { 1.0f, 1.0f });

		// normals
		if constexpr (bHasNormals)
		{
			constexpr std::initializer_list<float> NormalVec = { 0, 0, -1 };
			SetFVec<3>(v[0].normal, NormalVec);
			SetFVec<3>(v[1].normal, NormalVec);
			SetFVec<3>(v[2].normal, NormalVec);
		}

		// color
		if constexpr (bHasColor)
		{
			SetFVec<3>(v[0].color, { 1.0f, 0.0f, 0.0f });
			SetFVec<3>(v[1].color, { 0.0f, 1.0f, 0.0f });
			SetFVec<3>(v[2].color, { 0.0f, 0.0f, 1.0f });
			if constexpr (bHasAlpha)
			{
				v[0].color[3] = 1.0f;
				v[1].color[3] = 1.0f;
				v[2].color[3] = 1.0f;
			}
		}

		// tangent
		if constexpr (bHasTangents)
		{
			// todo: CalculateTangents(vector<T>& vertices, const vector<unsigned> indices)
		}

		return data;
	}


	//     ASCII Cube art from: http://www.lonniebest.com/ASCII/Art/?ID=2
	// 
	//             0 _________________________ 1        0, 1, 2, 0, 2, 3,       // Top
	//              / _____________________  /|         4, 5, 6, 4, 6, 7,       // Front
	//             / / ___________________/ / |         8, 9, 10, 8, 10, 11,    // Right
	//            / / /| |               / /  |         12, 13, 14, 12, 14, 15, // Left
	//           / / / | |              / / . |         16, 17, 18, 16, 18, 19, // Back
	//          / / /| | |             / / /| |         20, 22, 21, 20, 23, 22, // Bottom
	//         / / / | | |            / / / | |           
	//        / / /  | | |           / / /| | |          +Y
	//       / /_/__________________/ / / | | |           |  +Z
	//  4,3 /________________________/5/  | | |           |  /
	//      | ______________________8|2|  | | |           | /
	//      | | |    | | |_________| | |__| | |           |/______+X
	//      | | |    | |___________| | |____| |           
	//      | | |   / / ___________| | |_  / /
	//      | | |  / / /           | | |/ / /
	//      | | | / / /            | | | / /
	//      | | |/ / /             | | |/ /
	//      | | | / /              | | ' /
	//      | | |/_/_______________| |  /
	//      | |____________________| | /
	//      |________________________|/6
	//      7
	//
	// vertices - CW 
	template<class TVertex, class TIndex>
	constexpr GeometryData<TVertex, TIndex> Cube()
	{
		constexpr bool bHasTangents = std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasNormals = std::is_same<TVertex, FVertexWithNormal>() || std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasColor = std::is_same<TVertex, FVertexWithColor>() || std::is_same<TVertex, FVertexWithColorAndAlpha>();
		constexpr bool bHasAlpha = std::is_same<TVertex, FVertexWithColorAndAlpha>();

		constexpr int NUM_VERTS = 24;

		GeometryData<TVertex, TIndex> data(1);
		std::vector<TVertex>& v = data.LODVertices[0];
		v.resize(NUM_VERTS);

		// indices
		data.LODIndices[0] = {
			0, 1, 2, 0, 2, 3,		// Top
			4, 5, 6, 4, 6, 7,		// back
			8, 9, 10, 8, 10, 11,	// Right
			12, 13, 14, 12, 14, 15, // Back
			16, 17, 18, 16, 18, 19, // Left
			20, 22, 21, 20, 23, 22, // Bottom
		};

		// uv
		SetFVec<2>(v[0].uv, { +0.0f, +0.0f });    SetFVec<2>(v[3].uv, { +0.0f, +1.0f });
		SetFVec<2>(v[1].uv, { +1.0f, +0.0f });    SetFVec<2>(v[4].uv, { +0.0f, +0.0f });
		SetFVec<2>(v[2].uv, { +1.0f, +1.0f });    SetFVec<2>(v[5].uv, { +1.0f, +0.0f });

		SetFVec<2>(v[6].uv, { +1.0f, +1.0f });    SetFVec<2>(v[9].uv, { +1.0f, +0.0f });
		SetFVec<2>(v[7].uv, { +0.0f, +1.0f });    SetFVec<2>(v[10].uv, { +1.0f, +1.0f });
		SetFVec<2>(v[8].uv, { +0.0f, +0.0f });    SetFVec<2>(v[11].uv, { +0.0f, +1.0f });

		SetFVec<2>(v[12].uv, { +0.0f, +0.0f });    SetFVec<2>(v[15].uv, { +0.0f, +1.0f });
		SetFVec<2>(v[13].uv, { +1.0f, +0.0f });    SetFVec<2>(v[16].uv, { +0.0f, +0.0f });
		SetFVec<2>(v[14].uv, { +1.0f, +1.0f });    SetFVec<2>(v[17].uv, { +1.0f, +0.0f });

		SetFVec<2>(v[18].uv, { +1.0f, +1.0f });    SetFVec<2>(v[21].uv, { +0.0f, +0.0f });
		SetFVec<2>(v[19].uv, { +0.0f, +1.0f });    SetFVec<2>(v[22].uv, { +0.0f, +1.0f });
		SetFVec<2>(v[20].uv, { +1.0f, +0.0f });    SetFVec<2>(v[23].uv, { +1.0f, +1.0f });

		// positions / normals / tangents
		/* TOP */                   SetFVec<3>(v[0].position, { -1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[0].normal, { +0.0f, +1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[0].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[1].position, { +1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[1].normal, { +0.0f, +1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[1].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[2].position, { +1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[2].normal, { +0.0f, +1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[2].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[3].position, { -1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[3].normal, { +0.0f, +1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[3].tangent, { +1.0f, +0.0f, +0.0f });

		/* FRONT */                 SetFVec<3>(v[4].position, { -1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[4].normal, { +0.0f, +0.0f, -1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[4].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[5].position, { +1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[5].normal, { +0.0f, +0.0f, -1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[5].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[6].position, { +1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[6].normal, { +0.0f, +0.0f, -1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[6].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[7].position, { -1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[7].normal, { +0.0f, +0.0f, -1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[7].tangent, { +1.0f, +0.0f, +0.0f });

		/* RIGHT */                 SetFVec<3>(v[8].position, { +1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[8].normal, { +1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[8].tangent, { +0.0f, +0.0f, +1.0f });
		SetFVec<3>(v[9].position, { +1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[9].normal, { +1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[9].tangent, { +0.0f, +0.0f, +1.0f });
		SetFVec<3>(v[10].position, { +1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[10].normal, { +1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[10].tangent, { +0.0f, +0.0f, +1.0f });
		SetFVec<3>(v[11].position, { +1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[11].normal, { +1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[11].tangent, { +0.0f, +0.0f, +1.0f });

		/* BACK */                  SetFVec<3>(v[12].position, { +1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[12].normal, { +0.0f, +0.0f, +1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[12].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[13].position, { -1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[13].normal, { +0.0f, +0.0f, +1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[13].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[14].position, { -1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[14].normal, { +0.0f, +0.0f, +1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[14].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[15].position, { +1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[15].normal, { +0.0f, +0.0f, +1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[15].tangent, { +1.0f, +0.0f, +0.0f });

		/* LEFT */                  SetFVec<3>(v[16].position, { -1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[16].normal, { -1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[16].tangent, { +0.0f, +0.0f, -1.0f });
		SetFVec<3>(v[17].position, { -1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[17].normal, { -1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[17].tangent, { +0.0f, +0.0f, -1.0f });
		SetFVec<3>(v[18].position, { -1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[18].normal, { -1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[18].tangent, { +0.0f, +0.0f, -1.0f });
		SetFVec<3>(v[19].position, { -1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[19].normal, { -1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[19].tangent, { +0.0f, +0.0f, -1.0f });

		/* BOTTOM */                SetFVec<3>(v[20].position, { +1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[20].normal, { +0.0f, -1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[20].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[21].position, { -1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[21].normal, { +0.0f, -1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[21].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[22].position, { -1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[22].normal, { +0.0f, -1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[22].tangent, { +1.0f, +0.0f, +0.0f });
		SetFVec<3>(v[23].position, { +1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[23].normal, { +0.0f, -1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[23].tangent, { +1.0f, +0.0f, +0.0f });

		if constexpr (bHasColor)
		{
			for (int i = 0; i < (int)data.LODIndices.size(); i += 3)
			{
				TIndex Indices[3] =
				{
					  data.LODIndices[i + 0]
					, data.LODIndices[i + 1]
					, data.LODIndices[i + 2]
				};

				SetFVec<3>(v[Indices[0]].color, { 1.0f, 0.0f, 0.0f });
				SetFVec<3>(v[Indices[1]].color, { 0.0f, 1.0f, 0.0f });
				SetFVec<3>(v[Indices[2]].color, { 0.0f, 0.0f, 1.0f });
				if constexpr (bHasAlpha)
				{
					v[Indices[0]].color[3] = 1.0f;
					v[Indices[1]].color[3] = 1.0f;
					v[Indices[2]].color[3] = 1.0f;
				}
			}
		}

		return data;
	}


	//
	// CYLINDER
	//
	// From: Chapter 6.11.1.1 'Cylinder Side Geometry' from Frank Luna's DX11 book
	// with slight modifications (LOD calculation)
	template<class TVertex, class TIndex>
	constexpr GeometryData<TVertex, TIndex> Cylinder(
		float height        /*= 3.0f*/
		, float topRadius     /*= 1.0f*/
		, float bottomRadius  /*= 1.0f*/
		, unsigned sliceCount /*= 8	  */
		, unsigned stackCount /*= 4	  */
		, int numLODLevels    /*= 1   */
	)
	{
		using namespace DirectX;

		constexpr bool bHasTangents = std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasNormals = std::is_same<TVertex, FVertexWithNormal>() || std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasColor = std::is_same<TVertex, FVertexWithColor>() || std::is_same<TVertex, FVertexWithColorAndAlpha>();
		constexpr bool bHasAlpha = std::is_same<TVertex, FVertexWithColorAndAlpha>();

		GeometryData<TVertex, TIndex> data(numLODLevels);

		// parameters for each LOD level
		std::vector<unsigned> LODStackCounts(numLODLevels);
		std::vector<unsigned> LODSliceCounts(numLODLevels);

		constexpr unsigned MIN_STACK_COUNT = 4;
		constexpr unsigned MIN_SLICE_COUNT = 8;

		// using a simple lerp between min levels and given parameters so that:
		// - LOD level 0 represents the mesh defined with the function parameters @radius, @ringCount and @sliceCount
		// - the last LOD level is represented by MIN_RING_COUNT and MIN_SLICE_COUNT
		// 
		for (int LOD = 0; LOD < numLODLevels; ++LOD)
		{
			const float t = static_cast<float>(LOD) / (numLODLevels > 1 ? (numLODLevels - 1) : 1);
			LODStackCounts[LOD] = MathUtil::lerp(MIN_STACK_COUNT, stackCount, 1.0f - t);
			LODSliceCounts[LOD] = MathUtil::lerp(MIN_SLICE_COUNT, sliceCount, 1.0f - t);
		}

		// Generate VB/IB for each LOD level
		for (int LOD = 0; LOD < numLODLevels; ++LOD)
		{
			const unsigned stackCount = LODStackCounts[LOD];
			const unsigned sliceCount = LODSliceCounts[LOD];

			// slice count determines horizontal resolution
			// stack count determines height resolution
			float stackHeight = height / stackCount;
			float radiusStep = (topRadius - bottomRadius) / stackCount;
			unsigned ringCount = stackCount + 1;

			std::vector<TVertex>& Vertices = data.LODVertices[LOD];
			std::vector<TIndex>& Indices = data.LODIndices[LOD];

			// CYLINDER BODY
			//-----------------------------------------------------------
			for (unsigned i = 0; i < ringCount; ++i)
			{
				// Compute vertices for each stack ring starting at the bottom and moving up.
				float y = -0.5f * height + i * stackHeight;
				float r = bottomRadius + i * radiusStep;

				// vertices of ring
				float dTheta = 2.0f * PI / sliceCount;
				for (unsigned j = 0; j <= sliceCount; ++j)
				{
					TVertex vertex;

					// pos
					float c = cosf(j * dTheta);
					float s = sinf(j * dTheta);
					SetFVec<3>(vertex.position, { r * c, y, r * s });

					// uv
					{
						float u = (float)j / sliceCount;
						float v = 1.0f - (float)i / stackCount;
						SetFVec<2>(vertex.uv, { u, v });
					}


					// Cylinder can be parameterized as follows, where we
					// introduce v parameter that goes in the same direction
					// as the v tex-coord so that the bitangent goes in the
					// same direction as the v tex-coord.
					// Let r0 be the bottom radius and let r1 be the
					// top radius.
					// y(v) = h - hv for v in [0,1].
					// r(v) = r1 + (r0-r1)v
					//
					// x(t, v) = r(v)*cos(t)
					// y(t, v) = h - hv
					// z(t, v) = r(v)*sin(t)
					//
					// dx/dt = -r(v)*sin(t)
					// dy/dt = 0
					// dz/dt = +r(v)*cos(t)
					//
					// dx/dv = (r0-r1)*cos(t)
					// dy/dv = -h
					// dz/dv = (r0-r1)*sin(t)
					// TangentU us unit length.

					// tangent
					if constexpr (bHasTangents)
					{
						SetFVec<3>(vertex.tangent, { -s, 0.0f, c });
					}

					if constexpr (bHasNormals)
					{
						const float dr = bottomRadius - topRadius;
						XMFLOAT3 bitangent(dr * c, -height, dr * s);
						XMFLOAT3 tangent(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]);
						XMVECTOR T = XMLoadFloat3(&tangent);
						XMVECTOR B = XMLoadFloat3(&bitangent);
						XMVECTOR N = XMVector3Normalize(XMVector3Cross(T, B));

						SetFVec<3>(vertex.normal, { N.m128_f32[0], N.m128_f32[1], N.m128_f32[2] });
					}

					Vertices.push_back(vertex);
				}
			}

			// Add one because we duplicate the first and last vertex per ring since the texture coordinates are different.
			unsigned ringVertexCount = sliceCount + 1;

			// Compute indices for each stack.
			for (TIndex i = 0; i < stackCount; ++i)
			{
				for (TIndex j = 0; j < sliceCount; ++j)
				{
					Indices.push_back(i * ringVertexCount + j);
					Indices.push_back((i + 1) * ringVertexCount + j);
					Indices.push_back((i + 1) * ringVertexCount + j + 1);
					Indices.push_back(i * ringVertexCount + j);
					Indices.push_back((i + 1) * ringVertexCount + j + 1);
					Indices.push_back(i * ringVertexCount + j + 1);
				}
			}

			// CYLINDER TOP
			//-----------------------------------------------------------
			{
				TIndex baseIndex = (TIndex)Vertices.size();
				float y = 0.5f * height;
				float dTheta = 2.0f * PI / sliceCount;

				// Duplicate cap ring vertices because the texture coordinates and normals differ.
				for (unsigned i = 0; i <= sliceCount; ++i)
				{
					float x = topRadius * cosf(i * dTheta);
					float z = topRadius * sinf(i * dTheta);

					// Scale down by the height to try and make top cap texture coord area proportional to base.
					float u = x / height + 0.5f;
					float v = z / height + 0.5f;

					TVertex vertex;
					SetFVec<3>(vertex.position, { x, y, z });
					SetFVec<2>(vertex.uv, { u, v });
					if constexpr (bHasNormals)  SetFVec<3>(vertex.normal, { 0.0f, 1.0f, 0.0f });
					if constexpr (bHasTangents) SetFVec<3>(vertex.tangent, { 1.0f, 0.0f, 0.0f });
					Vertices.push_back(vertex);
				}

				// Cap center vertex.
				TVertex capCenter;
				SetFVec<3>(capCenter.position, { 0.0f, y, 0.0f });
				SetFVec<3>(capCenter.uv, { 0.5f, 0.5f });
				if constexpr (bHasNormals)  SetFVec<3>(capCenter.normal, { 0.0f, 1.0f, 0.0f });
				if constexpr (bHasTangents) SetFVec<3>(capCenter.tangent, { 1.0f, 0.0f, 0.0f });
				Vertices.push_back(capCenter);

				// Index of center vertex.
				TIndex centerIndex = (TIndex)Vertices.size() - 1;
				for (TIndex i = 0; i < sliceCount; ++i)
				{
					Indices.push_back(centerIndex);
					Indices.push_back(baseIndex + i + 1);
					Indices.push_back(baseIndex + i);
				}
			}

			// CYLINDER BOTTOM
			//-----------------------------------------------------------
			{
				TIndex baseIndex = (TIndex)Vertices.size();
				float y = -0.5f * height;
				float dTheta = 2.0f * XM_PI / sliceCount;

				// Duplicate cap ring vertices because the texture coordinates and normals differ.
				for (unsigned i = 0; i <= sliceCount; ++i)
				{
					float x = bottomRadius * cosf(i * dTheta);
					float z = bottomRadius * sinf(i * dTheta);

					// Scale down by the height to try and make top cap texture coord area proportional to base.
					float u = x / height + 0.5f;
					float v = z / height + 0.5f;

					TVertex vertex;
					SetFVec<3>(vertex.position, { x, y, z });
					SetFVec<2>(vertex.uv, { u, v });
					if constexpr (bHasNormals)  SetFVec<3>(vertex.normal, { 0.0f, -1.0f, 0.0f });
					if constexpr (bHasTangents) SetFVec<3>(vertex.tangent, { -1.0f, 0.0f, 0.0f });
					Vertices.push_back(vertex);
				}

				// Cap center vertex
				TVertex capCenter;
				SetFVec<3>(capCenter.position, { 0.0f, y, 0.0f });
				SetFVec<3>(capCenter.uv, { 0.5f, 0.5f });
				if constexpr (bHasNormals)  SetFVec<3>(capCenter.normal, { 0.0f, -1.0f, 0.0f });
				if constexpr (bHasTangents) SetFVec<3>(capCenter.tangent, { -1.0f, 0.0f, 0.0f });
				Vertices.push_back(capCenter);

				// Index of center vertex.
				TIndex centerIndex = (TIndex)Vertices.size() - 1;
				for (TIndex i = 0; i < sliceCount; ++i)
				{
					Indices.push_back(centerIndex);
					Indices.push_back(baseIndex + i);
					Indices.push_back(baseIndex + i + 1);
				}
			}
		}
		//------------------------------------------------

		return data;
	}

	//
	// SPHERE
	//
	template<class TVertex, class TIndex>
	constexpr GeometryData<TVertex, TIndex> Sphere(
		float radius /*= 1*/
		, unsigned ringCount  /*= 12*/
		, unsigned sliceCount /*= 12*/
		, int numLODLevels /*= 1*/
	)
	{
		using namespace DirectX;

		constexpr bool bHasTangents = std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasNormals = std::is_same<TVertex, FVertexWithNormal>() || std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasColor = std::is_same<TVertex, FVertexWithColor>() || std::is_same<TVertex, FVertexWithColorAndAlpha>();
		constexpr bool bHasAlpha = std::is_same<TVertex, FVertexWithColorAndAlpha>();

		GeometryData<TVertex, TIndex> data(numLODLevels);

		// parameters for each LOD level
		std::vector<unsigned> LODRingCounts(numLODLevels);
		std::vector<unsigned> LODSliceCounts(numLODLevels);

		const unsigned MIN_RING_COUNT__LOD0 = 12;
		const unsigned MIN_SLICE_COUNT__LOD0 = 12;

		const unsigned MIN_RING_COUNT__LAST_LOD = 4;
		const unsigned MIN_SLICE_COUNT__LAST_LOD = 4;

		// using a simple lerp between min levels and given parameters so that:
		// - LOD level 0 represents the mesh defined with the function parameters @radius, @ringCount and @sliceCount
		// - the last LOD level is represented by MIN_RING_COUNT and MIN_SLICE_COUNT
		// 
		for (int LOD = 0; LOD < numLODLevels; ++LOD)
		{
			const float t = static_cast<float>(LOD) / (numLODLevels > 1 ? (numLODLevels - 1) : 1);
			const unsigned MIN_RING_COUNT  = LOD == 0 ? MIN_RING_COUNT__LOD0 : MIN_RING_COUNT__LAST_LOD;
			const unsigned MIN_SLICE_COUNT = LOD == 0 ? MIN_SLICE_COUNT__LOD0 : MIN_SLICE_COUNT__LAST_LOD;

			const float fOneMinusT = (1.0f - t);
			const float fOneMinusTSq = fOneMinusT * fOneMinusT;
			const float fOneMinusTCb = fOneMinusTSq * fOneMinusT;

			LODRingCounts[LOD]  = MathUtil::lerp(MIN_RING_COUNT , ringCount , fOneMinusTSq);
			LODSliceCounts[LOD] = MathUtil::lerp(MIN_SLICE_COUNT, sliceCount, fOneMinusTSq);
		}

		// Generate VB/IB for each LOD level
		for (int LOD = 0; LOD < numLODLevels; ++LOD)
		{
			std::vector<TVertex>& Vertices = data.LODVertices[LOD];
			std::vector<TIndex>& Indices = data.LODIndices[LOD];

			// Compute vertices for each stack ring starting at the bottom and moving up.
			float dPhi = PI / (LODRingCounts[LOD] - 1);
			for (float phi = -PI_DIV2; phi <= PI_DIV2 + 0.00001f; phi += dPhi)
			{
				float y = radius * sinf(phi);	// horizontal slice center height
				float r = radius * cosf(phi);	// horizontal slice radius

				// vertices of ring
				float dTheta = 2.0f * PI / LODSliceCounts[LOD];
				for (unsigned j = 0; j <= LODSliceCounts[LOD]; ++j)	// for each pice(slice) in horizontal slice
				{
					TVertex vertex;
					float theta = j * dTheta;
					float x = r * cosf(theta);
					float z = r * sinf(theta);
					SetFVec<3>(vertex.position, { x,y,z });
					{
						float u = (float)j / LODSliceCounts[LOD];
						float v = (y + radius) / (2 * radius);
						SetFVec<2>(vertex.uv, { u, v });
					}

					// TangentU us unit length.
					if constexpr (bHasTangents)
					{
						SetFVec<3>(vertex.tangent, { -z, 0.0f, x });
					}
					if constexpr (bHasNormals)
					{

						//float dr = bottomRadius - topRadius;
						//vec3 bitangent(dr*x, -, dr*z);
						//XMVECTOR T = XMLoadFloat3(&vertex.tangent);
						//XMVECTOR B = XMLoadFloat3(&bitangent);
						//XMVECTOR N = XMVector3Normalize(XMVector3Cross(T, B));
						//XMStoreFloat3(&vertex.normal, N);
						XMVECTOR N = XMVectorSet(0, 1, 0, 1);
						XMVECTOR ROT = XMQuaternionRotationRollPitchYaw(0.0f, -PI - theta, PI_DIV2 - phi);
						N = XMVector3Rotate(N, ROT);

						SetFVec<3>(vertex.normal, { N.m128_f32[0], N.m128_f32[1], N.m128_f32[2] });
					}

					Vertices.push_back(vertex);
				}
			}

			// Add one because we duplicate the first and last vertex per ring since the texture coordinates are different.
			unsigned ringVertexCount = LODSliceCounts[LOD] + 1;

			// Compute indices for each stack.
			for (unsigned i = 0; i < LODRingCounts[LOD]; ++i)
			{
				for (unsigned j = 0; j < LODSliceCounts[LOD]; ++j)
				{
					Indices.push_back(i * ringVertexCount + j);
					Indices.push_back((i + 1) * ringVertexCount + j);
					Indices.push_back((i + 1) * ringVertexCount + j + 1);
					Indices.push_back(i * ringVertexCount + j);
					Indices.push_back((i + 1) * ringVertexCount + j + 1);
					Indices.push_back(i * ringVertexCount + j + 1);
				}
			}
		}
		//------------------------------------------------

		return data;
	}



	// m : NumVertsX
	// n : NumVertsY
	//		Grid of m x n vertices 
	//		-----------------------------------------------------------
	//		+	: Vertex
	//		d	: Depth
	//		w	: Width
	//		dx	: horizontal cell spacing = width / (m-1)
	//		dz	: z-axis	 cell spacing = depth / (n-1)
	// 
	//		  V(0,0)		  V(m-1,0)	^ Z
	//		^	+-------+-------+ ^		|
	//		|	|		|		| |		|
	//		|	|		|		| dz	|
	//		|	|		|		| |		|
	//		d	+-------+-------+ v		+--------> X
	//		|	|		|		|		
	//		|	|		|		|
	//		|	|		|		|
	//		v	+-------+-------+		
	//			<--dx--->		  V(m-1, n-1)
	//			<------ w ------>
	template<class TVertex, class TIndex>
	constexpr GeometryData<TVertex, TIndex> Grid(float Width, float Depth, unsigned NumVertsX, unsigned NumVertsY, int NumLODLevels)
	{
		using namespace DirectX;
		assert(NumVertsX > 1);
		assert(NumVertsY > 1);
		assert(NumLODLevels >= 1);

		constexpr bool bHasTangents = std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasNormals = std::is_same<TVertex, FVertexWithNormal>() || std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasColor = std::is_same<TVertex, FVertexWithColor>() || std::is_same<TVertex, FVertexWithColorAndAlpha>();
		constexpr bool bHasAlpha = std::is_same<TVertex, FVertexWithColorAndAlpha>();

		const unsigned MIN_HSLICE_COUNT = 2;
		const unsigned MIN_VSLICE_COUNT = 2;

		const float HalfWidth = Width * 0.5f;
		const float HalfDepth = Depth * 0.5f;

		GeometryData<TVertex, TIndex> data(NumLODLevels);
		for (int LOD = 0; LOD < NumLODLevels; ++LOD)
		{
			std::vector<TVertex>& Vertices = data.LODVertices[LOD];
			std::vector<TIndex>& Indices = data.LODIndices[LOD];

			const float t = static_cast<float>(LOD) / (NumLODLevels == 1 ? 1 : NumLODLevels - 1);
			const unsigned m = MathUtil::lerp(MIN_HSLICE_COUNT, NumVertsX, std::powf(1.0f - t, 2.0f));
			const unsigned n = MathUtil::lerp(MIN_VSLICE_COUNT, NumVertsY, std::powf(1.0f - t, 2.0f));

			const unsigned NumVerts = m * n;
			const unsigned NumQuads = (m - 1) * (n - 1);
			const unsigned NumTris = NumQuads * 2;
			const unsigned NumIndices = NumTris * 3;

			const float QuadWidth = Width / (m - 1);
			const float QuadHeight = Depth / (n - 1);

			Vertices.resize(NumVerts);
			for (unsigned iX = 0; iX < m; ++iX)
			{
				const float z = -HalfDepth + QuadHeight * iX;
				for (unsigned iY = 0; iY < n; ++iY)
				{
					TVertex& vert = Vertices[iX * n + iY];

					const float x = -HalfWidth + QuadWidth * iY;
					SetFVec<3>(vert.position, { x , 0.0f, z });

					const float v = float(iX) / (m-1);
					const float u = float(iY) / (n-1);
					SetFVec<2>(vert.uv, { u, 1.0f-v });

					if constexpr (bHasNormals)
					{
						SetFVec<3>(vert.normal, { 0.0f , 1.0f, 0.0f });
					}

					if constexpr (bHasTangents)
					{
						SetFVec<3>(vert.tangent, { 1.0f , 0.0f, 0.0f });
					}
				}
			}
			
			//	generate Indices
			Indices.resize(NumIndices);
			//
			//	  A	+------+ B
			//		|	 / |
			//		|	/  |
			//		|  /   |
			//		| /	   |
			//		|/	   |
			//	  C	+------+ D
			//
			//	A	: V(i  , j  )
			//	B	: V(i  , j+1)
			//	C	: V(i+1, j  )
			//	D	: V(i+1, j+1)
			//
			//	ABC	: (i*n +j    , i*n + j+1, (i+1)*n + j  )
			//	CBD : ((i+1)*n +j, i*n + j+1, (i+1)*n + j+1)
			// i = row | j = col
			int ii = 0;
			for (unsigned row = 0; row < m - 1; ++row)
			{
				for (unsigned col = 0; col < n - 1; ++col)
				{
					Indices[ii + 0] = row * n + col + 1;
					Indices[ii + 1] = row * n + col;
					Indices[ii + 2] = (row + 1) * n + col;
					Indices[ii + 3] = (row + 1) * n + col;
					Indices[ii + 4] = (row + 1) * n + col + 1;
					Indices[ii + 5] = row * n + col + 1;
					ii += 6;
				}
			}
		}

		return data;
	}

	//
	// CONE
	//
	template<class TVertex, class TIndex>
	constexpr GeometryData<TVertex, TIndex> Cone(float height, float radius, unsigned numSlices, int numLODLevels /*= 1*/)
	{
		using namespace DirectX;

		constexpr bool bHasTangents = std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasNormals = std::is_same<TVertex, FVertexWithNormal>() || std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasColor = std::is_same<TVertex, FVertexWithColor>() || std::is_same<TVertex, FVertexWithColorAndAlpha>();
		constexpr bool bHasAlpha = std::is_same<TVertex, FVertexWithColorAndAlpha>();

		GeometryData<TVertex, TIndex> data(numLODLevels);

		// parameters for each LOD level
		std::vector<unsigned> LODSliceCounts(numLODLevels);

		const unsigned MIN_SLICE_COUNT = 10;

		// using a simple lerp between min levels and given parameters
		for (int LOD = 0; LOD < numLODLevels; ++LOD)
		{
			const float t = static_cast<float>(LOD) / (numLODLevels - 1);
			LODSliceCounts[LOD] = MathUtil::lerp(MIN_SLICE_COUNT, numSlices, 1.0f - t);
		}

		// Generate VB/IB for each LOD level
		for (int LOD = 0; LOD < numLODLevels; ++LOD)
		{
			std::vector<TVertex>& Vertices = data.LODVertices[LOD];
			std::vector<TIndex>& Indices = data.LODIndices[LOD];

			int IndexOfConeBaseCenterVertex = -1;
			const unsigned& sliceCount = LODSliceCounts[LOD];

			// SURFACE
			//-----------------------------------------------------------
			unsigned baseIndex = (unsigned)Vertices.size();
			float y = 0.0f; // -0.33f*height;
			float dTheta = 2.0f * PI / sliceCount;

			// Duplicate cap ring vertices because the texture coordinates and normals differ.
			for (unsigned i = 0; i <= sliceCount; ++i)
			{
				const float x = radius * cosf(i * dTheta);
				const float z = radius * sinf(i * dTheta);

				const float u = x / height + 0.5f; // Scale down by the height to try and make top cap texture coord area proportional to base.
				const float v = z / height + 0.5f;

				TVertex vertex;
				SetFVec<3>(vertex.position, { x,y,z });
				if constexpr (bHasNormals) { SetFVec<3>(vertex.normal, { 0.0f , 1.0f, 0.0f }); }
				if constexpr (bHasTangents) { SetFVec<3>(vertex.tangent, { -1.0f, 0.0f, 0.0f }); }
				SetFVec<2>(vertex.uv, { u, v });

				Vertices.push_back(vertex);
			} // ConeRingVertices


			// BASE
			//-----------------------------------------------------------
			{
				// Cap center vertex.
				TVertex capCenter;
				SetFVec<3>(capCenter.position, { 0.0f, y, 0.0f });
				if constexpr (bHasNormals) { SetFVec<3>(capCenter.normal, { 0.0f , 1.0f, 0.0f }); }
				if constexpr (bHasTangents) { SetFVec<3>(capCenter.tangent, { -1.0f, 0.0f, 0.0f }); }
				SetFVec<2>(capCenter.uv, { 0.5f, 0.5f });
				Vertices.push_back(capCenter);

				// Index of center vertex.
				unsigned centerIndex = (unsigned)Vertices.size() - 1;
				IndexOfConeBaseCenterVertex = static_cast<int>(centerIndex);
				for (unsigned i = 0; i < sliceCount; ++i)
				{
					Indices.push_back(centerIndex);
					Indices.push_back(baseIndex + i + 1);
					Indices.push_back(baseIndex + i);
				}
			} // ConeBaseVertex


			constexpr bool bAddBackFaceForBase = true;
			if constexpr (bAddBackFaceForBase)
			{
				baseIndex = (unsigned)Vertices.size();
				const float offsetInNormalDirection = 0.0f;//-1.100001f;
				for (unsigned i = 0; i <= sliceCount; ++i)
				{
					const float x = radius * cosf(i * dTheta);
					const float z = radius * sinf(i * dTheta);
					const float u = x / height + 0.5f;
					const float v = z / height + 0.5f;

					TVertex vertex;
					SetFVec<3>(vertex.position, { x, y + offsetInNormalDirection, z });
					if constexpr (bHasNormals) { SetFVec<3>(vertex.normal, { 0.0f, -1.0f, 0.0f }); }
					if constexpr (bHasTangents) { SetFVec<3>(vertex.tangent, { -1.0f, 0.0f, 0.0f }); }
					SetFVec<2>(vertex.uv, { u, v });
					Vertices.push_back(vertex);
				}

				TVertex capCenter;
				SetFVec<3>(capCenter.position, { 0.0f, y + offsetInNormalDirection, 0.0f });
				SetFVec<3>(capCenter.normal, { 0.0f, -1.0f, 0.0f });
				SetFVec<3>(capCenter.tangent, { -1.0f, 0.0f, 0.0f });
				SetFVec<2>(capCenter.uv, { 0.5f, 0.5f });
				Vertices.push_back(capCenter);

				unsigned centerIndex = (unsigned)Vertices.size() - 1;
				for (unsigned i = 0; i < sliceCount; ++i)
				{
					Indices.push_back(centerIndex);
					Indices.push_back(baseIndex + i);
					Indices.push_back(baseIndex + i + 1);
				}
			}

			// TIP
			//-----------------------------------------------------------
			{
				// add the tip vertex
				TVertex tipVertex;
				SetFVec<3>(tipVertex.position, { 0.0f, height, 0.0f });
				SetFVec<3>(tipVertex.normal, { 0.0f, 1.0f, 0.0f });
				SetFVec<3>(tipVertex.tangent, { 1.0f, 0.0f, 0.0f });
				SetFVec<2>(tipVertex.uv, { 0.5f, 0.5f });
				Vertices.push_back(tipVertex);

				const unsigned tipVertIndex = (unsigned)Vertices.size() - 1;
				for (unsigned i = 0; i <= sliceCount; ++i)
				{
					Indices.push_back(tipVertIndex);
					Indices.push_back(i + 1);
					Indices.push_back(i);

					// TODO:
					// -----------------------------------
					// calculate the tangent and normal vectors
					// and override the placeholder values used earlier
					//
					// Pt : position of tip of cone
					// P0 : position of surface triangle bottom vertex0
					// P1 : position of surface triangle bottom vertex1
					//
					//const vec3 Pt = tipVertex.position;
					//const vec3 P0 = Vertices[i].position;
					//const vec3 P1 = Vertices[i + 1].position;
					//
					////  T  : tangent vector: vector along the cone surface.
					//vec3 T = Pt - (P0+P1)*0.5f;
					//T.normalize();
					//
					//// use the vectors pointing from P0->Pt: V0 and P0->P1: V1
					//// as the basis vectors for the triangle surface
					//vec3 V0 = Pt - P0;
					//vec3 V1 = P1 - P0;
					//V0.normalize();
					//V1.normalize();
					//
					//// cross product of the normalized vectors will give
					//// the surface normal vector
					//vec3 N = XMVector3Dot(V0, V1);
					//
					//Vertices[i + 1].normal = N;
					//Vertices[i + 0].normal = N;
					//Vertices[i + 0].tangent = T;
					//Vertices[i + 0].tangent = T;
				}
			}
		}
		//------------------------------------------------

		return data;
	}

}; // namespace GeometryGenerator


 

struct VertexIndexBufferIDPair
{
	BufferID  mVertexBufferID = -1;
	BufferID  mIndexBufferID  = -1;

	inline std::pair<BufferID, BufferID> GetIABufferPair() const { return std::make_pair(mVertexBufferID, mIndexBufferID); }
};

template<class TVertex, class TIndex = uint32>
struct MeshLODData
{
	MeshLODData< TVertex, TIndex >() = delete;
	MeshLODData< TVertex, TIndex >(int numLODs, const char* pMeshName)
		: LODVertices(numLODs)
		, LODIndices(numLODs)
		, meshName(pMeshName)
	{}
	
	std::vector<std::vector<TVertex>> LODVertices;
	std::vector<std::vector<TIndex>>  LODIndices ;
	std::string meshName;
};

// A Mesh is represented by a Vertex & Index buffer ID pair,
// where the buffers contain the local space vertex and connectivity data.
// Meshes can have multiple LOD levels, and a single local-space bounding box
struct Mesh
{
public:
	static EBuiltInMeshes GetBuiltInMeshType(const std::string& MeshTypeStr);
	//
	// Constructors / Operators
	//
	template<class TVertex, class TIndex = unsigned>
	Mesh(
		VQRenderer* pRenderer,
		const std::vector<TVertex>&  vertices,
		const std::vector<TIndex>&   indices,
		const std::string&           name
	);

	template<class TVertex, class TIndex>
	Mesh(VQRenderer* pRenderer, const GeometryGenerator::GeometryData<TVertex, TIndex>& meshLODData, const std::string& name);

	Mesh() = default;

	//
	// Interface
	//
	std::pair<BufferID, BufferID> GetIABufferIDs(int lod = 0) const;
	inline uint GetNumIndices(int lod = 0) const { assert(mNumIndicesPerLODLevel.size()>lod); return mNumIndicesPerLODLevel[lod]; }
	inline uint GetNumLODs() const { return static_cast<uint>(mLODBufferPairs.size()); }
	const FBoundingBox GetLocalSpaceBoundingBox() const { return mLocalSpaceBoundingBox; }
	
private:
	std::vector<VertexIndexBufferIDPair> mLODBufferPairs;
	std::vector<uint> mNumIndicesPerLODLevel;
	FBoundingBox mLocalSpaceBoundingBox;

private:

	template<class TVertex>
	static FBoundingBox CalculateBoundingBox(const std::vector<TVertex>& verts);
};

//
// Template Definitions
//
template<class TVertex, class TIndex>
Mesh::Mesh(
	VQRenderer* pRenderer,
	const std::vector<TVertex>& vertices,
	const std::vector<TIndex>& indices,
	const std::string& name
)
{
	assert(pRenderer);
	FBufferDesc bufferDesc = {};

	const std::string VBName = name + "_LOD[0]_VB";
	const std::string IBName = name + "_LOD[0]_IB";

	bufferDesc.Type         = VERTEX_BUFFER;
	//bufferDesc.Usage        = GPU_READ_WRITE;
	bufferDesc.NumElements  = static_cast<unsigned>(vertices.size());
	bufferDesc.Stride       = sizeof(vertices[0]);
	bufferDesc.pData        = static_cast<const void*>(vertices.data());
	bufferDesc.Name         = VBName;
	BufferID vertexBufferID = pRenderer->CreateBuffer(bufferDesc);

	bufferDesc.Type        = INDEX_BUFFER;
	//bufferDesc.Usage       = GPU_READ_WRITE;
	bufferDesc.NumElements = static_cast<unsigned>(indices.size());
	bufferDesc.Stride      = sizeof(unsigned);
	bufferDesc.pData       = static_cast<const void*>(indices.data());
	bufferDesc.Name        = IBName;
	BufferID indexBufferID = pRenderer->CreateBuffer(bufferDesc);

	mLODBufferPairs.push_back({ vertexBufferID, indexBufferID }); // LOD[0]
	mNumIndicesPerLODLevel.push_back(bufferDesc.NumElements);

	mLocalSpaceBoundingBox = CalculateBoundingBox(vertices);
}

template<class TVertex, class TIndex>
Mesh::Mesh(VQRenderer* pRenderer, const GeometryGenerator::GeometryData<TVertex, TIndex>& meshLODData, const std::string& name)
{
	for (size_t LOD = 0; LOD < meshLODData.LODVertices.size(); ++LOD)
	{
		FBufferDesc bufferDesc = {};

		const std::string VBName = name + "_LOD[" + std::to_string(LOD) + "]_VB";
		const std::string IBName = name + "_LOD[" + std::to_string(LOD) + "]_IB";
		const std::vector<TVertex>& vertices = meshLODData.LODVertices[LOD];
		const std::vector<TIndex>& indices = meshLODData.LODIndices[LOD];

		bufferDesc.Type = VERTEX_BUFFER;
		//bufferDesc.Usage = GPU_READ_WRITE;
		bufferDesc.NumElements = static_cast<unsigned>(vertices.size());
		bufferDesc.pData = static_cast<const void*>(vertices.data());
		bufferDesc.Stride = sizeof(vertices[0]);
		bufferDesc.Name = VBName;
		BufferID vertexBufferID = pRenderer->CreateBuffer(bufferDesc);

		bufferDesc.Type = INDEX_BUFFER;
		//bufferDesc.Usage = GPU_READ_WRITE;
		bufferDesc.NumElements = static_cast<unsigned>(indices.size());
		bufferDesc.pData = static_cast<const void*>(indices.data());
		bufferDesc.Stride = sizeof(unsigned);
		bufferDesc.Name = IBName;
		BufferID indexBufferID = pRenderer->CreateBuffer(bufferDesc);

		mLODBufferPairs.push_back({ vertexBufferID, indexBufferID });
		mNumIndicesPerLODLevel.push_back(bufferDesc.NumElements);

		if (LOD == 0)
		{
			mLocalSpaceBoundingBox = CalculateBoundingBox(vertices);
		}
	}
}

template<class TVertex>
inline FBoundingBox Mesh::CalculateBoundingBox(const std::vector<TVertex>& verts)
{
	using namespace DirectX;
	const float max_f = std::numeric_limits<float>::max();
	const float min_f = -(max_f - 1.0f);

	FBoundingBox bb;
	XMFLOAT3& mins = bb.ExtentMin;
	XMFLOAT3& maxs = bb.ExtentMax;
	mins = XMFLOAT3(max_f, max_f, max_f);
	maxs = XMFLOAT3(min_f, min_f, min_f);
	XMVECTOR vMins = XMLoadFloat3(&mins);
	XMVECTOR vMaxs = XMLoadFloat3(&maxs);
	for (const TVertex& vert : verts)
	{
		XMFLOAT3 f3Pos(vert.position[0], vert.position[1], vert.position[2]);
		XMVECTOR vPos = XMLoadFloat3(&f3Pos);
		vMins = XMVectorMin(vMins, vPos);
		vMaxs = XMVectorMax(vMaxs, vPos);
	}
	XMStoreFloat3(&mins, vMins);
	XMStoreFloat3(&maxs, vMaxs);
	return bb;
}



using BuiltinMeshArray_t = std::array<Mesh, EBuiltInMeshes::NUM_BUILTIN_MESHES>;