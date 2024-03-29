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

#include "HDR.hlsl"

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

cbuffer CBuffer : register(b0)
{
	float Brightness;
}

Texture2D SceneHDRTexture;
Texture2D UITexture;

PSInput VSMain(uint id : SV_VertexID)
{
	PSInput VSOut;

	// Fullscreen Triangle: https://rauwendaal.net/2014/06/14/rendering-a-screen-covering-triangle-in-opengl/
	//
	// V0.pos = (-1, -1) | V0.uv = (0,  1)
	// V1.pos = ( 3, -1) | V1.uv = (2,  1)
	// V2.pos = (-1,  3) | V2.uv = (0, -1)
	VSOut.position.x = -1.0f + (float)((id & 1) << 2);
	VSOut.position.y = -1.0f + (float)((id & 2) << 1);
	VSOut.uv[0] = 0.0f + (float)((id & 1) << 1);
	VSOut.uv[1] = 1.0f - (float)((id & 2) << 0);

	VSOut.position.z = 0.0f;
	VSOut.position.w = 1.0f;
	return VSOut;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	// sample source images: UI<SDR> + PostProcessed Scene<HDR>
	float4 UITexSample = UITexture.Load(int3(input.position.xy, 0));
	float4 ColorTexSample = SceneHDRTexture.Load(int3(input.position.xy, 0));

	// check if the pixel is completely black (= UI pass didn't write on top)
	bool bIsUIPixel = dot(UITexSample.rgb, UITexSample.rgb) != 0;

	// de-gamma the SDR UI content and get to linear space
	float3 LinearUIColor = SRGBToLinear(UITexSample.rgb);

	float3 OutColor = OutColor = ColorTexSample.rgb;
	if (bIsUIPixel)
	{
		// UI is currently not transparent
		OutColor = LinearUIColor.rgb * Brightness /* + ColorTexSample.rgb * (1.0f - UITexSample.a)*/;
	}
	float  OutAlpha = 1.0f;

	return float4(OutColor, OutAlpha);
}
