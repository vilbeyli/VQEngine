//	VQEngine | DirectX11 Renderer
//	Copyright(C) 2018  - Volkan Ilbeyli
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

};

