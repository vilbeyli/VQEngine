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
#include "Tessellation.h"
#include "../Engine/GPUMarker.h"
#include "../../Shaders/LightingConstantBufferData.h"
#include "../../Libs/VQUtils/Source/utils.h"

using namespace Microsoft::WRL;
using namespace VQSystemInfo;

void VQRenderer::LoadBuiltinPSOs()
{
	SCOPED_CPU_MARKER("LoadBuiltinPSOs");
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

	

	mLightingPSOs.Compile(*this, mRootSignatureLookup);

	// ---------------------------------------------------------------------------------------------------------------1

	// TODO: threaded PSO loading
	// single threaded PSO loading for now (shader compilation is still MTd)
	for (const std::pair<PSO_ID, FPSODesc>& psoLoadDescIDPair : PSOLoadDescs)
	{
		mPSOs[psoLoadDescIDPair.first] = this->LoadPSO(psoLoadDescIDPair.second);
	}

	int a = 5;
}


void VQRenderer::FLightingPSOs::Compile(VQRenderer& Renderer, const std::unordered_map<RS_ID, ID3D12RootSignature*>& mRootSignatureLookup)
{
	const std::wstring ShaderFilePath = GetFullPathOfShader(L"ForwardLighting.hlsl");

	FPSODesc psoLoadDesc = {};
	psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ForwardLighting);
	
	D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;


	const UINT SampleDescCounts[NUM_MSAA_OPTS] = { 1, 4 };
	const D3D12_CULL_MODE CullModes[NUM_FACECULL_OPTS] = { D3D12_CULL_MODE_NONE , D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_BACK };
	const D3D12_FILL_MODE FillModes[NUM_RASTER_OPTS] = { D3D12_FILL_MODE_SOLID, D3D12_FILL_MODE_WIREFRAME };
	const char* szPartitionNames[FLightingPSOs::NUM_PARTIT_OPTIONS] = { "integer", "fractional_even", "fractional_odd", "pow2" };
	const char* szOutTopologyNames[FLightingPSOs::NUM_OUTTOP_OPTIONS] = { "point", "line", "triangle_cw", "triangle_ccw" };

	for(size_t iMSAA = 0    ; iMSAA     < NUM_MSAA_OPTS     ; ++iMSAA) 
	for(size_t iRaster = 0  ; iRaster   < NUM_RASTER_OPTS   ; ++iRaster) 
	for(size_t iFaceCull = 0; iFaceCull < NUM_FACECULL_OPTS ; ++iFaceCull)
	for(size_t iOutMoVec = 0; iOutMoVec < NUM_MOVEC_OPTS    ; ++iOutMoVec) 
	for(size_t iOutRough = 0; iOutRough < NUM_ROUGH_OPTS    ; ++iOutRough) 
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED  ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS; ++iOutTopo) 
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS ; ++iAlpha)
	{
		if (iTess == 0 && (iDomain > 0 || iPart > 0 || iOutTopo > 0))
			continue; // skip tess permutations when tess is off

		if (iTess == 1 && iOutTopo == ETessellationOutputTopology::TESSELLATION_OUTPUT_LINE && iDomain != ETessellationDomain::ISOLINE_PATCH)
			continue; // line output topologies are only available to isoline domains
		
		const size_t key = Hash(iMSAA, iRaster, iFaceCull, iOutMoVec, iOutRough, iTess, iDomain, iPart, iOutTopo, iAlpha);
		
		std::string PSOName = "PSO_FwdLighting";
		if (iTess == 1) PSOName += "_Tessellated";
		if (iRaster == 1) PSOName += "_Wireframe";
		if (iMSAA == 1) PSOName += "_MSAA4";
		if (iTess == 1)
		{
			if (iDomain == 1) PSOName += "_Quad";
			PSOName += std::string("_") + szPartitionNames[iPart];
			PSOName += std::string("_") + szOutTopologyNames[iOutTopo];
		}
		psoLoadDesc.PSOName = PSOName;

		// MSAA
		psoDesc.SampleDesc.Count = SampleDescCounts[iMSAA];

		// RS
		psoDesc.RasterizerState.FillMode = FillModes[iRaster];
		psoDesc.RasterizerState.CullMode = CullModes[iFaceCull];
		
		// RTs
		psoDesc.NumRenderTargets = static_cast<UINT>(1 + iOutMoVec + iOutRough);
		psoDesc.RTVFormats[1] = psoDesc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
		{
			int iRT = 1;
			if(iOutRough) psoDesc.RTVFormats[iRT++] = DXGI_FORMAT_R16G16B16A16_FLOAT;
			if(iOutMoVec) psoDesc.RTVFormats[iRT++] = DXGI_FORMAT_R16G16_FLOAT;
		}

		// topology
		psoDesc.PrimitiveTopologyType = iTess == 1 ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		// shaders
		psoLoadDesc.ShaderStageCompileDescs.resize(iTess == 1 ? 4 : 2); // VS-HS-DS-PS : VS-PS
		if (iTess == 1)
		{
			psoLoadDesc.ShaderStageCompileDescs[0] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain_Tess", "vs_5_1" };
			psoLoadDesc.ShaderStageCompileDescs[1] = FShaderStageCompileDesc{ ShaderFilePath, "HSMain"     , "hs_5_1" };
			psoLoadDesc.ShaderStageCompileDescs[2] = FShaderStageCompileDesc{ ShaderFilePath, "DSMain"     , "ds_5_1" };
			psoLoadDesc.ShaderStageCompileDescs[3] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain"     , "ps_5_1" };
		}
		else
		{
			psoLoadDesc.ShaderStageCompileDescs[0] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" };
			psoLoadDesc.ShaderStageCompileDescs[1] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" };
		}

		// macros
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", std::to_string(RENDER_INSTANCED_SCENE_MESHES) });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SCENE_MESHES) });
		if (iTess == 1)
		{
			const char* szPartitioningMacros[FLightingPSOs::NUM_PARTIT_OPTIONS] = { "PARTITIONING__INT", "PARTITIONING__EVEN", "PARTITIONING__ODD", "PARTITIONING__POW2" };
			const char* szOutTopologyMacros [FLightingPSOs::NUM_OUTTOP_OPTIONS] = { "OUTTOPO__POINT", "OUTTOPO__LINE", "OUTTOPO__TRI_CW","OUTTOPO__TRI_CCW" };
			const char* szDomainMacros = iDomain == 1 ? "DOMAIN__QUAD" : "DOMAIN__TRIANGLE";	
			psoLoadDesc.ShaderStageCompileDescs[1/*HS*/].Macros = {
				  { szDomainMacros, "1" }
				, { szPartitioningMacros[iPart], "1" }
				, { szOutTopologyMacros[iOutTopo], "1" }
			};
			psoLoadDesc.ShaderStageCompileDescs[2/*DS*/].Macros = { {szDomainMacros, "1"} };
		}
		if (iOutMoVec)
		{
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
			psoLoadDesc.ShaderStageCompileDescs[iTess == 1 ? 3 : 1].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
			if (iTess == 1) psoLoadDesc.ShaderStageCompileDescs[2].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		}
		if (iOutRough)
		{
			psoLoadDesc.ShaderStageCompileDescs[iTess == 1 ? 3 : 1].Macros.push_back({ "OUTPUT_ALBEDO", "1" });
		}

		mapLoadDesc[key] = psoLoadDesc;
		mapPSO[key] = Renderer.CreatePSO_OnThisThread(psoLoadDesc);
	}


}

size_t VQRenderer::FLightingPSOs::Hash(size_t iMSAA, size_t iRaster, size_t iFaceCull, size_t iOutMoVec, size_t iOutRough, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iAlpha)
{
	return iMSAA
		+ NUM_MSAA_OPTS * iRaster
		+ NUM_MSAA_OPTS * NUM_RASTER_OPTS * iFaceCull
		+ NUM_MSAA_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * iOutMoVec
		+ NUM_MSAA_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * iOutRough
		+ NUM_MSAA_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * iTess
		+ NUM_MSAA_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * NUM_TESS_ENABLED * iDomain
		+ NUM_MSAA_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * iPart
		+ NUM_MSAA_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * iOutTopo
		+ NUM_MSAA_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * iAlpha;
}

PSO_ID VQRenderer::FLightingPSOs::Get(size_t hash) const
{
	auto it = mapPSO.find(hash);
	if (it == mapPSO.end())
	{
		return INVALID_ID;
	}
	return it->second;
}
