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

#include "Geometry.h"
#include "../Renderer/Renderer.h"

//#include "Utilities/Log.h"

#include <vector>

using std::vector;

#if 0 // VQEngine DX11 functions for reference. Currently lacking the math library (vec data types + tangent calculation)
Mesh GeometryGenerator::Quad(float scale)
{
	const float& size = scale;

	//	  1	+-----+ 2	0, 1, 2
	//		|	  |		2, 3, 0
	//		|	  |		
	//	  0 +-----+ 3	
	const vector<unsigned> indices = 
	{ 
		0, 1, 2,
		2, 3, 0
	};

	vector<DefaultVertexBufferData> vertices(4);

	// vertices - CW
	vertices[0].position	= vec3(-size, -size, 0.0f);
	vertices[0].normal		= vec3(0.0f, 0.0f, -1.0f);
	vertices[0].uv			= vec2(0.0f, 1.0f);

	vertices[1].position	= vec3(-size, +size, 0.0f);
	vertices[1].normal		= vec3(0.0f, 0.0f, -1.0f);
	vertices[1].uv			= vec2(0.0f, 0.0f);

	vertices[2].position	= vec3(+size, +size, 0.0f);
	vertices[2].normal		= vec3(0.0f, 0.0f, -1.0f);
	vertices[2].uv			= vec2(1.0f, 0.0f);

	vertices[3].position	= vec3(+size, -size, 0.0f);
	vertices[3].normal		= vec3(0.0f, 0.0f, -1.0f);
	vertices[3].uv			= vec2(1.0f, 1.0f);
	
	CalculateTangentsAndBitangents(vertices, indices);
	return Mesh(vertices, indices, "BuiltinQuad");
}

Mesh GeometryGenerator::FullScreenQuad()
{
	const float& size = 1.0f;

	//	  1	+-----+ 2	0, 1, 2
	//		|	  |		2, 3, 0
	//		|	  |		
	//	  0 +-----+ 3	
	const vector<unsigned> indices =
	{
		0, 1, 2,
		2, 3, 0
	};

	vector<FullScreenVertexBufferData> vertices(4);

	// vertices - CW
	vertices[0].position = vec3(-size, -size, 0.0f);
	vertices[0].uv = vec2(0.0f, 1.0f);

	vertices[1].position = vec3(-size, +size, 0.0f);
	vertices[1].uv = vec2(0.0f, 0.0f);

	vertices[2].position = vec3(+size, +size, 0.0f);
	vertices[2].uv = vec2(1.0f, 0.0f);

	vertices[3].position = vec3(+size, -size, 0.0f);
	vertices[3].uv = vec2(1.0f, 1.0f);

	return Mesh(vertices, indices, "BuiltinQuad");
}



Mesh GeometryGenerator::Grid(float width, float depth, unsigned horizontalTessellation, unsigned verticalTessellation, int numLODLevels /*= 1*/)
{
	MeshLODData< DefaultVertexBufferData> meshData(numLODLevels, "BuiltinGrid");

	// shorthands
	std::vector < vector<DefaultVertexBufferData>>& LODVertices = meshData.LODVertices;
	std::vector < std::vector<unsigned>>& LODIndices = meshData.LODIndices;

	// parameters for each LOD level
	std::vector<unsigned> LODNumHorizontalSlices(numLODLevels);
	std::vector<unsigned> LODNumVerticalSlices(numLODLevels);

	const unsigned MIN_HSLICE_COUNT = 8;
	const unsigned MIN_VSLICE_COUNT = 8;

	// using a simple lerp between min levels and given parameters 
	// so that:
	// - LOD level 0 represents the mesh defined with the function
	//   parameters @radius, @ringCount and @sliceCount
	// - the last LOD level is represented by MIN_RING_COUNT
	//   and MIN_SLICE_COUNT
	// 
	for (int LOD = 0; LOD < numLODLevels; ++LOD)
	{
		const float t = static_cast<float>(LOD) / (numLODLevels - 1);
		LODNumHorizontalSlices[LOD] = MathUtil::lerp(MIN_HSLICE_COUNT, horizontalTessellation, std::powf(1.0f - t, 2.0f));
		LODNumVerticalSlices[LOD]   = MathUtil::lerp(MIN_VSLICE_COUNT, verticalTessellation  , std::powf(1.0f - t, 2.0f));
	}

	//		Grid of m x n vertices
	//		-----------------------------------------------------------
	//		+	: Vertex
	//		d	: depth
	//		w	: width
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

	for (int LOD = 0; LOD < numLODLevels; ++LOD)
	{
		const unsigned m = LODNumHorizontalSlices[LOD];
		const unsigned n = LODNumVerticalSlices[LOD];

		unsigned numQuads = (m - 1) * (n - 1);
		unsigned faceCount = numQuads * 2; // 2 faces per quad = triangle count
		unsigned vertCount = m * n;
		float dx = width / (n - 1);
		float dz = depth / (m - 1);	// m & n mixed up??

		// offsets for centering the grid : V(0,0) = (-halfWidth, halfDepth)
		float halfDepth = depth / 2;
		float halfWidth = width / 2;

		// texture coord increments
		float du = 1.0f / (n - 1);
		float dv = 1.0f / (m - 1);

		vector<DefaultVertexBufferData>& Vertices = LODVertices[LOD];
		std::vector<unsigned>& Indices = LODIndices[LOD];

		Vertices.resize(vertCount);
		Indices.resize(faceCount * 3);

		// position the Vertices
		for (unsigned i = 0; i < m; ++i)
		{
			float z = halfDepth - i * dz;
			for (unsigned j = 0; j < n; ++j)
			{
				float x = -halfWidth + j * dx;
				float u = j * du;
				float v = i * dv;
				Vertices[i*n + j].position = vec3(x, 0.0f, z);
				Vertices[i*n + j].normal = vec3(0.0f, 0.0f, 0.0f);
				Vertices[i*n + j].uv = vec2(u, v);
				Vertices[i*n + j].tangent = vec3(1.0f, 0.0f, 0.0f);
			}
		}

		//	generate Indices
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

		unsigned k = 0;
		for (unsigned i = 0; i < m - 1; ++i)
		{
			for (unsigned j = 0; j < n - 1; ++j)
			{
				Indices[k] = i * n + j;
				Indices[k + 1] = i * n + j + 1;
				Indices[k + 2] = (i + 1)*n + j;
				Indices[k + 3] = (i + 1)*n + j;
				Indices[k + 4] = i * n + j + 1;
				Indices[k + 5] = (i + 1)*n + j + 1;
				k += 6;
			}
		}

		// apply height function
		for (size_t i = 0; i < Vertices.size(); ++i)
		{
			vec3& pos = Vertices[i].position;
			pos.y() = 0.2f * (pos.z() * sinf(20.0f * pos.x()) + pos.x() * cosf(10.0f * pos.z()));
		}

		CalculateTangentsAndBitangents(Vertices, Indices);
	}

	return Mesh(meshData);
}

#endif


#if 0
bool GeometryGenerator::Is2DGeometry(EGeometry meshID)
{
	return meshID == EGeometry::TRIANGLE || meshID == EGeometry::QUAD || meshID == EGeometry::GRID;
}

void GeometryGenerator::CalculateTangentsAndBitangents(vector<DefaultVertexBufferData>& vertices, const vector<unsigned> indices)	// Only Tangents
{
	//  Bitangent
	//	
	//	^  (uv1)
	//	|	 V1   ___________________ V2 (uv2)
	//	|		  \                 /
	//	|		   \               /
	//	|		    \             /
	//	|			 \           /
	//	|			  \         /
	//	|  dUV1 | E1   \       /  E2 | dUV2
	//	|			    \     /
	//	|			     \   /
	//	|			      \ /	
	//	|				   V
	//	|				   V0 (uv0)
	//	|				   
	// ----------------------------------------->  Tangent
	const size_t countVerts = vertices.size();
	const size_t countIndices = indices.size();
	assert(countIndices % 3 == 0);

	const vec3 N = vec3::Forward;	//  (0, 0, 1)

	for (size_t i = 0; i < countIndices; i += 3)
	{
		DefaultVertexBufferData& v0 = vertices[indices[i]];
		DefaultVertexBufferData& v1 = vertices[indices[i + 1]];
		DefaultVertexBufferData& v2 = vertices[indices[i + 2]];

		const vec3 E1 = v1.position - v0.position;
		const vec3 E2 = v2.position - v0.position;

		const vec2 dUV1 = vec2(v1.uv - v0.uv);
		const vec2 dUV2 = vec2(v2.uv - v0.uv);

		const float f = 1.0f / (dUV1.x() * dUV2.y() - dUV1.y() * dUV2.x());
		assert(!std::isinf(f));

		vec3 T(f * (dUV2.y() * E1.x() - dUV1.y() * E2.x()),
			f * (dUV2.y() * E1.y() - dUV1.y() * E2.y()),
			f * (dUV2.y() * E1.z() - dUV1.y() * E2.z()));
		T.normalize();

		vec3 B(f * (-dUV2.x() * E1.x() + dUV1.x() * E2.x()),
			f * (-dUV2.x() * E1.y() + dUV1.x() * E2.y()),
			f * (-dUV2.x() * E1.z() + dUV1.x() * E2.z()));
		B.normalize();

		v0.tangent = T;
		v1.tangent = T;
		v2.tangent = T;

		// calculated in shader
		//v0.bitangent = B;
		//v1.bitangent = B;
		//v2.bitangent = B;

		if (v0.normal == vec3::Zero)
		{
			v0.normal = static_cast<const vec3>((XMVector3Cross(T, B))).normalized();
		}
		if (v1.normal == vec3::Zero)
		{
			v1.normal = static_cast<const vec3>((XMVector3Cross(T, B))).normalized();
		}
		if (v2.normal == vec3::Zero)
		{
			v2.normal = static_cast<const vec3>((XMVector3Cross(T, B))).normalized();
		}
	}
}
#endif