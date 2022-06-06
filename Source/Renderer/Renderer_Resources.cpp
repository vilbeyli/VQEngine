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
#define NOMINMAX
#include "Renderer.h"
#include "Device.h"
#include "Texture.h"

#include "../Engine/Core/Window.h"

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
static PSO_ID    LAST_USED_PSO_ID_OFFSET     = 1; // TODO: atomic id
static std::atomic<TaskID> LAST_USED_TASK_ID = 1;

namespace VQ_DXGI_UTILS
{
	size_t BitsPerPixel(DXGI_FORMAT fmt)
	{
		switch (fmt)
		{
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return 128;

		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 96;

		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		case DXGI_FORMAT_Y416:
		case DXGI_FORMAT_Y210:
		case DXGI_FORMAT_Y216:
			return 64;

		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_AYUV:
		case DXGI_FORMAT_Y410:
		case DXGI_FORMAT_YUY2:
			return 32;

		case DXGI_FORMAT_P010:
		case DXGI_FORMAT_P016:
			return 24;

		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_B5G6R5_UNORM:
		case DXGI_FORMAT_B5G5R5A1_UNORM:
		case DXGI_FORMAT_A8P8:
		case DXGI_FORMAT_B4G4R4A4_UNORM:
			return 16;

		case DXGI_FORMAT_NV12:
		case DXGI_FORMAT_420_OPAQUE:
		case DXGI_FORMAT_NV11:
			return 12;

		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_AI44:
		case DXGI_FORMAT_IA44:
		case DXGI_FORMAT_P8:
			return 8;

		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return 8;

		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return 4;

		case DXGI_FORMAT_R1_UNORM:
			return 1;

		default:
			return 0;
		}
	}

	//--------------------------------------------------------------------------------------
	// return the byte size of a pixel (or block if block compressed)
	//--------------------------------------------------------------------------------------
	size_t GetPixelByteSize(DXGI_FORMAT fmt)
	{
		switch (fmt)
		{
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
			assert(0);
			break;
		}
		return 0;
	}

	void MipImage(void* pData, uint width, uint height, uint bytesPerPixel)
	{
		//compute mip so next call gets the lower mip    
		int offsetsX[] = { 0,1,0,1 };
		int offsetsY[] = { 0,0,1,1 };


		assert(bytesPerPixel == 4 || bytesPerPixel == 16);
		
		if (bytesPerPixel == 4)
		{
			uint32_t* pImg = (uint32_t*)pData;
#define GetByte(color, component) (((color) >> (8 * (component))) & 0xff)
#define GetColor(ptr, x,y) (ptr[(x)+(y)*width])
#define SetColor(ptr, x,y, col) ptr[(x)+(y)*width/2]=col;

			for (uint32_t y = 0; y < height; y += 2)
			{
				for (uint32_t x = 0; x < width; x += 2)
				{
					uint32_t ccc = 0;
					for (uint32_t c = 0; c < 4; c++)
					{
						uint32_t cc = 0;
						for (uint32_t i = 0; i < 4; i++)
							cc += GetByte(GetColor(pImg, x + offsetsX[i], y + offsetsY[i]), 3 - c);

						ccc = (ccc << 8) | (cc / 4);
					}
					SetColor(pImg, x / 2, y / 2, ccc);
				}
			}
		}

		if (bytesPerPixel == 16)
		{
			using std::min;
			float* pImg = (float*)pData;
			// each iteration handles 4 pixels from current level, writes out to a single pixel
			for (uint32_t y = 0; y < height; y += 2) // [0, 2, 4, ...]
			for (uint32_t x = 0; x < width ; x += 2) // [0, 2, 4, ...]
			{
				float rgb[4][3] = {}; // 4 samples of rgb
				for (uint smp = 0; smp < 4; ++smp)
				{
					for (int ch = 0; ch < 3; ++ch) // color channel ~ rgba, care for RGB only
						rgb[smp][ch] = pImg[(x + offsetsX[smp]) * 4 + (y + offsetsY[smp]) * 4 * width + ch];
				}

				// filter: use min filter rather than interpolation
				float rgbFiltered[4];
				for (int ch = 0; ch < 3; ++ch)
					rgbFiltered[ch] = min(rgb[0][ch], min(rgb[1][ch], min(rgb[2][ch], rgb[3][ch])));
				rgbFiltered[3] = 1.0f;

				uint outX = x >> 1;
				uint outY = y >> 1;

				for (int ch = 0; ch < 4; ++ch)
					pImg[(outX * 4) + 4 * outY * (width >> 1) + ch] = rgbFiltered[ch];
			}
			
		}

#if 0
		// For cutouts we need we need to scale the alpha channel to match the coverage of the top MIP map
		// otherwise cutouts seem to get thinner when smaller mips are used
		// Credits: http://the-witness.net/news/2010/09/computing-alpha-mipmaps/
		//
		if (m_alphaTestCoverage < 1.0)
		{
			float ini = 0;
			float fin = 10;
			float mid;
			float alphaPercentage;
			int iter = 0;
			for (; iter < 50; iter++)
			{
				mid = (ini + fin) / 2;
				alphaPercentage = GetAlphaCoverage(width / 2, height / 2, mid, (int)(m_cutOff * 255));

				if (fabs(alphaPercentage - m_alphaTestCoverage) < .001)
					break;

				if (alphaPercentage > m_alphaTestCoverage)
					fin = mid;
				if (alphaPercentage < m_alphaTestCoverage)
					ini = mid;
			}
			ScaleAlpha(width / 2, height / 2, mid);
			//Trace(format("(%4i x %4i), %f, %f, %i\n", width, height, alphaPercentage, 1.0f, 0));       
		}
#endif
	}

	void CopyPixels(void* pData, void* pDest, uint32_t stride, uint32_t bytesWidth, uint32_t height)
	{
		for (uint32_t y = 0; y < height; y++)
		{
			memcpy((char*)pDest + y * stride, (char*)pData + y * bytesWidth, bytesWidth);
		}
	}
}


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

TextureID VQRenderer::CreateTextureFromFile(const char* pFilePath, bool bGenerateMips /*= false*/)
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

	auto fnLoadImageFromDisk = [](const std::string& FilePath, Image& img)
	{
		if (FilePath.empty())
		{
			Log::Error("Cannot load Image from file: empty FilePath provided.");
			return false;
		}

		// process file path
		const std::vector<std::string> FilePathTokens = StrUtil::split(FilePath, { '/', '\\' });
		assert(FilePathTokens.size() >= 1);

		img = Image::LoadFromFile(FilePath.c_str());
		return img.pData && img.BytesPerPixel > 0;
	};

	Image image;
	const bool bSuccess = fnLoadImageFromDisk(pFilePath, image);
	const int MipLevels = bGenerateMips ? image.CalculateMipLevelCount() : 1;
	if (bSuccess)
	{
		// Fill D3D12 Descriptor
		tDesc.d3d12Desc = {};
		tDesc.d3d12Desc.Width = image.Width;
		tDesc.d3d12Desc.Height = image.Height;
		tDesc.d3d12Desc.Format = image.IsHDR() ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
		tDesc.d3d12Desc.DepthOrArraySize = 1;
		tDesc.d3d12Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		tDesc.d3d12Desc.Alignment = 0;
		tDesc.d3d12Desc.DepthOrArraySize = 1;
		tDesc.d3d12Desc.MipLevels = MipLevels;
		tDesc.d3d12Desc.SampleDesc.Count = 1;
		tDesc.d3d12Desc.SampleDesc.Quality = 0;
		tDesc.d3d12Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		tDesc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		
		tDesc.pData = image.pData;
		tDesc.bGenerateMips = bGenerateMips;

		tex.Create(mDevice.GetDevicePtr(), mpAllocator, tDesc);
		ID = AddTexture_ThreadSafe(std::move(tex));
		this->QueueTextureUpload(FTextureUploadDesc(std::move(image), ID, tDesc));

		this->StartTextureUploads();
		std::atomic<bool>& mbResident = mTextures.at(ID).mbResident; // Is this safe?

		// SYNC POINT - texture residency
		//------------------------------------------------------------------------------
		// NOTE: this isn't the best but good enough for small scenes for now.
		// >60% of the CPU time is spent here during a scene load on the threads
		while (!mbResident.load()); // BUSY WAIT here until the texture is made resident;
		//------------------------------------------------------------------------------

#if LOG_RESOURCE_CREATE
		Log::Info("VQRenderer::CreateTextureFromFile(): [%.2fs] %s", t.StopGetDeltaTimeAndReset(), pFilePath);
#endif
	}

	return ID;
}

TextureID VQRenderer::CreateTexture(const TextureCreateDesc& desc)
{
	if (desc.d3d12Desc.MipLevels == 0) assert( desc.bGenerateMips);
	if (desc.d3d12Desc.MipLevels == 1) assert(!desc.bGenerateMips);
	//if (desc.d3d12Desc.MipLevels >  1) assert( desc.bGenerateMips);
	Texture tex;
	Timer t; t.Start();

	tex.Create(mDevice.GetDevicePtr(), mpAllocator, desc);

	TextureID ID = AddTexture_ThreadSafe(std::move(tex));
	if (desc.pData)
	{
		this->QueueTextureUpload(FTextureUploadDesc(desc.pData, ID, desc));

		this->StartTextureUploads();
		std::atomic<bool>& mbResident = mTextures.at(ID).mbResident;
		while (!mbResident.load());  // busy wait here until the texture is made resident; (very not ideal)
	}

	if (desc.pData)
	{
#if LOG_RESOURCE_CREATE
		Log::Info("VQRenderer::CreateTexture() w/ pData: [%.2fs] %s", t.StopGetDeltaTimeAndReset(), desc.TexName.c_str());
#endif
	}

	return ID;
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

void VQRenderer::DestroyTexture(TextureID& texID)
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

	texID = INVALID_ID; // invalidate the ID
}

TextureID VQRenderer::AddTexture_ThreadSafe(Texture&& tex)
{
	TextureID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(mMtxTextures);
	Id = LAST_USED_TEXTURE_ID++;

	mTextures[Id] = std::move(tex);

	return Id;
}


// -----------------------------------------------------------------------------------------------------------------
//
// RESOURCE VIEW CREATION
//
// -----------------------------------------------------------------------------------------------------------------
SRV_ID VQRenderer::AllocateAndInitializeSRV(TextureID texID)
{
	SRV_ID Id = INVALID_ID;
	CBV_SRV_UAV SRV = {};
	if(texID != INVALID_ID)
	{
		std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);

		Texture& tex = mTextures.at(texID);
		if (!tex.mpResource)
		{
			Log::Error("Texture ID=%d failed initializing, cannot create the SRV", texID);
			return INVALID_ID;
		}
		mHeapCBV_SRV_UAV.AllocDescriptor(1, &SRV);
		tex.InitializeSRV(0, &SRV);
		Id = LAST_USED_SRV_ID++;
		mSRVs[Id] = SRV;
	}

	return Id;
}
DSV_ID VQRenderer::AllocateAndInitializeDSV(TextureID texID)
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


DSV_ID VQRenderer::AllocateDSV(uint NumDescriptors /*= 1*/)
{
	DSV dsv = {};
	DSV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxDSVs);

	this->mHeapDSV.AllocDescriptor(NumDescriptors, &dsv);
	Id = LAST_USED_DSV_ID++;
	this->mDSVs[Id] = dsv;

	return Id;
}
RTV_ID VQRenderer::AllocateRTV(uint NumDescriptors /*= 1*/)
{
	RTV rtv = {};
	RTV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxRTVs);

	this->mHeapRTV.AllocDescriptor(NumDescriptors, &rtv);
	Id = LAST_USED_RTV_ID++;
	this->mRTVs[Id] = rtv;

	return Id;
}
SRV_ID VQRenderer::AllocateSRV(uint NumDescriptors)
{
	CBV_SRV_UAV srv = {};
	SRV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxSRVs_CBVs_UAVs);

	this->mHeapCBV_SRV_UAV.AllocDescriptor(NumDescriptors, &srv);
	Id = LAST_USED_SRV_ID++;
	this->mSRVs[Id] = srv;

	return Id;
}
UAV_ID VQRenderer::AllocateUAV(uint NumDescriptors)
{
	CBV_SRV_UAV uav = {};
	UAV_ID Id = INVALID_ID;

	std::lock_guard<std::mutex> lk(this->mMtxSRVs_CBVs_UAVs);

	this->mHeapCBV_SRV_UAV.AllocDescriptor(NumDescriptors, &uav);
	Id = LAST_USED_UAV_ID++;
	this->mUAVs[Id] = uav;

	return Id;
}

void VQRenderer::InitializeDSV(DSV_ID dsvID, uint32 heapIndex, TextureID texID, int ArraySlice /*= 0*/)
{
	CHECK_TEXTURE(mTextures, texID);
	CHECK_RESOURCE_VIEW(DSV, dsvID);
	
	assert(mDSVs.find(dsvID) != mDSVs.end());

	mTextures.at(texID).InitializeDSV(heapIndex, &mDSVs.at(dsvID), ArraySlice);
}
void VQRenderer::InitializeSRV(SRV_ID srvID, uint heapIndex, TextureID texID, bool bInitAsArrayView /*= false*/, bool bInitAsCubeView /*= false*/, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc /*=nullptr*/, UINT ShaderComponentMapping)
{
	CHECK_RESOURCE_VIEW(SRV, srvID);
	if (texID != INVALID_ID)
	{
		CHECK_TEXTURE(mTextures, texID);
		mTextures.at(texID).InitializeSRV(heapIndex, &mSRVs.at(srvID), bInitAsArrayView, bInitAsCubeView, ShaderComponentMapping, pSRVDesc);
	}
	else // init NULL SRV
	{
		// Describe and create null SRV. Null descriptors are needed in order 
		// to achieve the effect of an "unbound" resource.
		D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
		nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullSrvDesc.Shader4ComponentMapping = ShaderComponentMapping;
		nullSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullSrvDesc.Texture2D.MipLevels = 1;
		nullSrvDesc.Texture2D.MostDetailedMip = 0;
		nullSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		mDevice.GetDevicePtr()->CreateShaderResourceView(nullptr, &nullSrvDesc, mSRVs.at(srvID).GetCPUDescHandle(heapIndex));
	}
}
void VQRenderer::InitializeSRV(SRV_ID srvID, uint heapIndex, D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc)
{
	mDevice.GetDevicePtr()->CreateShaderResourceView(nullptr, &srvDesc, mSRVs.at(srvID).GetCPUDescHandle(heapIndex));
}
void VQRenderer::InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID)
{
	CHECK_TEXTURE(mTextures, texID);
	CHECK_RESOURCE_VIEW(RTV, rtvID);
	D3D12_RENDER_TARGET_VIEW_DESC* pRTVDesc = nullptr; // unused
	mDevice.GetDevicePtr()->CreateRenderTargetView(mTextures.at(texID).GetResource(), pRTVDesc, mRTVs.at(rtvID).GetCPUDescHandle(heapIndex));
}

void VQRenderer::InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID, int arraySlice, int mipLevel)
{
	CHECK_TEXTURE(mTextures, texID);
	CHECK_RESOURCE_VIEW(RTV, rtvID);
	Texture& tex = mTextures.at(texID);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	D3D12_RESOURCE_DESC rscDesc = tex.GetResource()->GetDesc();

	rtvDesc.Format = rscDesc.Format;
	
	const bool& bCubemap = tex.mbCubemap;
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

	mDevice.GetDevicePtr()->CreateRenderTargetView(mTextures.at(texID).GetResource(), &rtvDesc, mRTVs.at(rtvID).GetCPUDescHandle(heapIndex));
}

void VQRenderer::InitializeUAVForBuffer(UAV_ID uavID, uint heapIndex, TextureID texID, DXGI_FORMAT bufferViewFormatOverride)
{
	CHECK_TEXTURE(mTextures, texID);
	CHECK_RESOURCE_VIEW(UAV, uavID);

	Texture& tex = mTextures.at(texID);
	D3D12_RESOURCE_DESC rscDesc = tex.GetResource()->GetDesc();
	
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
	uavDesc.Buffer.NumElements = tex.mWidth / GetDXGIFormatByteSize(bufferViewFormatOverride);
	uavDesc.Buffer.StructureByteStride = StructByteStride;

	// create the UAV
	ID3D12Resource* pRsc = mTextures.at(texID).GetResource();
	ID3D12Resource* pRscCounter = nullptr; // TODO: find a use case for this parameter and implement proper interface
	assert(pRsc);
	mDevice.GetDevicePtr()->CreateUnorderedAccessView(
		pRsc,
		pRscCounter,
		&uavDesc,
		mUAVs.at(uavID).GetCPUDescHandle(heapIndex)
	);
}
void VQRenderer::InitializeSRVForBuffer(SRV_ID srvID, uint heapIndex, TextureID texID, DXGI_FORMAT bufferViewFormatOverride)
{
	Texture& tex = mTextures.at(texID);
	ID3D12Resource* pRsc = tex.GetResource();
	D3D12_RESOURCE_DESC rscDesc = pRsc->GetDesc();
	assert(rscDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);

	D3D12_SHADER_RESOURCE_VIEW_DESC desc;
	desc.Buffer.NumElements = tex.mWidth / GetDXGIFormatByteSize(bufferViewFormatOverride);
	desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAGS::D3D12_BUFFER_SRV_FLAG_NONE;
	desc.Buffer.FirstElement = 0;
	desc.Buffer.StructureByteStride = GetDXGIFormatByteSize(bufferViewFormatOverride);
	
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	mDevice.GetDevicePtr()->CreateShaderResourceView(pRsc, &desc, mSRVs.at(srvID).GetCPUDescHandle(heapIndex));

}
void VQRenderer::InitializeUAV(UAV_ID uavID, uint heapIndex, TextureID texID, uint arraySlice /*=0*/, uint mipSlice /*=0*/)
{
	CHECK_TEXTURE(mTextures, texID);
	CHECK_RESOURCE_VIEW(UAV, uavID);

	Texture& tex = mTextures.at(texID);
	D3D12_RESOURCE_DESC rscDesc = tex.GetResource()->GetDesc();
	
	const bool& bCubemap = tex.mbCubemap;
	const bool  bArray   = bCubemap ? (rscDesc.DepthOrArraySize / 6 > 1) : rscDesc.DepthOrArraySize > 1;
	const bool  bMSAA    = rscDesc.SampleDesc.Count > 1; // don't care?

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = GetUAVDimensionFromResourceDimension(rscDesc.Dimension, bArray || bCubemap);
	uavDesc.Format = tex.mFormat; 

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
	ID3D12Resource* pRsc = mTextures.at(texID).GetResource();
	ID3D12Resource* pRscCounter = nullptr; // TODO: find a use case for this parameter and implement proper interface
	assert(pRsc);
	mDevice.GetDevicePtr()->CreateUnorderedAccessView(
		pRsc,
		pRscCounter,
		&uavDesc,
		mUAVs.at(uavID).GetCPUDescHandle(heapIndex)
	);
}

void VQRenderer::DestroySRV(SRV_ID& srvID)
{
	std::lock_guard<std::mutex> lk(mMtxSRVs_CBVs_UAVs);
	//mSRVs.at(srvID).Destroy(); // TODO
	mSRVs.erase(srvID);
	//Log::Info("Erase SRV_ID=%d", srvID); // todo: verbose logging preprocessor ifdef
	srvID = INVALID_ID;
}
void VQRenderer::DestroyDSV(DSV_ID& dsvID)
{
	std::lock_guard<std::mutex> lk(mMtxDSVs);
	//mDSVs.at(dsvID).Destroy(); // TODO
	mDSVs.erase(dsvID);
	dsvID = INVALID_ID;
}


// -----------------------------------------------------------------------------------------------------------------
//
// MULTI-THREADED PSO / SHADER / TEXTURE LOADING
//
// -----------------------------------------------------------------------------------------------------------------
void VQRenderer::EnqueueTask_ShaderLoad(TaskID PSOTaskID, const FShaderStageCompileDesc& ShaderStageCompileDesc)
{
	FShaderLoadTaskContext& taskContext = mLookup_ShaderLoadContext[PSOTaskID];
	taskContext.TaskQueue.push(ShaderStageCompileDesc);
}
std::vector<std::shared_future<FShaderStageCompileResult>> VQRenderer::StartShaderLoadTasks(TaskID PSOTaskID)
{
	if (mLookup_ShaderLoadContext.find(PSOTaskID) == mLookup_ShaderLoadContext.end())
	{
		Log::Error("Couldn't find PSO Load Task [ID=%d]", PSOTaskID);
		return {};
	}

	// get the task queue associated with the PSOTaskID
	FShaderLoadTaskContext& taskCtx = mLookup_ShaderLoadContext.at(PSOTaskID);
	std::queue<FShaderStageCompileDesc>& TaskQueue = taskCtx.TaskQueue;

	// kickoff shader load workers
	std::vector<std::shared_future<FShaderStageCompileResult>> taskResults;
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

	return taskResults;
}
void VQRenderer::EnqueueTask_CreatePSO(FPSOCreationTaskParameters&& params)
{
	const TaskID PSOTaskID = LAST_USED_TASK_ID.fetch_add(1);
	FPSOCreationTaskExecutionContext& ctx = mLookup_PSOCreationContext[PSOTaskID];
	assert(false); // TODO: continue
	//ctx.TaskQueue.push(params.Desc);
	//*params.pID = INVALID_ID;
}
void VQRenderer::StartPSOCreationTasks()
{
	assert(false); // TODO: continue
}

void VQRenderer::WaitForPSOCreationTaskQueueCompletion()
{
	assert(false); // TODO: continue
}

static PSO_ID GetNextAvailablePSOIdAndIncrement()
{
	return EBuiltinPSOs::NUM_BUILTIN_PSOs + LAST_USED_PSO_ID_OFFSET++;
}
PSO_ID VQRenderer::CreatePSO_OnThisThread(const FPSODesc& psoLoadDesc)
{
	ID3D12PipelineState* pPSO = this->LoadPSO(psoLoadDesc);
	if (!pPSO)
		return INVALID_ID;
	PSO_ID id = GetNextAvailablePSOIdAndIncrement();
	mPSOs[id] = pPSO;
	return id;
}

ID3D12PipelineState* VQRenderer::LoadPSO(const FPSODesc& psoLoadDesc)
{
	
	TaskID               PSOTaskID = LAST_USED_TASK_ID.fetch_add(1);
	ID3D12PipelineState* pPSO = nullptr;

	HRESULT hr = {};
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	// calc PSO hash
	//std::hash<FPSOLoadDesc> PSO_HASH = 

	// check if PSO is cached
	const bool bCachedPSOExists = false;
	const bool bCacheDirty = false;
	const bool bComputePSO = std::find_if(RANGE(psoLoadDesc.ShaderStageCompileDescs) // check if ShaderModel has cs_*_*
			, [](const FShaderStageCompileDesc& desc) { return ShaderUtils::GetShaderStageEnumFromShaderModel(desc.ShaderModel) == EShaderStage::CS; }
		) != psoLoadDesc.ShaderStageCompileDescs.end();

	std::vector<std::shared_future<FShaderStageCompileResult>> shaderCompileResults;
	std::unordered_map<EShaderStage, ID3D12ShaderReflection*> ShaderReflections;

	// compile PSO if no cache or cache dirty, otherwise load cached binary
	if (!bCachedPSOExists || bCacheDirty) 
	{
		// Prepare shader loading tasks for worker threads
		for (const FShaderStageCompileDesc& shaderStageDesc : psoLoadDesc.ShaderStageCompileDescs)
		{
			if (shaderStageDesc.FilePath.empty())
				continue;
			EnqueueTask_ShaderLoad(PSOTaskID, shaderStageDesc);
		}

		// kickoff shader compiler workers
		shaderCompileResults = std::move( StartShaderLoadTasks(PSOTaskID) );

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
			if (ShaderCompileResult.ShaderBlob.IsNull())
			{
				Log::Error("PSO Compile failed: PSOTaskID=%d", PSOTaskID);
				return nullptr;
			}
		}

		// Compile the PSO using the shaders
		if (bComputePSO) // COMPUTE PSO ------------------------------------------------------------
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC  d3d12ComputePSODesc = psoLoadDesc.D3D12ComputeDesc;

			// Assign CS shader blob to PSODesc
			for (std::shared_future<FShaderStageCompileResult>& TaskResult : shaderCompileResults)
			{
				FShaderStageCompileResult ShaderCompileResult = TaskResult.get();

				CD3DX12_SHADER_BYTECODE ShaderByteCode(ShaderCompileResult.ShaderBlob.GetByteCode(), ShaderCompileResult.ShaderBlob.GetByteCodeSize());
				d3d12ComputePSODesc.CS = ShaderByteCode;
			}

			// TODO: assign root signature

			// Compile PSO
			hr = pDevice->CreateComputePipelineState(&d3d12ComputePSODesc, IID_PPV_ARGS(&pPSO));
		}
		else // GRAPHICS PSO ------------------------------------------------------------------------
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC d3d12GraphicsPSODesc = psoLoadDesc.D3D12GraphicsDesc;

			// Assign shader blobs to PSODesc
			for (std::shared_future<FShaderStageCompileResult>& TaskResult : shaderCompileResults)
			{
				FShaderStageCompileResult ShaderCompileResult = TaskResult.get();

				CD3DX12_SHADER_BYTECODE ShaderByteCode(ShaderCompileResult.ShaderBlob.GetByteCode(), ShaderCompileResult.ShaderBlob.GetByteCodeSize());
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
			hr = pDevice->CreateGraphicsPipelineState(&d3d12GraphicsPSODesc, IID_PPV_ARGS(&pPSO));
		}
	}
	else // load cached PSO
	{
		assert(false); // TODO
	}

	// Check PSO compile result
	assert(hr == S_OK);
	if(hr == S_OK)
		SetName(pPSO, psoLoadDesc.PSOName.c_str());

	// release reflections
	for(auto it = ShaderReflections.begin(); it != ShaderReflections.end(); ++it)
	{
		it->second->Release();
	}
	ShaderReflections.clear();

	return pPSO;
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
		? DirectoryUtil::GetFileNameWithoutExtension(ShaderSourcePath) + "_" + ShaderStageCompileDesc.EntryPoint + SHADER_BINARY_EXTENSION
		: DirectoryUtil::GetFileNameWithoutExtension(ShaderSourcePath) + "_" + ShaderStageCompileDesc.EntryPoint + "_" + std::to_string(ShaderHash) + SHADER_BINARY_EXTENSION;

	// determine cached shader file path
	const std::string CachedShaderBinaryPath = VQRenderer::ShaderCacheDirectory + "\\" + cacheFileName;

	// decide whether to use shader cache or compile from source
	const bool bUseCachedShaders = DirectoryUtil::FileExists(CachedShaderBinaryPath)
		&& !ShaderUtils::IsCacheDirty(ShaderSourcePath, CachedShaderBinaryPath);

	// load the shader d3dblob
	FShaderStageCompileResult Result = {};
	Shader::FBlob& ShaderBlob = Result.ShaderBlob;
	Result.ShaderStageEnum = ShaderUtils::GetShaderStageEnumFromShaderModel(ShaderStageCompileDesc.ShaderModel);

	if (bUseCachedShaders)
	{
		ShaderBlob = CompileFromCachedBinary(CachedShaderBinaryPath);
	}
	else
	{
		std::string errMsg;
		
		// check if file exists
		const std::string ShaderFilePath = StrUtil::UnicodeToASCII<512>(ShaderStageCompileDesc.FilePath.c_str());
		if (!DirectoryUtil::FileExists(ShaderFilePath))
		{
			std::stringstream ss; ss << "Shader file doesn't exist:\n\t" << ShaderFilePath;
			const std::string errMsg = ss.str();

			Log::Error("%s", errMsg.c_str());
			MessageBox(NULL, errMsg.c_str(), "Shader Compiler Error", MB_OK);
			return Result; // no crash until runtime
		}

		ShaderBlob = CompileFromSource(ShaderStageCompileDesc, errMsg);
		const bool bCompileSuccessful = !ShaderBlob.IsNull();
		if (bCompileSuccessful)
		{
			CacheShaderBinary(CachedShaderBinaryPath, ShaderBlob.GetByteCodeSize(), ShaderBlob.GetByteCode());
		}
		else
		{
			Log::Error(errMsg);
			MessageBox(NULL, errMsg.c_str(), "Shader Compiler Error", MB_OK);
			return Result; // no crash until runtime
		}
	}

	return Result;
}



void VQRenderer::QueueTextureUpload(const FTextureUploadDesc& desc)
{
	std::unique_lock<std::mutex> lk(mMtxTextureUploadQueue);
	mTextureUploadQueue.push(desc);
}


void VQRenderer::ProcessTextureUpload(const FTextureUploadDesc& desc)
{
	ID3D12GraphicsCommandList* pCmd = mHeapUpload.GetCommandList();
	ID3D12Device* pDevice = mDevice.GetDevicePtr();
	const D3D12_RESOURCE_DESC& d3dDesc = desc.desc.d3d12Desc;
	//--------------------------------------------------------------
	ID3D12Resource* pResc = GetTextureResource(desc.id);
	const void* pData = desc.img.pData ? desc.img.pData : desc.pData;
	assert(pData);

	const uint MIP_COUNT = desc.desc.bGenerateMips ? desc.img.CalculateMipLevelCount() : 1;

	UINT64 UplHeapSize;
	uint32_t num_rows[D3D12_REQ_MIP_LEVELS] = { 0 };
	UINT64 row_size_in_bytes[D3D12_REQ_MIP_LEVELS] = { 0 };
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedSubresource[D3D12_REQ_MIP_LEVELS];

	pDevice->GetCopyableFootprints(&d3dDesc, 0, MIP_COUNT, 0, placedSubresource, num_rows, row_size_in_bytes, &UplHeapSize);

	UINT8* pUploadBufferMem = mHeapUpload.Suballocate(SIZE_T(UplHeapSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	if (pUploadBufferMem == NULL)
	{
		mHeapUpload.UploadToGPUAndWait(); // We ran out of mem in the upload heap, upload contents and try allocating again
		pUploadBufferMem = mHeapUpload.Suballocate(SIZE_T(UplHeapSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
		assert(pUploadBufferMem);
	}

	const bool bBufferResource = d3dDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;

	if (bBufferResource)
	{
		const UINT64 SourceRscOffset = pUploadBufferMem - mHeapUpload.BasePtr();
		memcpy(pUploadBufferMem, pData, d3dDesc.Width);
		pCmd->CopyBufferRegion(pResc, 0, mHeapUpload.GetResource(), SourceRscOffset, d3dDesc.Width);
	}
	else // textures
	{
		const int szArray = 1; // array size (not impl for now)
		const UINT bytePP = static_cast<UINT>(VQ_DXGI_UTILS::GetPixelByteSize(d3dDesc.Format));
		const UINT imgSizeInBytes = bytePP * placedSubresource[0].Footprint.Width * placedSubresource[0].Footprint.Height;

		//---------------------------------------------------------------------------------------------
		// Note: the img copy is used here to enable writing MIP levels on top of the available memory.
		//       This will slow things down, ideally we should be able to use the img in the @desc,
		//       but const correctness has to be revisited.
		Image imgCopy = Image::CreateEmptyImage(imgSizeInBytes);
		memcpy(imgCopy.pData, pData, imgSizeInBytes);
		//---------------------------------------------------------------------------------------------

		for (int a = 0; a < szArray; ++a)
		{
			for (uint mip = 0; mip < MIP_COUNT; ++mip)
			{
				VQ_DXGI_UTILS::CopyPixels(imgCopy.pData
					, pUploadBufferMem + placedSubresource[mip].Offset
					, placedSubresource[mip].Footprint.RowPitch
					, placedSubresource[mip].Footprint.Width * bytePP
					, num_rows[mip]
				);

				if (MIP_COUNT > 1)
				{
					VQ_DXGI_UTILS::MipImage(imgCopy.pData, placedSubresource[mip].Footprint.Width, num_rows[mip], bytePP);
				}

				D3D12_PLACED_SUBRESOURCE_FOOTPRINT slice = placedSubresource[mip];
				slice.Offset += (pUploadBufferMem - mHeapUpload.BasePtr());

				CD3DX12_TEXTURE_COPY_LOCATION Dst(pResc, a * MIP_COUNT + mip);
				CD3DX12_TEXTURE_COPY_LOCATION Src(mHeapUpload.GetResource(), slice);
				pCmd->CopyTextureRegion(&Dst, 0, 0, 0, &Src, NULL);
			}
		}
		imgCopy.Destroy();
	}


	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = pResc;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = desc.desc.ResourceState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	pCmd->ResourceBarrier(1, &barrier);
}

void VQRenderer::ProcessTextureUploadQueue()
{
	std::unique_lock<std::mutex> lk(mMtxTextureUploadQueue);
	if (mTextureUploadQueue.empty())
		return;

	std::vector<std::atomic<bool>*> vTexResidentBools;
	std::vector<Image> vImages;

	while (!mTextureUploadQueue.empty())
	{
		FTextureUploadDesc desc = std::move(mTextureUploadQueue.front());
		mTextureUploadQueue.pop();

		ProcessTextureUpload(desc);

		assert(mTextures.find(desc.id) != mTextures.end());
		{
			std::unique_lock<std::mutex> lk(mMtxTextures);
			Texture& tex = mTextures.at(desc.id);
			vTexResidentBools.push_back(&tex.mbResident);
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

// -----------------------------------------------------------------------------------------------------------------
//
// GETTERS
//
// -----------------------------------------------------------------------------------------------------------------
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
	assert(Id < LAST_USED_SRV_ID && mSRVs.find(Id) != mSRVs.end());
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

DXGI_FORMAT VQRenderer::GetTextureFormat(TextureID Id) const
{
	CHECK_TEXTURE(mTextures, Id);
	return mTextures.at(Id).GetFormat();
}

void VQRenderer::GetTextureDimensions(TextureID Id, int& SizeX, int& SizeY, int& NumSlices, int& NumMips) const
{
	if (Id != INVALID_ID)
	{
		CHECK_TEXTURE(mTextures, Id);
		const Texture& tex = mTextures.at(Id);
		SizeX = tex.mWidth;
		SizeY = tex.mHeight;
		NumSlices = tex.mNumArraySlices;
		NumMips = tex.mMipMapCount;
	}
	else
	{
		Log::Warning("GetTextureDimensions() called on uninitialized texture w/ TexID=INVALID_ID");
		SizeX     = 0;
		SizeY     = 0;
		NumSlices = 0;
		NumMips   = 0;
	}
}

uint VQRenderer::GetTextureMips(TextureID Id) const
{
	if (Id == INVALID_ID) return 0;
	CHECK_TEXTURE(mTextures, Id);
	const Texture& tex = mTextures.at(Id);
	return tex.mMipMapCount;
}

uint VQRenderer::GetTextureSampleCount(TextureID Id) const
{
	CHECK_TEXTURE(mTextures, Id);
	const Texture& tex = mTextures.at(Id);
	assert(false);
	return 0; // TODO:
}

