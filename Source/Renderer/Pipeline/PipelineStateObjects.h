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

#include "ShaderCompileUtils.h"
#include <unordered_map>
#include "Tessellation.h"
#include "Engine/Core/Types.h"

namespace D3D12MA { class Allocator; }
class Window;
struct ID3D12RootSignature;
struct ID3D12PipelineState;
class VQRenderer;

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// TYPE DEFINITIONS
//
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct FPSODesc
{
	std::string PSOName;
	D3D12_COMPUTE_PIPELINE_STATE_DESC  D3D12ComputeDesc;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC D3D12GraphicsDesc;
	std::vector<FShaderStageCompileDesc> ShaderStageCompileDescs;
};

struct FPSOCreationTaskParameters
{
	PSO_ID* pID = nullptr; // ID to be set by the task once the PSO loads
	FPSODesc Desc = {};
};


enum EBuiltinPSOs // TODO: remove the hardcoded PSOs when a generic Shader solution is integrated
{
	FULLSCREEN_TRIANGLE_PSO = 0,
	VIZUALIZATION_CS_PSO,
	UI_PSO,
	UI_HDR_scRGB_PSO,
	TONEMAPPER_PSO,
	HDR_FP16_SWAPCHAIN_PSO,
	SKYDOME_PSO,
	SKYDOME_PSO_MSAA_4,
	WIREFRAME_PSO,
	WIREFRAME_PSO_MSAA_4,
	WIREFRAME_INSTANCED_PSO,
	WIREFRAME_INSTANCED_MSAA4_PSO,
	UNLIT_PSO,
	UNLIT_BLEND_PSO,
	UNLIT_PSO_MSAA_4,
	UNLIT_BLEND_PSO_MSAA_4,
	CUBEMAP_CONVOLUTION_DIFFUSE_PSO,
	CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO,
	CUBEMAP_CONVOLUTION_SPECULAR_PSO,
	GAUSSIAN_BLUR_CS_NAIVE_X_PSO,
	GAUSSIAN_BLUR_CS_NAIVE_Y_PSO,
	BRDF_INTEGRATION_CS_PSO,
	FFX_CAS_CS_PSO,
	FFX_SPD_CS_PSO,
	FFX_FSR1_EASU_CS_PSO,
	FFX_FSR1_RCAS_CS_PSO,
	DOWNSAMPLE_DEPTH_CS_PSO,
	DEBUGVERTEX_LOCALSPACEVECTORS_PSO,
	DEBUGVERTEX_LOCALSPACEVECTORS_PSO_MSAA_4,
	NUM_BUILTIN_PSOs
};

enum EBuiltinRootSignatures
{
	CS__SRV1_UAV1_ROOTCBV1,
	CS__SRV2_UAV1_ROOTCBV1,
	//--------------------------
	LEGACY__FullScreenTriangle,
	LEGACY__HelloWorldCube,
	LEGACY__Object,
	LEGACY__ForwardLighting,
	LEGACY__WireframeUnlit,
	LEGACY__ShadowPass,
	LEGACY__ConvolutionCubemap,
	LEGACY__BRDFIntegrationCS,
	LEGACY__FFX_SPD_CS,
	LEGACY__ZPrePass,
	LEGACY__OutlinePass,
	LEGACY__FFX_FSR1,
	LEGACY__UI_HDR_Composite,
	LEGACY__DownsampleDepthCS,

	NUM_BUILTIN_ROOT_SIGNATURES
};


// off/msaa4  TODO: other msaa
static constexpr size_t NUM_MSAA_OPTIONS = 2; 
static constexpr UINT MSAA_SAMPLE_COUNTS[NUM_MSAA_OPTIONS] = { 1, 4 };

struct PSOCollection
{
	virtual void GatherPSOLoadDescs(const std::unordered_map<RS_ID, ID3D12RootSignature*>& mRootSignatureLookup) = 0;
	void Compile(VQRenderer& Renderer);
	PSO_ID Get(size_t hash) const;

	std::unordered_map<size_t, PSO_ID>   mapPSO;
	std::unordered_map<size_t, FPSODesc> mapLoadDesc;
};

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

struct FLightingPSOs : PSOCollection
{
	// rendering
	static constexpr size_t NUM_RASTER_OPTS = 2; // solid/wireframe
	static constexpr size_t NUM_FACECULL_OPTS = 3; // none/front/back
	static constexpr size_t NUM_RENDERING_OPTIONS = NUM_FACECULL_OPTS * NUM_RASTER_OPTS * NUM_MSAA_OPTIONS;

	// pass output
	static constexpr size_t NUM_MOVEC_OPTS = 2; // on/off
	static constexpr size_t NUM_ROUGH_OPTS = 2; // on/off
	static constexpr size_t NUM_OUTPUT_OPTS = NUM_ROUGH_OPTS * NUM_MOVEC_OPTS;

	// material
	static constexpr size_t NUM_ALPHA_OPTIONS = 2; // opaque/alpha masked
	static constexpr size_t NUM_MAT_OPTIONS = NUM_ALPHA_OPTIONS;

	static size_t Hash(size_t iMSAA, size_t iRaster, size_t iFaceCull, size_t iOutMoVec, size_t iOutRough, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha);
	inline PSO_ID  Get(size_t iMSAA, size_t iRaster, size_t iFaceCull, size_t iOutMoVec, size_t iOutRough, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha) const
	{
		return PSOCollection::Get(Hash(iMSAA, iRaster, iFaceCull, iOutMoVec, iOutRough, iTess, iDomain, iPart, iOutTopo, iTessCullMode, iAlpha));
	}

	void GatherPSOLoadDescs(const std::unordered_map<RS_ID, ID3D12RootSignature*>& mRootSignatureLookup) override;

	static constexpr size_t NUM_OPTIONS_PERMUTATIONS = NUM_MAT_OPTIONS * NUM_RENDERING_OPTIONS * NUM_OUTPUT_OPTS * Tessellation::NUM_TESS_OPTIONS;
};

// ------------------------------------------------------------------------------------------------------------------------

struct FDepthPrePassPSOs : public PSOCollection
{
	// rendering
	static constexpr size_t NUM_RASTER_OPTS = 2; // solid/wireframe
	static constexpr size_t NUM_FACECULL_OPTS = 3; // none/front/back
	static constexpr size_t NUM_RENDERING_OPTIONS = NUM_FACECULL_OPTS * NUM_RASTER_OPTS * NUM_MSAA_OPTIONS;

	// material
	static constexpr size_t NUM_ALPHA_OPTIONS = 2; // opaque/alpha masked
	static constexpr size_t NUM_MAT_OPTIONS = NUM_ALPHA_OPTIONS;

	static size_t Hash(size_t iMSAA, size_t iRaster, size_t iFaceCull, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha);
	inline PSO_ID  Get(size_t iMSAA, size_t iRaster, size_t iFaceCull, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha) const
	{
		return PSOCollection::Get(Hash(iMSAA, iRaster, iFaceCull, iTess, iDomain, iPart, iOutTopo, iTessCullMode, iAlpha));
	}

	void GatherPSOLoadDescs(const std::unordered_map<RS_ID, ID3D12RootSignature*>& mRootSignatureLookup) override;

	static constexpr size_t NUM_OPTIONS_PERMUTATIONS = NUM_MAT_OPTIONS * NUM_RENDERING_OPTIONS * Tessellation::NUM_TESS_OPTIONS;
};

// ------------------------------------------------------------------------------------------------------------------------

struct FShadowPassPSOs : public PSOCollection
{
	// rendering
	static constexpr size_t NUM_DEPTH_RENDER_OPTS = 2; // NDC/WorldSpace(linear)
	static constexpr size_t NUM_RASTER_OPTS = 2; // solid/wireframe
	static constexpr size_t NUM_FACECULL_OPTS = 3; // none/front/back
	static constexpr size_t NUM_RENDERING_OPTIONS = NUM_FACECULL_OPTS * NUM_RASTER_OPTS;

	// material
	static constexpr size_t NUM_ALPHA_OPTIONS = 2; // opaque/alpha masked
	static constexpr size_t NUM_MAT_OPTIONS = NUM_ALPHA_OPTIONS;


	static size_t Hash(size_t iDepthMode, size_t iRaster, size_t iFaceCull, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha);
	inline PSO_ID  Get(size_t iDepthMode, size_t iRaster, size_t iFaceCull, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha) const
	{
		return PSOCollection::Get(Hash(iDepthMode, iRaster, iFaceCull, iTess, iDomain, iPart, iOutTopo, iTessCullMode, iAlpha));
	}

	void GatherPSOLoadDescs(const std::unordered_map<RS_ID, ID3D12RootSignature*>& mRootSignatureLookup) override;

	static constexpr size_t NUM_OPTIONS_PERMUTATIONS = NUM_MAT_OPTIONS * NUM_RENDERING_OPTIONS * Tessellation::NUM_TESS_OPTIONS;
};

// ------------------------------------------------------------------------------------------------------------------------
