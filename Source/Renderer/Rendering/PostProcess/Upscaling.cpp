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

#define FFX_DEBUG_LOG 1

#define A_CPU 1

#include "PostProcess.h"

#include "Engine/Core/Types.h"
#if FFX_DEBUG_LOG
#include "Libs/VQUtils/Include/Log.h"
#endif

#include <cmath>

//#include "Shaders/AMD/CAS/ffx_a.h"
#include "Shaders/AMD/FSR1.0/ffx_a.h"
#include "Shaders/AMD/CAS/ffx_cas.h"
#include "Shaders/AMD/FSR1.0/ffx_fsr1.h"

namespace AMD_FidelityFX_SuperResolution1
{
	float FShaderParameters::RCAS::GetLinearSharpness() const { return std::powf(0.5f, this->RCASSharpnessStops); }
	void  FShaderParameters::RCAS::SetLinearSharpness(float Sharpness) { this->RCASSharpnessStops = std::log10f(Sharpness) / std::log10f(0.5f); }
	void  FShaderParameters::RCAS::UpdateConstantBlock()
	{
#if FFX_DEBUG_LOG
		Log::Info("[FidelityFX][FSR-RCAS]: FsrRcasCon() called with SharpnessStops=%.2f", this->RCASSharpnessStops);
#endif
		FsrRcasCon(reinterpret_cast<AU1*>(&this->RCASConstantBlock[0]), this->RCASSharpnessStops);
	}

	void FShaderParameters::EASU::UpdateConstantBlock(
		uint InputWidth
		, uint InputHeight
		, uint InputContainerWidth
		, uint InputContainerHeight
		, uint OutputWidth
		, uint OutputHeight
	)
	{
#if FFX_DEBUG_LOG
		Log::Info("[FidelityFX][Super Resolution]: FsrEasuCon() called with InputResolution=%ux%u, ContainerDimensions=%ux%u, OutputResolution==%ux%u",
			InputWidth
			, InputHeight
			, InputContainerWidth
			, InputContainerHeight
			, OutputWidth
			, OutputHeight);
#endif
		FsrEasuCon(
			reinterpret_cast<AU1*>(&this->EASUConstantBlock[0])
			, reinterpret_cast<AU1*>(&this->EASUConstantBlock[4])
			, reinterpret_cast<AU1*>(&this->EASUConstantBlock[8])
			, reinterpret_cast<AU1*>(&this->EASUConstantBlock[12])
			, static_cast<AF1>(InputWidth) // This the rendered image resolution being upscaled
			, static_cast<AF1>(InputHeight)
			, static_cast<AF1>(InputContainerWidth) // This is the resolution of the resource containing the input image (useful for dynamic resolution)
			, static_cast<AF1>(InputContainerHeight)
			, static_cast<AF1>(OutputWidth) // This is the display resolution which the input image gets upscaled to
			, static_cast<AF1>(OutputHeight)
		);
	}

#if !DISABLE_FIDELITYFX_CAS
	void FPostProcessParameters::FFFXCAS::UpdateCASConstantBlock(
		uint InputWidth
		, uint InputHeight
		, uint OutputWidth
		, uint OutputHeight)
	{
#if FFX_DEBUG_LOG
		Log::Info("[FidelityFX][CAS]: CasSetup() called with Sharpness=%.2f, InputResolution=%ux%u, OutputResolution==%ux%u",
			this->CASSharpen
			, InputWidth
			, InputHeight
			, OutputWidth
			, OutputHeight);
#endif
		CasSetup(&this->CASConstantBlock[0], &this->CASConstantBlock[4], this->CASSharpen,
			static_cast<AF1>(InputWidth),
			static_cast<AF1>(InputHeight),  // input resolution
			static_cast<AF1>(OutputWidth),
			static_cast<AF1>(OutputHeight)  // output resolution
		);
	}
#endif
}
