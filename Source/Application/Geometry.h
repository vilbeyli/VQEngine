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

#include "Mesh.h"

#include <type_traits>

namespace GeometryGenerator
{
	template<class TVertex, class TIndex = unsigned>
	struct GeometryData
	{
		static_assert(std::is_same<TIndex, unsigned>() || std::is_same<TIndex, unsigned short>()); // ensure UINT32 or UINT16 indices
		std::vector<TVertex>  Vertices;
		std::vector<TIndex>   Indices;
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
	constexpr GeometryData<TVertex, TIndex> Sphere(float radius, unsigned ringCount, unsigned sliceCount, int numLODLevels = 1);

	template<class TVertex, class TIndex = unsigned> 
	constexpr GeometryData<TVertex, TIndex> Grid(float width, float depth, unsigned tilingX, unsigned tilingY, int numLODLevels = 1);

	template<class TVertex, class TIndex = unsigned> 
	constexpr GeometryData<TVertex, TIndex> Cylinder(float height, float topRadius, float bottomRadius, unsigned sliceCount, unsigned stackCount, int numLODLevels = 1);

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
		constexpr bool bHasNormals  = std::is_same<TVertex, FVertexWithNormal>() || std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasColor    = std::is_same<TVertex, FVertexWithColor>()  || std::is_same<TVertex, FVertexWithColorAndAlpha>();
		constexpr bool bHasAlpha    = std::is_same<TVertex, FVertexWithColorAndAlpha>();

		constexpr size_t NUM_VERTS = 3;

		GeometryData<TVertex, TIndex> data;
		std::vector<TVertex>& v = data.Vertices;
		v.resize(NUM_VERTS);

		// indices
		data.Indices = { 0u, 1u, 2u };
		
		// position
		SetFVec<3>(v[0].position, { -size, -size, 0.0f });
		SetFVec<3>(v[1].position, {  0.0f,  size, 0.0f });
		SetFVec<3>(v[2].position, {  size, -size, 0.0f });

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
		constexpr bool bHasNormals  = std::is_same<TVertex, FVertexWithNormal>() || std::is_same<TVertex, FVertexWithNormalAndTangent>();
		constexpr bool bHasColor    = std::is_same<TVertex, FVertexWithColor>()  || std::is_same<TVertex, FVertexWithColorAndAlpha>();
		constexpr bool bHasAlpha    = std::is_same<TVertex, FVertexWithColorAndAlpha>();

		constexpr int NUM_VERTS   = 24;

		GeometryData<TVertex, TIndex> data;
		std::vector<TVertex>& v = data.Vertices;
		v.resize(NUM_VERTS);

		// indices
		data.Indices = {
			0, 1, 2, 0, 2, 3,		// Top
			4, 5, 6, 4, 6, 7,		// back
			8, 9, 10, 8, 10, 11,	// Right
			12, 13, 14, 12, 14, 15, // Back
			16, 17, 18, 16, 18, 19, // Left
			20, 22, 21, 20, 23, 22, // Bottom
		};

		// uv
		SetFVec<2>(v[0] .uv, { +0.0f, +0.0f });    SetFVec<2>(v[3] .uv, { +0.0f, +1.0f });
		SetFVec<2>(v[1] .uv, { +1.0f, +0.0f });    SetFVec<2>(v[4] .uv, { +0.0f, +0.0f });
		SetFVec<2>(v[2] .uv, { +1.0f, +1.0f });    SetFVec<2>(v[5] .uv, { +1.0f, +0.0f });

		SetFVec<2>(v[6] .uv, { +1.0f, +1.0f });    SetFVec<2>(v[9] .uv, { +1.0f, +0.0f });
		SetFVec<2>(v[7] .uv, { +0.0f, +1.0f });    SetFVec<2>(v[10].uv, { +1.0f, +1.0f });
		SetFVec<2>(v[8] .uv, { +0.0f, +0.0f });    SetFVec<2>(v[11].uv, { +0.0f, +1.0f });
		
		SetFVec<2>(v[12].uv, { +0.0f, +0.0f });    SetFVec<2>(v[15].uv, { +0.0f, +1.0f });
		SetFVec<2>(v[13].uv, { +1.0f, +0.0f });    SetFVec<2>(v[16].uv, { +0.0f, +0.0f });
		SetFVec<2>(v[14].uv, { +1.0f, +1.0f });    SetFVec<2>(v[17].uv, { +1.0f, +0.0f });

		SetFVec<2>(v[18].uv, { +1.0f, +1.0f });    SetFVec<2>(v[21].uv, { +0.0f, +0.0f });
		SetFVec<2>(v[19].uv, { +0.0f, +1.0f });    SetFVec<2>(v[22].uv, { +0.0f, +1.0f });
		SetFVec<2>(v[20].uv, { +1.0f, +0.0f });    SetFVec<2>(v[23].uv, { +1.0f, +1.0f });

		// positions / normals / tangents
		/* TOP */                   SetFVec<3>(v[0].position , { -1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[0].normal   , { +0.0f, +1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[0].tangent  , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[1].position , { +1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[1].normal   , { +0.0f, +1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[1].tangent  , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[2].position , { +1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[2].normal   , { +0.0f, +1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[2].tangent  , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[3].position , { -1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[3].normal   , { +0.0f, +1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[3].tangent  , { +1.0f, +0.0f, +0.0f });

		/* FRONT */                 SetFVec<3>(v[4].position , { -1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[4].normal   , { +0.0f, +0.0f, -1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[4].tangent  , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[5].position , { +1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[5].normal   , { +0.0f, +0.0f, -1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[5].tangent  , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[6].position , { +1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[6].normal   , { +0.0f, +0.0f, -1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[6].tangent  , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[7].position , { -1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[7].normal   , { +0.0f, +0.0f, -1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[7].tangent  , { +1.0f, +0.0f, +0.0f });

		/* RIGHT */                 SetFVec<3>(v[8].position , { +1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[8].normal   , { +1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[8].tangent  , { +0.0f, +0.0f, +1.0f });
		                            SetFVec<3>(v[9].position , { +1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[9].normal   , { +1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[9].tangent  , { +0.0f, +0.0f, +1.0f });
		                            SetFVec<3>(v[10].position, { +1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[10].normal  , { +1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[10].tangent , { +0.0f, +0.0f, +1.0f });
		                            SetFVec<3>(v[11].position, { +1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[11].normal  , { +1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[11].tangent , { +0.0f, +0.0f, +1.0f });

		/* BACK */                  SetFVec<3>(v[12].position, { +1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[12].normal  , { +0.0f, +0.0f, +1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[12].tangent , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[13].position, { -1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[13].normal  , { +0.0f, +0.0f, +1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[13].tangent , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[14].position, { -1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[14].normal  , { +0.0f, +0.0f, +1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[14].tangent , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[15].position, { +1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[15].normal  , { +0.0f, +0.0f, +1.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[15].tangent , { +1.0f, +0.0f, +0.0f });

		/* LEFT */                  SetFVec<3>(v[16].position, { -1.0f, +1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[16].normal  , { -1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[16].tangent , { +0.0f, +0.0f, -1.0f });
		                            SetFVec<3>(v[17].position, { -1.0f, +1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[17].normal  , { -1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[17].tangent , { +0.0f, +0.0f, -1.0f });
		                            SetFVec<3>(v[18].position, { -1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[18].normal  , { -1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[18].tangent , { +0.0f, +0.0f, -1.0f });
		                            SetFVec<3>(v[19].position, { -1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[19].normal  , { -1.0f, +0.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[19].tangent , { +0.0f, +0.0f, -1.0f });

		/* BOTTOM */                SetFVec<3>(v[20].position, { +1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[20].normal  , { +0.0f, -1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[20].tangent , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[21].position, { -1.0f, -1.0f, -1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[21].normal  , { +0.0f, -1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[21].tangent , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[22].position, { -1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[22].normal  , { +0.0f, -1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[22].tangent , { +1.0f, +0.0f, +0.0f });
		                            SetFVec<3>(v[23].position, { +1.0f, -1.0f, +1.0f });
		if constexpr (bHasNormals)  SetFVec<3>(v[23].normal  , { +0.0f, -1.0f, +0.0f });
		if constexpr (bHasTangents) SetFVec<3>(v[23].tangent , { +1.0f, +0.0f, +0.0f });

		if constexpr (bHasColor)
		{
			for (int i = 0; i < (int)data.Indices.size(); i += 3)
			{
				int Indices[3] = 
				{
					  data.Indices[i + 0]
					, data.Indices[i + 1]
					, data.Indices[i + 2]
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


};

