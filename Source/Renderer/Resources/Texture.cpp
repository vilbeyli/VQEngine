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

#include <vector>

// from Microsoft's D3D12HelloTexture
std::vector<uint8> FTexture::GenerateTexture_Checkerboard(uint Dimension, bool bUseMidtones /*= false*/)
{
    const uint TexturePixelSizeInBytes = 4; // byte/px
    const uint& TextureWidth = Dimension;
    const uint& TextureHeight = Dimension;

    const uint rowPitch = TextureWidth * TexturePixelSizeInBytes;
    const uint cellPitch = rowPitch / 8;      // The width of a cell in the texture.
    const uint cellHeight = TextureWidth / 8; // The height of a cell in the texture.
    const uint textureSize = rowPitch * TextureHeight;

    std::vector<uint8> data(textureSize);
    uint8* pData = &data[0];

    for (uint n = 0; n < textureSize; n += TexturePixelSizeInBytes)
    {
        uint x = n % rowPitch;
        uint y = n / rowPitch;
        uint i = x / cellPitch;
        uint j = y / cellHeight;

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
