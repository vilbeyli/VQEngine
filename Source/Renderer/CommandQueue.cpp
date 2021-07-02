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


#include "CommandQueue.h"
#include "Device.h"
#include "../../Libs/VQUtils/Source/Log.h"
#include "Common.h"

#include <d3d12.h>
#include <cassert>

void CommandQueue::Create(Device* pDevice, EType type, const char* pName /*= nullptr*/)
{
	HRESULT hr = {};
	ID3D12Device* pDevice_ = pDevice->GetDevicePtr();
	D3D12_COMMAND_QUEUE_DESC qDesc = {};

	qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	qDesc.NodeMask = 0;
	qDesc.Priority = 0;
	switch (type)
	{
	case CommandQueue::GFX     : qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;  break;
	case CommandQueue::COMPUTE : qDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE; break;
	case CommandQueue::COPY    : qDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;    break;
	default: assert(false); break;
	}

	hr = pDevice_->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&this->pQueue));
	if (FAILED(hr))
	{
		Log::Error("Couldn't create Command List: %s", "TODO:reason");
	}
	if (pName)
		SetName(this->pQueue, pName);
}

void CommandQueue::Destroy()
{
	if (pQueue) pQueue->Release();
}
