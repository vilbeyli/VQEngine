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

enum EShaderStageFlags : unsigned
{
	SHADER_STAGE_NONE         = 0x00000000,
	SHADER_STAGE_VS           = 0x00000001,
	SHADER_STAGE_GS           = 0x00000002,
	SHADER_STAGE_DS           = 0x00000004,
	SHADER_STAGE_HS           = 0x00000008,
	SHADER_STAGE_PS           = 0x00000010,
	SHADER_STAGE_ALL_GRAPHICS = 0X0000001F,
	SHADER_STAGE_CS           = 0x00000020,

	SHADER_STAGE_COUNT = 6
};
enum EShaderStage : unsigned // use this enum for array indexing
{	
	VS = 0,
	GS,
	DS,
	HS,
	PS,
	CS,

	NUM_SHADER_STAGES // =6
	, UNINITIALIZED = NUM_SHADER_STAGES
};
enum EShaderModel : unsigned
{   // https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model
	SM5_0 = 0 , // DX11 Tessellation, compute shaders - DXBC
	SM6_0,      // DX12 DXIL
	SM6_1,      // Conservative rasterization, ROVs
	SM6_2,      // Wave intrinsics, quad ops
	SM6_3,      // DXR 1.0
	SM6_4,      // VRS, enhanced ray tracing
	SM6_5,      // Mesh shaders, task (amplification) shaders
	SM6_6,      // Atomics, dynamic resources
	SM6_7,      // DXR 1.1
	SM6_8,      // Work graphs
	
	NUM_SHADER_MODELS
};

struct FShaderMacro
{
	char Name[256];
	char Value[128];
	static FShaderMacro CreateShaderMacro(const char* name, const char* format, ...);
};
