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


static TextureID LAST_USED_TEXTURE_ID = 0;
static SRV_ID    LAST_USED_SRV_ID = 0;
static DSV_ID    LAST_USED_DSV_ID = 0;
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
	// TODO: check if file already loaded

	Texture tex;

	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/residency#heap-resources
	// Heap creation can be slow; but it is optimized for background thread processing. 
	// It's recommended to create heaps on background threads to avoid glitching the render 
	// thread. In D3D12, multiple threads may safely call create routines concurrently.
	UploadHeap uploadHeap;
	uploadHeap.Create(mDevice.GetDevicePtr(), 32 * MEGABYTE); // TODO: drive the heapsize through RendererSettings.ini

	TextureCreateDesc tDesc(DirectoryUtil::GetFileNameFromPath(pFilePath));
	tDesc.pAllocator = mpAllocator;
	tDesc.pDevice = mDevice.GetDevicePtr();
	tDesc.pUploadHeap = &uploadHeap;
	tDesc.Desc = {};

	tex.CreateFromFile(tDesc, pFilePath);

	uploadHeap.UploadToGPUAndWait(mGFXQueue.pQueue);
	uploadHeap.Destroy();

	return AddTexture_ThreadSafe(std::move(tex));
}

TextureID VQRenderer::CreateTexture(const std::string& name, const D3D12_RESOURCE_DESC& desc, const void* pData)
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

	tex.Create(tDesc, pData);

	uploadHeap.UploadToGPUAndWait(mGFXQueue.pQueue);
	uploadHeap.Destroy();

	return AddTexture_ThreadSafe(std::move(tex));
}
SRV_ID VQRenderer::CreateAndInitializeSRV(TextureID texID)
{

	SRV_ID Id = INVALID_ID;
	CBV_SRV_UAV SRV = {};

	{
		std::lock_guard<std::mutex> lk(mMtxSRVs);

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
		std::lock_guard<std::mutex> lk(mMtxDSVs);

		mHeapDSV.AllocDescriptor(1, &dsv);
		Id = LAST_USED_DSV_ID++;
		mTextures.at(texID).InitializeDSV(0, &dsv);
		mDSVs[Id] = dsv;
	}
	return Id;
}
DSV_ID VQRenderer::CreateDSV()
{
	DSV dsv = {};
	DSV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(mMtxDSVs);

	mHeapDSV.AllocDescriptor(1, &dsv);
	Id = LAST_USED_DSV_ID++;
	mDSVs[Id] = dsv;

	return Id;
}
void VQRenderer::InitializeDSV(DSV_ID dsvID, uint32 heapIndex, TextureID texID)
{
	assert(mTextures.find(texID) != mTextures.end());
	assert(mDSVs.find(dsvID) != mDSVs.end());

	mTextures.at(texID).InitializeDSV(heapIndex, &mDSVs.at(dsvID));
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

const DSV& VQRenderer::GetDepthStencilView(RTV_ID Id) const
{
	return mDSVs.at(Id);
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
	std::lock_guard<std::mutex> lk(mMtxSRVs);
	//mSRVs.at(srvID).Destroy(); // TODO
	mSRVs.erase(srvID);
}
void VQRenderer::DestroyDSV(DSV_ID dsvID)
{
	std::lock_guard<std::mutex> lk(mMtxDSVs);
	//mDSVs.at(dsvID).Destroy(); // TODO
	mDSVs.erase(dsvID);
}

