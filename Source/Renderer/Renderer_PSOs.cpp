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
#include "../../Libs/DirectXCompiler/inc/dxcapi.h"

#include <unordered_set>
#include <set>
#include <map>

using namespace Microsoft::WRL;
using namespace VQSystemInfo;
using namespace Tessellation;

void VQRenderer::ReservePSOMap(size_t NumPSOs)
{
	SCOPED_CPU_MARKER("ReservePSOMap");
	for (int i = 0; i < EBuiltinPSOs::NUM_BUILTIN_PSOs; ++i)
		mPSOs[i] = nullptr;
	for (size_t i = 0; i < NumPSOs; ++i)
		mPSOs[EBuiltinPSOs::NUM_BUILTIN_PSOs + (int)i] = nullptr;
}

static std::vector<const FShaderStageCompileDesc*> GatherUniqueShaderCompileDescs(const std::vector<FPSODesc>& PSODescs, std::map<PSO_ID, std::vector<size_t>>& PSOShaderMap)
{
	SCOPED_CPU_MARKER("GatherUniqueShaderCompileDescs");
	std::vector<const FShaderStageCompileDesc*> UniqueCompileDescs;
	std::unordered_map<size_t, size_t> UniqueCompilePathHashToIndex;
	std::hash<std::string> hasher;

	for (int i = 0; i < PSODescs.size(); ++i)
	{
		const FPSODesc& psoDesc = PSODescs[i];
		for (const FShaderStageCompileDesc& shaderDesc : psoDesc.ShaderStageCompileDescs)
		{
			const std::string CachedShaderBinaryPath = VQRenderer::GetCachedShaderBinaryPath(shaderDesc);
			const size_t hash = hasher(CachedShaderBinaryPath);

			auto it = UniqueCompilePathHashToIndex.find(hash);
			if (it != UniqueCompilePathHashToIndex.end())
			{
				const size_t& iShader = it->second;
				PSOShaderMap[i].push_back(iShader);
				continue;
			}

			UniqueCompileDescs.push_back(&shaderDesc);
			const size_t iShader = UniqueCompileDescs.size() - 1;
			UniqueCompilePathHashToIndex[hash] = iShader;
			PSOShaderMap[i].push_back(iShader);
		}
	}
	return UniqueCompileDescs;
}

void VQRenderer::StartPSOCompilation_MT(std::vector<FPSOCreationTaskParameters>&& PSOCompileParams_RenderPass)
{
	SCOPED_CPU_MARKER("StartPSOCompilation_MT");
	std::vector<FPSODesc> PSODescs_BuiltinLegacy = LoadBuiltinPSODescs_Legacy();
	assert(EBuiltinPSOs::NUM_BUILTIN_PSOs == PSODescs_BuiltinLegacy.size());

	std::vector<FPSODesc> PSODescs_Builtin = LoadBuiltinPSODescs();
	
	const size_t NumBuiltinPSOs = PSODescs_BuiltinLegacy.size() + PSODescs_Builtin.size();

	std::vector<FPSODesc> PSODescs(NumBuiltinPSOs + PSOCompileParams_RenderPass.size());
	{
		SCOPED_CPU_MARKER("GatherDescs");
		size_t i = 0;
		for (auto&& desc : PSODescs_BuiltinLegacy) { PSODescs[i++] = std::move(desc); }
		for (auto&& desc : PSODescs_Builtin) { PSODescs[i++] = std::move(desc); }
		for (auto&& params : PSOCompileParams_RenderPass) { PSODescs[i++] = std::move(params.Desc); }
	}
	ReservePSOMap(PSODescs.size() - EBuiltinPSOs::NUM_BUILTIN_PSOs);
	
	{
		SCOPED_CPU_MARKER("AssignRenderPassPSOIDs");
		int i = 0;
		for (auto& params : PSOCompileParams_RenderPass)
		{
			*params.pID = (PSO_ID)(NumBuiltinPSOs + i++);
		}
	}

	// shader compile context
	std::map<PSO_ID, std::vector<size_t>> PSOShaderMap; // pso -> shader_index[]
	std::vector<const FShaderStageCompileDesc*> UniqueShaderCompileDescs = GatherUniqueShaderCompileDescs(PSODescs, PSOShaderMap);
	mShaderCompileResults.clear();
	mShaderCompileResults.resize(UniqueShaderCompileDescs.size());

	// kickoff shader workers
	{
		SCOPED_CPU_MARKER("DispatchShaderWorkers");
		for (size_t i = 0; i < UniqueShaderCompileDescs.size(); ++i)
		{
			const FShaderStageCompileDesc& ShaderStageCompileDesc = *UniqueShaderCompileDescs[i];
			if (ShaderStageCompileDesc.FilePath.empty())
			{
				continue;
			}

			mShaderCompileResults[i] = mWorkers_ShaderLoad.AddTask([=]()
			{
				return this->LoadShader(ShaderStageCompileDesc);
			});
		}
	}

	// kickoff PSO workers
	{
		SCOPED_CPU_MARKER("DispatchPSOWorkers");
		mPSOCompileResults.clear();
		for (auto it = PSOShaderMap.begin(); it != PSOShaderMap.end(); ++it)
		{
			const PSO_ID psoID = it->first;
			const std::vector<size_t>& iPSOShaders = it->second;

			assert(psoID < PSODescs.size() && psoID >= 0);
			const FPSODesc& PSODesc = PSODescs[psoID];

			std::shared_future<FPSOCompileResult> PSOCompileResult = mWorkers_PSOLoad.AddTask([=]()
			{
				SCOPED_CPU_MARKER("PSOCompileWorker");
				ID3D12Device* pDevice = mDevice.GetDevicePtr();
				FPSOCompileResult result;
				result.pPSO = nullptr;
				result.id = psoID;
				ID3D12PipelineState*& pPSO = result.pPSO;

				// check if PSO is cached
				const bool bCachedPSOExists = false;
				const bool bCacheDirty = false;
				const bool bComputePSO = std::find_if(RANGE(PSODesc.ShaderStageCompileDescs) // check if ShaderModel has cs_*_*
					, [](const FShaderStageCompileDesc& desc) { return ShaderUtils::GetShaderStageEnumFromShaderModel(desc.ShaderModel) == EShaderStage::CS; }
				) != PSODesc.ShaderStageCompileDescs.end();

				// compile PSO if no cache or cache dirty, otherwise load cached binary
				if (!bCachedPSOExists || bCacheDirty)
				{
					{
						SCOPED_CPU_MARKER("WaitShaderCompileWorkers");
						for (size_t i : iPSOShaders)
						{
							assert(mShaderCompileResults[i].valid());
							{
								SCOPED_CPU_MARKER_C("WAIT_future", 0xFF0000AA);
								mShaderCompileResults[i].wait();
							}
						}
					}
					{
						SCOPED_CPU_MARKER("ErrorCheck");
						bool bShaderErrors = false;
						for (size_t i : iPSOShaders)
						{
							const std::shared_future<FShaderStageCompileResult>& TaskResult = mShaderCompileResults[i];
							const FShaderStageCompileResult& ShaderCompileResult = TaskResult.get();
							if (ShaderCompileResult.ShaderBlob.IsNull())
							{
								bShaderErrors = true;
								break;
							}
						}
						if (bShaderErrors)
						{
							Log::Error("PSO Compile failed: %s", PSODesc.PSOName.c_str());
							return result;
						}
					}

					std::vector< std::shared_future<FShaderStageCompileResult> > ShaderCompileResultsForThisPSO(iPSOShaders.size());
					for (size_t i = 0; i < ShaderCompileResultsForThisPSO.size(); ++i)
					{
						ShaderCompileResultsForThisPSO[i] = mShaderCompileResults[iPSOShaders[i]];
					}

					pPSO = bComputePSO
						? CompileComputePSO(PSODesc, ShaderCompileResultsForThisPSO)
						: CompileGraphicsPSO(PSODesc, ShaderCompileResultsForThisPSO);
				}
				else // load cached PSO
				{
					assert(false); // TODO
				}

				return result;
			});

			mPSOCompileResults.push_back(PSOCompileResult);
		}
	}
}

void VQRenderer::WaitPSOCompilation()
{
	SCOPED_CPU_MARKER_C("WaitPSOWorkers", 0xFF770000);
	for (std::shared_future<FPSOCompileResult>& pPSOResult : mPSOCompileResults)
	{
		assert(pPSOResult.valid());
		pPSOResult.wait();
	}
}

void VQRenderer::AssignPSOs()
{
	SCOPED_CPU_MARKER("AssignPSOs");
	for (size_t i = 0; i < mPSOCompileResults.size(); ++i)
	{
		const FPSOCompileResult& r = mPSOCompileResults[i].get();
		mPSOs[r.id] = r.pPSO;
	}

	{
		SCOPED_CPU_MARKER("CleanUp");
		mPSOCompileResults.clear();
		mShaderCompileResults.clear();
		mPSOCompileResults.shrink_to_fit();
		mShaderCompileResults.shrink_to_fit();
	}
}

std::vector<FPSODesc> VQRenderer::LoadBuiltinPSODescs_Legacy()
{
	SCOPED_CPU_MARKER("LoadPSODescs_BuiltinLegacy");
	std::vector<FPSODesc> descs;
	descs.resize(EBuiltinPSOs::NUM_BUILTIN_PSOs);

	
	// FULLSCREEN TRIANGLE PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"FullscreenTriangle.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_FullscreenTriangle";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" });

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

		descs[EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO] = psoLoadDesc;

		// HDR SWAPCHAIN PSO
		psoLoadDesc.PSOName = "PSO_HDRSwapchain";
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		descs[EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO] = psoLoadDesc;
	}

	// DEBUG VISUALIZATION PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Visualization.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Visualization";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_1" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);

		descs[EBuiltinPSOs::VIZUALIZATION_CS_PSO] = psoLoadDesc;
	}


	// UI PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"UI.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UI";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" });

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

		descs[EBuiltinPSOs::UI_PSO] = psoLoadDesc;
	}

	// UIHDRComposition PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"UIHDRComposite.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UI_HDR";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" });

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

		descs[EBuiltinPSOs::UI_HDR_scRGB_PSO] = psoLoadDesc;
	}

	// SKYDOME PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Skydome.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Skydome";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" });

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

		descs[EBuiltinPSOs::SKYDOME_PSO] = psoLoadDesc;

		// MSAA PSO
		psoLoadDesc.PSOName = "PSO_Skydome_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		descs[EBuiltinPSOs::SKYDOME_PSO_MSAA_4] = psoLoadDesc;
	}

	// TONEMAPPER CS PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Tonemapper.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_TonemapperCS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_1" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);

		descs[EBuiltinPSOs::TONEMAPPER_PSO] = psoLoadDesc;
	}


	// WIREFRAME/UNLIT PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Unlit.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UnlitVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_1" });
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
		psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
#endif

		descs[EBuiltinPSOs::UNLIT_PSO] = psoLoadDesc;
		{
			// MSAA
			psoLoadDesc.PSOName = "PSO_UnlitVSPS_MSAA4";
			psoDesc.SampleDesc.Count = 4;
			descs[EBuiltinPSOs::UNLIT_PSO_MSAA_4] = psoLoadDesc;
		}

		// wireframe
		psoLoadDesc.PSOName = "PSO_WireframeVSPS";
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		psoDesc.SampleDesc.Count = 1;
		descs[EBuiltinPSOs::WIREFRAME_PSO] = psoLoadDesc;

		psoLoadDesc.PSOName = "PSO_Wireframe_VSPS_Instanced";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", "1" });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__UNLIT_SHADER) });
		descs[EBuiltinPSOs::WIREFRAME_INSTANCED_PSO] = psoLoadDesc;
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		{
			// MSAA
			psoLoadDesc.PSOName = "PSO_WireframeVSPS_MSAA4";
			psoDesc.SampleDesc.Count = 4;
			descs[EBuiltinPSOs::WIREFRAME_PSO_MSAA_4] = psoLoadDesc;

			psoLoadDesc.PSOName = "PSO_WireframeVSPS_Instanced_MSAA4";
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", "1" });
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__UNLIT_SHADER) });
			descs[EBuiltinPSOs::WIREFRAME_INSTANCED_MSAA4_PSO] = psoLoadDesc;
		}
	}

	// CUBEMAP CONVOLUTION PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"CubemapConvolution.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSGSPS_Diffuse";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "GSMain", "gs_6_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_DiffuseIrradiance", "ps_6_1" });
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

		descs[EBuiltinPSOs::CUBEMAP_CONVOLUTION_DIFFUSE_PSO] = psoLoadDesc;

		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSPS_Diffuse";
		psoLoadDesc.ShaderStageCompileDescs.clear();
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain_PerFace", "vs_6_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_DiffuseIrradiance", "ps_6_1" });
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
		descs[EBuiltinPSOs::CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO] = psoLoadDesc;

		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSPS_Specular";
		psoLoadDesc.ShaderStageCompileDescs.clear();
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain_PerFace", "vs_6_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_SpecularIrradiance", "ps_6_1" });
		descs[EBuiltinPSOs::CUBEMAP_CONVOLUTION_SPECULAR_PSO] = psoLoadDesc;
	}

	// GAUSSIAN BLUR CS PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"GaussianBlur.hlsl");
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_GaussianBlurNaiveXCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain_X", "cs_6_1" });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);
			descs[EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO] = psoLoadDesc;
		}
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_GaussianBlurNaiveYCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain_Y", "cs_6_1" });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);
			descs[EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO] = psoLoadDesc;
		}
	}

	// BRDF INTEGRATION CS PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"CubemapConvolution.hlsl");
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_BRDFIntegrationCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain_BRDFIntegration", "cs_6_1" });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__BRDFIntegrationCS);
			descs[EBuiltinPSOs::BRDF_INTEGRATION_CS_PSO] = psoLoadDesc;
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
			descs[EBuiltinPSOs::FFX_CAS_CS_PSO] = psoLoadDesc;
		}
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FFXSPDCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "SPD_CSMain", "cs_6_0", {{"FFXSPD_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_SPD_CS);
			//descs[EBuiltinPSOs::FFX_SPD_CS_PSO] = psoLoadDesc; 
		}
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FSR_EASU_CS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "FSR_EASU_CSMain", "cs_6_2", {{"FSR_EASU_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_FSR1);
			descs[EBuiltinPSOs::FFX_FSR1_EASU_CS_PSO] = psoLoadDesc;
		}
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FSR_RCAS_CS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "FSR_RCAS_CSMain", "cs_6_2", {{"FSR_RCAS_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_FSR1);
			descs[EBuiltinPSOs::FFX_FSR1_RCAS_CS_PSO] = psoLoadDesc;
		}
	}

	// DepthDownsampleCS PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"DownsampleDepth.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_DownsampleDepthCS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_0" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__DownsampleDepthCS);
		descs[EBuiltinPSOs::DOWNSAMPLE_DEPTH_CS_PSO] = psoLoadDesc;
	}

	// VertexDebug PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"VertexDebug.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_VertexDebugLocalSpaceVectors";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "GSMain", "gs_6_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_1" });
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

		descs[EBuiltinPSOs::DEBUGVERTEX_LOCALSPACEVECTORS_PSO] = psoLoadDesc;

		psoLoadDesc.PSOName = "PSO_VertexDebugLocalSpaceVectors_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		descs[EBuiltinPSOs::DEBUGVERTEX_LOCALSPACEVECTORS_PSO_MSAA_4] = psoLoadDesc;
	}

	return descs;
}


void PSOCollection::Compile(VQRenderer& Renderer)
{
	SCOPED_CPU_MARKER("PSOCollection::Compile");
	for (auto it = mapLoadDesc.begin(); it != mapLoadDesc.end(); ++it)
	{
		const size_t hash = it->first;
		FPSODesc& desc = it->second;
		mapPSO[hash] = Renderer.CreatePSO_OnThisThread(desc);
	}
}
PSO_ID PSOCollection::Get(size_t hash) const
{
	auto it = mapPSO.find(hash);
	if (it == mapPSO.end())
	{
		return INVALID_ID;
	}
	return it->second;
}

void FLightingPSOs::GatherPSOLoadDescs(const std::unordered_map<RS_ID, ID3D12RootSignature*>& mRootSignatureLookup)
{
	SCOPED_CPU_MARKER("GatherPSOLoadDescs_ForwardLighting");
	const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader(L"ForwardLighting.hlsl");

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

	const D3D12_CULL_MODE CullModes[NUM_FACECULL_OPTS] = { D3D12_CULL_MODE_NONE , D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_BACK };
	const D3D12_FILL_MODE FillModes[NUM_RASTER_OPTS] = { D3D12_FILL_MODE_SOLID, D3D12_FILL_MODE_WIREFRAME };

	for(size_t iMSAA = 0    ; iMSAA     < NUM_MSAA_OPTIONS  ; ++iMSAA) 
	for(size_t iRaster = 0  ; iRaster   < NUM_RASTER_OPTS   ; ++iRaster) 
	for(size_t iFaceCull = 0; iFaceCull < NUM_FACECULL_OPTS ; ++iFaceCull)
	for(size_t iOutMoVec = 0; iOutMoVec < NUM_MOVEC_OPTS    ; ++iOutMoVec) 
	for(size_t iOutRough = 0; iOutRough < NUM_ROUGH_OPTS    ; ++iOutRough) 
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED  ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS; ++iOutTopo) 
	for(size_t iTessCull = 0; iTessCull < NUM_TESS_CULL_OPTIONS; ++iTessCull)
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS ; ++iAlpha)
	{
		if (ShouldSkipTessellationVariant(iTess, iDomain, iPart, iOutTopo, iTessCull))
			continue;
		
		const size_t key = Hash(iMSAA, iRaster, iFaceCull, iOutMoVec, iOutRough, iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
		
		std::string PSOName = "PSO_FwdLighting";
		if (iAlpha == 1) PSOName += "_AlphaMasked";
		if (iRaster == 1) PSOName += "_Wireframe";
		if (iMSAA == 1) PSOName += "_MSAA4";
		if (iTess == 1)
		{
			AppendTessellationPSONameTokens(PSOName, iDomain, iPart, iOutTopo, iTessCull);
		}

		psoLoadDesc.PSOName = PSOName;

		// MSAA
		psoDesc.SampleDesc.Count = MSAA_SAMPLE_COUNTS[iMSAA];

		// DS
		psoDesc.DepthStencilState.DepthFunc = iRaster == 1 ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = iRaster == 1 ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;

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
		size_t NumShaders = 2; // VS-PS
		if (iTess == 1)
		{	                 // VS-HS-DS-PS | VS-HS-DS-GS-PS
			NumShaders = iTessCull == 0 ? 4 : 5; 
		}
		psoLoadDesc.ShaderStageCompileDescs.resize(NumShaders);
		size_t iShader = 0;
		if (iTess == 1)
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain_Tess", "vs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "HSMain"     , "hs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "DSMain"     , "ds_6_0" };
			if(iTessCull > 0)
				psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "GSMain" , "gs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain"     , "ps_6_0" };
		}
		else
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" };
		}
		const size_t iPixelShader = iShader - 1;

		// macros
		const FShaderMacro InstancedDrawMacro = { "INSTANCED_DRAW", std::to_string(RENDER_INSTANCED_SCENE_MESHES) };
		const FShaderMacro InstanceCountMacro = { "INSTANCE_COUNT",std::to_string(MAX_INSTANCE_COUNT__SCENE_MESHES) };
		if (iTess == 1)
		{
			AppendTessellationVSMacros(psoLoadDesc.ShaderStageCompileDescs[0/*VS*/].Macros, iDomain);
			AppendTessellationHSMacros(psoLoadDesc.ShaderStageCompileDescs[1/*HS*/].Macros, iDomain, iPart, iOutTopo, iTessCull);
			AppendTessellationDSMacros(psoLoadDesc.ShaderStageCompileDescs[2/*DS*/].Macros, iDomain, iOutTopo, iTessCull);
			if (iTessCull > 0)
			{
				AppendTessellationGSMacros(psoLoadDesc.ShaderStageCompileDescs[3/*GS*/].Macros, iOutTopo, iTessCull);
			}
		}
		if (iOutMoVec)
		{
			const FShaderMacro MVMacro = { "OUTPUT_MOTION_VECTORS", "1" };
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back(MVMacro);
			psoLoadDesc.ShaderStageCompileDescs[iPixelShader].Macros.push_back(MVMacro);
			if (iTess == 1)
			{
				psoLoadDesc.ShaderStageCompileDescs[2/*DS*/].Macros.push_back(MVMacro);
				if (iTessCull > 0)
				{
					psoLoadDesc.ShaderStageCompileDescs[3/*GS*/].Macros.push_back(MVMacro);
				}
			}
		}
		if (iOutRough)
		{
			psoLoadDesc.ShaderStageCompileDescs[iPixelShader].Macros.push_back({ "OUTPUT_ALBEDO", "1" });
		}
		for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs) // all stages
		{
			shdDesc.Macros.push_back(InstancedDrawMacro);
			shdDesc.Macros.push_back(InstanceCountMacro);
			if (iAlpha == 1)
			{
				shdDesc.Macros.push_back({ "ENABLE_ALPHA_MASK", "1" });
			}
		}

		mapLoadDesc[key] = psoLoadDesc;
		mapPSO[key] = INVALID_ID; 
	}
}

void FDepthPrePassPSOs::GatherPSOLoadDescs(const std::unordered_map<RS_ID, ID3D12RootSignature*>& mRootSignatureLookup)
{
	SCOPED_CPU_MARKER("GatherPSOLoadDescs_ZPrePass");
	const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader(L"DepthPrePass.hlsl");

	FPSODesc psoLoadDesc = {};
	psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ZPrePass);
	
	D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleMask = UINT_MAX;

	const D3D12_CULL_MODE CullModes[NUM_FACECULL_OPTS] = { D3D12_CULL_MODE_NONE , D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_BACK };
	const D3D12_FILL_MODE FillModes[NUM_RASTER_OPTS] = { D3D12_FILL_MODE_SOLID, D3D12_FILL_MODE_WIREFRAME };

	for(size_t iMSAA = 0    ; iMSAA     < NUM_MSAA_OPTIONS     ; ++iMSAA) 
	for(size_t iRaster = 0  ; iRaster   < NUM_RASTER_OPTS   ; ++iRaster) 
	for(size_t iFaceCull = 0; iFaceCull < NUM_FACECULL_OPTS ; ++iFaceCull)
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED  ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS; ++iOutTopo) 
	for(size_t iTessCull = 0; iTessCull < NUM_TESS_CULL_OPTIONS; ++iTessCull)
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS ; ++iAlpha)
	{
		if (ShouldSkipTessellationVariant(iTess, iDomain, iPart, iOutTopo, iTessCull))
			continue;

		const size_t key = Hash(iMSAA, iRaster, iFaceCull, iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
		
		std::string PSOName = "PSO_ZPrePass";
		if (iAlpha == 1) PSOName += "_AlphaMasked";
		if (iRaster == 1) PSOName += "_Wireframe";
		if (iMSAA == 1) PSOName += "_MSAA4";
		if (iTess == 1)
		{
			AppendTessellationPSONameTokens(PSOName, iDomain, iPart, iOutTopo, iTessCull);
		}
		psoLoadDesc.PSOName = PSOName;

		// MSAA
		psoDesc.SampleDesc.Count = MSAA_SAMPLE_COUNTS[iMSAA];

		// RS
		psoDesc.RasterizerState.FillMode = FillModes[iRaster];
		psoDesc.RasterizerState.CullMode = CullModes[iFaceCull];
		
		// RTs
		psoDesc.NumRenderTargets = static_cast<UINT>(1);
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;

		// topology
		psoDesc.PrimitiveTopologyType = iTess == 1 ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		// shaders
		size_t NumShaders = 2; // VS-PS
		if (iTess == 1)
		{	                 // VS-HS-DS-PS | VS-HS-DS-GS-PS
			NumShaders = iTessCull == 0 ? 4 : 5;
		}
		psoLoadDesc.ShaderStageCompileDescs.resize(NumShaders);
		size_t iShader = 0;
		if (iTess == 1)
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain_Tess", "vs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "HSMain"     , "hs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "DSMain"     , "ds_6_0" };
			if (iTessCull > 0)
				psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "GSMain" , "gs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain"     , "ps_6_0" };
		}
		else
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" };
		}
		const size_t iPixelShader = iShader - 1;

		// macros
		const FShaderMacro InstancedDrawMacro = { "INSTANCED_DRAW", std::to_string(RENDER_INSTANCED_SCENE_MESHES) };
		const FShaderMacro InstanceCountMacro = { "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SCENE_MESHES) };
		if (iTess == 1)
		{
			AppendTessellationVSMacros(psoLoadDesc.ShaderStageCompileDescs[0/*VS*/].Macros, iDomain);
			AppendTessellationHSMacros(psoLoadDesc.ShaderStageCompileDescs[1/*HS*/].Macros, iDomain, iPart, iOutTopo, iTessCull);
			AppendTessellationDSMacros(psoLoadDesc.ShaderStageCompileDescs[2/*DS*/].Macros, iDomain, iOutTopo, iTessCull);
			if (iTessCull > 0)
			{
				AppendTessellationGSMacros(psoLoadDesc.ShaderStageCompileDescs[3/*GS*/].Macros, iOutTopo, iTessCull);
			}
		}
		for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs) // all stages
		{
			shdDesc.Macros.push_back(InstancedDrawMacro);
			shdDesc.Macros.push_back(InstanceCountMacro);
			if (iAlpha == 1)
			{
				shdDesc.Macros.push_back({ "ENABLE_ALPHA_MASK", "1" });
			}
		}

		mapLoadDesc[key] = psoLoadDesc;
		mapPSO[key] = INVALID_ID; 
	}
}

void FShadowPassPSOs::GatherPSOLoadDescs(const std::unordered_map<RS_ID, ID3D12RootSignature*>& mRootSignatureLookup)
{
	SCOPED_CPU_MARKER("GatherPSOLoadDescs_ZPrePass");
	const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader(L"ShadowDepthPass.hlsl");

	FPSODesc psoLoadDesc = {};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
	psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ShadowPass);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.NumRenderTargets = 0; // no color targets, PS will discard alpha
	psoDesc.SampleDesc.Count = 1; // No MSAA

	const UINT SampleDescCounts[NUM_MSAA_OPTIONS] = { 1, 4 };
	const D3D12_CULL_MODE CullModes[NUM_FACECULL_OPTS] = { D3D12_CULL_MODE_NONE , D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_BACK };
	const D3D12_FILL_MODE FillModes[NUM_RASTER_OPTS] = { D3D12_FILL_MODE_SOLID, D3D12_FILL_MODE_WIREFRAME };

	for(size_t iDepthMode =0; iDepthMode < NUM_DEPTH_RENDER_OPTS; ++iDepthMode)
	for(size_t iRaster = 0  ; iRaster   < NUM_RASTER_OPTS   ; ++iRaster) 
	for(size_t iFaceCull = 0; iFaceCull < NUM_FACECULL_OPTS ; ++iFaceCull)
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED  ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS; ++iOutTopo) 
	for(size_t iTessCull = 0; iTessCull < NUM_TESS_CULL_OPTIONS; ++iTessCull)
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS ; ++iAlpha)
	{
		if (ShouldSkipTessellationVariant(iTess, iDomain, iPart, iOutTopo, iTessCull))
			continue;

		const size_t key = Hash(iDepthMode, iRaster, iFaceCull, iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
		
		std::string PSOName = "PSO_ShadowPass";
		if (iDepthMode == 1) PSOName += "_LinearDepth";
		if (iAlpha == 1) PSOName += "_AlphaMasked";
		if (iRaster == 1) PSOName += "_Wireframe";
		if (iTess == 1)
		{
			AppendTessellationPSONameTokens(PSOName, iDomain, iPart, iOutTopo, iTessCull);
		}
		psoLoadDesc.PSOName = PSOName;

		// RS
		psoDesc.RasterizerState.FillMode = FillModes[iRaster];
		psoDesc.RasterizerState.CullMode = CullModes[iFaceCull];
		
		// topology
		psoDesc.PrimitiveTopologyType = iTess == 1 
			? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH 
			: D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		// shaders
		size_t NumPixelShader = iDepthMode > 0 ? 1 : 0;
		size_t NumShaders = 1 + NumPixelShader; // VS-PS(optional)
		if (iTess == 1)
		{	                 // VS-HS-DS-PS | VS-HS-DS-GS-PS
			NumShaders += iTessCull == 0 ? 2 : 3;
		}
		psoLoadDesc.ShaderStageCompileDescs.resize(NumShaders);
		size_t iShader = 0;
		if (iTess == 1)
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain_Tess", "vs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "HSMain"     , "hs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "DSMain"     , "ds_6_0" };
			if (iTessCull > 0)
				psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "GSMain" , "gs_6_0" };
			if (iDepthMode > 0)
				psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain"     , "ps_6_0" };
		}
		else
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" };
			if(iDepthMode > 0)
				psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" };
		}
		const size_t iPixelShader = iShader - 1;

		// macros
		const FShaderMacro InstancedDrawMacro = { "INSTANCED_DRAW", "1" };
		const FShaderMacro InstanceCountMacro = { "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SHADOW_MESHES) };
		if (iTess == 1)
		{
			AppendTessellationVSMacros(psoLoadDesc.ShaderStageCompileDescs[0/*VS*/].Macros, iDomain);
			AppendTessellationHSMacros(psoLoadDesc.ShaderStageCompileDescs[1/*HS*/].Macros, iDomain, iPart, iOutTopo, iTessCull);
			AppendTessellationDSMacros(psoLoadDesc.ShaderStageCompileDescs[2/*DS*/].Macros, iDomain, iOutTopo, iTessCull);
			if(iTessCull > 0)
				AppendTessellationGSMacros(psoLoadDesc.ShaderStageCompileDescs[3/*GS*/].Macros, iOutTopo, iTessCull);
		}
		for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs)
		{
			shdDesc.Macros.push_back(InstancedDrawMacro);
			shdDesc.Macros.push_back(InstanceCountMacro);
			if (iAlpha == 1)
			{
				shdDesc.Macros.push_back({ "ENABLE_ALPHA_MASK", "1" });
			}
		}
		
		mapLoadDesc[key] = psoLoadDesc;
		mapPSO[key] = INVALID_ID; 
	}
}



size_t FLightingPSOs::Hash(size_t iMSAA, size_t iRaster, size_t iFaceCull, size_t iOutMoVec, size_t iOutRough, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha)
{
	return iMSAA
		+ NUM_MSAA_OPTIONS * iRaster
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * iFaceCull
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * iOutMoVec
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * iOutRough
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * iTess
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * NUM_TESS_ENABLED * iDomain
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * iPart
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * iOutTopo
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * iTessCullMode
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_MOVEC_OPTS * NUM_ROUGH_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * NUM_TESS_CULL_OPTIONS * iAlpha;
}

size_t FDepthPrePassPSOs::Hash(size_t iMSAA, size_t iRaster, size_t iFaceCull, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha)
{
	return iMSAA
		+ NUM_MSAA_OPTIONS * iRaster
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * iFaceCull
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * iTess
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * iDomain
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * iPart
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * iOutTopo
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * iTessCullMode
		+ NUM_MSAA_OPTIONS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * NUM_TESS_CULL_OPTIONS * iAlpha;
}

size_t FShadowPassPSOs::Hash(size_t iDepthMode, size_t iRaster, size_t iFaceCull, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha)
{
	return iDepthMode
		+ NUM_DEPTH_RENDER_OPTS * iRaster
		+ NUM_DEPTH_RENDER_OPTS * NUM_RASTER_OPTS * iFaceCull
		+ NUM_DEPTH_RENDER_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * iTess
		+ NUM_DEPTH_RENDER_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * iDomain
		+ NUM_DEPTH_RENDER_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * iPart
		+ NUM_DEPTH_RENDER_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * iOutTopo
		+ NUM_DEPTH_RENDER_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * iTessCullMode
		+ NUM_DEPTH_RENDER_OPTS * NUM_RASTER_OPTS * NUM_FACECULL_OPTS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * NUM_TESS_CULL_OPTIONS * iAlpha;
}
