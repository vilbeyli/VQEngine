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

struct IDXGIFactory6;
struct ID3D12Device;
struct IDXGIAdapter;

struct FDeviceCreateDesc
{
	bool bEnableDebugLayer = false;
	bool bEnableValidationLayer = false;
	IDXGIFactory6* pFactory = nullptr;
};


class Device 
{
public:
	bool Create(const FDeviceCreateDesc& desc);
	void Destroy();

	inline ID3D12Device* GetDevicePtr()  const { return mpDevice; }
	inline IDXGIAdapter* GetAdapterPtr() const { return mpAdapter; }

	unsigned GetDeviceMemoryMax() const;
	unsigned GetDeviceMemoryAvailable() const;

private:
	ID3D12Device* mpDevice  = nullptr;
	IDXGIAdapter* mpAdapter = nullptr;
	// TODO: Multi-adapter systems: https://docs.microsoft.com/en-us/windows/win32/direct3d12/multi-engine

	//UINT mRTVDescIncremenetSize = 0;
};