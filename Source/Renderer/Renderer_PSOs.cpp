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

#include "Renderer.h"
#include "../../Shaders/LightingConstantBufferData.h"
#include "../../Libs/VQUtils/Source/utils.h"
using namespace Microsoft::WRL;
using namespace VQSystemInfo;

void VQRenderer::LoadBuiltinPSOs()
{
	// temp: singl thread pso load from vector
	// todo: enqueue load descs into the MT queue
	std::vector< std::pair<PSO_ID, FPSODesc >> PSOLoadDescs;

	// FULLSCREEN TRIANGLE PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"FullscreenTriangle.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_FullscreenTriangle";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.InputLayout = { };
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FullScreenTriangle);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;


		PSOLoadDescs.push_back({ EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO, psoLoadDesc });

		// HDR SWAPCHAIN PSO
		psoLoadDesc.PSOName = "PSO_HDRSwapchain";
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		PSOLoadDescs.push_back({ EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO, psoLoadDesc });
	}

	// DEBUG VISUALIZATION PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Visualization.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Visualization";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_5_1" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);

		PSOLoadDescs.push_back({ EBuiltinPSOs::VIZUALIZATION_CS_PSO, psoLoadDesc });
	}


	// UI PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"UI.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UI";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__HelloWorldCube);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
		psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::UI_PSO, psoLoadDesc });
	}

	// UIHDRComposition PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"UIHDRComposite.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UI_HDR";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__UI_HDR_Composite);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState.RenderTarget[0].BlendEnable = false;
		psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::UI_HDR_scRGB_PSO, psoLoadDesc });
	}

	// SKYDOME PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Skydome.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Skydome";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__HelloWorldCube);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::SKYDOME_PSO, psoLoadDesc });

		// MSAA PSO
		psoLoadDesc.PSOName = "PSO_Skydome_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::SKYDOME_PSO_MSAA_4, psoLoadDesc });
	}


	// OBJECT PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Object.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Object";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__Object);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::OBJECT_PSO, psoLoadDesc });

		// MSAA PSO
		psoLoadDesc.PSOName = "PSO_Object_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::OBJECT_PSO_MSAA_4, psoLoadDesc });
	}


	// TONEMAPPER CS PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Tonemapper.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_TonemapperCS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_5_1" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);

		PSOLoadDescs.push_back({ EBuiltinPSOs::TONEMAPPER_PSO, psoLoadDesc });
	}

	// DEPTH PREPASS PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"DepthPrePass.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_FDepthPrePassVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ZPrePass);
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({"INSTANCED_DRAW", std::to_string(RENDER_INSTANCED_SCENE_MESHES) });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({"INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SCENE_MESHES)});

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PREPASS_PSO, psoLoadDesc });


		// MSAA PSO
		psoLoadDesc.PSOName = "PSO_FDepthPrePassVSPS_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PREPASS_PSO_MSAA_4, psoLoadDesc });

	}

	// FORWARD LIGHTING PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"ForwardLighting.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ForwardLighting);
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", std::to_string(RENDER_INSTANCED_SCENE_MESHES) });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SCENE_MESHES) });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MV";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoDesc.NumRenderTargets = 2;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_MV_PSO, psoLoadDesc });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.pop_back();

		// MSAA PSOs
		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_PSO_MSAA_4, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MV_MSAA4";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoDesc.NumRenderTargets = 2;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_MV_PSO_MSAA_4, psoLoadDesc });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.pop_back();

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_Viz";
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({ "OUTPUT_ALBEDO", "1" });
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT; // rgba16f vs rgba8unorm 
		psoDesc.SampleDesc.Count = 1;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MSAA4_Viz";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_PSO_MSAA_4, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_Viz_MV";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoDesc.NumRenderTargets = 3;
		psoDesc.RTVFormats[2] = DXGI_FORMAT_R16G16_FLOAT;
		psoDesc.SampleDesc.Count = 1;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_AND_MV_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MSAA4_Viz_MV";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_AND_MV_PSO_MSAA_4, psoLoadDesc });
		// ^ lol this is not maintainable, gotta refactor all this into render passes and proper shader perms
	}

	// WIREFRAME/UNLIT PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Unlit.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UnlitVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__WireframeUnlit);

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
#if 0
		psoDesc.BlendState.RenderTarget[0].BlendEnable = false;
		psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND::D3D12_BLEND_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND::D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
		psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
#endif

		PSOLoadDescs.push_back({ EBuiltinPSOs::UNLIT_PSO, psoLoadDesc });
		{
			// MSAA
			psoLoadDesc.PSOName = "PSO_UnlitVSPS_MSAA4";
			psoDesc.SampleDesc.Count = 4;
			PSOLoadDescs.push_back({ EBuiltinPSOs::UNLIT_PSO_MSAA_4, psoLoadDesc });
		}

		// wireframe
		psoLoadDesc.PSOName = "PSO_WireframeVSPS";
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		psoDesc.SampleDesc.Count = 1;
		PSOLoadDescs.push_back({ EBuiltinPSOs::WIREFRAME_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_Wireframe_VSPS_Instanced";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", "1" });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__UNLIT_SHADER) });
		PSOLoadDescs.push_back({ EBuiltinPSOs::WIREFRAME_INSTANCED_PSO, psoLoadDesc });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		{
			// MSAA
			psoLoadDesc.PSOName = "PSO_WireframeVSPS_MSAA4";
			psoDesc.SampleDesc.Count = 4;
			PSOLoadDescs.push_back({ EBuiltinPSOs::WIREFRAME_PSO_MSAA_4, psoLoadDesc });

			psoLoadDesc.PSOName = "PSO_WireframeVSPS_Instanced_MSAA4";
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", "1" });
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__UNLIT_SHADER) });
			PSOLoadDescs.push_back({ EBuiltinPSOs::WIREFRAME_INSTANCED_MSAA4_PSO, psoLoadDesc });
		}
	}

	//OUTLINE PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Outline.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_OutlineVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__WireframeUnlit);

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

		PSOLoadDescs.push_back({ EBuiltinPSOs::OUTLINE_PSO, psoLoadDesc });
		{
			// MSAA
			psoLoadDesc.PSOName = "PSO_OutlineVSPS_MSAA4";
			psoDesc.SampleDesc.Count = 4;
			PSOLoadDescs.push_back({ EBuiltinPSOs::OUTLINE_PSO_MSAA_4, psoLoadDesc });
		}
	}

	// SHADOWMAP PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"ShadowDepthPass.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_DepthOnlyVS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", "1" });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SHADOW_MESHES) });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ShadowPassDepthOnlyVS);

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 0;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PASS_PSO, psoLoadDesc });
		{
			psoLoadDesc.PSOName = "PSO_LinearDepthVSPS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
			psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ShadowPassLinearDepthVSPS);
			PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PASS_LINEAR_PSO, psoLoadDesc });
			psoLoadDesc.ShaderStageCompileDescs.pop_back();
		}
		{
			psoLoadDesc.PSOName = "PSO_MaskedDepthVSPS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
			for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs)
			{
				shdDesc.Macros.push_back({ "ALPHA_MASK", "1" });
			}
			psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ShadowPassMaskedDepthVSPS);
			PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PASS_ALPHAMASKED_PSO, psoLoadDesc });
			psoLoadDesc.ShaderStageCompileDescs.pop_back();
		}
	}


	// CUBEMAP CONVOLUTION PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"CubemapConvolution.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSGSPS_Diffuse";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "GSMain", "gs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_DiffuseIrradiance", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap);

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;

		// unlit
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 6;
		for (uint rt = 0; rt < psoDesc.NumRenderTargets; ++rt) psoDesc.RTVFormats[rt] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::CUBEMAP_CONVOLUTION_DIFFUSE_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSPS_Diffuse";
		psoLoadDesc.ShaderStageCompileDescs.clear();
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain_PerFace", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_DiffuseIrradiance", "ps_5_1" });
		for (uint rt = 1; rt < psoDesc.NumRenderTargets; ++rt) psoDesc.RTVFormats[rt] = DXGI_FORMAT_UNKNOWN;
		psoDesc.NumRenderTargets = 1;
		// determine diffuse irradiance integration iteration count based on GPU dedicated memory size
		const std::vector<FGPUInfo> iGPU = VQSystemInfo::GetGPUInfo();
		assert(!iGPU.empty());
		const bool bLowEndGPU = iGPU[0].DedicatedGPUMemory < 2.1 * GIGABYTE;
		const bool bHighEndGPU = iGPU[0].DedicatedGPUMemory > 4.0 * GIGABYTE;
		for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs)
		{
			shdDesc.Macros.push_back({ "INTEGRATION_STEP_DIFFUSE_IRRADIANCE", (bLowEndGPU ? "0.050f" : (bHighEndGPU ? "0.010f" : "0.025f")) });
		}
		PSOLoadDescs.push_back({ EBuiltinPSOs::CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSPS_Specular";
		psoLoadDesc.ShaderStageCompileDescs.clear();
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain_PerFace", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_SpecularIrradiance", "ps_5_1" });
		PSOLoadDescs.push_back({ EBuiltinPSOs::CUBEMAP_CONVOLUTION_SPECULAR_PSO, psoLoadDesc });
	}

	// GAUSSIAN BLUR CS PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"GaussianBlur.hlsl");
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_GaussianBlurNaiveXCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain_X", "cs_5_1" });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);
			PSOLoadDescs.push_back({ EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO, psoLoadDesc });
		}
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_GaussianBlurNaiveYCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain_Y", "cs_5_1" });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);
			PSOLoadDescs.push_back({ EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO, psoLoadDesc });
		}
	}

	// BRDF INTEGRATION CS PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"CubemapConvolution.hlsl");
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_BRDFIntegrationCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain_BRDFIntegration", "cs_5_1" });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__BRDFIntegrationCS);
			PSOLoadDescs.push_back({ EBuiltinPSOs::BRDF_INTEGRATION_CS_PSO, psoLoadDesc });
		}
	}

	// AMD FidelityFX PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"AMDFidelityFX.hlsl");
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FFXCASCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CAS_CSMain", "cs_6_1", {{"FFXCAS_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1); // share root signature with tonemapper pass
			PSOLoadDescs.push_back({ EBuiltinPSOs::FFX_CAS_CS_PSO, psoLoadDesc });
		}
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FFXSPDCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "SPD_CSMain", "cs_6_0", {{"FFXSPD_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_SPD_CS);
			//PSOLoadDescs.push_back({ EBuiltinPSOs::FFX_SPD_CS_PSO, psoLoadDesc }); 
		}
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FSR_EASU_CS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "FSR_EASU_CSMain", "cs_6_2", {{"FSR_EASU_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_FSR1);
			PSOLoadDescs.push_back({ EBuiltinPSOs::FFX_FSR1_EASU_CS_PSO, psoLoadDesc });
		}
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FSR_RCAS_CS"; 
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "FSR_RCAS_CSMain", "cs_6_2", {{"FSR_RCAS_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_FSR1);
			PSOLoadDescs.push_back({ EBuiltinPSOs::FFX_FSR1_RCAS_CS_PSO, psoLoadDesc });
		}
	}

	// DepthDownsampleCS PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"DownsampleDepth.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_DownsampleDepthCS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_0" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__DownsampleDepthCS);
		PSOLoadDescs.push_back({ EBuiltinPSOs::DOWNSAMPLE_DEPTH_CS_PSO, psoLoadDesc });
	}

	// VertexDebug PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"VertexDebug.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_VertexDebugLocalSpaceVectors";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "GSMain", "gs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ZPrePass);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		psoDesc.NumRenderTargets = 1;
		for (uint rt = 0; rt < psoDesc.NumRenderTargets; ++rt) psoDesc.RTVFormats[rt] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::DEBUGVERTEX_LOCALSPACEVECTORS_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_VertexDebugLocalSpaceVectors_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::DEBUGVERTEX_LOCALSPACEVECTORS_PSO_MSAA_4, psoLoadDesc });
	}

	// Terrain PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Terrain.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Terrain";
#if 1
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain_Heightmap", "vs_5_1" }); // TODO: fix 6_0 error
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_Heightmap", "ps_5_1" }); // TODO: fix 6_0 error
#else
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "HSMain", "hs_6_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "DSMain", "ds_6_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" });
#endif
		
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__Terrain);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		for (uint rt = 0; rt < psoDesc.NumRenderTargets; ++rt) psoDesc.RTVFormats[rt] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::TERRAIN, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_Terrain_Wireframe";
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE::D3D12_FILL_MODE_WIREFRAME;
		PSOLoadDescs.push_back({ EBuiltinPSOs::TERRAIN_WIREFRAME, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_Terrain_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE::D3D12_FILL_MODE_SOLID;
		PSOLoadDescs.push_back({ EBuiltinPSOs::TERRAIN_MSAA4, psoLoadDesc });
		
		psoLoadDesc.PSOName = "PSO_Terrain_MSAA4_Wireframe";
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE::D3D12_FILL_MODE_WIREFRAME;
		PSOLoadDescs.push_back({ EBuiltinPSOs::TERRAIN_MSAA4_WIREFRAME, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_Terrain_Tessellated";
		psoLoadDesc.ShaderStageCompileDescs[0].EntryPoint = "VSMain";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "HSMain", "hs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "DSMain", "ds_5_1" });
		psoDesc.SampleDesc.Count = 1;
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE::D3D12_FILL_MODE_SOLID;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
		PSOLoadDescs.push_back({ EBuiltinPSOs::TERRAIN_TESSELLATED, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_Terrain_Tessellated_Wireframe";
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE::D3D12_FILL_MODE_WIREFRAME;
		PSOLoadDescs.push_back({ EBuiltinPSOs::TERRAIN_TESSELLATED, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_Terrain_Tessellated_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE::D3D12_FILL_MODE_SOLID;
		PSOLoadDescs.push_back({ EBuiltinPSOs::TERRAIN_TESSELLATED_MSAA4, psoLoadDesc });
		
		psoLoadDesc.PSOName = "PSO_Terrain_Tessellated_MSAA4_Wireframe";
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE::D3D12_FILL_MODE_WIREFRAME;
		PSOLoadDescs.push_back({ EBuiltinPSOs::TERRAIN_TESSELLATED_MSAA4_WIREFRAME, psoLoadDesc });
	}

	// ---------------------------------------------------------------------------------------------------------------1

	// TODO: threaded PSO loading
	// single threaded PSO loading for now (shader compilation is still MTd)
	for (const std::pair<PSO_ID, FPSODesc>& psoLoadDescIDPair : PSOLoadDescs)
	{
		mPSOs[psoLoadDescIDPair.first] = this->LoadPSO(psoLoadDescIDPair.second);
	}

	int a = 5;
}
