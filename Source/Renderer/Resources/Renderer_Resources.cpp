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

#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Renderer/Renderer.h"
#include "Core/Device.h"
#include "Texture.h"

#include "Engine/GPUMarker.h"
#include "Engine/Core/Window.h"

#include "Libs/VQUtils/Source/Log.h"
#include "Libs/VQUtils/Source/utils.h"
#include "Libs/VQUtils/Source/Timer.h"
#include "Libs/VQUtils/Source/Image.h"
#include "Libs/D3D12MA/src/Common.h"
#include "Libs/DirectXCompiler/inc/dxcapi.h"

#include <cassert>
#include <atomic>

using namespace Microsoft::WRL;
using namespace VQSystemInfo;

#ifdef _DEBUG
	#define ENABLE_DEBUG_LAYER       1
	#define ENABLE_VALIDATION_LAYER  1
#else
	#define ENABLE_DEBUG_LAYER       0
	#define ENABLE_VALIDATION_LAYER  0
#endif
#define LOG_CACHED_RESOURCES_ON_LOAD 0
#define LOG_RESOURCE_CREATE          1


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



// TODO: initialize from functions?
static TextureID LAST_USED_TEXTURE_ID        = 1;
static SRV_ID    LAST_USED_SRV_ID            = 1;
static UAV_ID    LAST_USED_UAV_ID            = 1;
static DSV_ID    LAST_USED_DSV_ID            = 1;
static RTV_ID    LAST_USED_RTV_ID            = 1;
static BufferID  LAST_USED_VBV_ID            = 1;
static BufferID  LAST_USED_IBV_ID            = 1;
static BufferID  LAST_USED_CBV_ID            = 1;

static UINT GetDXGIFormatByteSize(DXGI_FORMAT format)
{
	switch (format)
	{
	case(DXGI_FORMAT_A8_UNORM):
		return 1;

	case(DXGI_FORMAT_R10G10B10A2_TYPELESS):
	case(DXGI_FORMAT_R10G10B10A2_UNORM):
	case(DXGI_FORMAT_R10G10B10A2_UINT):
	case(DXGI_FORMAT_R11G11B10_FLOAT):
	case(DXGI_FORMAT_R8G8B8A8_TYPELESS):
	case(DXGI_FORMAT_R8G8B8A8_UNORM):
	case(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB):
	case(DXGI_FORMAT_R8G8B8A8_UINT):
	case(DXGI_FORMAT_R8G8B8A8_SNORM):
	case(DXGI_FORMAT_R8G8B8A8_SINT):
	case(DXGI_FORMAT_B8G8R8A8_UNORM):
	case(DXGI_FORMAT_B8G8R8X8_UNORM):
	case(DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM):
	case(DXGI_FORMAT_B8G8R8A8_TYPELESS):
	case(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB):
	case(DXGI_FORMAT_B8G8R8X8_TYPELESS):
	case(DXGI_FORMAT_B8G8R8X8_UNORM_SRGB):
	case(DXGI_FORMAT_R16G16_TYPELESS):
	case(DXGI_FORMAT_R16G16_FLOAT):
	case(DXGI_FORMAT_R16G16_UNORM):
	case(DXGI_FORMAT_R16G16_UINT):
	case(DXGI_FORMAT_R16G16_SNORM):
	case(DXGI_FORMAT_R16G16_SINT):
	case(DXGI_FORMAT_R32_TYPELESS):
	case(DXGI_FORMAT_D32_FLOAT):
	case(DXGI_FORMAT_R32_FLOAT):
	case(DXGI_FORMAT_R32_UINT):
	case(DXGI_FORMAT_R32_SINT):
		return 4;

	case(DXGI_FORMAT_BC1_TYPELESS):
	case(DXGI_FORMAT_BC1_UNORM):
	case(DXGI_FORMAT_BC1_UNORM_SRGB):
	case(DXGI_FORMAT_BC4_TYPELESS):
	case(DXGI_FORMAT_BC4_UNORM):
	case(DXGI_FORMAT_BC4_SNORM):
	case(DXGI_FORMAT_R16G16B16A16_FLOAT):
	case(DXGI_FORMAT_R16G16B16A16_TYPELESS):
		return 8;

	case(DXGI_FORMAT_BC2_TYPELESS):
	case(DXGI_FORMAT_BC2_UNORM):
	case(DXGI_FORMAT_BC2_UNORM_SRGB):
	case(DXGI_FORMAT_BC3_TYPELESS):
	case(DXGI_FORMAT_BC3_UNORM):
	case(DXGI_FORMAT_BC3_UNORM_SRGB):
	case(DXGI_FORMAT_BC5_TYPELESS):
	case(DXGI_FORMAT_BC5_UNORM):
	case(DXGI_FORMAT_BC5_SNORM):
	case(DXGI_FORMAT_BC6H_TYPELESS):
	case(DXGI_FORMAT_BC6H_UF16):
	case(DXGI_FORMAT_BC6H_SF16):
	case(DXGI_FORMAT_BC7_TYPELESS):
	case(DXGI_FORMAT_BC7_UNORM):
	case(DXGI_FORMAT_BC7_UNORM_SRGB):
	case(DXGI_FORMAT_R32G32B32A32_FLOAT):
	case(DXGI_FORMAT_R32G32B32A32_TYPELESS):
		return 16;

	default:
		assert(false);
		break;
	}
	return 0;
}
static D3D12_UAV_DIMENSION GetUAVDimensionFromResourceDimension(D3D12_RESOURCE_DIMENSION rscDim, bool bArrayOrCubemap)
{
	switch (rscDim)
	{
	case D3D12_RESOURCE_DIMENSION_UNKNOWN: return D3D12_UAV_DIMENSION_UNKNOWN;
	case D3D12_RESOURCE_DIMENSION_BUFFER: return D3D12_UAV_DIMENSION_BUFFER;
	case D3D12_RESOURCE_DIMENSION_TEXTURE1D: return bArrayOrCubemap ? D3D12_UAV_DIMENSION_TEXTURE1DARRAY : D3D12_UAV_DIMENSION_TEXTURE1D;
	case D3D12_RESOURCE_DIMENSION_TEXTURE2D: return bArrayOrCubemap ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
	case D3D12_RESOURCE_DIMENSION_TEXTURE3D: return D3D12_UAV_DIMENSION_TEXTURE3D;
	}
	return D3D12_UAV_DIMENSION_UNKNOWN;
}


// -----------------------------------------------------------------------------------------------------------------
//
// RESOURCE CREATION
//
// -----------------------------------------------------------------------------------------------------------------
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

TextureID VQRenderer::CreateTextureFromFile(const char* pFilePath, bool bCheckAlpha, bool bGenerateMips /*= false*/)
{
	FTextureRequest desc;
	desc.Name = DirectoryUtil::GetFileNameFromPath(pFilePath);
	desc.FilePath = pFilePath;
	desc.bGenerateMips = bGenerateMips;
	return mTextureManager.CreateTexture(desc, bCheckAlpha);
}

TextureID VQRenderer::CreateTexture(const FTextureRequest& desc, bool bCheckAlpha)
{
	return mTextureManager.CreateTexture(desc, bCheckAlpha);
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

void VQRenderer::DestroyTexture(TextureID& texID) { mTextureManager.DestroyTexture(texID); }


#if 0
const Texture& VQRenderer::GetTexture_ThreadSafe(TextureID Id) const
{
	std::lock_guard<std::mutex> lk(mMtxTextures);
	return mTextures.at(Id);
}

Texture& VQRenderer::GetTexture_ThreadSafe(TextureID Id)
{
	std::lock_guard<std::mutex> lk(mMtxTextures);
	return mTextures.at(Id);
}
#endif

// -----------------------------------------------------------------------------------------------------------------
//
// RESOURCE VIEW CREATION
//
// -----------------------------------------------------------------------------------------------------------------
SRV_ID VQRenderer::AllocateAndInitializeSRV(TextureID texID)
{
	SRV_ID srvID = AllocateSRV(1);
	InitializeSRV(srvID, 0, texID);
	return srvID;
}
DSV_ID VQRenderer::AllocateAndInitializeDSV(TextureID texID)
{
	DSV_ID Id = AllocateDSV(1);
	InitializeDSV(Id, 0, texID);
	return Id;
}


DSV_ID VQRenderer::AllocateDSV(uint NumDescriptors /*= 1*/)
{
	DSV dsv = {};
	DSV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxDSVs);

	this->mHeapDSV.AllocateDescriptor(NumDescriptors, &dsv);
	Id = LAST_USED_DSV_ID++;
	this->mDSVs[Id] = dsv;

	return Id;
}
RTV_ID VQRenderer::AllocateRTV(uint NumDescriptors /*= 1*/)
{
	RTV rtv = {};
	RTV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxRTVs);

	this->mHeapRTV.AllocateDescriptor(NumDescriptors, &rtv);
	Id = LAST_USED_RTV_ID++;
	this->mRTVs[Id] = rtv;

	return Id;
}
SRV_ID VQRenderer::AllocateSRV(uint NumDescriptors)
{
	CBV_SRV_UAV srv = {};
	SRV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxSRVs_CBVs_UAVs);

	this->mHeapCBV_SRV_UAV.AllocateDescriptor(NumDescriptors, &srv);
	Id = LAST_USED_SRV_ID++;
	this->mSRVs[Id] = srv;

	return Id;
}
UAV_ID VQRenderer::AllocateUAV(uint NumDescriptors)
{
	CBV_SRV_UAV uav = {};
	UAV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxSRVs_CBVs_UAVs);

	this->mHeapCBV_SRV_UAV.AllocateDescriptor(NumDescriptors, &uav);
	Id = LAST_USED_UAV_ID++;
	this->mUAVs[Id] = uav;

	return Id;
}

void VQRenderer::InitializeDSV(DSV_ID dsvID, uint32 heapIndex, TextureID texID, int ArraySlice /*= 0*/)
{
	CHECK_RESOURCE_VIEW(DSV, dsvID);
	auto itDSV = mDSVs.find(dsvID);
	assert(itDSV != mDSVs.end());
	DSV& dsv = itDSV->second;

	const FTexture* pTexture = mTextureManager.GetTexture(texID);
	assert(pTexture);

	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	assert(pDevice);

	mTextureManager.WaitForTexture(texID);
	const D3D12_RESOURCE_DESC texDesc = pTexture->Resource->GetDesc();

	D3D12_DEPTH_STENCIL_VIEW_DESC DSViewDesc = {};
	if (texDesc.Format == DXGI_FORMAT_R32_TYPELESS)
		DSViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
	if (texDesc.Format == DXGI_FORMAT_R24G8_TYPELESS)
		DSViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	if (texDesc.SampleDesc.Count == 1)
	{
		if (texDesc.DepthOrArraySize == 1)
		{
			DSViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			DSViewDesc.Texture2D.MipSlice = 0;
		}
		else
		{
			DSViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			DSViewDesc.Texture2DArray.MipSlice = 0;
			DSViewDesc.Texture2DArray.FirstArraySlice = ArraySlice;
			DSViewDesc.Texture2DArray.ArraySize = pTexture->IsCubemap ? (6 - ArraySlice % 6) : 1;
		}
	}
	else
	{
		DSViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
	}

	pDevice->CreateDepthStencilView(pTexture->Resource, &DSViewDesc, dsv.GetCPUDescHandle(heapIndex));
}

void VQRenderer::InitializeNullSRV(SRV_ID srvID, uint heapIndex, UINT ShaderComponentMapping)
{
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
	nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullSrvDesc.Shader4ComponentMapping = ShaderComponentMapping;
	nullSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	nullSrvDesc.Texture2D.MipLevels = 1;
	nullSrvDesc.Texture2D.MostDetailedMip = 0;
	nullSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	{
		std::lock_guard<std::mutex> lk(this->mMtxSRVs_CBVs_UAVs);
		pDevice->CreateShaderResourceView(nullptr, &nullSrvDesc, mSRVs.at(srvID).GetCPUDescHandle(heapIndex));
	}
}

void VQRenderer::InitializeSRV(SRV_ID srvID, uint heapIndex, TextureID texID, bool bInitAsArrayView /*= false*/, bool bInitAsCubeView /*= false*/, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc /*=nullptr*/, UINT ShaderComponentMapping)
{
	CHECK_RESOURCE_VIEW(SRV, srvID);
	if (mSRVs.find(srvID) == mSRVs.end())
	{
		Log::Error("SRV Not allocated for texID = %d", texID);
	}

	if (texID != INVALID_ID)
	{
		mTextureManager.WaitForTexture(texID);
	}

	const FTexture* pTexture = texID == INVALID_ID ? nullptr : mTextureManager.GetTexture(texID);

	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	assert(pDevice);

	if (pTexture)
	{
		std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);
		if (bInitAsCubeView)
		{
			if (!pTexture->IsCubemap)
			{
				Log::Warning("Cubemap view requested on a non-cubemap resource");
			}
			assert(pTexture->IsCubemap); // could this be an actual use case: e.g. view array[6] as cubemap?
		}

		//
		// TODO: bool bInitAsArrayView needed so that InitializeSRV() can initialize a per-face SRV of a cubemap
		//
		ID3D12Resource* pResource = pTexture->Resource;

		if (!pResource)
		{
			InitializeNullSRV(srvID, heapIndex, ShaderComponentMapping);
			return;
		}

		D3D12_RESOURCE_DESC resourceDesc = pResource->GetDesc();
		const int NumCubes = pTexture->IsCubemap ? resourceDesc.DepthOrArraySize / 6 : 0;
		const bool bInitializeAsCubemapView = pTexture->IsCubemap && bInitAsCubeView;
		if (bInitializeAsCubemapView)
		{
			const bool bArraySizeMultipleOfSix = resourceDesc.DepthOrArraySize % 6 == 0;
			if (!bArraySizeMultipleOfSix)
			{
				Log::Warning("Cubemap Texture's array size is not multiple of 6");
			}
			assert(bArraySizeMultipleOfSix);
		}

		SRV& srv = mSRVs.at(srvID);
		const bool bBufferSRV = resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
		const bool bCustomComponentMappingSpecified = ShaderComponentMapping != D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		if (pTexture->IsTypeless || bCustomComponentMappingSpecified || pTexture->IsCubemap || bBufferSRV)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			int mipLevel = 0;// TODO resourceDesc.MipLevels;
			int arraySize = resourceDesc.DepthOrArraySize; // TODO
			int firstArraySlice = heapIndex;
			//assert(mipLevel > 0);

			const bool bDepthSRV = resourceDesc.Format == DXGI_FORMAT_R32_TYPELESS;
			const bool bMSAA = resourceDesc.SampleDesc.Count != 1;
			const bool bArraySRV = /*bInitAsArrayView &&*/ resourceDesc.DepthOrArraySize > 1;

			if (bDepthSRV)
			{
				srvDesc.Format = DXGI_FORMAT_R32_FLOAT; //special case for the depth buffer
			}
			else
			{
				D3D12_RESOURCE_DESC desc = pResource->GetDesc();
				srvDesc.Format = desc.Format;
			}

			if (bMSAA)
			{
				assert(!pTexture->IsCubemap); // no need so far, implement MS cubemaps if this is hit.
				if (bArraySRV)
				{
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
					srvDesc.Texture2DMSArray.FirstArraySlice = (firstArraySlice == -1) ? 0 : firstArraySlice;
					srvDesc.Texture2DMSArray.ArraySize = (arraySize == -1) ? resourceDesc.DepthOrArraySize : arraySize;
					assert(mipLevel == -1);
				}
				else
				{
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
				}
			}
			else // non-MSAA Texture SRV
			{
				if (bArraySRV)
				{
					srvDesc.ViewDimension = bInitAsCubeView ? D3D12_SRV_DIMENSION_TEXTURECUBEARRAY : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
					if (bInitAsCubeView)
					{
						srvDesc.TextureCubeArray.MipLevels = resourceDesc.MipLevels;
						srvDesc.TextureCubeArray.ResourceMinLODClamp = 0;
						srvDesc.TextureCubeArray.MostDetailedMip = 0;
						srvDesc.TextureCubeArray.First2DArrayFace = 0;
						srvDesc.TextureCubeArray.NumCubes = NumCubes;
					}
					else
					{
						srvDesc.Texture2DArray.MostDetailedMip = (mipLevel == -1) ? 0 : mipLevel;
						srvDesc.Texture2DArray.MipLevels = (mipLevel == -1) ? pTexture->MipCount : 1;
						srvDesc.Texture2DArray.FirstArraySlice = (firstArraySlice == -1) ? 0 : firstArraySlice;
						srvDesc.Texture2DArray.ArraySize = arraySize - srvDesc.Texture2DArray.FirstArraySlice;
					}
				}

				else // single SRV
				{
					srvDesc.ViewDimension = bInitAsCubeView ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
					if (bInitAsCubeView)
					{
						srvDesc.TextureCube.MostDetailedMip = 0;
						srvDesc.TextureCube.MipLevels = resourceDesc.MipLevels;
						srvDesc.TextureCube.ResourceMinLODClamp = 0;
					}
					else
					{
						srvDesc.Texture2D.MostDetailedMip = mipLevel;
						srvDesc.Texture2D.MipLevels = (mipLevel == -1) ? pTexture->MipCount : 1;
					}
				}
			}

			srvDesc.Shader4ComponentMapping = ShaderComponentMapping;

			// Create array SRV
			if (bArraySRV)
			{
				if (bInitializeAsCubemapView)
				{
					for (int cube = 0; cube < NumCubes; ++cube)
					{
						srvDesc.TextureCubeArray.First2DArrayFace = cube;
						srvDesc.TextureCubeArray.NumCubes = NumCubes - cube;
						pDevice->CreateShaderResourceView(pResource, &srvDesc, srv.GetCPUDescHandle(heapIndex + cube));
					}
				}
				else
				{
					//for (int i = 0; i < resourceDesc.DepthOrArraySize; ++i)
					{
						srvDesc.Texture2DArray.FirstArraySlice = heapIndex;
						srvDesc.Texture2DArray.ArraySize = resourceDesc.DepthOrArraySize - heapIndex;
						pDevice->CreateShaderResourceView(pResource, &srvDesc, srv.GetCPUDescHandle(0 /*+ i*/));
					}
				}
			}

			// Create single SRV
			else
			{
				pDevice->CreateShaderResourceView(pResource, &srvDesc, srv.GetCPUDescHandle(0));
			}
		}
		else
		{
			if (!pSRVDesc && bBufferSRV)
			{
				Log::Error("AllocateSRV() for RWBuffer cannot have null SRVDescriptor, specify a SRV "
					"format for a buffer (as it has no format from the pResource's point of view).");
				return;
			}
			pDevice->CreateShaderResourceView(pResource, pSRVDesc, srv.GetCPUDescHandle(heapIndex));
		}

		Log::Info("InitializeSRV %d[%d] for Texture %d | desc_handl = %ul", srvID, heapIndex, texID, srv.GetGPUDescHandle(heapIndex));
	}
	else // init NULL SRV
	{
		// Describe and create null SRV. Null descriptors are needed in order 
		// to achieve the effect of an "unbound" resource.
		InitializeNullSRV(srvID, heapIndex, ShaderComponentMapping);
	}
}
void VQRenderer::InitializeSRV(SRV_ID srvID, uint heapIndex, D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc)
{
	std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);
	mDevice.GetDevicePtr()->CreateShaderResourceView(nullptr, &srvDesc, mSRVs.at(srvID).GetCPUDescHandle(heapIndex));
}
void VQRenderer::InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID)
{
	CHECK_RESOURCE_VIEW(RTV, rtvID);

	const FTexture* pTexture = mTextureManager.GetTexture(texID);
	assert(pTexture);

	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	assert(pDevice);

	mTextureManager.WaitForTexture(texID);
	D3D12_RENDER_TARGET_VIEW_DESC* pRTVDesc = nullptr; // unused
	pDevice->CreateRenderTargetView(pTexture->Resource, pRTVDesc, mRTVs.at(rtvID).GetCPUDescHandle(heapIndex));
}

void VQRenderer::InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID, int arraySlice, int mipLevel)
{
	CHECK_RESOURCE_VIEW(RTV, rtvID);
	
	const FTexture* pTexture = mTextureManager.GetTexture(texID);
	assert(pTexture);

	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	assert(pDevice);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	const D3D12_RESOURCE_DESC rscDesc = pTexture->Resource->GetDesc();

	rtvDesc.Format = rscDesc.Format;
	
	const bool& bCubemap = pTexture->IsCubemap;
	const bool  bArray   = bCubemap ? (rscDesc.DepthOrArraySize/6 > 1) : rscDesc.DepthOrArraySize > 1;
	const bool  bMSAA    = rscDesc.SampleDesc.Count > 1;

	assert(arraySlice < rscDesc.DepthOrArraySize);
	assert(mipLevel < rscDesc.MipLevels);

	if (bArray || bCubemap)
	{	
		if (bMSAA)
		{
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
			rtvDesc.Texture2DMSArray.ArraySize = rscDesc.DepthOrArraySize - arraySlice;
			rtvDesc.Texture2DMSArray.FirstArraySlice = arraySlice;
		}
		else
		{
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			rtvDesc.Texture2DArray.ArraySize = rscDesc.DepthOrArraySize - arraySlice;
			rtvDesc.Texture2DArray.FirstArraySlice = arraySlice;
			rtvDesc.Texture2DArray.MipSlice  = mipLevel;
		}
	}

	else // non-array
	{
		if (bMSAA)
		{
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
		}
		else
		{
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Texture2D.MipSlice = mipLevel;
		}
	}

	mDevice.GetDevicePtr()->CreateRenderTargetView(pTexture->Resource, &rtvDesc, mRTVs.at(rtvID).GetCPUDescHandle(heapIndex));
}

void VQRenderer::InitializeUAVForBuffer(UAV_ID uavID, uint heapIndex, TextureID texID, DXGI_FORMAT bufferViewFormatOverride)
{
	CHECK_RESOURCE_VIEW(UAV, uavID);

	const FTexture* pTexture = mTextureManager.GetTexture(texID);
	assert(pTexture);

	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	assert(pDevice);

	mTextureManager.WaitForTexture(texID);
	const D3D12_RESOURCE_DESC rscDesc = pTexture->Resource->GetDesc();
	
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = GetUAVDimensionFromResourceDimension(rscDesc.Dimension, false);
	assert(uavDesc.ViewDimension == D3D12_UAV_DIMENSION_BUFFER); // this function must be called on Buffer resources
	
	// https://docs.microsoft.com/en-gb/windows/win32/direct3d12/typed-unordered-access-view-loads
	// tex.mFormat will be UNKNOWN if it is created for a buffer
	// This will trigger a device remove if we want to use a typed UAV (buffer).
	// In this case, we'll need to provide the data type of the RWBuffer to create the UAV.
	uavDesc.Format = bufferViewFormatOverride;

	// tex.Width should be representing the Bytes of the Buffer UAV.
	constexpr UINT StructByteStride = 0; // TODO: get as a parameter?
	uavDesc.Buffer.NumElements = pTexture->Width / GetDXGIFormatByteSize(bufferViewFormatOverride);
	uavDesc.Buffer.StructureByteStride = StructByteStride;

	// create the UAV
	ID3D12Resource* pRscCounter = nullptr; // TODO: find a use case for this parameter and implement proper interface
	assert(pTexture->Resource);
	mDevice.GetDevicePtr()->CreateUnorderedAccessView(
		pTexture->Resource,
		pRscCounter,
		&uavDesc,
		mUAVs.at(uavID).GetCPUDescHandle(heapIndex)
	);
}
void VQRenderer::InitializeSRVForBuffer(SRV_ID srvID, uint heapIndex, TextureID texID, DXGI_FORMAT bufferViewFormatOverride)
{
	const FTexture* pTexture = mTextureManager.GetTexture(texID);
	assert(pTexture);

	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	assert(pDevice);

	ID3D12Resource* pRsc = pTexture->Resource;
	D3D12_RESOURCE_DESC rscDesc = pRsc->GetDesc();
	assert(rscDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);

	D3D12_SHADER_RESOURCE_VIEW_DESC desc;
	desc.Buffer.NumElements = pTexture->Width / GetDXGIFormatByteSize(bufferViewFormatOverride);
	desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAGS::D3D12_BUFFER_SRV_FLAG_NONE;
	desc.Buffer.FirstElement = 0;
	desc.Buffer.StructureByteStride = GetDXGIFormatByteSize(bufferViewFormatOverride);
	
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	{
		std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);
		mDevice.GetDevicePtr()->CreateShaderResourceView(pRsc, &desc, mSRVs.at(srvID).GetCPUDescHandle(heapIndex));
	}

}
void VQRenderer::InitializeUAV(UAV_ID uavID, uint heapIndex, TextureID texID, uint arraySlice /*=0*/, uint mipSlice /*=0*/)
{
	CHECK_RESOURCE_VIEW(UAV, uavID);

	const FTexture* pTexture = mTextureManager.GetTexture(texID);
	assert(pTexture);

	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	assert(pDevice);

	mTextureManager.WaitForTexture(texID);
	D3D12_RESOURCE_DESC rscDesc = pTexture->Resource->GetDesc();
	
	const bool& bCubemap = pTexture->IsCubemap;
	const bool  bArray   = bCubemap ? (rscDesc.DepthOrArraySize / 6 > 1) : rscDesc.DepthOrArraySize > 1;
	const bool  bMSAA    = rscDesc.SampleDesc.Count > 1; // don't care?

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = GetUAVDimensionFromResourceDimension(rscDesc.Dimension, bArray || bCubemap);
	uavDesc.Format = pTexture->Format; 

	// prevent device removal if this function is called on a Buffer resource with DXGI_FORMAT_UNKNOWN resource type:
	//     DX12 will remove dvice w/ reason #232: DEVICE_REMOVAL_PROCESS_AT_FAULT
	// Note that buffer resources *must* be initialized as DXGI_FORMAT_UNKNOWN as per the API,
	// hence we do a check here and return early, warning the programmer.
	if (uavDesc.ViewDimension == D3D12_UAV_DIMENSION_BUFFER && uavDesc.Format == DXGI_FORMAT_UNKNOWN)
	{
		Log::Error("VQRenderer::InitializeUAV() called for buffer resource, did you mean VQRenderer::InitializeUAVForBuffer()?");
		assert(false);
		return;
	}

	if (bArray || bCubemap)
	{
		switch (uavDesc.ViewDimension)
		{
		case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
		{
			uavDesc.Texture2DArray.ArraySize = rscDesc.DepthOrArraySize - arraySlice;
			uavDesc.Texture2DArray.FirstArraySlice = arraySlice;
			uavDesc.Texture2DArray.MipSlice = mipSlice;
		} break;

		case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
		{
			uavDesc.Texture1DArray.ArraySize = rscDesc.DepthOrArraySize - arraySlice;
			uavDesc.Texture1DArray.FirstArraySlice = arraySlice;
			uavDesc.Texture1DArray.MipSlice = mipSlice;
		} break;

		}
	}
	else // non-array
	{
		switch (uavDesc.ViewDimension)
		{
			case D3D12_UAV_DIMENSION_TEXTURE3D: uavDesc.Texture3D.MipSlice = mipSlice; break;
			case D3D12_UAV_DIMENSION_TEXTURE2D: uavDesc.Texture2D.MipSlice = mipSlice; break;
			case D3D12_UAV_DIMENSION_TEXTURE1D: uavDesc.Texture1D.MipSlice = mipSlice; break;
		}
	}

	// create the UAV
	ID3D12Resource* pRscCounter = nullptr; // TODO: find a use case for this parameter and implement proper interface
	assert(pTexture->Resource);
	mDevice.GetDevicePtr()->CreateUnorderedAccessView(
		pTexture->Resource,
		pRscCounter,
		&uavDesc,
		mUAVs.at(uavID).GetCPUDescHandle(heapIndex)
	);
}

void VQRenderer::DestroySRV(SRV_ID srvID)
{
	if (srvID == INVALID_ID)
		return;
	std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);
	mHeapCBV_SRV_UAV.FreeDescriptor(&mSRVs.at(srvID));
	mSRVs.erase(srvID);
}
void VQRenderer::DestroyDSV(DSV_ID dsvID)
{
	std::lock_guard<std::mutex> lk(mMtxDSVs);
	mHeapDSV.FreeDescriptor(&mDSVs.at(dsvID));
	mDSVs.erase(dsvID);
}


// -----------------------------------------------------------------------------------------------------------------
//
// MULTI-THREADED PSO / SHADER / TEXTURE LOADING
//
// -----------------------------------------------------------------------------------------------------------------
constexpr const char* SHADER_BINARY_EXTENSION = ".bin";
std::string VQRenderer::GetCachedShaderBinaryPath(const FShaderStageCompileDesc& ShaderStageCompileDesc)
{
	using namespace ShaderUtils;
	const std::string ShaderSourcePath = StrUtil::UnicodeToASCII<256>(ShaderStageCompileDesc.FilePath.c_str());

	// calculate shader hash
	const size_t ShaderHash = GeneratePreprocessorDefinitionsHash(ShaderStageCompileDesc.Macros);

	// determine cached shader file name
	const std::string cacheFileName = ShaderStageCompileDesc.Macros.empty()
		? DirectoryUtil::GetFileNameWithoutExtension(ShaderSourcePath) + "_" + ShaderStageCompileDesc.EntryPoint + SHADER_BINARY_EXTENSION
		: DirectoryUtil::GetFileNameWithoutExtension(ShaderSourcePath) + "_" + ShaderStageCompileDesc.EntryPoint + "_" + std::to_string(ShaderHash) + SHADER_BINARY_EXTENSION;

	// determine cached shader file path
	const std::string CachedShaderBinaryPath = VQRenderer::ShaderCacheDirectory + "\\" + cacheFileName;
	return CachedShaderBinaryPath;
}

FShaderStageCompileResult VQRenderer::LoadShader(const FShaderStageCompileDesc& ShaderStageCompileDesc, const std::unordered_map<std::string, bool>& ShaderCacheDirtyMap)
{
	SCOPED_CPU_MARKER("LoadShader");
	using namespace ShaderUtils;
	
	const std::string ShaderSourcePath = StrUtil::UnicodeToASCII<512>(ShaderStageCompileDesc.FilePath.c_str());
	const std::string CachedShaderBinaryPath = GetCachedShaderBinaryPath(ShaderStageCompileDesc);

	// decide whether to use shader cache or compile from source
#if 1
	const bool bUseCachedShaders = !ShaderCacheDirtyMap.at(CachedShaderBinaryPath);
#else
	const bool bUseCachedShaders = DirectoryUtil::FileExists(CachedShaderBinaryPath)
		&& !ShaderUtils::IsCacheDirty(ShaderSourcePath, CachedShaderBinaryPath);
#endif

	// load the shader d3dblob
	FShaderStageCompileResult Result = {};
	Result.FilePath = ShaderStageCompileDesc.FilePath;
	Result.bSM6 = IsShaderSM6(ShaderStageCompileDesc.ShaderModel.c_str());
	FBlob& ShaderBlob = Result.ShaderBlob;
	Result.ShaderStageEnum = ShaderUtils::GetShaderStageEnumFromShaderModel(ShaderStageCompileDesc.ShaderModel);
	
	std::string errMsg;
	bool bCompileSuccessful = false;
	
	if (bUseCachedShaders)
	{
		bCompileSuccessful = CompileFromCachedBinary(CachedShaderBinaryPath, ShaderBlob, Result.bSM6, errMsg);
	}
	else
	{	
		// check if file exists
		if (!DirectoryUtil::FileExists(ShaderSourcePath))
		{
			std::stringstream ss; ss << "Shader file doesn't exist:\n\t" << ShaderSourcePath;
			const std::string errMsg = ss.str();

			Log::Error("%s", errMsg.c_str());
			MessageBox(NULL, errMsg.c_str(), "Shader Compiler Error", MB_OK);
			return Result; // no crash until runtime
		}

		ShaderBlob = CompileFromSource(ShaderStageCompileDesc, errMsg);
		bCompileSuccessful = !ShaderBlob.IsNull();
		if (bCompileSuccessful)
		{
			CacheShaderBinary(CachedShaderBinaryPath, ShaderBlob.GetByteCodeSize(), ShaderBlob.GetByteCode());
		}
	}

	if (!bCompileSuccessful)
	{
		const std::string ShaderCompilerErrorMessageHeader = ShaderSourcePath + " | " + ShaderStageCompileDesc.EntryPoint;
		const std::string ShaderCompileErrorMessage = ShaderCompilerErrorMessageHeader
			+ "\n--------------------------------------------------------\n" + errMsg;
		Log::Error(ShaderCompileErrorMessage);
		MessageBox(NULL, errMsg.c_str(), ("Compile Error @ " + ShaderCompilerErrorMessageHeader).c_str(), MB_OK);
		return Result; // no crash until runtime
	}

	return Result;
}

// -----------------------------------------------------------------------------------------------------------------
//
// GETTERS
//
// -----------------------------------------------------------------------------------------------------------------
const VBV& VQRenderer::GetVertexBufferView(BufferID Id) const
{
	if (mVBVs.find(Id) == mVBVs.end())
	{
		static D3D12_VERTEX_BUFFER_VIEW kDefaultVBV = {};
		return kDefaultVBV;
	}
	return mVBVs.at(Id);
}

const IBV& VQRenderer::GetIndexBufferView(BufferID Id) const
{
	if (mIBVs.find(Id) == mIBVs.end())
	{
		static D3D12_INDEX_BUFFER_VIEW kDefaultIBV = {};
		return kDefaultIBV;
	}
	return mIBVs.at(Id);
}
const CBV_SRV_UAV& VQRenderer::GetShaderResourceView(SRV_ID Id) const
{
	std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);
	assert(Id < LAST_USED_SRV_ID && mSRVs.find(Id) != mSRVs.end());
	return mSRVs.at(Id);
}

const CBV_SRV_UAV& VQRenderer::GetUnorderedAccessView(UAV_ID Id) const { return mUAVs.at(Id); }

const DSV& VQRenderer::GetDepthStencilView(RTV_ID Id) const { return mDSVs.at(Id); }
const RTV& VQRenderer::GetRenderTargetView(RTV_ID Id) const { return mRTVs.at(Id); }

ID3D12Resource* VQRenderer::GetTextureResource(TextureID Id) const                                              { return mTextureManager.GetTextureResource(Id); }
DXGI_FORMAT VQRenderer::GetTextureFormat(TextureID Id) const                                                    { return mTextureManager.GetTextureFormat(Id); }
bool VQRenderer::GetTextureAlphaChannelUsed(TextureID Id) const                                                 { return mTextureManager.GetTextureAlphaChannelUsed(Id); }
void VQRenderer::GetTextureDimensions(TextureID Id, int& SizeX, int& SizeY, int& NumSlices, int& NumMips) const { mTextureManager.GetTextureDimensions(Id, SizeX, SizeY, NumSlices, NumMips); }
uint VQRenderer::GetTextureMips(TextureID Id) const                                                             { return mTextureManager.GetTextureMips(Id); }

uint VQRenderer::GetTextureSampleCount(TextureID Id) const
{
	//CHECK_TEXTURE(mTextures, Id);
	//const Texture& tex = GetTexture_ThreadSafe(Id);
	assert(false);
	return 0; // TODO:
}

