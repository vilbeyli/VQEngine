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

#define NOMINMAX

#define FFX_DEBUG_LOG 1

#define A_CPU 1

#include "PostProcess.h"

#include "../Core/Types.h"
#if FFX_DEBUG_LOG
#include "../../VQUtils/Source/Log.h"
#endif

#include <cmath>

//#include "Shaders/AMDFidelityFX/CAS/ffx_a.h"
#include "Shaders/AMDFidelityFX/FSR1.0/ffx_a.h"
#include "Shaders/AMDFidelityFX/CAS/ffx_cas.h"
#include "Shaders/AMDFidelityFX/FSR1.0/ffx_fsr1.h"


FPostProcessParameters::FFSR_RCAS::FFSR_RCAS()
{
	memset(RCASConstantBlock, 0, sizeof(RCASConstantBlock));
}

FPostProcessParameters::FFSR_RCAS::FFSR_RCAS(const FFSR_RCAS& other)
	: RCASSharpnessStops(other.RCASSharpnessStops) 
{ 
	memcpy(RCASConstantBlock, other.RCASConstantBlock, sizeof(RCASConstantBlock)); 
}

float FPostProcessParameters::FFSR_RCAS::GetLinearSharpness() const { return std::powf(0.5f, this->RCASSharpnessStops); }
void FPostProcessParameters::FFSR_RCAS::SetLinearSharpness(float Sharpness) { this->RCASSharpnessStops = std::log10f(Sharpness) / std::log10f(0.5f); }
void FPostProcessParameters::FFSR_RCAS::UpdateRCASConstantBlock()
{
#if FFX_DEBUG_LOG
	Log::Info("[FidelityFX][FSR-RCAS]: FsrRcasCon() called with SharpnessStops=%.2f", this->RCASSharpnessStops);
#endif
	FsrRcasCon(reinterpret_cast<AU1*>(&this->RCASConstantBlock[0]), this->RCASSharpnessStops);
}

float FPostProcessParameters::FFSR_EASU::GetScreenPercentage() const
{
	switch (this->SelectedFSRPreset)
	{
	case FPostProcessParameters::FFSR_EASU::ULTRA_QUALITY: return 0.77f;
	case FPostProcessParameters::FFSR_EASU::QUALITY      : return 0.67f;
	case FPostProcessParameters::FFSR_EASU::BALANCED     : return 0.58f;
	case FPostProcessParameters::FFSR_EASU::PERFORMANCE  : return 0.50f;
	case FPostProcessParameters::FFSR_EASU::CUSTOM       : return fCustomScaling;
	}
	return 1.0f;
}

FPostProcessParameters::FFSR_EASU::FFSR_EASU()
	: SelectedFSRPreset(EPresets::ULTRA_QUALITY)
	, fCustomScaling(1.0f) 
{
	memset(EASUConstantBlock, 0, sizeof(EASUConstantBlock));
}
FPostProcessParameters::FFSR_EASU::FFSR_EASU(const FFSR_EASU& other)
{
	memcpy(EASUConstantBlock, other.EASUConstantBlock, sizeof(EASUConstantBlock));
	this->SelectedFSRPreset = other.SelectedFSRPreset;
	this->fCustomScaling = other.fCustomScaling;
}

void FPostProcessParameters::FFSR_EASU::UpdateEASUConstantBlock(
	  uint InputWidth
	, uint InputHeight
	, uint InputContainerWidth
	, uint InputContainerHeight
	, uint OutputWidth
	, uint OutputHeight)
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
