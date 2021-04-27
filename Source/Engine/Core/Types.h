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

using int64 = long long;
using int32 = int;
using int16 = short;
using int8  = char;

using uint64 = unsigned long long;
using uint32 = unsigned;
using uint16 = unsigned short;
using uint8  = unsigned char;

using uint = unsigned;

using fp32 = float;

// -------------------------------------

using ID_TYPE = int;
constexpr ID_TYPE INVALID_ID = -1;

using BufferID = ID_TYPE;
using TextureID = ID_TYPE;
using SamplerID = ID_TYPE;
using SRV_ID = ID_TYPE;
using UAV_ID = ID_TYPE;
using CBV_ID = ID_TYPE;
using RTV_ID = ID_TYPE;
using DSV_ID = ID_TYPE;

using PSO_ID = ID_TYPE;

using EnvironmentMapID = ID_TYPE;
using MeshID = ID_TYPE;
using MaterialID = ID_TYPE;
using ModelID = ID_TYPE;
using TransformID = ID_TYPE;

using TaskID = ID_TYPE;