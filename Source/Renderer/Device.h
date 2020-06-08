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

#include <d3d12.h>
#include <dxgi.h>

struct FDeviceCreateDesc
{
	bool bEnableDebugLayer = (_DEBUG != 0);
	bool bEnableValidationLayer = false;
	
};


class Device 
{
public:
	bool Create(const FDeviceCreateDesc& desc);
	void Destroy();

private:
	ID3D12Device* mpDevice  = nullptr;
	IDXGIAdapter* mpAdapter = nullptr;
};