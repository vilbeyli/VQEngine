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

#include "RenderResources.h"
#include "Renderer.h"

void VQRenderer::LoadWindowSizeDependentResources(HWND hwnd, unsigned Width, unsigned Height, float fResolutionScale, bool bRenderingHDR)
{
	const uint RenderResolutionX = static_cast<uint>(Width * fResolutionScale);
	const uint RenderResolutionY = static_cast<uint>(Height * fResolutionScale);

	constexpr DXGI_FORMAT MainColorRTFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	const DXGI_FORMAT TonemapperOutputFormat = bRenderingHDR ? VQRenderer::PREFERRED_HDR_FORMAT : DXGI_FORMAT_R8G8B8A8_UNORM;

	FRenderingResources_MainWindow& r = mResources_MainWnd;

	{	// Scene depth stencil view /w MSAA
		TextureCreateDesc desc("SceneDepthMSAA");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_TYPELESS
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, MSAA_SAMPLE_COUNT // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		r.Tex_SceneDepthMSAA = this->CreateTexture(desc);
		this->InitializeDSV(r.DSV_SceneDepthMSAA, 0u, r.Tex_SceneDepthMSAA);
		this->InitializeSRV(r.SRV_SceneDepthMSAA, 0u, r.Tex_SceneDepthMSAA);
	}
	{	// Scene depth stencil resolve target
		TextureCreateDesc desc("SceneDepthResolve");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_FLOAT
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		r.Tex_SceneDepthResolve = this->CreateTexture(desc);
		this->InitializeUAV(r.UAV_SceneDepth, 0u, r.Tex_SceneDepthResolve, 0u, 0u);
		this->InitializeSRV(r.SRV_SceneDepth, 0u, r.Tex_SceneDepthResolve, 0u, 0u);
	}
	{	// Scene depth stencil target (for MSAA off)
		TextureCreateDesc desc("SceneDepth");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_TYPELESS
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		r.Tex_SceneDepth = this->CreateTexture(desc);
		this->InitializeDSV(r.DSV_SceneDepth, 0u, r.Tex_SceneDepth);
	}
	{
		TextureCreateDesc desc("DownsampledSceneDepth");
		const int NumMIPs = Image::CalculateMipLevelCount(RenderResolutionX, RenderResolutionY);
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_FLOAT
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, NumMIPs
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		r.Tex_DownsampledSceneDepth = this->CreateTexture(desc);
		for (int mip = 0; mip < 13; ++mip) // 13 comes from downsampledepth.hlsl resource count, TODO: fix magic number
			this->InitializeUAV(r.UAV_DownsampledSceneDepth, mip, r.Tex_DownsampledSceneDepth, 0, std::min(mip, NumMIPs - 1));
	}

	{ // Main render target view w/ MSAA
		TextureCreateDesc desc("SceneColorMSAA");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			MainColorRTFormat
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, MSAA_SAMPLE_COUNT // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);

		desc.ResourceState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		r.Tex_SceneColorMSAA = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_SceneColorMSAA, 0u, r.Tex_SceneColorMSAA);
		this->InitializeSRV(r.SRV_SceneColorMSAA, 0u, r.Tex_SceneColorMSAA);

		// scene visualization
		desc.TexName = "SceneVizMSAA";
		r.Tex_SceneVisualizationMSAA = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_SceneVisualizationMSAA, 0u, r.Tex_SceneVisualizationMSAA);
		this->InitializeSRV(r.SRV_SceneVisualizationMSAA, 0u, r.Tex_SceneVisualizationMSAA);

		// motion vectors
		desc.TexName = "SceneMotionVectorsMSAA";
		desc.d3d12Desc.Format = DXGI_FORMAT_R16G16_FLOAT;
		r.Tex_SceneMotionVectorsMSAA = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_SceneMotionVectorsMSAA, 0u, r.Tex_SceneMotionVectorsMSAA);
	}
	{ // MSAA resolve target
		TextureCreateDesc desc("SceneColor");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			MainColorRTFormat
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		r.Tex_SceneColor = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_SceneColor, 0u, r.Tex_SceneColor);
		this->InitializeSRV(r.SRV_SceneColor, 0u, r.Tex_SceneColor);
		this->InitializeUAV(r.UAV_SceneColor, 0u, r.Tex_SceneColor);

		// scene bounding volumes
		desc.TexName = "SceneBVs";
		desc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		r.Tex_SceneColorBoundingVolumes = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_SceneColorBoundingVolumes, 0u, r.Tex_SceneColorBoundingVolumes);
		this->InitializeSRV(r.SRV_SceneColorBoundingVolumes, 0u, r.Tex_SceneColorBoundingVolumes);

		// scene visualization
		desc.TexName = "SceneViz";
		desc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		r.Tex_SceneVisualization = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_SceneVisualization, 0u, r.Tex_SceneVisualization);
		this->InitializeSRV(r.SRV_SceneVisualization, 0u, r.Tex_SceneVisualization);

		// motion vectors
		desc.TexName = "SceneMotionVectors";
		desc.d3d12Desc.Format = DXGI_FORMAT_R16G16_FLOAT;
		r.Tex_SceneMotionVectors = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_SceneMotionVectors, 0u, r.Tex_SceneMotionVectors);
		this->InitializeSRV(r.SRV_SceneMotionVectors, 0u, r.Tex_SceneMotionVectors);
	}
	{ // Scene Normals
		TextureCreateDesc desc("SceneNormals");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R10G10B10A2_UNORM
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		r.Tex_SceneNormals = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_SceneNormals, 0u, r.Tex_SceneNormals);
		this->InitializeSRV(r.SRV_SceneNormals, 0u, r.Tex_SceneNormals);
		this->InitializeUAV(r.UAV_SceneNormals, 0u, r.Tex_SceneNormals);
	}

	{ // Scene Normals /w MSAA
		TextureCreateDesc desc("SceneNormalsMSAA");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R10G10B10A2_UNORM
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, MSAA_SAMPLE_COUNT // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		r.Tex_SceneNormalsMSAA = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_SceneNormalsMSAA, 0u, r.Tex_SceneNormalsMSAA);
		this->InitializeSRV(r.SRV_SceneNormalsMSAA, 0u, r.Tex_SceneNormalsMSAA);
	}

	{ // BlurIntermediate UAV & SRV
		TextureCreateDesc desc("BlurIntermediate");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			MainColorRTFormat
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		r.Tex_PostProcess_BlurIntermediate = this->CreateTexture(desc);
		this->InitializeUAV(r.UAV_PostProcess_BlurIntermediate, 0u, r.Tex_PostProcess_BlurIntermediate);
		this->InitializeSRV(r.SRV_PostProcess_BlurIntermediate, 0u, r.Tex_PostProcess_BlurIntermediate);

		desc.TexName = "BlurOutput";
		desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		r.Tex_PostProcess_BlurOutput = this->CreateTexture(desc);
		this->InitializeUAV(r.UAV_PostProcess_BlurOutput, 0u, r.Tex_PostProcess_BlurOutput);
		this->InitializeSRV(r.SRV_PostProcess_BlurOutput, 0u, r.Tex_PostProcess_BlurOutput);
	}

	{ // Tonemapper Resources
		TextureCreateDesc desc("TonemapperOut");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			TonemapperOutputFormat
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		r.Tex_PostProcess_TonemapperOut = this->CreateTexture(desc);
		this->InitializeUAV(r.UAV_PostProcess_TonemapperOut, 0u, r.Tex_PostProcess_TonemapperOut);
		this->InitializeSRV(r.SRV_PostProcess_TonemapperOut, 0u, r.Tex_PostProcess_TonemapperOut);
	}

	{ // Visualization Resources
		TextureCreateDesc desc("VizOut");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			TonemapperOutputFormat
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		r.Tex_PostProcess_VisualizationOut = this->CreateTexture(desc);
		this->InitializeUAV(r.UAV_PostProcess_VisualizationOut, 0u, r.Tex_PostProcess_VisualizationOut);
		this->InitializeSRV(r.SRV_PostProcess_VisualizationOut, 0u, r.Tex_PostProcess_VisualizationOut);
	}


	{ // FFX-CAS Resources
		TextureCreateDesc desc("FFXCAS_Out");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			TonemapperOutputFormat
			, RenderResolutionX
			, RenderResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		r.Tex_PostProcess_FFXCASOut = this->CreateTexture(desc);
		this->InitializeUAV(r.UAV_PostProcess_FFXCASOut, 0u, r.Tex_PostProcess_FFXCASOut);
		this->InitializeSRV(r.SRV_PostProcess_FFXCASOut, 0u, r.Tex_PostProcess_FFXCASOut);
	}

	{ // FSR1-EASU Resources
		TextureCreateDesc desc("FSR_EASU_Out");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			TonemapperOutputFormat
			, Width
			, Height
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		r.Tex_PostProcess_FSR_EASUOut = this->CreateTexture(desc);
		this->InitializeUAV(r.UAV_PostProcess_FSR_EASUOut, 0u, r.Tex_PostProcess_FSR_EASUOut);
		this->InitializeSRV(r.SRV_PostProcess_FSR_EASUOut, 0u, r.Tex_PostProcess_FSR_EASUOut);
	}
	{ // FSR1-RCAS Resources
		TextureCreateDesc desc("FSR_RCAS_Out");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			TonemapperOutputFormat
			, Width
			, Height
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		r.Tex_PostProcess_FSR_RCASOut = this->CreateTexture(desc);
		this->InitializeUAV(r.UAV_PostProcess_FSR_RCASOut, 0u, r.Tex_PostProcess_FSR_RCASOut);
		this->InitializeSRV(r.SRV_PostProcess_FSR_RCASOut, 0u, r.Tex_PostProcess_FSR_RCASOut);
	}

	{ // UI Resources
		TextureCreateDesc desc("UI_SDR");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM
			, Width
			, Height
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		r.Tex_UI_SDR = this->CreateTexture(desc);
		this->InitializeRTV(r.RTV_UI_SDR, 0u, r.Tex_UI_SDR);
		this->InitializeSRV(r.SRV_UI_SDR, 0u, r.Tex_UI_SDR);
	}
}

void VQRenderer::UnloadWindowSizeDependentResources(HWND hwnd)
{
	FRenderingResources_MainWindow& r = mResources_MainWnd;

	// sync GPU
	auto& ctx = this->GetWindowRenderContext(hwnd);
	ctx.SwapChain.WaitForGPU();

	this->DestroyTexture(r.Tex_SceneDepthMSAA);
	this->DestroyTexture(r.Tex_SceneColorMSAA);
	this->DestroyTexture(r.Tex_SceneNormalsMSAA);
	this->DestroyTexture(r.Tex_SceneVisualizationMSAA);
	this->DestroyTexture(r.Tex_SceneMotionVectorsMSAA);

	this->DestroyTexture(r.Tex_SceneDepth);
	this->DestroyTexture(r.Tex_SceneDepthResolve);
	this->DestroyTexture(r.Tex_SceneColor);
	this->DestroyTexture(r.Tex_SceneColorBoundingVolumes);
	this->DestroyTexture(r.Tex_SceneNormals);
	this->DestroyTexture(r.Tex_SceneVisualization);

	this->DestroyTexture(r.Tex_AmbientOcclusion);

	this->DestroyTexture(r.Tex_SceneMotionVectors);

	this->DestroyTexture(r.Tex_DownsampledSceneDepth);

	this->DestroyTexture(r.Tex_PostProcess_BlurOutput);
	this->DestroyTexture(r.Tex_PostProcess_BlurIntermediate);
	this->DestroyTexture(r.Tex_PostProcess_TonemapperOut);
	this->DestroyTexture(r.Tex_PostProcess_VisualizationOut);
	this->DestroyTexture(r.Tex_PostProcess_FFXCASOut);
	this->DestroyTexture(r.Tex_PostProcess_FSR_EASUOut);
	this->DestroyTexture(r.Tex_PostProcess_FSR_RCASOut);

	this->DestroyTexture(r.Tex_UI_SDR);
}
