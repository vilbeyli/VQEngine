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

#include <d3d12.h>
#include "GPUMarker.h"

ScopedMarker::ScopedMarker(const char* pLabel, unsigned PIXColor)
{
	PIXBeginEvent(PIXColor, pLabel);
}
ScopedMarker::~ScopedMarker()
{
	PIXEndEvent();
}

// https://devblogs.microsoft.com/pix/winpixeventruntime/#:~:text=An%20%E2%80%9Cevent%E2%80%9D%20represents%20a%20region,a%20single%20point%20in%20time.
// https://devblogs.microsoft.com/pix/pix-2008-26-new-capture-layer/

ScopedGPUMarker::ScopedGPUMarker(ID3D12GraphicsCommandList* pCmdList, const char* pLabel, unsigned PIXColor)
	: mpCmdList(pCmdList)
{
	PIXBeginEvent(mpCmdList, (unsigned long long)PIXColor, pLabel);
}

ScopedGPUMarker::ScopedGPUMarker(ID3D12CommandQueue* pCmdQueue, const char* pLabel, unsigned PIXColor)
	:mpCmdQueue(pCmdQueue)
{
	PIXBeginEvent(mpCmdQueue, PIXColor, pLabel);
}

ScopedGPUMarker::~ScopedGPUMarker()
{
	PIXEndEvent(mpCmdList);
}
