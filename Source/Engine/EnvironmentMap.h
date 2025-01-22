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

#include <string>
#include "Core/Types.h"

class VQRenderer;

struct FEnvironmentMapDescriptor
{
	std::string Name;
	std::string FilePath;
	float MaxContentLightLevel = 0.0f;
};

struct FEnvironmentMapRenderingResources
{
	TextureID Tex_HDREnvironment = INVALID_ID; // equirect input
	TextureID Tex_IrradianceDiff = INVALID_ID; // Kd
	TextureID Tex_IrradianceSpec = INVALID_ID; // Ks

	// temporary resources
	TextureID Tex_BlurTemp = INVALID_ID;
	TextureID Tex_IrradianceDiffBlurred = INVALID_ID; // Kd

	RTV_ID RTV_IrradianceDiff = INVALID_ID;
	RTV_ID RTV_IrradianceSpec = INVALID_ID;

	SRV_ID SRV_HDREnvironment = INVALID_ID;
	SRV_ID SRV_IrradianceDiff = INVALID_ID;
	SRV_ID SRV_IrradianceSpec = INVALID_ID;
	SRV_ID SRV_IrradianceDiffFaces[6] = { INVALID_ID };
	SRV_ID SRV_IrradianceDiffBlurred = INVALID_ID;

	UAV_ID UAV_BlurTemp = INVALID_ID;
	UAV_ID UAV_IrradianceDiffBlurred = INVALID_ID;

	SRV_ID SRV_BlurTemp = INVALID_ID;

	SRV_ID SRV_BRDFIntegrationLUT = INVALID_ID;

	//
	// HDR10 Static Metadata Parameters -------------------------------
	// https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_5/ns-dxgi1_5-dxgi_hdr_metadata_hdr10
	//
	// The maximum content light level (MaxCLL). This is the nit value 
	// corresponding to the brightest pixel used anywhere in the content.
	int MaxContentLightLevel = 0;

	// The maximum frame average light level (MaxFALL). 
	// This is the nit value corresponding to the average luminance of 
	// the frame which has the brightest average luminance anywhere in 
	// the content.
	int MaxFrameAverageLightLevel = 0;

	int GetNumSpecularIrradianceCubemapLODLevels(const VQRenderer& Renderer) const;
};
