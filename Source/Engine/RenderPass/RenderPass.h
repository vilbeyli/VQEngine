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

struct ID3D12Device;
struct IRenderPass
{
	virtual bool Initialize(ID3D12Device* pDevice) = 0;
	virtual void Exit() = 0;

	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const void* pRscParameters = nullptr) = 0;
	virtual void OnDestroyWindowSizeDependentResources() = 0;

	virtual void RecordCommands(const void* pDrawParameters = nullptr) = 0;
};

