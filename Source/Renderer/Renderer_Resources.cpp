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
#include "Device.h"
#include "Texture.h"

#include "../Application/Window.h"

#include "../../Libs/VQUtils/Source/Log.h"
#include "../../Libs/VQUtils/Source/utils.h"
#include "../../Libs/VQUtils/Source/Timer.h"
#include "../../Libs/VQUtils/Source/Image.h"
#include "../../Libs/D3D12MA/src/Common.h"

#include <cassert>
#include <atomic>

using namespace Microsoft::WRL;
using namespace VQSystemInfo;

#ifdef _DEBUG
	#define ENABLE_DEBUG_LAYER      1
	#define ENABLE_VALIDATION_LAYER 1
#else
	#define ENABLE_DEBUG_LAYER      0
	#define ENABLE_VALIDATION_LAYER 0
#endif
#define LOG_CACHED_RESOURCES_ON_LOAD 0
#define LOG_RESOURCE_CREATE          1

// TODO: initialize from functions?
static TextureID LAST_USED_TEXTURE_ID = 0;
static SRV_ID    LAST_USED_SRV_ID = 0;
static UAV_ID    LAST_USED_UAV_ID = 0;
static DSV_ID    LAST_USED_DSV_ID = 0;
static RTV_ID    LAST_USED_RTV_ID = 0;
static BufferID  LAST_USED_VBV_ID = 0;
static BufferID  LAST_USED_IBV_ID = 0;
static BufferID  LAST_USED_CBV_ID = 0;


//
// PUBLIC
//

BufferID VQRenderer::CreateBuffer(const FBufferDesc& desc)
{
	BufferID Id = INVALID_ID;
	bool bSuccess = false;

	switch (desc.Type)
	{
	case VERTEX_BUFFER   : Id = CreateVertexBuffer(desc); break;
	case INDEX_BUFFER    : Id = CreateIndexBuffer(desc); break;
	case CONSTANT_BUFFER : Id = CreateConstantBuffer(desc); break;
	default              : assert(false); break; // shouldn't happen
	}
	return Id;
}

TextureID VQRenderer::CreateTextureFromFile(const char* pFilePath)
{
	// check if we've already loaded the texture
	auto it = mLoadedTexturePaths.find(pFilePath);
	if (it != mLoadedTexturePaths.end())
	{
#if LOG_CACHED_RESOURCES_ON_LOAD
		Log::Info("Texture already loaded: %s", pFilePath);
#endif
		return it->second;
	}
	// check path
	if (strlen(pFilePath) == 0)
	{
		Log::Warning("VQRenderer::CreateTextureFromFile: Empty FilePath provided");
		return INVALID_ID;
	}

	// --------------------------------------------------------

	TextureID ID = INVALID_ID;

	Timer t; t.Start();
	Texture tex;

	const std::string FileNameAndExtension = DirectoryUtil::GetFileNameFromPath(pFilePath);
	TextureCreateDesc tDesc(FileNameAndExtension);
	tDesc.pAllocator = mpAllocator;
	tDesc.pDevice = mDevice.GetDevicePtr();

	Image image;
	const bool bSuccess = Texture::ReadImageFromDisk(pFilePath, image);
	const bool bHDR = image.BytesPerPixel > 4;
	if (bSuccess)
	{
		// Fill D3D12 Descriptor
		tDesc.d3d12Desc = {};
		tDesc.d3d12Desc.Width = image.Width;
		tDesc.d3d12Desc.Height = image.Height;
		tDesc.d3d12Desc.Format = bHDR ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
		tDesc.d3d12Desc.DepthOrArraySize = 1;
		tDesc.d3d12Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		tDesc.d3d12Desc.Alignment = 0;
		tDesc.d3d12Desc.DepthOrArraySize = 1;
		tDesc.d3d12Desc.MipLevels = 1;
		tDesc.d3d12Desc.SampleDesc.Count = 1;
		tDesc.d3d12Desc.SampleDesc.Quality = 0;
		tDesc.d3d12Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		tDesc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		tex.Create(tDesc, image.pData);
		ID = AddTexture_ThreadSafe(std::move(tex));
		this->QueueTextureUpload(FTextureUploadDesc(std::move(image), ID, tDesc));

		this->StartTextureUploads();
		std::atomic<bool>& bResident = mTextures.at(ID).bResident;
		while (!bResident.load());  // busy wait here until the texture is made resident;

#if LOG_RESOURCE_CREATE
		Log::Info("VQRenderer::CreateTextureFromFile(): [%.2fs] %s", t.StopGetDeltaTimeAndReset(), pFilePath);
#endif
	}

	return ID;
}

TextureID VQRenderer::CreateTexture(const std::string& name, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES ResourceState, const void* pData)
{
	Texture tex;
	Timer t; t.Start();

	TextureCreateDesc tDesc(name);
	tDesc.d3d12Desc = desc;
	tDesc.pAllocator = mpAllocator;
	tDesc.pDevice = mDevice.GetDevicePtr();
	tDesc.ResourceState = ResourceState;

	tex.Create(tDesc, pData);

	TextureID ID = AddTexture_ThreadSafe(std::move(tex));
	if (pData)
	{
		this->QueueTextureUpload(FTextureUploadDesc(pData, ID, tDesc));

		this->StartTextureUploads();
		std::atomic<bool>& bResident = mTextures.at(ID).bResident;
		while (!bResident.load());  // busy wait here until the texture is made resident;
	}

	if (pData)
	{
#if LOG_RESOURCE_CREATE
		Log::Info("VQRenderer::CreateTexture(): [%.2fs] %s", t.StopGetDeltaTimeAndReset(), name.c_str());
#endif
	}

	return ID;
}
SRV_ID VQRenderer::CreateAndInitializeSRV(TextureID texID)
{
	SRV_ID Id = INVALID_ID;
	CBV_SRV_UAV SRV = {};
	if(texID != INVALID_ID)
	{
		std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);

		mHeapCBV_SRV_UAV.AllocDescriptor(1, &SRV);
		mTextures.at(texID).InitializeSRV(0, &SRV);
		Id = LAST_USED_SRV_ID++;
		mSRVs[Id] = SRV;
	}

	return Id;
}
DSV_ID VQRenderer::CreateAndInitializeDSV(TextureID texID)
{
	assert(mTextures.find(texID) != mTextures.end());

	DSV_ID Id = INVALID_ID;
	DSV dsv = {};
	{
		std::lock_guard<std::mutex> lk(this->mMtxDSVs);

		this->mHeapDSV.AllocDescriptor(1, &dsv);
		Id = LAST_USED_DSV_ID++;
		this->mTextures.at(texID).InitializeDSV(0, &dsv);
		this->mDSVs[Id] = dsv;
	}
	return Id;
}


DSV_ID VQRenderer::CreateDSV(uint NumDescriptors /*= 1*/)
{
	DSV dsv = {};
	DSV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxDSVs);

	this->mHeapDSV.AllocDescriptor(NumDescriptors, &dsv);
	Id = LAST_USED_DSV_ID++;
	this->mDSVs[Id] = dsv;

	return Id;
}
RTV_ID VQRenderer::CreateRTV(uint NumDescriptors /*= 1*/)
{
	RTV rtv = {};
	RTV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxRTVs);

	this->mHeapRTV.AllocDescriptor(NumDescriptors, &rtv);
	Id = LAST_USED_RTV_ID++;
	this->mRTVs[Id] = rtv;

	return Id;
}
SRV_ID VQRenderer::CreateSRV(uint NumDescriptors)
{
	CBV_SRV_UAV srv = {};
	SRV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxSRVs_CBVs_UAVs);

	this->mHeapCBV_SRV_UAV.AllocDescriptor(NumDescriptors, &srv);
	Id = LAST_USED_SRV_ID++;
	this->mSRVs[Id] = srv;

	return Id;
}
UAV_ID VQRenderer::CreateUAV(uint NumDescriptors)
{
	CBV_SRV_UAV uav = {};
	UAV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxSRVs_CBVs_UAVs);

	this->mHeapCBV_SRV_UAV.AllocDescriptor(NumDescriptors, &uav);
	Id = LAST_USED_UAV_ID++;
	this->mUAVs[Id] = uav;

	return Id;
}

#define CHECK_TEXTURE(map, id)\
if(map.find(id) == map.end())\
{\
Log::Error("Texture not created. Call mRenderer.CreateTexture() on the given Texture (id=%d)", id);\
}\
assert(map.find(id) != map.end());

#define CHECK_RESOURCE_VIEW(RV_t, id)\
if(m ## RV_t ## s.find(id) == m ## RV_t ## s.end())\
{\
Log::Error("Resource View <type=%s> was not allocated. Call mRenderer.Create%s() on the given %s (id=%d)", #RV_t,  #RV_t, #RV_t, id);\
}\
assert(m ## RV_t ## s.find(id) != m ## RV_t ## s.end());

void VQRenderer::InitializeDSV(DSV_ID dsvID, uint32 heapIndex, TextureID texID)
{
	CHECK_TEXTURE(mTextures, texID);
	CHECK_RESOURCE_VIEW(DSV, dsvID);
	
	assert(mDSVs.find(dsvID) != mDSVs.end());

	mTextures.at(texID).InitializeDSV(heapIndex, &mDSVs.at(dsvID));
}
void VQRenderer::InitializeSRV(SRV_ID srvID, uint heapIndex, TextureID texID)
{
	CHECK_RESOURCE_VIEW(SRV, srvID);
	if (texID != INVALID_ID)
	{
		CHECK_TEXTURE(mTextures, texID);
		mTextures.at(texID).InitializeSRV(heapIndex, &mSRVs.at(srvID));
	}
	else // init NULL SRV
	{
		// Describe and create 2 null SRVs. Null descriptors are needed in order 
		// to achieve the effect of an "unbound" resource.
		D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
		nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullSrvDesc.Texture2D.MipLevels = 1;
		nullSrvDesc.Texture2D.MostDetailedMip = 0;
		nullSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		mDevice.GetDevicePtr()->CreateShaderResourceView(nullptr, &nullSrvDesc, mSRVs.at(srvID).GetCPUDescHandle(heapIndex));
	}
}
void VQRenderer::InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID)
{
	CHECK_TEXTURE(mTextures, texID);
	CHECK_RESOURCE_VIEW(RTV, rtvID);
	mTextures.at(texID).InitializeRTV(heapIndex, &mRTVs.at(rtvID));
}

void VQRenderer::InitializeUAV(UAV_ID uavID, uint heapIndex, TextureID texID)
{
	CHECK_TEXTURE(mTextures, texID);
	CHECK_RESOURCE_VIEW(UAV, uavID);
	mTextures.at(texID).InitializeUAV(heapIndex, &mUAVs.at(uavID));
}

void VQRenderer::EnqueueShaderLoadTask(TaskID PSOLoadTaskID, const FShaderStageCompileDesc& ShaderStageCompileDesc)
{
	FShaderLoadTaskContext& taskContext = mLookup_ShaderLoadContext[PSOLoadTaskID];
	taskContext.LoadQueue.push(ShaderStageCompileDesc);
}

std::vector<std::shared_future<FShaderStageCompileResult>> VQRenderer::StartShaderLoadTasks(TaskID PSOLoadTaskID)
{
	if (mLookup_ShaderLoadContext.find(PSOLoadTaskID) == mLookup_ShaderLoadContext.end())
	{
		Log::Error("Couldn't find PSO Load Task [ID=%d]", PSOLoadTaskID);
		return {};
	}

	// get the task queue associated with the PSOLoadTaskID
	FShaderLoadTaskContext& taskCtx = mLookup_ShaderLoadContext.at(PSOLoadTaskID);
	std::queue<FShaderStageCompileDesc>& TaskQueue = taskCtx.LoadQueue;

	// get the load result future<>s
	std::vector<std::shared_future<FShaderStageCompileResult>> taskResults;

	// kickoff shader load workers
	while (!TaskQueue.empty())
	{
		FShaderStageCompileDesc compileDesc = std::move(TaskQueue.front());
		TaskQueue.pop();

		std::shared_future<FShaderStageCompileResult> ShaderCompileResult = mWorkers_ShaderLoad.AddTask([=]()
		{
			return this->LoadShader(compileDesc);
		});
		taskResults.push_back(std::move(ShaderCompileResult));
	}

	// return results
	return taskResults;
}

PSO_ID VQRenderer::LoadPSO(const FPSOLoadDesc& psoLoadDesc)
{
	static std::atomic<TaskID> LAST_USED_TASK_ID = 0;
	
	TaskID               PSOLoadTaskID = LAST_USED_TASK_ID.fetch_add(1);
	ID3D12PipelineState* pPSO = nullptr;

	HRESULT hr = {};
	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	D3D12_COMPUTE_PIPELINE_STATE_DESC  d3d12ComputePSODesc  = psoLoadDesc.D3D12ComputeDesc;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC d3d12GraphicsPSODesc = psoLoadDesc.D3D12GraphicsDesc;

	// calc PSO hash
	// TODO

	// check if PSO is cached
	const bool bCachedPSOExists = false;
	const bool bCacheDirty = false;
	const bool bComputePSO = std::find_if(RANGE(psoLoadDesc.ShaderStageCompileDescs) // check if ShaderModel has cs_*_*
			, [](const FShaderStageCompileDesc& desc) { return ShaderUtils::GetShaderStageEnumFromShaderModel(desc.ShaderModel) == EShaderStage::CS; }
		) != psoLoadDesc.ShaderStageCompileDescs.end();

	std::vector<std::shared_future<FShaderStageCompileResult>> shaderCompileResults;
	std::unordered_map<EShaderStage, ID3D12ShaderReflection*> ShaderReflections;

	if (!bCachedPSOExists || bCacheDirty) // compile PSO if no cache or cache dirty
	{
		// Prepare shader loading tasks for worker threads
		for (const FShaderStageCompileDesc& shaderStageDesc : psoLoadDesc.ShaderStageCompileDescs)
		{
			if (shaderStageDesc.FilePath.empty())
				continue;
			EnqueueShaderLoadTask(PSOLoadTaskID, shaderStageDesc);
		}

		// kickoff shader compiler workers
		shaderCompileResults = std::move( StartShaderLoadTasks(PSOLoadTaskID) );

		// SYNC POINT - wait for shaders to load / compile
		for (std::shared_future<FShaderStageCompileResult>& result : shaderCompileResults)
		{
			assert(result.valid());
			result.wait();
		}

		// Check for compile errors
		for (std::shared_future<FShaderStageCompileResult>& TaskResult : shaderCompileResults)
		{
			FShaderStageCompileResult ShaderCompileResult = TaskResult.get();
			if (ShaderCompileResult.pBlob == nullptr)
			{
				Log::Error("PSO Compile failed: PSOLoadTaskID=%d", PSOLoadTaskID);
				return INVALID_ID;
			}
		}

		// Compile the PSO using the shaders
		if (bComputePSO)
		{
			// Assign CS shader blob to PSODesc
			for (std::shared_future<FShaderStageCompileResult>& TaskResult : shaderCompileResults)
			{
				FShaderStageCompileResult ShaderCompileResult = TaskResult.get();

				CD3DX12_SHADER_BYTECODE ShaderByteCode(ShaderCompileResult.pBlob);
				d3d12ComputePSODesc.CS = ShaderByteCode;
			}

			// TODO: assign root signature

			// Compile PSO
			hr = pDevice->CreateComputePipelineState(&d3d12ComputePSODesc, IID_PPV_ARGS(&pPSO));
		}
		else // graphics PSO
		{
			// Assign shader blobs to PSODesc
			for (std::shared_future<FShaderStageCompileResult>& TaskResult : shaderCompileResults)
			{
				FShaderStageCompileResult ShaderCompileResult = TaskResult.get();

				CD3DX12_SHADER_BYTECODE ShaderByteCode(ShaderCompileResult.pBlob);
				switch (ShaderCompileResult.ShaderStageEnum)
				{
				case EShaderStage::VS: d3d12GraphicsPSODesc.VS = ShaderByteCode; break;
				case EShaderStage::GS: d3d12GraphicsPSODesc.GS = ShaderByteCode; break;
				case EShaderStage::DS: d3d12GraphicsPSODesc.DS = ShaderByteCode; break;
				case EShaderStage::HS: d3d12GraphicsPSODesc.HS = ShaderByteCode; break;
				case EShaderStage::PS: d3d12GraphicsPSODesc.PS = ShaderByteCode; break;
				}

				// reflect shader
				ID3D12ShaderReflection*& pReflection = ShaderReflections[ShaderCompileResult.ShaderStageEnum];
				D3DReflect(ShaderByteCode.pShaderBytecode, ShaderByteCode.BytecodeLength, IID_PPV_ARGS(&pReflection));
			}
			
			// assign input layout
			std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
			const bool bHasVS = ShaderReflections.find(EShaderStage::VS) != ShaderReflections.end();
			if (bHasVS)
			{
				inputLayout = ShaderUtils::ReflectInputLayoutFromVS(ShaderReflections.at(EShaderStage::VS));
				d3d12GraphicsPSODesc.InputLayout = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };
			}

			// TODO: assign root signature

			// Compile PSO
			hr = pDevice->CreateGraphicsPipelineState(&d3d12GraphicsPSODesc, IID_PPV_ARGS(&pPSO));
		}
	}
	else // load cached PSO
	{
		assert(false); // TODO
	}

	// Check PSO compile result
	assert(hr == S_OK);
	SetName(pPSO, psoLoadDesc.PSOName.c_str());

	// release reflections
	for(auto it = ShaderReflections.begin(); it != ShaderReflections.end(); ++it)
	{
		it->second->Release();
	}
	ShaderReflections.clear();

	return PSO_ID();
}

FShaderStageCompileResult VQRenderer::LoadShader(const FShaderStageCompileDesc& ShaderStageCompileDesc)
{
	using namespace ShaderUtils;
	constexpr const char* SHADER_BINARY_EXTENSION = ".bin";

	const std::string ShaderSourcePath = StrUtil::UnicodeToASCII<256>( ShaderStageCompileDesc.FilePath.c_str() ); 

	// calculate shader hash
	const size_t ShaderHash = GeneratePreprocessorDefinitionsHash(ShaderStageCompileDesc.Macros);

	// determine cached shader file name
	const std::string cacheFileName = ShaderStageCompileDesc.Macros.empty()
		? DirectoryUtil::GetFileNameFromPath(ShaderSourcePath) + SHADER_BINARY_EXTENSION
		: DirectoryUtil::GetFileNameFromPath(ShaderSourcePath) + "_" + std::to_string(ShaderHash) + SHADER_BINARY_EXTENSION;

	// determine cached shader file path
	const std::string CachedShaderBinaryPath = "";// Application::s_ShaderCacheDirectory + "\\" + cacheFileName;

	// decide whether to use shader cache or compile from source
	const bool bUseCachedShaders = DirectoryUtil::FileExists(CachedShaderBinaryPath) 
		&& !ShaderUtils::IsCacheDirty(ShaderSourcePath, CachedShaderBinaryPath)
		&& false; // TODO: remove this

	// load the shader d3dblob
	FShaderStageCompileResult Result = {};
	ID3DBlob*& pShaderBlob = Result.pBlob;
	Result.ShaderStageEnum = ShaderUtils::GetShaderStageEnumFromShaderModel(ShaderStageCompileDesc.ShaderModel);

	if (bUseCachedShaders)
	{
		pShaderBlob = CompileFromCachedBinary(CachedShaderBinaryPath);
	}
	else
	{
		std::string errMsg;
		pShaderBlob = CompileFromSource(ShaderStageCompileDesc, errMsg);
		const bool bCompileSuccessful = pShaderBlob != nullptr;
		if (bCompileSuccessful)
		{
			CacheShaderBinary(CachedShaderBinaryPath, pShaderBlob);
		}
		else
		{
			Log::Error(errMsg);
			return Result;
		}
	}

	return Result;
}

BufferID VQRenderer::CreateVertexBuffer(const FBufferDesc& desc)
{
	BufferID Id = INVALID_ID;
	VBV vbv = {};

	std::lock_guard <std::mutex> lk(mMtxStaticVBHeap);

	bool bSuccess = mStaticHeap_VertexBuffer.AllocVertexBuffer(desc.NumElements, desc.Stride, desc.pData, &vbv);
	if (bSuccess)
	{
		Id = LAST_USED_VBV_ID++;
		mVBVs[Id] = vbv;
	}
	else
		Log::Error("VQRenderer: Couldn't allocate vertex buffer");
	return Id;
}
BufferID VQRenderer::CreateIndexBuffer(const FBufferDesc& desc)
{
	BufferID Id = INVALID_ID;
	IBV ibv;

	std::lock_guard<std::mutex> lk(mMtxStaticIBHeap);

	bool bSuccess = mStaticHeap_IndexBuffer.AllocIndexBuffer(desc.NumElements, desc.Stride, desc.pData, &ibv);
	if (bSuccess)
	{
		Id = LAST_USED_IBV_ID++;
		mIBVs[Id] = ibv;
	}
	else
		Log::Error("Couldn't allocate index buffer");
	return Id;
}
BufferID VQRenderer::CreateConstantBuffer(const FBufferDesc& desc)
{
	BufferID Id = INVALID_ID;

	assert(false);
	return Id;
}


const VBV& VQRenderer::GetVertexBufferView(BufferID Id) const
{
	//assert(Id < mVBVs.size() && Id != INVALID_ID);
	return mVBVs.at(Id);
}

const IBV& VQRenderer::GetIndexBufferView(BufferID Id) const
{
	//assert(Id < mIBVs.size() && Id != INVALID_ID);
	return mIBVs.at(Id);
}
const CBV_SRV_UAV& VQRenderer::GetShaderResourceView(SRV_ID Id) const
{
	//assert(Id < mSRVs.size() && Id != INVALID_ID);
	return mSRVs.at(Id);
}

const CBV_SRV_UAV& VQRenderer::GetUnorderedAccessView(UAV_ID Id) const
{
	return mUAVs.at(Id);
}

const DSV& VQRenderer::GetDepthStencilView(RTV_ID Id) const
{
	return mDSVs.at(Id);
}
const RTV& VQRenderer::GetRenderTargetView(RTV_ID Id) const
{
	return mRTVs.at(Id);
}
const ID3D12Resource* VQRenderer::GetTextureResource(TextureID Id) const
{
	CHECK_TEXTURE(mTextures, Id);
	return mTextures.at(Id).GetResource();
}
ID3D12Resource* VQRenderer::GetTextureResource(TextureID Id) 
{
	CHECK_TEXTURE(mTextures, Id);
	return mTextures.at(Id).GetResource();
}

void VQRenderer::QueueTextureUpload(const FTextureUploadDesc& desc)
{
	std::unique_lock<std::mutex> lk(mMtxTextureUploadQueue);
	mTextureUploadQueue.push(desc);
}

void VQRenderer::ProcessTextureUploadQueue()
{
	std::unique_lock<std::mutex> lk(mMtxTextureUploadQueue);
	if (mTextureUploadQueue.empty())
		return;

	ID3D12GraphicsCommandList* pCmd = mHeapUpload.GetCommandList();
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	std::vector<std::atomic<bool>*> vTexResidentBools;
	std::vector<Image> vImages;

	while (!mTextureUploadQueue.empty())
	{
		FTextureUploadDesc desc = std::move(mTextureUploadQueue.front());
		mTextureUploadQueue.pop();

		ID3D12Resource* pResc = GetTextureResource(desc.id);
		const void* pData = desc.img.pData ? desc.img.pData : desc.pData;
		assert(pData);

		const UINT64 UploadBufferSize = GetRequiredIntermediateSize(pResc, 0, 1);

		UINT8* pUploadBufferMem = mHeapUpload.Suballocate(SIZE_T(UploadBufferSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
		if (pUploadBufferMem == NULL)
		{
			// We ran out of mem in the upload heap, flush it and try allocating mem from it again
			mHeapUpload.UploadToGPUAndWait();
			pUploadBufferMem = mHeapUpload.Suballocate(SIZE_T(UploadBufferSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
			assert(pUploadBufferMem);
		}

		UINT64 UplHeapSize;
		uint32_t num_rows = {};
		UINT64 row_size_in_bytes = {};
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedTex2D = {};
		D3D12_RESOURCE_DESC d3dDesc = {};

		pDevice->GetCopyableFootprints(&desc.desc.d3d12Desc, 0, 1, 0, &placedTex2D, &num_rows, &row_size_in_bytes, &UplHeapSize);
		placedTex2D.Offset += UINT64(pUploadBufferMem - mHeapUpload.BasePtr());

		// copy data row by row
		for (uint32_t y = 0; y < num_rows; y++)
		{
			const UINT64 UploadMemOffset = y * placedTex2D.Footprint.RowPitch;
			const UINT64   DataMemOffset = y * row_size_in_bytes;
			memcpy(pUploadBufferMem + UploadMemOffset, (UINT8*)pData + DataMemOffset, row_size_in_bytes);
		}

		CD3DX12_TEXTURE_COPY_LOCATION Dst(pResc, 0);
		CD3DX12_TEXTURE_COPY_LOCATION Src(mHeapUpload.GetResource(), placedTex2D);
		pCmd->CopyTextureRegion(&Dst, 0, 0, 0, &Src, NULL);

		D3D12_RESOURCE_BARRIER textureBarrier = {};
		textureBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		textureBarrier.Transition.pResource = pResc;
		textureBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		textureBarrier.Transition.StateAfter = desc.desc.ResourceState;
		textureBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		pCmd->ResourceBarrier(1, &textureBarrier);

		assert(mTextures.find(desc.id) != mTextures.end());
		{
			std::unique_lock<std::mutex> lk(mMtxTextures);
			Texture& tex = mTextures.at(desc.id);
			vTexResidentBools.push_back(&tex.bResident);
		}

		if (desc.img.pData)
		{
			vImages.push_back(std::move(desc.img));
		}
	}

	mHeapUpload.UploadToGPUAndWait();

	for (std::atomic<bool>* pbResident : vTexResidentBools)
		pbResident->store(true);

	for (Image& img : vImages)
		img.Destroy(); // free the image memory
}

void VQRenderer::TextureUploadThread_Main()
{
	while (!mbExitUploadThread)
	{
		mSignal_UploadThreadWorkReady.Wait([&]() { return mbExitUploadThread.load() || !mTextureUploadQueue.empty(); });
		
		if (mbExitUploadThread)
			break;

		this->ProcessTextureUploadQueue();
	}
}


TextureID VQRenderer::AddTexture_ThreadSafe(Texture&& tex)
{
	TextureID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(mMtxTextures);
	Id = LAST_USED_TEXTURE_ID++;

	mTextures[Id] = std::move(tex);
	
	return Id;
}
void VQRenderer::DestroyTexture(TextureID texID)
{
	// Remove texID
	std::lock_guard<std::mutex> lk(mMtxTextures);
	mTextures.at(texID).Destroy();
	mTextures.erase(texID);

	// Remove texture path from cache
	std::string texPath = "";
	bool bTexturePathRegistered = false;
	for (const auto& path_id_pair : mLoadedTexturePaths)
	{
		if (path_id_pair.second == texID)
		{
			texPath = path_id_pair.first;
			bTexturePathRegistered = true;
			break;
		}
	}
	if (bTexturePathRegistered)
		mLoadedTexturePaths.erase(texPath);
}
void VQRenderer::DestroySRV(SRV_ID srvID)
{
	std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);
	//mSRVs.at(srvID).Destroy(); // TODO
	mSRVs.erase(srvID);
}
void VQRenderer::DestroyDSV(DSV_ID dsvID)
{
	std::lock_guard<std::mutex> lk(mMtxDSVs);
	//mDSVs.at(dsvID).Destroy(); // TODO
	mDSVs.erase(dsvID);
}

