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

#include "../../Renderer/Renderer.h"

#include "../Core/Types.h"
#include <DirectXMath.h>
#include <dxgiformat.h>

#include <vector>
#include <array>

struct IRenderPassResourceCollection {};
struct IRenderPassDrawParameters {};
struct ID3D12RootSignature;
class VQRenderer;

// Interface for Render Passes
class IRenderPass
{
public:
	virtual ~IRenderPass() = 0;

	virtual bool Initialize() = 0;
	virtual void Destroy() = 0;

	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) = 0;
	virtual void OnDestroyWindowSizeDependentResources() = 0;

	virtual void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) = 0;

	//virtual std::vector<ID3D12RootSignature*> CreateAllPassRootSignatures();
	virtual std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() = 0;
};


// Base class for render passes.
// Keeps references of the common resources necessary for rendering/resource management.
// No instantiation of base class is allowed, only the derived can call the protected ctor.
class RenderPassBase : public IRenderPass
{
public:
	RenderPassBase() = delete;
	RenderPassBase(const RenderPassBase&) = delete;
	virtual ~RenderPassBase() = 0;

	virtual bool Initialize() = 0;
	virtual void Destroy() = 0;

	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) = 0;
	virtual void OnDestroyWindowSizeDependentResources() = 0;

	// TODO: shouldn't this be const? #threaded_access
	virtual void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) = 0;

	virtual std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() = 0;

protected:
	RenderPassBase(VQRenderer& RendererIn) : mRenderer(RendererIn) {};

protected:
	VQRenderer& mRenderer;
	// TODO: consider using a { subpassID<uint>, pRS } lookup in basepass?
};