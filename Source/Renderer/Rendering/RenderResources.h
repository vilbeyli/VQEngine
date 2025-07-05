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

#include "Engine/Core/Types.h"

#include "Rendering/EnvironmentMapRendering.h"

constexpr bool MSAA_ENABLE = true;
constexpr uint MSAA_SAMPLE_COUNT = 4;

struct FRenderingResources_MainWindow
{
	TextureID Tex_ShadowMaps_Spot = INVALID_ID;
	TextureID Tex_ShadowMaps_Point = INVALID_ID;
	TextureID Tex_ShadowMaps_Directional = INVALID_ID;

	TextureID Tex_SceneColorMSAA = INVALID_ID;
	TextureID Tex_SceneColor = INVALID_ID;
	TextureID Tex_SceneColorBoundingVolumes = INVALID_ID;
	TextureID Tex_SceneDepthMSAA = INVALID_ID;
	TextureID Tex_SceneDepth = INVALID_ID;
	TextureID Tex_SceneDepthResolve = INVALID_ID;
	TextureID Tex_SceneNormalsMSAA = INVALID_ID;
	TextureID Tex_SceneNormals = INVALID_ID;
	TextureID Tex_AmbientOcclusion = INVALID_ID;
	TextureID Tex_SceneVisualization = INVALID_ID;
	TextureID Tex_SceneVisualizationMSAA = INVALID_ID;
	TextureID Tex_SceneMotionVectors = INVALID_ID;
	TextureID Tex_SceneMotionVectorsMSAA = INVALID_ID;

	TextureID Tex_DownsampledSceneDepth = INVALID_ID;
	TextureID Tex_DownsampledSceneDepthAtomicCounter = INVALID_ID;

	TextureID Tex_PostProcess_BlurIntermediate = INVALID_ID;
	TextureID Tex_PostProcess_BlurOutput = INVALID_ID;
	TextureID Tex_PostProcess_TonemapperOut = INVALID_ID;
	TextureID Tex_PostProcess_VisualizationOut = INVALID_ID;
	TextureID Tex_PostProcess_FFXCASOut = INVALID_ID;
	TextureID Tex_PostProcess_FSR_EASUOut = INVALID_ID;
	TextureID Tex_PostProcess_FSR_RCASOut = INVALID_ID;
	TextureID Tex_UI_SDR = INVALID_ID;

	RTV_ID    RTV_SceneColorMSAA = INVALID_ID;
	RTV_ID    RTV_SceneColor = INVALID_ID;
	RTV_ID    RTV_SceneColorBoundingVolumes = INVALID_ID;
	RTV_ID    RTV_SceneNormalsMSAA = INVALID_ID;
	RTV_ID    RTV_SceneNormals = INVALID_ID;
	RTV_ID    RTV_UI_SDR = INVALID_ID;
	RTV_ID    RTV_SceneVisualization = INVALID_ID;
	RTV_ID    RTV_SceneVisualizationMSAA = INVALID_ID;
	RTV_ID    RTV_SceneMotionVectors = INVALID_ID;
	RTV_ID    RTV_SceneMotionVectorsMSAA = INVALID_ID;

	SRV_ID    SRV_PostProcess_BlurIntermediate = INVALID_ID;
	SRV_ID    SRV_PostProcess_BlurOutput = INVALID_ID;
	SRV_ID    SRV_PostProcess_TonemapperOut = INVALID_ID;
	SRV_ID    SRV_PostProcess_VisualizationOut = INVALID_ID;
	SRV_ID    SRV_PostProcess_FFXCASOut = INVALID_ID;
	SRV_ID    SRV_PostProcess_FSR_EASUOut = INVALID_ID;
	SRV_ID    SRV_PostProcess_FSR_RCASOut = INVALID_ID;
	SRV_ID    SRV_ShadowMaps_Spot = INVALID_ID;
	SRV_ID    SRV_ShadowMaps_Point = INVALID_ID;
	SRV_ID    SRV_ShadowMaps_Directional = INVALID_ID;
	SRV_ID    SRV_SceneColor = INVALID_ID;
	SRV_ID    SRV_SceneColorBoundingVolumes = INVALID_ID;
	SRV_ID    SRV_SceneColorMSAA = INVALID_ID;
	SRV_ID    SRV_SceneNormals = INVALID_ID;
	SRV_ID    SRV_SceneNormalsMSAA = INVALID_ID;
	SRV_ID    SRV_SceneDepth = INVALID_ID;
	SRV_ID    SRV_SceneDepthMSAA = INVALID_ID;
	SRV_ID    SRV_FFXCACAO_Out = INVALID_ID;
	SRV_ID    SRV_UI_SDR = INVALID_ID;
	SRV_ID    SRV_SceneVisualization = INVALID_ID;
	SRV_ID    SRV_SceneVisualizationMSAA = INVALID_ID;
	SRV_ID    SRV_SceneMotionVectors = INVALID_ID;
	SRV_ID    SRV_BRDFIntegrationLUT = INVALID_ID;

	UAV_ID    UAV_FFXCACAO_Out = INVALID_ID;
	UAV_ID    UAV_PostProcess_BlurIntermediate = INVALID_ID;
	UAV_ID    UAV_PostProcess_BlurOutput = INVALID_ID;
	UAV_ID    UAV_PostProcess_TonemapperOut = INVALID_ID;
	UAV_ID    UAV_PostProcess_VisualizationOut = INVALID_ID;
	UAV_ID    UAV_PostProcess_FFXCASOut = INVALID_ID;
	UAV_ID    UAV_PostProcess_FSR_EASUOut = INVALID_ID;
	UAV_ID    UAV_PostProcess_FSR_RCASOut = INVALID_ID;
	UAV_ID    UAV_SceneDepth = INVALID_ID;
	UAV_ID    UAV_SceneColor = INVALID_ID;
	UAV_ID    UAV_SceneNormals = INVALID_ID;

	UAV_ID    UAV_DownsampledSceneDepth = INVALID_ID;
	UAV_ID    UAV_DownsampledSceneDepthAtomicCounter = INVALID_ID;

	DSV_ID    DSV_SceneDepth = INVALID_ID;
	DSV_ID    DSV_SceneDepthMSAA = INVALID_ID;
	DSV_ID    DSV_ShadowMaps_Spot = INVALID_ID;
	DSV_ID    DSV_ShadowMaps_Point = INVALID_ID;
	DSV_ID    DSV_ShadowMaps_Directional = INVALID_ID;

	FEnvironmentMapRenderingResources EnvironmentMap;

	SRV_ID SRV_NullCubemap = INVALID_ID;
	SRV_ID SRV_NullTexture2D = INVALID_ID;
};
struct FRenderingResources_DebugWindow
{
	// TODO
};
