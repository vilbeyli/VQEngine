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

#include "Texture.h"
#include "ResourceHeaps.h"
#include "ResourceViews.h"
#include "Libs/D3DX12/d3dx12.h"

#include "../../Libs/D3D12MA/src/D3D12MemAlloc.h"
#include "../../Libs/VQUtils/Source/utils.h"
#include "../../Libs/VQUtils/Source/Image.h"

#include <unordered_map>
#include <set>
#include <cassert>
 
#define GetDevice(pDevice, pTex)\
ID3D12Device* pDevice;\
pTex->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice))\




Texture::Texture(const Texture& other)
    : mpAlloc                (other.mpAlloc)
    , mpTexture              (other.mpTexture)
    , mbResident             (other.mbResident.load())
    , mbTypelessTexture      (other.mbTypelessTexture)
    , mStructuredBufferStride(other.mStructuredBufferStride)
    , mMipMapCount           (other.mMipMapCount)
    , mFormat                (other.mFormat)
    , mbCubemap              (other.mbCubemap)
    , mWidth                 (other.mWidth         )
    , mHeight                (other.mHeight        )
    , mNumArraySlices        (other.mNumArraySlices)
{}

Texture& Texture::operator=(const Texture& other)
{
    mpAlloc                 = other.mpAlloc;
    mpTexture               = other.mpTexture;
    mbResident              = other.mbResident.load();
    mbTypelessTexture       = other.mbTypelessTexture;
    mStructuredBufferStride = other.mStructuredBufferStride;
    mMipMapCount            = other.mMipMapCount;
    mFormat                 = other.mFormat;
    mbCubemap               = other.mbCubemap;
    mWidth                  = other.mWidth;
    mHeight                 = other.mHeight;
    mNumArraySlices         = other.mNumArraySlices;
    
    return *this;
}


bool Texture::ReadImageFromDisk(const std::string& FilePath, Image& img)
{
    if (FilePath.empty())
    {
        Log::Error("Cannot load Image from file: empty FilePath provided.");
        return false;
    }

    // process file path
    const std::vector<std::string> FilePathTokens = StrUtil::split(FilePath, { '/', '\\' });
    assert(FilePathTokens.size() >= 1);

    const std::string& FileNameAndExtension = FilePathTokens.back();
    const std::vector<std::string> FileNameTokens = StrUtil::split(FileNameAndExtension, '.');
    assert(FileNameTokens.size() == 2);

    const std::string FileDirectory = FilePath.substr(0, FilePath.find(FileNameAndExtension));
    const std::string FileName = FileNameTokens.front();
    const std::string FileExtension = StrUtil::GetLowercased(FileNameTokens.back());

    static const std::set<std::string> S_HDR_FORMATS = { "hdr", /*"exr"*/ };
    assert(FileExtension != "exr"); // TODO: add exr loading support to Image class
    const bool bHDR = S_HDR_FORMATS.find(FileExtension) != S_HDR_FORMATS.end();

    img = Image::LoadFromFile(FilePath.c_str(), bHDR);
    return img.pData && img.BytesPerPixel > 0;
}

//
// TEXTURE
//
void Texture::Create(ID3D12Device* pDevice, D3D12MA::Allocator* pAllocator, const TextureCreateDesc& desc)
{
    HRESULT hr = {};

    const bool bDepthStencilTexture    = desc.d3d12Desc.Format == DXGI_FORMAT_R32_TYPELESS; // TODO: change this?
    const bool bRenderTargetTexture    = (desc.d3d12Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0;
    const bool bUnorderedAccessTexture = (desc.d3d12Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;

    // determine resource state & optimal clear value
    D3D12_RESOURCE_STATES ResourceState = desc.pData 
        ? D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_DEST
        : desc.ResourceState;
    D3D12_CLEAR_VALUE* pClearValue = nullptr;
    if (bDepthStencilTexture)
    {
        D3D12_CLEAR_VALUE ClearValue = {};
        ClearValue.Format = (desc.d3d12Desc.Format == DXGI_FORMAT_R32_TYPELESS) ? DXGI_FORMAT_D32_FLOAT : desc.d3d12Desc.Format;
        ClearValue.DepthStencil.Depth = 1.0f;
        ClearValue.DepthStencil.Stencil = 0;
        pClearValue = &ClearValue;
    }
    if (bRenderTargetTexture)
    {
        D3D12_CLEAR_VALUE ClearValue = {};
        ClearValue.Format = desc.d3d12Desc.Format;
        pClearValue = &ClearValue;
    }
    if (bUnorderedAccessTexture)
    {

    }


    // Create resource
    D3D12MA::ALLOCATION_DESC textureAllocDesc = {};
    textureAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    hr = pAllocator->CreateResource(
        &textureAllocDesc,
        &desc.d3d12Desc,
        ResourceState,
        pClearValue,
        &mpAlloc,
        IID_PPV_ARGS(&mpTexture));
    if (FAILED(hr))
    {
        Log::Error("Couldn't create texture: ", desc.TexName.c_str());
        return;
    }
    SetName(mpTexture, desc.TexName.c_str());

    this->mbTypelessTexture = bDepthStencilTexture;
    this->mbCubemap = desc.bCubemap;
    this->mWidth  = static_cast<int>(desc.d3d12Desc.Width );
    this->mHeight = static_cast<int>(desc.d3d12Desc.Height);
    this->mNumArraySlices = desc.d3d12Desc.DepthOrArraySize;
    this->mMipMapCount = desc.d3d12Desc.MipLevels;
}


void Texture::Destroy()
{
    if (mpTexture)
    {
        mpTexture->Release(); 
        mpTexture = nullptr;
    }
    if (mpAlloc)
    {
        mpAlloc->Release();
        mpAlloc = nullptr;
    }
}



//
// RESOURCE VIEW CREATION
//

void Texture::InitializeSRV(uint32 index, CBV_SRV_UAV* pRV, bool bInitAsArrayView, bool bInitAsCubeView, UINT ShaderComponentMapping, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc)
{
    if (bInitAsCubeView)
    {
        if (!this->mbCubemap)
        {
            Log::Warning("Cubemap view requested on a non-cubemap resource");
        }
        assert(this->mbCubemap); // could this be an actual use case: view array[6] as cubemap?
    }

    //
    // TODO: bool bInitAsArrayView needed so that InitializeSRV() can initialize a per-face SRV of a cubemap
    //

    GetDevice(pDevice, mpTexture);
    const bool bCustomComponentMappingSpecified = ShaderComponentMapping != D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    D3D12_RESOURCE_DESC resourceDesc = mpTexture->GetDesc();
    const int NumCubes = this->mbCubemap ? resourceDesc.DepthOrArraySize / 6 : 0;
    const bool bInitializeAsCubemapView = this->mbCubemap && bInitAsCubeView;
    if (bInitializeAsCubemapView)
    {
        const bool bArraySizeMultipleOfSix = resourceDesc.DepthOrArraySize % 6 == 0;
        if (!bArraySizeMultipleOfSix)
        {
            Log::Warning("Cubemap Texture's array size is not multiple of 6");
        }
        assert(bArraySizeMultipleOfSix);
    }

    if (mbTypelessTexture || bCustomComponentMappingSpecified || this->mbCubemap)
    {

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        int mipLevel        = 0;// TODO resourceDesc.MipLevels;
        int arraySize       = resourceDesc.DepthOrArraySize; // TODO
        int firstArraySlice = index;
        //assert(mipLevel > 0);


        const bool bBufferSRV = resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
        const bool bDepthSRV = resourceDesc.Format == DXGI_FORMAT_R32_TYPELESS;
        const bool bMSAA = resourceDesc.SampleDesc.Count != 1;
        const bool bArraySRV = /*bInitAsArrayView &&*/ resourceDesc.DepthOrArraySize > 1;

        if (bBufferSRV)
        {
            srvDesc.Format = mFormat;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = (UINT)resourceDesc.Width;
            srvDesc.Buffer.StructureByteStride = mStructuredBufferStride;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        }

        // Texture SRV
        else
        {
            if (bDepthSRV)
            {
                srvDesc.Format = DXGI_FORMAT_R32_FLOAT; //special case for the depth buffer
            }
            else
            {
                D3D12_RESOURCE_DESC desc = mpTexture->GetDesc();
                srvDesc.Format = desc.Format;
            }

            if (bMSAA)
            {
                assert(!this->mbCubemap); // no need so far, implement MS cubemaps if this is hit.
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
                    assert(mipLevel == -1);
                    assert(arraySize == -1);
                    assert(firstArraySlice == -1);
                }
            }

            else // non-MSAA SRV
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
                        srvDesc.Texture2DArray.MipLevels = (mipLevel == -1) ? mMipMapCount : 1;
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
                        srvDesc.Texture2D.MipLevels = (mipLevel == -1) ? mMipMapCount : 1;
                    }
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
                    pDevice->CreateShaderResourceView(mpTexture, &srvDesc, pRV->GetCPUDescHandle(index + cube));
                }
            }
            else
            {
                //for (int i = 0; i < resourceDesc.DepthOrArraySize; ++i)
                {
                    srvDesc.Texture2DArray.FirstArraySlice = index;
                    srvDesc.Texture2DArray.ArraySize = resourceDesc.DepthOrArraySize - index;
                    pDevice->CreateShaderResourceView(mpTexture, &srvDesc, pRV->GetCPUDescHandle(0 /*+ i*/));
                }
            }
        }

        // Create single SRV
        else 
        {
            pDevice->CreateShaderResourceView(mpTexture, &srvDesc, pRV->GetCPUDescHandle(0));
        }
    }
    else
    {
        pDevice->CreateShaderResourceView(mpTexture, pSRVDesc, pRV->GetCPUDescHandle(index));
    }

    pDevice->Release();
}

void Texture::InitializeDSV(uint32 index, DSV* pRV, int ArraySlice /*= 1*/)
{
    GetDevice(pDevice, mpTexture);
    D3D12_RESOURCE_DESC texDesc = mpTexture->GetDesc();

    D3D12_DEPTH_STENCIL_VIEW_DESC DSViewDesc = {};
    DSViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
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
            DSViewDesc.Texture2DArray.ArraySize = this->mbCubemap ? (6 - ArraySlice%6) : 1;
        }
    }
    else
    {
        DSViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
    }

    pDevice->CreateDepthStencilView(mpTexture, &DSViewDesc, pRV->GetCPUDescHandle(index));
    pDevice->Release();
}

void Texture::InitializeRTV(uint32 index, RTV* pRV, D3D12_RENDER_TARGET_VIEW_DESC* pRTVDesc)
{
    GetDevice(pDevice, mpTexture);
    pDevice->CreateRenderTargetView(mpTexture, pRTVDesc, pRV->GetCPUDescHandle(index));
    pDevice->Release();
}

void Texture::InitializeUAV(uint32 index, CBV_SRV_UAV* pRV, D3D12_UNORDERED_ACCESS_VIEW_DESC* pUAVDesc, const Texture* pCounterTexture /*= nullptr*/)
{
    GetDevice(pDevice, mpTexture);
    pDevice->CreateUnorderedAccessView(
          mpTexture
        , pCounterTexture ? pCounterTexture->mpTexture : NULL
        , pUAVDesc
        , pRV->GetCPUDescHandle(index)
    );
    pDevice->Release();
}


#if 0
void Texture::CreateUAV(uint32_t index, Texture* pCounterTex, CBV_SRV_UAV* pRV, D3D12_UNORDERED_ACCESS_VIEW_DESC* pUavDesc)
{
    ID3D12Device* pDevice;
    m_pResource->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice));

    pDevice->CreateUnorderedAccessView(m_pResource, pCounterTex ? pCounterTex->GetResource() : NULL, pUavDesc, pRV->GetCPUDescHandle(index));

    pDevice->Release();
}

void Texture::CreateRTV(uint32_t index, RTV* pRV, int mipLevel, int arraySize, int firstArraySlice)
{
    D3D12_RESOURCE_DESC texDesc = m_pResource->GetDesc();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = texDesc.Format;
    if (texDesc.DepthOrArraySize == 1)
    {
        assert(arraySize == -1);
        assert(firstArraySlice == -1);
        if (texDesc.SampleDesc.Count == 1)
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = (mipLevel == -1) ? 0 : mipLevel;
        }
        else
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            assert(mipLevel == -1);
        }
    }
    else
    {
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.ArraySize = arraySize;
        rtvDesc.Texture2DArray.FirstArraySlice = firstArraySlice;
        rtvDesc.Texture2DArray.MipSlice = (mipLevel == -1) ? 0 : mipLevel;
    }

    CreateRTV(index, pRV, &rtvDesc);
}
#endif

// ================================================================================================================================================

//
// STATIC
//

// from Microsoft's D3D12HelloTexture
std::vector<uint8> Texture::GenerateTexture_Checkerboard(uint Dimension, bool bUseMidtones /*= false*/)
{
    constexpr UINT TexturePixelSizeInBytes = 4; // byte/px
    const UINT& TextureWidth = Dimension;
    const UINT& TextureHeight = Dimension;

    const UINT rowPitch = TextureWidth * TexturePixelSizeInBytes;
    const UINT cellPitch = rowPitch >> 3;        // The width of a cell in the checkboard texture.
    const UINT cellHeight = TextureWidth >> 3;    // The height of a cell in the checkerboard texture.
    const UINT textureSize = rowPitch * TextureHeight;

    std::vector<UINT8> data(textureSize);
    UINT8* pData = &data[0];

    for (UINT n = 0; n < textureSize; n += TexturePixelSizeInBytes)
    {
        UINT x = n % rowPitch;
        UINT y = n / rowPitch;
        UINT i = x / cellPitch;
        UINT j = y / cellHeight;

        if (i % 2 == j % 2)
        {
            pData[n + 0] = bUseMidtones ? 0x03 : 0x00;    // R
            pData[n + 1] = bUseMidtones ? 0x03 : 0x00;    // G
            pData[n + 2] = bUseMidtones ? 0x03 : 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n + 0] = bUseMidtones ? 0x6F : 0xff;    // R
            pData[n + 1] = bUseMidtones ? 0x6F : 0xff;    // G
            pData[n + 2] = bUseMidtones ? 0x6F : 0xff;    // B
            pData[n + 3] = 0xff;    // A
        }
    }

    return data;
}

#include "../Application/Math.h"
DirectX::XMMATRIX Texture::CubemapUtility::CalculateViewMatrix(Texture::CubemapUtility::ECubeMapLookDirections cubeFace, const DirectX::XMFLOAT3& position)
{
    using namespace DirectX;
    const XMVECTOR pos = XMLoadFloat3(&position);

    static XMVECTOR VEC3_UP      = XMLoadFloat3(&UpVector);
    static XMVECTOR VEC3_DOWN    = XMLoadFloat3(&DownVector);
    static XMVECTOR VEC3_LEFT    = XMLoadFloat3(&LeftVector);
    static XMVECTOR VEC3_RIGHT   = XMLoadFloat3(&RightVector);
    static XMVECTOR VEC3_FORWARD = XMLoadFloat3(&ForwardVector);
    static XMVECTOR VEC3_BACK    = XMLoadFloat3(&BackVector);

    // cube face order: https://msdn.microsoft.com/en-us/library/windows/desktop/ff476906(v=vs.85).aspx
    //------------------------------------------------------------------------------------------------------
    // 0: RIGHT		1: LEFT
    // 2: UP		3: DOWN
    // 4: FRONT		5: BACK
    //------------------------------------------------------------------------------------------------------
    switch (cubeFace)
    {
    case Texture::CubemapUtility::CUBEMAP_LOOK_RIGHT:	return XMMatrixLookAtLH(pos, pos + VEC3_RIGHT  , VEC3_UP);
    case Texture::CubemapUtility::CUBEMAP_LOOK_LEFT:	return XMMatrixLookAtLH(pos, pos + VEC3_LEFT   , VEC3_UP);
    case Texture::CubemapUtility::CUBEMAP_LOOK_UP:		return XMMatrixLookAtLH(pos, pos + VEC3_UP     , VEC3_BACK);
    case Texture::CubemapUtility::CUBEMAP_LOOK_DOWN:	return XMMatrixLookAtLH(pos, pos + VEC3_DOWN   , VEC3_FORWARD);
    case Texture::CubemapUtility::CUBEMAP_LOOK_FRONT:	return XMMatrixLookAtLH(pos, pos + VEC3_FORWARD, VEC3_UP);
    case Texture::CubemapUtility::CUBEMAP_LOOK_BACK:	return XMMatrixLookAtLH(pos, pos + VEC3_BACK   , VEC3_UP);
    default: return XMMatrixIdentity();
    }
}