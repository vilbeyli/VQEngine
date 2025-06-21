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

#include "RenderPass.h"
#include "FSR3UpscalePass.h"
#include "Renderer.h"

FSR3UpscalePass::FSR3UpscalePass(VQRenderer& Renderer)
	: RenderPassBase(Renderer)
{
}

FSR3UpscalePass::~FSR3UpscalePass()
{
}

bool FSR3UpscalePass::Initialize()
{
	ID3D12Device* pDevice = mRenderer.GetDevicePtr();
	//Context.Initialize(pDevice);
	return true;
}

void FSR3UpscalePass::Destroy()
{
}

void FSR3UpscalePass::OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters)
{

}

void FSR3UpscalePass::OnDestroyWindowSizeDependentResources()
{
}

void FSR3UpscalePass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
}

std::vector<FPSOCreationTaskParameters> FSR3UpscalePass::CollectPSOCreationParameters()
{
	return std::vector<FPSOCreationTaskParameters>();
}
