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

#pragma once

#include "../Shaders/LightingConstantBufferData.h"
#include "Shader.h"

enum ETessellationDomain
{
	TRIANGLE_PATCH = 0,
	QUAD_PATCH,
	ISOLINE_PATCH,

	NUM_TESSELLATION_DOMAINS
};
enum ETessellationOutputTopology
{
	TESSELLATION_OUTPUT_POINT = 0,
	TESSELLATION_OUTPUT_LINE,
	TESSELLATION_OUTPUT_TRIANGLE_CW,
	TESSELLATION_OUTPUT_TRIANGLE_CCW,

	NUM_TESSELLATION_OUTPUT_TOPOLOGY
};
enum ETessellationPartitioning
{
	INTEGER = 0,
	FRACTIONAL_EVEN,
	FRACTIONAL_ODD,
	POWER_OF_TWO,
};

struct FTessellationParameters
{
	static constexpr float MAX_TESSELLATION_FACTOR = 64.0f;

	bool bEnableTessellation = false;
	ETessellationDomain Domain = ETessellationDomain::TRIANGLE_PATCH;
	ETessellationOutputTopology OutputTopology = ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CW;
	ETessellationPartitioning Partitioning = ETessellationPartitioning::FRACTIONAL_ODD;

	VQ_SHADER_DATA::TessellationParams GPUParams;
	bool bEnableVertexTBNVisualization = false;

	static const char* GetShaderMacro_Domain_Tri() { return ""; }
	static const char* GetShaderMacro_Domain_Quad() { return ""; }
	static const char* GetShaderMacro_Domain_Line() { return ""; }
	static const char* GetShaderMacro_() { return ""; }

	inline void SetAllTessellationFactors(float TessellationFactor)
	{
		switch (Domain)
		{
		case TRIANGLE_PATCH: GPUParams.TriInnerTessFactor = GPUParams.TriEdgeTessFactor.x = GPUParams.TriEdgeTessFactor.y = GPUParams.TriEdgeTessFactor.z = TessellationFactor; break;
		case QUAD_PATCH: GPUParams.QuadInsideFactor.x = GPUParams.QuadInsideFactor.y = GPUParams.QuadEdgeTessFactor.x = GPUParams.QuadEdgeTessFactor.y = GPUParams.QuadEdgeTessFactor.z = GPUParams.QuadEdgeTessFactor.w = TessellationFactor; break;
		case ISOLINE_PATCH: // TODO: line
			break;
		}
	}
};
