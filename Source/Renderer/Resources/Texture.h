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

#pragma once

#include <dxgiformat.h>

#include "Engine/Core/Types.h"
#include <vector>

struct ID3D12Resource;
namespace D3D12MA { class Allocation; }

struct FTexture
{
    ID3D12Resource* Resource = nullptr;
    D3D12MA::Allocation* Allocation = nullptr;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    int Width = 0;
    int Height = 0;
    int ArraySlices = 1;
    int MipCount = 1;
    bool IsCubemap = false;
    bool UsesAlphaChannel = false;
    bool IsTypeless = false;

    void Reset()
    {
        Resource = nullptr;
        Allocation = nullptr;
        Format = DXGI_FORMAT_UNKNOWN;
        Width = Height = ArraySlices = MipCount = 0;
        IsCubemap = UsesAlphaChannel = IsTypeless = false;
    }

    // procedural texture generators
    static std::vector<uint8> GenerateTexture_Checkerboard(uint Dimension, bool bUseMidtones = false);
};