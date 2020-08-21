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
	

	Texture tex;

	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/residency#heap-resources
	// Heap creation can be slow; but it is optimized for background thread processing. 
	// It's recommended to create heaps on background threads to avoid glitching the render 
	// thread. In D3D12, multiple threads may safely call create routines concurrently.
	UploadHeap uploadHeap;
	{
		std::unique_lock<std::mutex> lk(mMtxUploadHeapCreation);
		uploadHeap.Create(mDevice.GetDevicePtr(), 1024 * MEGABYTE); // TODO: drive the heapsize through RendererSettings.ini
	}
	const std::string FileNameAndExtension = DirectoryUtil::GetFileNameFromPath(pFilePath);
	TextureCreateDesc tDesc(FileNameAndExtension);
	tDesc.pAllocator = mpAllocator;
	tDesc.pDevice = mDevice.GetDevicePtr();
	tDesc.pUploadHeap = &uploadHeap;
	tDesc.Desc = {};

	bool bSuccess = tex.CreateFromFile(tDesc, pFilePath);
	TextureID ID = INVALID_ID;
	if (bSuccess)
	{
		uploadHeap.UploadToGPUAndWait(mGFXQueue.pQueue);
		ID = AddTexture_ThreadSafe(std::move(tex));
		mLoadedTexturePaths[std::string(pFilePath)] = ID;
#if LOG_RESOURCE_CREATE
		Log::Info("VQRenderer::CreateTextureFromFile(): %s", pFilePath);
#endif
	}
	uploadHeap.Destroy(); // this is VERY expensive, TODO: find another solution.
	return ID;
}

TextureID VQRenderer::CreateTexture(const std::string& name, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES ResourceState, const void* pData)
{
	Texture tex;

	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/residency#heap-resources
	// Heap creation can be slow; but it is optimized for background thread processing. 
	// It's recommended to create heaps on background threads to avoid glitching the render 
	// thread. In D3D12, multiple threads may safely call create routines concurrently.
	UploadHeap uploadHeap;
	uploadHeap.Create(mDevice.GetDevicePtr(), 32 * MEGABYTE); // TODO: drive the heapsize through RendererSettings.ini

	TextureCreateDesc tDesc(name);
	tDesc.Desc = desc;
	tDesc.pAllocator = mpAllocator;
	tDesc.pDevice = mDevice.GetDevicePtr();
	tDesc.pUploadHeap = &uploadHeap;
	tDesc.ResourceState = ResourceState;

	tex.Create(tDesc, pData);

	uploadHeap.UploadToGPUAndWait(mGFXQueue.pQueue);
	uploadHeap.Destroy();

	return AddTexture_ThreadSafe(std::move(tex));
}
SRV_ID VQRenderer::CreateAndInitializeSRV(TextureID texID)
{
	SRV_ID Id = INVALID_ID;
	CBV_SRV_UAV SRV = {};
	if(texID != INVALID_ID)
	{
		std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);

		mHeapCBV_SRV_UAV.AllocDescriptor(1, &SRV);
		mTextures[texID].InitializeSRV(0, &SRV);
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
	CHECK_TEXTURE(mTextures, texID);
	CHECK_RESOURCE_VIEW(SRV, srvID);
	mTextures.at(texID).InitializeSRV(heapIndex, &mSRVs.at(srvID));
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

BufferID VQRenderer::CreateVertexBuffer(const FBufferDesc& desc)
{
	BufferID Id = INVALID_ID;
	VBV vbv;

	std::lock_guard <std::mutex> lk(mMtxStaticVBHeap);

	bool bSuccess = mStaticHeap_VertexBuffer.AllocVertexBuffer(desc.NumElements, desc.Stride, desc.pData, &vbv);
	if (bSuccess)
	{
		Id = LAST_USED_VBV_ID++;
		mVBVs[Id] = vbv;
	}
	else
		Log::Error("Couldn't allocate vertex buffer");
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


TextureID VQRenderer::AddTexture_ThreadSafe(Texture&& tex)
{
	TextureID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(mMtxTextures);
	Id = LAST_USED_TEXTURE_ID++;
	mTextures[Id] = tex;
	
	return Id;
}
void VQRenderer::DestroyTexture(TextureID texID)
{
	std::lock_guard<std::mutex> lk(mMtxTextures);
	mTextures.at(texID).Destroy();
	mTextures.erase(texID);
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

