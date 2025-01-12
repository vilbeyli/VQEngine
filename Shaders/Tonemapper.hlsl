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

//--------------------------------------------------------------------------------------
// Reinhard tone mapper
//--------------------------------------------------------------------------------------
float3 Tonemap_Reinhard(float3 color)
{
	return color / (color + 1.0f.xxx);
}

//--------------------------------------------------------------------------------------
// Timothy Lottes tone mapper
//--------------------------------------------------------------------------------------
// General tonemapping operator, build 'b' term.
float ColToneB(float hdrMax, float contrast, float shoulder, float midIn, float midOut)
{
	return
        -((-pow(midIn, contrast) + (midOut * (pow(hdrMax, contrast * shoulder) * pow(midIn, contrast) -
            pow(hdrMax, contrast) * pow(midIn, contrast * shoulder) * midOut)) /
            (pow(hdrMax, contrast * shoulder) * midOut - pow(midIn, contrast * shoulder) * midOut)) /
            (pow(midIn, contrast * shoulder) * midOut));
}

// General tonemapping operator, build 'c' term.
float ColToneC(float hdrMax, float contrast, float shoulder, float midIn, float midOut)
{
	return (pow(hdrMax, contrast * shoulder) * pow(midIn, contrast) - pow(hdrMax, contrast) * pow(midIn, contrast * shoulder) * midOut) /
           (pow(hdrMax, contrast * shoulder) * midOut - pow(midIn, contrast * shoulder) * midOut);
}

// General tonemapping operator, p := {contrast,shoulder,b,c}.
float ColTone(float x, float4 p)
{
	float z = pow(x, p.r);
	return z / (pow(z, p.g) * p.b + p.a);
}
float3 TimothyTonemapper(float3 color)
{
	static float hdrMax = 16.0; // How much HDR range before clipping. HDR modes likely need this pushed up to say 25.0.
	static float contrast = 2.0; // Use as a baseline to tune the amount of contrast the tonemapper has.
	static float shoulder = 1.0; // Likely don’t need to mess with this factor, unless matching existing tonemapper is not working well..
	static float midIn = 0.18; // most games will have a {0.0 to 1.0} range for LDR so midIn should be 0.18.
	static float midOut = 0.18; // Use for LDR. For HDR10 10:10:10:2 use maybe 0.18/25.0 to start. For scRGB, I forget what a good starting point is, need to re-calculate.

	float b = ColToneB(hdrMax, contrast, shoulder, midIn, midOut);
	float c = ColToneC(hdrMax, contrast, shoulder, midIn, midOut);

#define EPS 1e-6f
	float peak = max(color.r, max(color.g, color.b));
	peak = max(EPS, peak);

	float3 ratio = color / peak;
	peak = ColTone(peak, float4(contrast, shoulder, b, c));
    // then process ratio

    // probably want send these pre-computed (so send over saturation/crossSaturation as a constant)
	float crosstalk = 4.0; // controls amount of channel crosstalk
	float saturation = contrast; // full tonal range saturation control
	float crossSaturation = contrast * 16.0; // crosstalk saturation

	float white = 1.0;

    // wrap crosstalk in transform
	ratio = pow(abs(ratio), saturation / crossSaturation);
	ratio = lerp(ratio, white, pow(peak, crosstalk));
	ratio = pow(abs(ratio), crossSaturation);

    // then apply ratio to peak
	color = peak * ratio;
	return color;
}


//
// RESOURCE BINDING
//
Texture2D           texColorInput;
RWTexture2D<float4> texColorOutput;

cbuffer TonemapperParameters : register(b0)
{
    int   ContentColorSpaceEnum;
    int   OutputDisplayCurveEnum;
    float DisplayReferenceBrightnessLevel;
    int   ToggleGammaCorrection;
}

//
// ENTRY POINT
//

[numthreads(8, 8, 1)]
void CSMain(
    uint3 LocalThreadId    : SV_GroupThreadID
  , uint3 WorkGroupId      : SV_GroupID
  , uint3 DispatchThreadID : SV_DispatchThreadID
) 
{
    float4 InRGBA = texColorInput[DispatchThreadID.xy]; // scene color in linear space
    float3 OutRGB = 0.0.xxx;
    
    switch (OutputDisplayCurveEnum)
    {
        case DISPLAY_CURVE_SRGB   : 
            // Reinhard tonemap
            OutRGB = Tonemap_Reinhard(InRGBA.rgb);
			//OutRGB = TimothyTonemapper(InRGBA.rgb);
            if(ToggleGammaCorrection)
                OutRGB = LinearToSRGB(OutRGB.rgb);
            break;
        
        case DISPLAY_CURVE_ST2084 : // indicates Display color space is Rec2020
        {
            const float HDR_Scalar = DisplayReferenceBrightnessLevel / ST2084_MAX;
            OutRGB = InRGBA.rgb;
            if(ContentColorSpaceEnum == COLOR_SPACE__REC_709)
            {
                OutRGB = Rec709ToRec2020(OutRGB);
            }
            OutRGB = LinearToST2084(OutRGB * HDR_Scalar); 
        } break;
        
        case DISPLAY_CURVE_LINEAR:
            OutRGB = InRGBA.rgb;
            break;
        default:
            OutRGB = float4(1, 1, 0, 1).rgb;
            break;

    }

    texColorOutput[DispatchThreadID.xy] = float4(OutRGB, InRGBA.a);
}
