//	VQE
//	Copyright(C) 2025  - Volkan Ilbeyli
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

#include "Core/Types.h"
#include <mutex>
#include <array>
#include <vector>

struct FLoadingScreenData
{
	std::array<float, 4> SwapChainClearColor;

	int SelectedLoadingScreenSRVIndex = INVALID_ID;
	std::mutex Mtx;
	std::vector<SRV_ID> SRVs;

	SRV_ID GetSelectedLoadingScreenSRV_ID() const;
	void RotateLoadingScreenImageIndex();

	// TODO: animation resources
};
