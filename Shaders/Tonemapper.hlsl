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

//
// COLOR SPACE CONVERSIONS
//

//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// These values must match the EDisplayCurve enum in HDR.h
#define DISPLAY_CURVE_SRGB      0
#define DISPLAY_CURVE_ST2084    1
#define DISPLAY_CURVE_LINEAR    2

// These values must match the EColorSpace enum in HDR.h
#define COLOR_SPACE__REC_709    0
#define COLOR_SPACE__REC_2020   1

// PQ Curve theoretical reference display of 10,000 nits
#define ST2084_MAX 10000.0f

float3 xyYToRec709(float2 xy, float Y = 1.0)
{
    // https://github.com/ampas/aces-dev/blob/v1.0.3/transforms/ctl/README-MATRIX.md
    static const float3x3 XYZtoRGB =
    {
        3.2409699419, -1.5373831776, -0.4986107603,
        -0.9692436363, 1.8759675015, 0.0415550574,
        0.0556300797, -0.2039769589, 1.0569715142
    };
    float3 XYZ = Y * float3(xy.x / xy.y, 1.0, (1.0 - xy.x - xy.y) / xy.y);
    float3 RGB = mul(XYZtoRGB, XYZ);
    float maxChannel = max(RGB.r, max(RGB.g, RGB.b));
    return RGB / max(maxChannel, 1.0);
}

float3 xyYToRec2020(float2 xy, float Y = 1.0)
{
    // https://github.com/ampas/aces-dev/blob/v1.0.3/transforms/ctl/README-MATRIX.md
    static const float3x3 XYZtoRGB =
    {
        1.7166511880, -0.3556707838, -0.2533662814,
        -0.6666843518, 1.6164812366, 0.0157685458,
        0.0176398574, -0.0427706133, 0.9421031212
    };
    float3 XYZ = Y * float3(xy.x / xy.y, 1.0, (1.0 - xy.x - xy.y) / xy.y);
    float3 RGB = mul(XYZtoRGB, XYZ);
    float maxChannel = max(RGB.r, max(RGB.g, RGB.b));
    return RGB / max(maxChannel, 1.0);
}

float3 LinearToSRGB(float3 color)
{
    // Approximately pow(color, 1.0 / 2.2)
    return color < 0.0031308 ? 12.92 * color : 1.055 * pow(abs(color), 1.0 / 2.4) - 0.055;
}

float3 SRGBToLinear(float3 color)
{
    // Approximately pow(color, 2.2)
    return color < 0.04045 ? color / 12.92 : pow(abs(color + 0.055) / 1.055, 2.4);
}

float3 Rec709ToRec2020(float3 color)
{
    static const float3x3 conversion =
    {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return mul(conversion, color);
}

float3 Rec2020ToRec709(float3 color)
{
    static const float3x3 conversion =
    {
        1.660496, -0.587656, -0.072840,
        -0.124547, 1.132895, -0.008348,
        -0.018154, -0.100597, 1.118751
    };
    return mul(conversion, color);
}

float3 LinearToST2084(float3 color)
{
    float m1 = 2610.0 / 4096.0 / 4;
    float m2 = 2523.0 / 4096.0 * 128;
    float c1 = 3424.0 / 4096.0;
    float c2 = 2413.0 / 4096.0 * 32;
    float c3 = 2392.0 / 4096.0 * 32;
    float3 cp = pow(abs(color), m1);
    return pow((c1 + c2 * cp) / (1 + c3 * cp), m2);
}

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
            OutRGB = float4(1, 1, 0, 1);
            break;

    }

    texColorOutput[DispatchThreadID.xy] = float4(OutRGB, InRGBA.a);
}
