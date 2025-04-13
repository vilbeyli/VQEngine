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

#include "Renderer/Renderer.h"
#include "Tessellation.h"
#include "Engine/GPUMarker.h"
#include "Shaders/LightingConstantBufferData.h"
#include "Libs/VQUtils/Source/utils.h"
#include "Libs/DirectXCompiler/inc/dxcapi.h"

#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <set>
#include <map>
#include <d3dcompiler.h>

using namespace Microsoft::WRL;
using namespace VQSystemInfo;
using namespace Tessellation;

static std::atomic<PSO_ID> LAST_USED_PSO_ID_OFFSET = 1;
static std::atomic<TaskID> LAST_USED_TASK_ID = 1;

static PSO_ID GetNextAvailablePSOIdAndIncrement()
{
	return EBuiltinPSOs::NUM_BUILTIN_PSOs + LAST_USED_PSO_ID_OFFSET.fetch_add(1);
}


static void PreAssignPSOIDs(PSOCollection& psoCollection, int& i, std::vector<FPSODesc>& descs)
{
	for (auto it = psoCollection.mapLoadDesc.begin(); it != psoCollection.mapLoadDesc.end(); ++it)
	{
		descs[i] = std::move(it->second);
		psoCollection.mapPSO[it->first] = EBuiltinPSOs::NUM_BUILTIN_PSOs + i++; // assign PSO_IDs beforehand
	}
	psoCollection.mapLoadDesc.clear();
}

static std::vector<const FShaderStageCompileDesc*> GatherUniqueShaderCompileDescs(const std::vector<FPSODesc>& PSODescs, std::map<PSO_ID, std::vector<size_t>>& PSOShaderMap)
{
	SCOPED_CPU_MARKER("GatherUniqueShaderCompileDescs");
	std::vector<const FShaderStageCompileDesc*> UniqueCompileDescs;
	std::unordered_map<size_t, size_t> UniqueCompilePathHashToIndex;
	std::hash<std::string> hasher;

	for (int i = 0; i < PSODescs.size(); ++i)
	{
		for (const FShaderStageCompileDesc& shaderDesc : PSODescs[i].ShaderStageCompileDescs)
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

// BUGGY MULTITHREADED, TODO: FIX
static std::vector<const FShaderStageCompileDesc*> GatherUniqueShaderCompileDescs(
	const std::vector<FPSODesc>& PSODescs,
	const std::vector<std::pair<size_t, size_t>>& vRanges,
	std::map<PSO_ID, std::vector<size_t>>& PSOShaderMap,
	ThreadPool& PSOWorkers
)
{
	SCOPED_CPU_MARKER("GatherUniqueShaderCompileDescs");

	// Thread-local storage for each range
	struct ThreadResult 
	{
		std::vector<const FShaderStageCompileDesc*> LocalUniqueCompileDescs;
		std::unordered_map<size_t, size_t> LocalUniqueCompilePathHashToIndex;
	};
	std::vector<ThreadResult> ThreadResults(vRanges.size() - 1);

	// Lambda to process a range of PSODescs
	auto fnProcessPSODescs = [&PSODescs, &PSOShaderMap](size_t start, size_t end, ThreadResult& result)
	{
		std::hash<std::string> hasher;
		for (size_t i = start; i <= end; ++i) 
		{
			for (const FShaderStageCompileDesc& shaderDesc : PSODescs[i].ShaderStageCompileDescs) 
			{
				const std::string CachedShaderBinaryPath = VQRenderer::GetCachedShaderBinaryPath(shaderDesc);
				const size_t hash = hasher(CachedShaderBinaryPath);

				auto it = result.LocalUniqueCompilePathHashToIndex.find(hash);
				if (it != result.LocalUniqueCompilePathHashToIndex.end()) 
				{
					PSOShaderMap[i].push_back(it->second);
					continue;
				}

				result.LocalUniqueCompileDescs.push_back(&shaderDesc);
				const size_t iShader = result.LocalUniqueCompileDescs.size() - 1;
				result.LocalUniqueCompilePathHashToIndex[hash] = iShader;
				PSOShaderMap[i].push_back(iShader);
			}
		}
	};

	// Launch threaded tasks
	std::latch Latch{ __int64(vRanges.size() - 1) };
	for (size_t iRange = 0; iRange < vRanges.size() - 1; ++iRange) 
	{
		PSOWorkers.AddTask([&, iRange]() 
		{
			SCOPED_CPU_MARKER("ProcessPSODescs");
			fnProcessPSODescs(vRanges[iRange].first, vRanges[iRange].second, ThreadResults[iRange]);
			Latch.count_down();
		});
	}

	// Main thread processes the last range
	std::vector<const FShaderStageCompileDesc*> UniqueCompileDescs;
	std::unordered_map<size_t, size_t> UniqueCompilePathHashToIndex;
	{
		SCOPED_CPU_MARKER("ProcessPSODescs");
		ThreadResult mainThreadResult;
		fnProcessPSODescs(vRanges.back().first, vRanges.back().second, mainThreadResult);
		UniqueCompileDescs = std::move(mainThreadResult.LocalUniqueCompileDescs);
		UniqueCompilePathHashToIndex = std::move(mainThreadResult.LocalUniqueCompilePathHashToIndex);
	}

	// Wait for threads to finish
	{
		SCOPED_CPU_MARKER_C("WAIT_Workers", 0xFFAA0000);
		Latch.wait();
	}

	// Merge results
	{
		SCOPED_CPU_MARKER("Merge");
		for (size_t iRange = 0; iRange < vRanges.size() - 1; ++iRange)
		{
			const auto& threadResult = ThreadResults[iRange];
			std::unordered_map<size_t, size_t> tempHashToGlobalIndex;

			// Merge descriptors and update hash-to-index mapping
			for (size_t iLocal = 0; iLocal < threadResult.LocalUniqueCompileDescs.size(); ++iLocal)
			{
				const auto* shaderDesc = threadResult.LocalUniqueCompileDescs[iLocal];
				const std::string CachedShaderBinaryPath = VQRenderer::GetCachedShaderBinaryPath(*shaderDesc);
				const size_t hash = std::hash<std::string>{}(CachedShaderBinaryPath);

				auto it = UniqueCompilePathHashToIndex.find(hash);
				if (it != UniqueCompilePathHashToIndex.end() &&
					VQRenderer::GetCachedShaderBinaryPath(*UniqueCompileDescs[it->second]) == CachedShaderBinaryPath)
				{
					tempHashToGlobalIndex[threadResult.LocalUniqueCompilePathHashToIndex.at(hash)] = it->second;
				}
				else
				{
					tempHashToGlobalIndex[threadResult.LocalUniqueCompilePathHashToIndex.at(hash)] = UniqueCompileDescs.size();
					UniqueCompileDescs.push_back(shaderDesc);
					UniqueCompilePathHashToIndex[hash] = UniqueCompileDescs.size() - 1;
				}
			}

			// Update PSOShaderMap indices for this thread's range
			// Note: Assumes PSO_ID is compatible with size_t; verify PSOShaderMap key type
			for (size_t i = vRanges[iRange].first; i <= vRanges[iRange].second; ++i)
			{
				for (size_t& shaderIndex : PSOShaderMap[i])
				{
					shaderIndex = tempHashToGlobalIndex.at(shaderIndex);
				}
			}
		}
	}

	return UniqueCompileDescs;
}

static void LoadRenderPassPSODescs(std::vector<std::shared_ptr<IRenderPass>>& mRenderPasses, std::vector<FPSOCreationTaskParameters>& RenderPassPSOTaskParams)
{
	SCOPED_CPU_MARKER("LoadRenderPassPSODescs");
	for (std::shared_ptr<IRenderPass>& pPass : mRenderPasses)
	{
		const std::vector<FPSOCreationTaskParameters> vPSOTaskParams = pPass->CollectPSOCreationParameters();
		RenderPassPSOTaskParams.insert(RenderPassPSOTaskParams.end()
			, std::make_move_iterator(vPSOTaskParams.begin())
			, std::make_move_iterator(vPSOTaskParams.end())
		);
	}
}

static size_t GeneratePSOHash(const FPSODesc& PSODesc)
{
	SCOPED_CPU_MARKER("HashPSO");
	if (PSODesc.ShaderStageCompileDescs.empty())
	{
		Log::Error("Empty shader compile desc for %s", PSODesc.PSOName.c_str());
		return 0;
	}

	std::string hashInput = PSODesc.PSOName;

	// shader details
	for (const FShaderStageCompileDesc& shaderDesc : PSODesc.ShaderStageCompileDescs)
	{
		hashInput += StrUtil::UnicodeToASCII<256>(shaderDesc.FilePath.c_str()) + shaderDesc.EntryPoint + shaderDesc.ShaderModel;
		for (const FShaderMacro& macro : shaderDesc.Macros)
		{
			hashInput += macro.Name;
			hashInput += macro.Value;
		}
	}

	const bool bGfxPSO = ShaderUtils::GetShaderStageEnumFromShaderModel(PSODesc.ShaderStageCompileDescs[0].ShaderModel) != EShaderStage::CS;

	// TODO: we need to handle root signatures at some point.
	//       either ID all root signatures 
	//       OR     use serialized root signature data for hashing vec<uint8>

	// pipeline details
	if (bGfxPSO)
	{
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc = PSODesc.D3D12GraphicsDesc;

		// Input Layout
		hashInput += std::to_string(desc.InputLayout.NumElements);
		for (UINT i = 0; i < desc.InputLayout.NumElements; ++i) 
		{
			const D3D12_INPUT_ELEMENT_DESC& elem = desc.InputLayout.pInputElementDescs[i];
			hashInput += elem.SemanticName + std::to_string(elem.SemanticIndex) +
				std::to_string(elem.Format) + std::to_string(elem.InputSlot) +
				std::to_string(elem.AlignedByteOffset) + std::to_string(elem.InputSlotClass) +
				std::to_string(elem.InstanceDataStepRate);
		}

		// Rasterizer State
		hashInput += std::to_string(desc.RasterizerState.FillMode) +
			std::to_string(desc.RasterizerState.CullMode) +
			std::to_string(desc.RasterizerState.FrontCounterClockwise) +
			std::to_string(desc.RasterizerState.DepthBias) +
			std::to_string(desc.RasterizerState.DepthBiasClamp) +
			std::to_string(desc.RasterizerState.SlopeScaledDepthBias) +
			std::to_string(desc.RasterizerState.DepthClipEnable) +
			std::to_string(desc.RasterizerState.MultisampleEnable) +
			std::to_string(desc.RasterizerState.AntialiasedLineEnable) +
			std::to_string(desc.RasterizerState.ForcedSampleCount) +
			std::to_string(desc.RasterizerState.ConservativeRaster);

		// Depth Stencil State
		hashInput += std::to_string(desc.DepthStencilState.DepthEnable) +
			std::to_string(desc.DepthStencilState.DepthWriteMask) +
			std::to_string(desc.DepthStencilState.DepthFunc) +
			std::to_string(desc.DepthStencilState.StencilEnable) +
			std::to_string(desc.DepthStencilState.StencilReadMask) +
			std::to_string(desc.DepthStencilState.StencilWriteMask) +
			std::to_string(desc.DepthStencilState.FrontFace.StencilFailOp) +
			std::to_string(desc.DepthStencilState.FrontFace.StencilDepthFailOp) +
			std::to_string(desc.DepthStencilState.FrontFace.StencilPassOp) +
			std::to_string(desc.DepthStencilState.FrontFace.StencilFunc) +
			std::to_string(desc.DepthStencilState.BackFace.StencilFailOp) +
			std::to_string(desc.DepthStencilState.BackFace.StencilDepthFailOp) +
			std::to_string(desc.DepthStencilState.BackFace.StencilPassOp) +
			std::to_string(desc.DepthStencilState.BackFace.StencilFunc);

		// Blend State
		hashInput += std::to_string(desc.BlendState.AlphaToCoverageEnable) +
			std::to_string(desc.BlendState.IndependentBlendEnable);
		for (UINT i = 0; i < 8; ++i) {
			const D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[i];
			hashInput += std::to_string(rt.BlendEnable) +
				std::to_string(rt.LogicOpEnable) +
				std::to_string(rt.SrcBlend) +
				std::to_string(rt.DestBlend) +
				std::to_string(rt.BlendOp) +
				std::to_string(rt.SrcBlendAlpha) +
				std::to_string(rt.DestBlendAlpha) +
				std::to_string(rt.BlendOpAlpha) +
				std::to_string(rt.LogicOp) +
				std::to_string(rt.RenderTargetWriteMask);
		}

		// Other fields
		hashInput += std::to_string(desc.IBStripCutValue) +
			std::to_string(desc.PrimitiveTopologyType) +
			std::to_string(desc.NumRenderTargets);

		for (UINT i = 0; i < desc.NumRenderTargets; ++i) 
			hashInput += std::to_string(desc.RTVFormats[i]);

		hashInput += std::to_string(desc.DSVFormat) +
			std::to_string(desc.SampleDesc.Count) +
			std::to_string(desc.SampleDesc.Quality) +
			std::to_string(desc.SampleMask) +
			std::to_string(desc.Flags);
	}
	else // compute PSO
	{
		const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc = PSODesc.D3D12ComputeDesc;
		hashInput += std::to_string(desc.Flags);
	}

	return std::hash<std::string>{}(hashInput);
}

static void WaitPSOShaders(const std::vector<std::shared_future<FShaderStageCompileResult>>& mShaderCompileResults, const std::vector<size_t>& iPSOShaders)
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
static bool CheckErrors(const std::vector<std::shared_future<FShaderStageCompileResult>>& mShaderCompileResults, const std::vector<size_t>& iPSOShaders)
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
	return bShaderErrors;
}
static std::string GetErrString(HRESULT hr)
{
	switch (hr)
	{
	case E_OUTOFMEMORY: return "Out of memory";
	case E_INVALIDARG: return "Invalid arguments";
	}
	return "Unspecified error, contact dev";
}

static bool CachePSO(ID3D12PipelineState* pPSO, const std::string& PSOCacheFilePath)
{
	SCOPED_CPU_MARKER("CachePSO");
	Microsoft::WRL::ComPtr<ID3DBlob> psoBlob;
	HRESULT hr = pPSO->GetCachedBlob(&psoBlob);
	if (!SUCCEEDED(hr))
	{
		Log::Error("Error getting cached PSO blob");
		return false;
	}
	
	std::ofstream cacheFile(PSOCacheFilePath, std::ios::binary);
	if (!cacheFile.is_open())
	{
		Log::Error("Error opening PSO cache file: %s", PSOCacheFilePath.c_str());
		return false;
	}
	
	cacheFile.write(static_cast<const char*>(psoBlob->GetBufferPointer()), psoBlob->GetBufferSize());
	cacheFile.close();
	return true;
}

static std::vector<size_t> ComputePSOHashes(const std::vector<FPSODesc>& Descs, const std::vector<std::pair<size_t, size_t>>& vRanges, ThreadPool& PSOLoadWorkers)
{
	std::vector<size_t> hashes(Descs.size());

	__int64 LATCH_COUNTER = (__int64)vRanges.size() - 1;
	std::latch latch{ LATCH_COUNTER };

	auto fnComputeHashes = [](std::vector<size_t>& Results, size_t iBegin, size_t iEnd, const std::vector<FPSODesc>& Descs)
	{
		SCOPED_CPU_MARKER("ComputeHashes");
		assert(iBegin <= iEnd && iBegin < Descs.size() && iEnd < Descs.size());
		for (size_t i = iBegin; i <= iEnd; ++i)
			Results[i] = GeneratePSOHash(Descs[i]);
	};

	{
		SCOPED_CPU_MARKER("Dispatch");
		for (size_t iRange = 0; iRange < vRanges.size() - 1; ++iRange)
		{
			PSOLoadWorkers.AddTask([&, iRange]()
			{
				fnComputeHashes(hashes, vRanges[iRange].first, vRanges[iRange].second, Descs);
				latch.count_down();
			});
		}
	}

	fnComputeHashes(hashes, vRanges.back().first, vRanges.back().second, Descs);

	{
		SCOPED_CPU_MARKER_C("Sync", 0xFFAA0000);
		latch.wait();
	}

	return hashes;
}

static std::vector<std::string> ComputeCachedPSOFilePaths(const std::vector<FPSODesc>& PSODescs, const std::vector<size_t>& PSOHashes)
{
	SCOPED_CPU_MARKER("ComputeCachedPSOFilePaths");
	std::vector<std::string> PSOCacheFilePaths(PSODescs.size());
	size_t i = 0;
	const std::string PSOCacheFolder = VQRenderer::PSOCacheDirectory + "/";
	for (const FPSODesc& psoDesc : PSODescs)
	{
		PSOCacheFilePaths[i] = PSOCacheFolder + std::to_string(PSOHashes[i]) + ".pso";
		++i;
	}
	return PSOCacheFilePaths;
}

static std::vector<bool> CheckIsCached(
	const std::vector<FPSODesc>& Descs,
	const std::vector<std::string>& PSOCacheFilePaths,
	const std::unordered_map<std::string, bool>& ShaderCacheDirtyMap,
	const std::vector<std::pair<size_t, size_t>>& vRanges,
	ThreadPool& PSOWorkers
)
{
	SCOPED_CPU_MARKER("IsCached");
	std::vector<bool> IsPSOCached(Descs.size());

	std::latch latch{ (__int64)vRanges.size()-1 };

	auto fnProcessIsCached = [&IsPSOCached, &PSOCacheFilePaths, &ShaderCacheDirtyMap, &Descs](size_t iBegin, size_t iEnd)
	{
		SCOPED_CPU_MARKER("ProcessIsCached");
		for (size_t i = iBegin; i <= iEnd; ++i)
		{
			const bool bCachedFileExists = std::filesystem::exists(PSOCacheFilePaths[i]);
			if (!bCachedFileExists)
			{
				IsPSOCached[i] = false;
				continue;
			}

			// PSO cache may be dirty if any of its cached shader is dirty
			bool bShaderCacheDirty = false;
			for (const FShaderStageCompileDesc& shaderDesc : Descs[i].ShaderStageCompileDescs)
			{
				const std::string ShaderSourcePath = StrUtil::UnicodeToASCII<256>(shaderDesc.FilePath.c_str());
				const std::string CachedShaderBinaryPath = VQRenderer::GetCachedShaderBinaryPath(shaderDesc);
				bShaderCacheDirty = ShaderCacheDirtyMap.at(CachedShaderBinaryPath);
				if (bShaderCacheDirty)
					break;
			}

			IsPSOCached[i] = !bShaderCacheDirty;
		}
	};
	{
		SCOPED_CPU_MARKER("Dispatch");
		for (size_t iRange = 0; iRange < vRanges.size() - 1; ++iRange)
		{
			PSOWorkers.AddTask([&, iRange]() 
			{
				fnProcessIsCached(vRanges[iRange].first, vRanges[iRange].second);
				latch.count_down();
			});
		}
	}

	fnProcessIsCached(vRanges.back().first, vRanges.back().second);

	{
		SCOPED_CPU_MARKER_C("Sync", 0xFFAA0000);
		latch.wait();
	}

	return IsPSOCached;
}

static std::unordered_map<std::string, bool> BuildShaderCacheDirtyMap(
	const std::vector<const FShaderStageCompileDesc*>& UniqueShaderCompileDescs,
	std::unordered_map<std::string, bool>& IncludeDirtyMap,
	ThreadPool& PSOWorkers
)
{
	SCOPED_CPU_MARKER("BuildShaderCacheDirtyMaps");
	const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(UniqueShaderCompileDescs.size(), PSOWorkers.GetThreadPoolSize());
	
	std::vector<std::unordered_map<std::string, bool>> ShaderCacheDirtyMaps(vRanges.size() - 1);
	std::vector<std::unordered_map<std::string, bool>> IncludeDirtyMaps(vRanges.size() - 1);

	auto fnProcessShaders = [](const FShaderStageCompileDesc* shaderDesc, std::unordered_map<std::string, bool>& ShaderCacheDirtyMap, std::unordered_map<std::string, bool>& IncludeDirtyMap)
	{
		const std::string ShaderSourcePath = StrUtil::UnicodeToASCII<256>(shaderDesc->FilePath.c_str());
		const std::string CachedShaderBinaryPath = VQRenderer::GetCachedShaderBinaryPath(*shaderDesc);

		if (!DirectoryUtil::FileExists(CachedShaderBinaryPath))
		{
			ShaderCacheDirtyMap[CachedShaderBinaryPath] = true;
			return;
		}

		if (DirectoryUtil::IsFileNewer(ShaderSourcePath, CachedShaderBinaryPath))
		{
			ShaderCacheDirtyMap[CachedShaderBinaryPath] = true;
			return;
		}

		auto it = IncludeDirtyMap.find(ShaderSourcePath);
		if (it == IncludeDirtyMap.end())
		{
			IncludeDirtyMap[ShaderSourcePath] = ShaderUtils::AreIncludesDirty(ShaderSourcePath, CachedShaderBinaryPath);
		}
		if (IncludeDirtyMap.at(ShaderSourcePath))
		{
			ShaderCacheDirtyMap[CachedShaderBinaryPath] = true;
			return;
		}
		ShaderCacheDirtyMap[CachedShaderBinaryPath] = false;
	};


	std::latch Latch{ __int64(vRanges.size() - 1) };
	for (size_t iRange = 0; iRange < vRanges.size() - 1; ++iRange)
	{
		PSOWorkers.AddTask([&, iRange]()
		{
			SCOPED_CPU_MARKER("BuildShaderCacheDirtyMap");
			for (size_t i = vRanges[iRange].first; i <= vRanges[iRange].second; ++i)
			{
				const FShaderStageCompileDesc* shaderDesc = UniqueShaderCompileDescs[i];
				fnProcessShaders(shaderDesc, ShaderCacheDirtyMaps[iRange], IncludeDirtyMaps[iRange]);
			}
			Latch.count_down();
		});
	}

	std::unordered_map<std::string, bool> ShaderCacheDirtyMap; // reduce everything into this final map
	{
		SCOPED_CPU_MARKER("BuildShaderCacheDirtyMap");
		for (size_t i = vRanges.back().first; i <= vRanges.back().second; ++i)
		{
			const FShaderStageCompileDesc* shaderDesc = UniqueShaderCompileDescs[i];
			fnProcessShaders(shaderDesc, ShaderCacheDirtyMap, IncludeDirtyMap);
		}
	}

	{
		SCOPED_CPU_MARKER_C("WAIT_Workers", 0xFFAA0000);
		Latch.wait();
	}

	{
		SCOPED_CPU_MARKER("Merge");
		for (size_t iRange = 0; iRange < vRanges.size() - 1; ++iRange)
		{
			ShaderCacheDirtyMap.insert(ShaderCacheDirtyMaps[iRange].begin(), ShaderCacheDirtyMaps[iRange].end());
		}
	}

	return ShaderCacheDirtyMap;
}

static std::vector<uint8_t> LoadPSOBinary(const std::string& CachedPSOBinaryPath)
{
	Log::Info("Loading PSO Binary: %s ", DirectoryUtil::GetFileNameFromPath(CachedPSOBinaryPath).c_str());

	std::ifstream cacheFile(CachedPSOBinaryPath, std::ios::binary);
	if (!cacheFile.is_open())
	{
		Log::Error("Cannot open Cached PSO binary file: %s", CachedPSOBinaryPath.c_str());
		return {};
	}

	std::vector<uint8_t> psoBinary((std::istreambuf_iterator<char>(cacheFile)), std::istreambuf_iterator<char>());
	cacheFile.close();

	return psoBinary;
}

void VQRenderer::ReservePSOMap(size_t NumPSOs)
{
	SCOPED_CPU_MARKER("ReservePSOMap");
	for (int i = 0; i < EBuiltinPSOs::NUM_BUILTIN_PSOs; ++i)
		mPSOs[i] = nullptr;
	for (size_t i = 0; i < NumPSOs; ++i)
		mPSOs[EBuiltinPSOs::NUM_BUILTIN_PSOs + (int)i] = nullptr;
}

std::vector<FPSODesc> VQRenderer::LoadBuiltinPSODescs()
{
	SCOPED_CPU_MARKER("LoadPSODescs_Builtin");

	mLightingPSOs.GatherPSOLoadDescs(mRootSignatureLookup);
	mZPrePassPSOs.GatherPSOLoadDescs(mRootSignatureLookup);
	mShadowPassPSOs.GatherPSOLoadDescs(mRootSignatureLookup);

	std::vector<FPSODesc> descs(
		mLightingPSOs.mapLoadDesc.size()
		+ mZPrePassPSOs.mapLoadDesc.size()
		+ mShadowPassPSOs.mapLoadDesc.size()
	);

	{
		SCOPED_CPU_MARKER("PreAssignPSOIds");
		int i = 0;
		PreAssignPSOIDs(mLightingPSOs, i, descs);
		PreAssignPSOIDs(mZPrePassPSOs, i, descs);
		PreAssignPSOIDs(mShadowPassPSOs, i, descs);
	}
	return descs;
}

ID3D12PipelineState* VQRenderer::CompileGraphicsPSO(FPSODesc& Desc, std::vector<std::shared_future<FShaderStageCompileResult>>& ShaderCompileResults)
{
	SCOPED_CPU_MARKER("CompileGraphicsPSO");
	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	ID3D12PipelineState* pPSO = nullptr;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC& d3d12GraphicsPSODesc = Desc.D3D12GraphicsDesc;

	std::unordered_map<EShaderStage, Microsoft::WRL::ComPtr<ID3D12ShaderReflection>> ShaderReflections;

	// Assign shader blobs to PSODesc
	for (const std::shared_future<FShaderStageCompileResult>& TaskResult : ShaderCompileResults)
	{
		const FShaderStageCompileResult& ShaderCompileResult = TaskResult.get();

		CD3DX12_SHADER_BYTECODE ShaderByteCode(ShaderCompileResult.ShaderBlob.GetByteCode(), ShaderCompileResult.ShaderBlob.GetByteCodeSize());
		switch (ShaderCompileResult.ShaderStageEnum)
		{
		case EShaderStage::VS: d3d12GraphicsPSODesc.VS = ShaderByteCode; break;
		case EShaderStage::HS: d3d12GraphicsPSODesc.HS = ShaderByteCode; break;
		case EShaderStage::DS: d3d12GraphicsPSODesc.DS = ShaderByteCode; break;
		case EShaderStage::GS: d3d12GraphicsPSODesc.GS = ShaderByteCode; break;
		case EShaderStage::PS: d3d12GraphicsPSODesc.PS = ShaderByteCode; break;
		}

		// reflect shader
		Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& pShaderReflection = ShaderReflections[ShaderCompileResult.ShaderStageEnum];
		if (ShaderCompileResult.bSM6)
		{
			assert(ShaderCompileResult.ShaderBlob.pBlobDxc);
			HRESULT hr;

			Microsoft::WRL::ComPtr<IDxcContainerReflection> pReflection;
			hr = DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&pReflection));
			if (FAILED(hr)) {
				Log::Error("Failed ");
			}

			hr = pReflection->Load(ShaderCompileResult.ShaderBlob.pBlobDxc.Get());
			if (FAILED(hr)) {
				Log::Error("Failed ");
			}

			UINT32 index;
			hr = pReflection->FindFirstPartKind(DXC_PART_REFLECTION_DATA, &index);
			if (FAILED(hr)) {
				Log::Error("Failed ");
			}

			hr = pReflection->GetPartReflection(index, IID_PPV_ARGS(&pShaderReflection));
			if (FAILED(hr)) {
				Log::Error("Failed ");
			}

			assert(pShaderReflection);
		}
		else
		{
			HRESULT hr = D3DReflect(ShaderByteCode.pShaderBytecode, ShaderByteCode.BytecodeLength, IID_PPV_ARGS(&pShaderReflection));
			assert(pShaderReflection);
		}
	}

	// assign input layout
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
	const bool bHasVS = ShaderReflections.find(EShaderStage::VS) != ShaderReflections.end();
	if (bHasVS)
	{
		inputLayout = ShaderUtils::ReflectInputLayoutFromVS(ShaderReflections.at(EShaderStage::VS).Get());
		d3d12GraphicsPSODesc.InputLayout = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };
	}

	// TODO: assign root signature
#if 0
	{
		for (auto& it : ShaderReflections)
		{
			EShaderStage eShaderStage = it.first;
			ID3D12ShaderReflection*& pRefl = it.second;

			D3D12_SHADER_DESC shaderDesc = {};
			pRefl->GetDesc(&shaderDesc);

			std::vector< D3D12_SHADER_INPUT_BIND_DESC> boundRscDescs(shaderDesc.BoundResources);
			for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
			{
				pRefl->GetResourceBindingDesc(i, &boundRscDescs[i]);
			}

			int a = 5;
		}
	}
#endif

	// Compile PSO
	HRESULT hr = pDevice->CreateGraphicsPipelineState(&d3d12GraphicsPSODesc, IID_PPV_ARGS(&pPSO));
	if (hr != S_OK)
	{
		std::string errMsg = Desc.PSOName + ": PSO compile failed (HR=" + std::to_string(hr) + "): " + GetErrString(hr);
		Log::Error("%s", errMsg.c_str());
		MessageBox(NULL, errMsg.c_str(), "PSO Compile Error", MB_OK);
	}
	assert(hr == S_OK);
	SetName(pPSO, Desc.PSOName.c_str());


	return pPSO;
}
ID3D12PipelineState* VQRenderer::CompileComputePSO(FPSODesc& Desc, std::vector<std::shared_future<FShaderStageCompileResult>>& ShaderCompileResults)
{
	SCOPED_CPU_MARKER("CompileComputePSO");
	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	D3D12_COMPUTE_PIPELINE_STATE_DESC& d3d12ComputePSODesc = Desc.D3D12ComputeDesc;

	// Assign CS shader blob to PSODesc
	for (std::shared_future<FShaderStageCompileResult>& TaskResult : ShaderCompileResults)
	{
		FShaderStageCompileResult ShaderCompileResult = TaskResult.get();

		CD3DX12_SHADER_BYTECODE ShaderByteCode(ShaderCompileResult.ShaderBlob.GetByteCode(), ShaderCompileResult.ShaderBlob.GetByteCodeSize());
		d3d12ComputePSODesc.CS = ShaderByteCode;
	}

	// TODO: assign root signature

	// Compile PSO
	ID3D12PipelineState* pPSO = nullptr;
	HRESULT hr = pDevice->CreateComputePipelineState(&d3d12ComputePSODesc, IID_PPV_ARGS(&pPSO));
	if (hr == S_OK)
	{
		SetName(pPSO, Desc.PSOName.c_str());
	}
	else
	{
		std::string errMsg = "PSO compile failed (HR=" + std::to_string(hr) + "): " + GetErrString(hr);
		Log::Error("%s", errMsg.c_str());
		MessageBox(NULL, errMsg.c_str(), "PSO Compile Error", MB_OK);
	}
	return pPSO;
}

void VQRenderer::StartPSOCompilation_MT()
{
	SCOPED_CPU_MARKER("StartPSOCompilation_MT");
	{
		SCOPED_CPU_MARKER_C("WaitRootSignatures", 0xFF0000AA);
		mLatchRootSignaturesInitialized.wait();
	}

	std::vector<FPSODesc> PSODescs_BuiltinLegacy = LoadBuiltinPSODescs_Legacy();
	std::vector<FPSODesc> PSODescs_Builtin = LoadBuiltinPSODescs();
	std::vector<FPSOCreationTaskParameters> RenderPassPSOTaskParams;
	{
		SCOPED_CPU_MARKER_C("WaitRenderPassInit", 0xFF0000AA);
		mLatchRenderPassesInitialized.wait();
	}
	LoadRenderPassPSODescs(mRenderPasses, RenderPassPSOTaskParams);

	assert(EBuiltinPSOs::NUM_BUILTIN_PSOs == PSODescs_BuiltinLegacy.size());
	const size_t NumBuiltinPSOs = PSODescs_BuiltinLegacy.size() + PSODescs_Builtin.size();

	std::vector<FPSODesc> PSODescs(NumBuiltinPSOs + RenderPassPSOTaskParams.size());
	{
		SCOPED_CPU_MARKER("GatherDescs");
		size_t i = 0;
		for (auto&& desc : PSODescs_BuiltinLegacy)    { PSODescs[i++] = std::move(desc); }
		for (auto&& desc : PSODescs_Builtin)          { PSODescs[i++] = std::move(desc); }
		for (auto&& params : RenderPassPSOTaskParams) { PSODescs[i++] = std::move(params.Desc); }
	}

	const size_t NUM_PSO_DESCS = PSODescs.size();
	ReservePSOMap(NUM_PSO_DESCS - EBuiltinPSOs::NUM_BUILTIN_PSOs);
	{
		SCOPED_CPU_MARKER("AssignRenderPassPSOIDs");
		int i = 0;
		for (auto& params : RenderPassPSOTaskParams)
		{
			*params.pID = (PSO_ID)(NumBuiltinPSOs + i++);
		}
	}

	
	std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(NUM_PSO_DESCS, mWorkers_PSOLoad.GetThreadPoolSize());
	__int64 LATCH_COUNTER = (__int64)vRanges.size() - 1;

	const std::vector<size_t> PSOHashes = ComputePSOHashes(PSODescs, vRanges, mWorkers_PSOLoad);

	{
		SCOPED_CPU_MARKER("CheckCollision");
		std::unordered_set<size_t> PSOHashSet(PSOHashes.begin(), PSOHashes.end());
		assert(PSOHashSet.size(), PSOHashes.size());
		if (PSOHashSet.size() != PSOHashes.size())
		{
			Log::Warning("PSO hash collision! duplicate PSO hashes found!");
		}
	}

	const std::vector<std::string> PSOCacheFilePaths = ComputeCachedPSOFilePaths(PSODescs, PSOHashes);

	// shader compile context
	std::map<PSO_ID, std::vector<size_t>> PSOShaderMap; // pso_index -> shader_index[]
	std::vector<const FShaderStageCompileDesc*> UniqueShaderCompileDescs = GatherUniqueShaderCompileDescs(PSODescs, PSOShaderMap);
	//std::vector<const FShaderStageCompileDesc*> UniqueShaderCompileDescs = GatherUniqueShaderCompileDescs(PSODescs, vRanges, PSOShaderMap, mWorkers_PSOLoad);
	mShaderCompileResults.clear();
	mShaderCompileResults.resize(UniqueShaderCompileDescs.size());

	std::unordered_map<std::string, bool> IncludeDirtyMap;
	mShaderCacheDirtyMap = BuildShaderCacheDirtyMap(UniqueShaderCompileDescs, IncludeDirtyMap, mWorkers_PSOLoad);

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
				return this->LoadShader(ShaderStageCompileDesc, mShaderCacheDirtyMap);
			});
		}
	}

	const std::vector<bool> IsPSOCached = CheckIsCached(PSODescs, PSOCacheFilePaths, mShaderCacheDirtyMap, vRanges, mWorkers_PSOLoad);

	// kickoff PSO workers
	{
		SCOPED_CPU_MARKER("DispatchPSOWorkers");
		mPSOCompileResults.clear();
		for (auto it = PSOShaderMap.begin(); it != PSOShaderMap.end(); ++it)
		{
			const PSO_ID psoID = it->first;
			const std::vector<size_t>& iPSOShaders = it->second;

			assert(psoID < NUM_PSO_DESCS && psoID >= 0);
			FPSODesc& PSODesc = PSODescs[psoID];
			const bool bLoadCachedPSO = IsPSOCached[psoID];
			const std::string& PSOCacheFilePath = PSOCacheFilePaths[psoID];

			std::shared_future<FPSOCompileResult> PSOCompileResult = mWorkers_PSOLoad.AddTask([=]() mutable
			{
				SCOPED_CPU_MARKER("PSOCompileWorker");
				ID3D12Device* pDevice = mDevice.GetDevicePtr();
				FPSOCompileResult result{ nullptr , psoID };

				// check if PSO is cached
				const bool bComputePSO = std::find_if(RANGE(PSODesc.ShaderStageCompileDescs) // check if ShaderModel has cs_*_*
					, [](const FShaderStageCompileDesc& desc) { return ShaderUtils::GetShaderStageEnumFromShaderModel(desc.ShaderModel) == EShaderStage::CS; }
				) != PSODesc.ShaderStageCompileDescs.end();

				std::vector<uint8_t> psoBinary;
				if (bLoadCachedPSO)
				{
					D3D12_CACHED_PIPELINE_STATE& cachedPS = bComputePSO ? PSODesc.D3D12ComputeDesc.CachedPSO : PSODesc.D3D12GraphicsDesc.CachedPSO;
					psoBinary = LoadPSOBinary(PSOCacheFilePath);
					cachedPS = { psoBinary.data(), psoBinary.size() };
				}

				WaitPSOShaders(mShaderCompileResults, iPSOShaders);

				const bool bShaderErrors = CheckErrors(mShaderCompileResults, iPSOShaders);
				if (bShaderErrors)
				{
					Log::Error("PSO Compile failed: %s", PSODesc.PSOName.c_str());
					return result;
				}

				std::vector<std::shared_future<FShaderStageCompileResult>> ShaderCompileResultsForThisPSO(iPSOShaders.size());
				for (size_t i = 0; i < ShaderCompileResultsForThisPSO.size(); ++i)
					ShaderCompileResultsForThisPSO[i] = mShaderCompileResults[iPSOShaders[i]];
				
				result.pPSO = bComputePSO
					? CompileComputePSO(PSODesc, ShaderCompileResultsForThisPSO)
					: CompileGraphicsPSO(PSODesc, ShaderCompileResultsForThisPSO);

				if(!bLoadCachedPSO)
					CachePSO(result.pPSO, PSOCacheFilePath);

				return result;
			});

			mPSOCompileResults.push_back(PSOCompileResult);
		}
		mLatchPSOLoaderDispatched.count_down();
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


	const FShaderMacro UnlitShaderInstanceCountMacro = FShaderMacro::CreateShaderMacro("INSTANCE_COUNT", "%zu", MAX_INSTANCE_COUNT__UNLIT_SHADER);
	const FShaderMacro InstancedDrawEnabledMacro = FShaderMacro::CreateShaderMacro("INSTANCED_DRAW", "1");

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
		{
			psoLoadDesc.PSOName = "PSO_Unlit_Blended";
			psoDesc.SampleDesc.Count = 1;
			psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
			psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND::D3D12_BLEND_SRC_ALPHA;
			psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND::D3D12_BLEND_INV_SRC_ALPHA;
			psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
			psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
			psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
			descs[EBuiltinPSOs::UNLIT_BLEND_PSO] = psoLoadDesc;
		}
		{
			psoLoadDesc.PSOName = "PSO_Unlit_Blended_MSAA";
			psoDesc.SampleDesc.Count = 4;
			descs[EBuiltinPSOs::UNLIT_BLEND_PSO_MSAA_4] = psoLoadDesc;
		}

		// wireframe
		psoLoadDesc.PSOName = "PSO_WireframeVSPS";
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		psoDesc.SampleDesc.Count = 1;
		descs[EBuiltinPSOs::WIREFRAME_PSO] = psoLoadDesc;

		psoLoadDesc.PSOName = "PSO_Wireframe_VSPS_Instanced";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back(InstancedDrawEnabledMacro);
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back(UnlitShaderInstanceCountMacro);
		descs[EBuiltinPSOs::WIREFRAME_INSTANCED_PSO] = psoLoadDesc;
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		{
			// MSAA
			psoLoadDesc.PSOName = "PSO_WireframeVSPS_MSAA4";
			psoDesc.SampleDesc.Count = 4;
			descs[EBuiltinPSOs::WIREFRAME_PSO_MSAA_4] = psoLoadDesc;

			psoLoadDesc.PSOName = "PSO_WireframeVSPS_Instanced_MSAA4";
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back(InstancedDrawEnabledMacro);
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back(UnlitShaderInstanceCountMacro);
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
			shdDesc.Macros.push_back(FShaderMacro::CreateShaderMacro("INTEGRATION_STEP_DIFFUSE_IRRADIANCE", (bLowEndGPU ? "0.050f" : (bHighEndGPU ? "0.010f" : "0.025f"))) );
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
		//{
		//	FPSODesc psoLoadDesc = {};
		//	psoLoadDesc.PSOName = "PSO_FFXSPDCS";
		//	psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "SPD_CSMain", "cs_6_0", {{"FFXSPD_CS", "1"}} });
		//	psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_SPD_CS);
		//	descs[EBuiltinPSOs::FFX_SPD_CS_PSO] = psoLoadDesc; 
		//}
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

	for(size_t iMSAA = 0    ; iMSAA     < NUM_MSAA_OPTIONS     ; ++iMSAA) 
	for(size_t iRaster = 0  ; iRaster   < NUM_RASTER_OPTS      ; ++iRaster) 
	for(size_t iFaceCull = 0; iFaceCull < NUM_FACECULL_OPTS    ; ++iFaceCull)
	for(size_t iOutMoVec = 0; iOutMoVec < NUM_MOVEC_OPTS       ; ++iOutMoVec) 
	for(size_t iOutRough = 0; iOutRough < NUM_ROUGH_OPTS       ; ++iOutRough) 
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED     ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS   ; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS   ; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS   ; ++iOutTopo) 
	for(size_t iTessCull = 0; iTessCull < NUM_TESS_CULL_OPTIONS; ++iTessCull)
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS    ; ++iAlpha)
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
		const FShaderMacro InstancedDrawMacro = FShaderMacro::CreateShaderMacro("INSTANCED_DRAW", "%d", RENDER_INSTANCED_SCENE_MESHES );
		const FShaderMacro InstanceCountMacro = FShaderMacro::CreateShaderMacro("INSTANCE_COUNT", "%d", MAX_INSTANCE_COUNT__SCENE_MESHES );
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
	for(size_t iRaster = 0  ; iRaster   < NUM_RASTER_OPTS      ; ++iRaster) 
	for(size_t iFaceCull = 0; iFaceCull < NUM_FACECULL_OPTS    ; ++iFaceCull)
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED     ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS   ; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS   ; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS   ; ++iOutTopo) 
	for(size_t iTessCull = 0; iTessCull < NUM_TESS_CULL_OPTIONS; ++iTessCull)
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS    ; ++iAlpha)
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
		const FShaderMacro InstancedDrawMacro = FShaderMacro::CreateShaderMacro("INSTANCED_DRAW", "%d", RENDER_INSTANCED_SCENE_MESHES );
		const FShaderMacro InstanceCountMacro = FShaderMacro::CreateShaderMacro("INSTANCE_COUNT", "%d", MAX_INSTANCE_COUNT__SCENE_MESHES );
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
	SCOPED_CPU_MARKER("GatherPSOLoadDescs_ShadowPass");
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
		const bool bNeedPS = (iDepthMode > 0 || iAlpha > 0);
		size_t NumShaders = bNeedPS ? 2 : 1; // VS-PS(optional)
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
		}
		else
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" };
		}
		if (bNeedPS)
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" };

		// macros
		const FShaderMacro InstancedDrawMacro = { "INSTANCED_DRAW", "1" };
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
			if (iAlpha == 1)
			{
				shdDesc.Macros.push_back({ "ENABLE_ALPHA_MASK", "1" });
			}
			if (iDepthMode == 1)
			{
				shdDesc.Macros.push_back({ "LINEAR_DEPTH", "1" });
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
