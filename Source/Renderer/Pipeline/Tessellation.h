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

#include "Engine/Core/Types.h"
#include "Shaders/LightingConstantBufferData.h"
#include "Shader.h"

#include <string>
#include <vector>

enum class ETessellationDomain : uint8
{
	TRIANGLE_PATCH = 0,
	QUAD_PATCH,
	ISOLINE_PATCH,

	NUM_TESSELLATION_DOMAINS
};
enum class ETessellationOutputTopology : uint8
{
	TESSELLATION_OUTPUT_POINT = 0,
	TESSELLATION_OUTPUT_LINE,
	TESSELLATION_OUTPUT_TRIANGLE_CW,
	TESSELLATION_OUTPUT_TRIANGLE_CCW,

	NUM_TESSELLATION_OUTPUT_TOPOLOGY
};
enum class ETessellationPartitioning : uint8
{
	INTEGER = 0,
	FRACTIONAL_EVEN,
	FRACTIONAL_ODD,
	POWER_OF_TWO,
};

struct FTessellationParameters
{
	static constexpr float MAX_TESSELLATION_FACTOR = 64.0f;

	VQ_SHADER_DATA::TessellationParams GPUParams;

	// bit 0  : bEnableTessellation
	// bit 1  : bEnableVertexTBNVisualization
	// bit 2-3: ETessellationDomain
	// bit 4-5: ETessellationOutputTopology
	// bit 6-7: ETessellationPartitioning;
	uint8 PackedTessellationConfig = 
		(((uint8)ETessellationDomain::TRIANGLE_PATCH                         ) << 2) | 
		(((uint8)ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CW) << 4) |
		(((uint8)ETessellationPartitioning::FRACTIONAL_ODD                   ) << 6) ;

	inline bool IsTessellationEnabled() const { return PackedTessellationConfig & 0x1; }
	inline bool IsVertexTBNVisualization() const { return PackedTessellationConfig & 0x2; }
	inline ETessellationDomain GetDomain() const { return static_cast<ETessellationDomain>((PackedTessellationConfig >> 2) & 0x3); }
	inline ETessellationOutputTopology GetOutputTopology() const { return static_cast<ETessellationOutputTopology>((PackedTessellationConfig >> 4) & 0x3); }
	inline ETessellationPartitioning GetPartitioning() const { return static_cast<ETessellationPartitioning>((PackedTessellationConfig >> 6) & 0x3); }

	inline void SetTessellationEnabled(bool b) { PackedTessellationConfig |= (b ? 1 : 0); }
	inline void SetVertexTBNVisualization(bool b) { PackedTessellationConfig |= (b ? 2 : 0); }
	inline void SetDomain(ETessellationDomain d) { PackedTessellationConfig |= ((uint8)d) << 2; }
	inline void SetOutputTopology(ETessellationOutputTopology t) { PackedTessellationConfig |= ((uint8)t) << 4; }
	inline void SetPartitioning(ETessellationPartitioning p) { PackedTessellationConfig |= ((uint8)p) << 6; }

	inline void SetAllTessellationFactors(float TessellationFactor)
	{
		const ETessellationDomain Domain = GetDomain();
		switch (Domain)
		{
		case ETessellationDomain::TRIANGLE_PATCH: GPUParams.TriInnerTessFactor = GPUParams.TriEdgeTessFactor.x = GPUParams.TriEdgeTessFactor.y = GPUParams.TriEdgeTessFactor.z = TessellationFactor; break;
		case ETessellationDomain::QUAD_PATCH    : GPUParams.QuadInsideFactor.x = GPUParams.QuadInsideFactor.y  = GPUParams.QuadEdgeTessFactor.x = GPUParams.QuadEdgeTessFactor.y = GPUParams.QuadEdgeTessFactor.z = GPUParams.QuadEdgeTessFactor.w = TessellationFactor; break;
		case ETessellationDomain::ISOLINE_PATCH: // TODO: line
			break;
		}
	}
};


namespace Tessellation
{
	constexpr size_t NUM_TESS_ENABLED = 2; // on/off
	constexpr size_t NUM_DOMAIN_OPTIONS = 3; // tri/quad/line
	constexpr size_t NUM_PARTIT_OPTIONS = 4; // integer, fractional_even, fractional_odd, or pow2
	constexpr size_t NUM_OUTTOP_OPTIONS = 4; // point, line, triangle_cw, or triangle_ccw
	constexpr size_t NUM_TESS_CULL_OPTIONS = 2; // Cull[on/off], dynamic branch for face/frustum params
	constexpr size_t NUM_TESS_OPTIONS = NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * NUM_TESS_CULL_OPTIONS;


	// TESSELLATION CONSTANT DATA FOR COMPILATION
	constexpr const char* szPartitionNames[NUM_PARTIT_OPTIONS] = { "integer", "fractional_even", "fractional_odd", "pw2" };
	constexpr const char* szOutTopologyNames[NUM_OUTTOP_OPTIONS] = { "point", "line", "triangle_cw", "triangle_ccw" };

	constexpr const char* szOutTopologyMacros[NUM_OUTTOP_OPTIONS] = { "OUTTOPO__POINT"   , "OUTTOPO__LINE"     , "OUTTOPO__TRI_CW"  , "OUTTOPO__TRI_CCW" };
	constexpr const char* szPartitioningMacros[NUM_PARTIT_OPTIONS] = { "PARTITIONING__INT", "PARTITIONING__EVEN", "PARTITIONING__ODD", "PARTITIONING__POW2" };
	constexpr const char* szDomainMacros[NUM_DOMAIN_OPTIONS] = { "DOMAIN__TRIANGLE" , "DOMAIN__QUAD"      , "DOMAIN__LINE" };

	const FShaderMacro TessellationOutputTopologyEnabledMacros[NUM_PARTIT_OPTIONS] = {
		FShaderMacro::CreateShaderMacro( szOutTopologyMacros[0], "1" ),
		FShaderMacro::CreateShaderMacro( szOutTopologyMacros[1], "1" ),
		FShaderMacro::CreateShaderMacro( szOutTopologyMacros[2], "1" ),
		FShaderMacro::CreateShaderMacro( szOutTopologyMacros[3], "1" ),
	};
	const FShaderMacro TessellationPartitioningEnabledMacros[NUM_PARTIT_OPTIONS] = {
		FShaderMacro::CreateShaderMacro( szPartitioningMacros[0], "1" ),
		FShaderMacro::CreateShaderMacro( szPartitioningMacros[1], "1" ),
		FShaderMacro::CreateShaderMacro( szPartitioningMacros[2], "1" ),
		FShaderMacro::CreateShaderMacro( szPartitioningMacros[3], "1" ),
	};
	const FShaderMacro TessellationDomainEnabledMacros[NUM_DOMAIN_OPTIONS] = {
		FShaderMacro::CreateShaderMacro( szDomainMacros[0], "1" ),
		FShaderMacro::CreateShaderMacro( szDomainMacros[1], "1" ),
		FShaderMacro::CreateShaderMacro( szDomainMacros[2], "1" ),
	};
	constexpr const FShaderMacro TessellationGSEnabledMacro = { "ENABLE_GEOMETRY_SHADER", "1" };
	inline void AppendTessellationVSMacros(std::vector<FShaderMacro>& Macros, size_t iDomain)
	{
		Macros.push_back(TessellationDomainEnabledMacros[iDomain]);
	}
	inline void AppendTessellationHSMacros(std::vector<FShaderMacro>& Macros, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCull)
	{
		Macros.push_back(TessellationPartitioningEnabledMacros[iPart]);
		Macros.push_back(TessellationDomainEnabledMacros[iDomain]);
		Macros.push_back(TessellationOutputTopologyEnabledMacros[iOutTopo]);
		if (iTessCull > 0)
		{
			Macros.push_back(TessellationGSEnabledMacro);
		}
	}
	inline void AppendTessellationDSMacros(std::vector<FShaderMacro>& Macros, size_t iDomain, size_t iOutTopo, size_t iTessCull)
	{
		Macros.push_back(TessellationDomainEnabledMacros[iDomain]);
		Macros.push_back(TessellationOutputTopologyEnabledMacros[iOutTopo]);
		if (iTessCull > 0)
		{
			Macros.push_back(TessellationGSEnabledMacro);
		}
	}
	inline void AppendTessellationGSMacros(std::vector<FShaderMacro>& Macros, size_t iOutTopo, size_t iTessCull)
	{
		if (iTessCull == 0)
			return;
		Macros.push_back(TessellationOutputTopologyEnabledMacros[iOutTopo]);
		Macros.push_back(TessellationGSEnabledMacro);
	}

	inline void AppendTessellationPSONameTokens(std::string& PSOName, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCull)
	{
		PSOName += "_Tessellated";
		if (iDomain == 1) PSOName += "_Quad";
		PSOName += std::string("_") + szPartitionNames[iPart];
		PSOName += std::string("_") + szOutTopologyNames[iOutTopo];
		if (iTessCull > 1) PSOName += "_GSCull";
	}
	inline bool ShouldSkipTessellationVariant(size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCull)
	{
		const bool bOutputTopologyIsTriangle = ((ETessellationOutputTopology)iOutTopo != ETessellationOutputTopology::TESSELLATION_OUTPUT_LINE && (ETessellationOutputTopology)iOutTopo != ETessellationOutputTopology::TESSELLATION_OUTPUT_POINT);

		if (iTess == 0 && (iDomain > 0 || iPart > 0 || iOutTopo > 0 || iTessCull > 0))
			return true; // skip tess permutations when tess is off
		if (iTess == 1 && (ETessellationOutputTopology)iOutTopo == ETessellationOutputTopology::TESSELLATION_OUTPUT_LINE && (ETessellationDomain)iDomain != ETessellationDomain::ISOLINE_PATCH)
			return true; // line output topologies are only available to isoline domains
		if (iTess == 1 && (ETessellationDomain)iDomain == ETessellationDomain::ISOLINE_PATCH && bOutputTopologyIsTriangle)
			return true; // // IsoLine domain must specify output primitive point or line
		if (iTess == 1 && iTessCull > 0 && !bOutputTopologyIsTriangle)
			return true; // prevent non-tri output topologies to utilize GS (because it'll only be a perf penalty)
		return false;
	}

	inline void GetTessellationPSOConfig(const FTessellationParameters& tess,
		uint8& iTess,
		uint8& iDomain,
		uint8& iPart,
		uint8& iOutTopo,
		uint8& iTessCull
	)
	{
		const bool bFrustumCull  = tess.GPUParams.bFrustumCull_FaceCull_AdaptiveTessellation & 0x1;
		const bool bFaceCull     = tess.GPUParams.bFrustumCull_FaceCull_AdaptiveTessellation & 0x2;
		const bool bAdaptiveTess = tess.GPUParams.bFrustumCull_FaceCull_AdaptiveTessellation & 0x4;
		const bool bSWCullTessellation = bFaceCull || bFrustumCull;

		iTess     = tess.IsTessellationEnabled() ? 1 : 0;
		iDomain   = iTess == 0 ? 0 : (uint8)tess.GetDomain();
		iPart     = iTess == 0 ? 0 : (uint8)tess.GetPartitioning();
		iOutTopo  = iTess == 0 ? 0 : (uint8)tess.GetOutputTopology();
		iTessCull = iTess == 1 && (bSWCullTessellation ? 1 : 0);
		return;
	}
};