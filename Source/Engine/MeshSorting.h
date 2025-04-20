//	VQE
//	Copyright(C) 2025  - Volkan Ilbeyli
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

#include "Core/Types.h"
#include <cassert>
#include <utility>

namespace MeshSorting
{
	// Shadow Mesh Sort Key
	// 
	// Bits[0  -  3] : LOD
	// Bits[4  - 33] : MeshID
	// Bits[34 - 63] : MaterialID
	inline uint64     GetShadowMeshKey(MaterialID matID, MeshID meshID, int lod, bool bTessellated)
	{
		assert(matID != -1); assert(meshID != -1); assert(lod >= 0 && lod < 16);

		constexpr int mask = 0x3FFFFFFF; // __11 1111 1111 1111 ...| use the first 30 bits of IDs
		uint64 hash = std::max(0, std::min(1 << 4, lod));
		hash |= ((uint64)(meshID & mask)) << 4;
		if (bTessellated)
		{
			hash |= ((uint64)(matID & mask)) << 34;
		}
		return hash;
	}
	inline MaterialID GetMatIDFromShadowMeshKey(uint64 key) { return MaterialID(key >> 34); }
	inline MeshID     GetMeshIDFromShadowMeshKey(uint64 key) { return MeshID((key >> 4) & 0x3FFFFFFF); }
	inline int        GetLODFromShadowMeshKey(uint64 key) { return int(key & 0xF); }

	// Lit Mesh Sort Key
	//
	// Bits[0  -  3] : LOD
	// Bits[4  - 33] : MeshID
	// Bits[34 - 34] : IsAlphaMasked (or opaque)
	// Bits[35 - 35] : IsTessellated
	inline uint64 GetLitMeshKey(MaterialID matID, MeshID meshID, int lod, /*UNUSED*/bool bTessellated)
	{
		assert(matID != -1);
		assert(meshID != -1);
		assert(lod >= 0 && lod < 16);
		constexpr int mask = 0x3FFFFFFF; // __11 1111 1111 1111 ... | use the first 30 bits of IDs
		uint64 hash = std::max(0, std::min(1 << 4, lod));
		hash |= ((uint64)(meshID & mask)) << 4;
		hash |= ((uint64)(matID & mask)) << 34;
		return hash;
	}
	inline MaterialID GetMatIDFromLitMeshKey(uint64 key) { return MaterialID(key >> 34); }
	inline MeshID     GetMeshIDFromLitMeshKey(uint64 key) { return MeshID((key >> 4) & 0x3FFFFFFF); }
	inline int        GetLODFromLitMeshKey(uint64 key) { return int(key & 0xF); }
	//--------------------------------------------------------------------------------------------------------------------------------------------

	//--------------------------------------------------------------------------------------------------------------------------------------------
	// Keys help collect instance data based on Material, and then Mesh.
	//--------------------------------------------------------------------------------------------------------------------------------------------
	// E.g. LitMesh Sorting
	// 
	// MAT0
	// +---- PSO0                       MAT1       
	//     +----MESH0                 +----MESH37             
	//         +----LOD0                  +----LOD0                
	//             +----InstData0             +----InstData0                        
	//             +----InstData1             +----InstData1                        
	//          +----LOD1                     +----InstData2                
	//             +----InstData0      +----MESH225                        
	//             +----InstData1          +----LOD0                        
	// +---- PSO1
	//     +----MESH1                        +----InstData0             
	//         +----LOD0                     +----InstData1                
	//            +----InstData0          +----LOD1                        
	//            +----InstData1             +----InstData0                        
	//            +----InstData2             +----InstData1                        
	//     +----MESH2                              
	//         +----LOD0
	//            +----InstData0
	//
};
