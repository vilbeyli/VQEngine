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

// AMD Cauldron code
// 
// Copyright(c) 2018 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};


Texture2D        srcColor : register(t0);
SamplerState     samPoint : register(s0);
cbuffer          cb       : register(b0)
{
    uint     uImageWidth;
    uint     uImageHeight;
    int      iMousePosX; // driven by ImGuiIO.MousePos.x
    int      iMousePosY; // driven by ImGuiIO.MousePos.y

    float4   fBorderColorRGB;

    float    fMagnificationAmount;
    float    fMagnifierScreenRadius;
    int      iMagnifierOffsetX;
    int      iMagnifierOffsetY;
};

// TODO: move logic to function, add CS support
float4 Magnify(float2 uv)
{
    return 0.0.rrrr;
}

// TODO: get this VSMain from some common file instead of this duplicate
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

float4 PSMain(PSInput Input) : SV_Target
{
    const float2 ImageSize = float2(uImageWidth, uImageHeight);

    const float2 uv = Input.uv;
    float4 outColor = srcColor.Load(int3(uv*(ImageSize-0.5f.xx),0));

    const float  AspectRatio               = ImageSize.x / ImageSize.y;
    const float  AspectRatioInv            = ImageSize.y / ImageSize.x;
    const float2 MagnifierAreaCenter       = float2(iMousePosX, iMousePosY);
    const float2 MagnifierOnScreenLocation = float2(iMousePosX + iMagnifierOffsetX, iMousePosY + iMagnifierOffsetY);

    const float2 CurrPixel = uv * ImageSize;
    const float2 uvMagnifiedArea = float2(MagnifierAreaCenter / ImageSize);
    const float2 uvMagnifierOnScreen = float2(MagnifierOnScreenLocation / ImageSize);

    const float MagnifiedAreaR = fMagnifierScreenRadius / fMagnificationAmount;
    const float MagnifierOnScreenR = fMagnifierScreenRadius;


    float2 uvRArea = uv - uvMagnifiedArea;
    if (ImageSize.x > ImageSize.y)
        uvRArea.x *= AspectRatio;
    else
        uvRArea.y *= AspectRatioInv;

    float2 uvROnScreen = uv - uvMagnifierOnScreen;
    if (ImageSize.x > ImageSize.y)
        uvROnScreen.x *= AspectRatio;
    else
        uvROnScreen.y *= AspectRatioInv;

    const float BORDER_THICKNESS_OFFSET = 0.001f;

    const bool bAreWeInMagnifiedAreaRegion = length(uvRArea) < MagnifiedAreaR;
    const bool bAreWeInMagnifiedAreaBorderRegion = length(uvRArea) < (MagnifiedAreaR + BORDER_THICKNESS_OFFSET);


    const bool bAreWeInMagnifierOnScreenRegion = length(uvROnScreen) < MagnifierOnScreenR;
    const bool bAreWeInMagnifierOnScreenBorderRegion = length(uvROnScreen) < (MagnifierOnScreenR + BORDER_THICKNESS_OFFSET);

    if (bAreWeInMagnifiedAreaRegion)
    {
        return outColor;
    }
    if (bAreWeInMagnifierOnScreenBorderRegion || bAreWeInMagnifiedAreaBorderRegion)
    {
        outColor.r = fBorderColorRGB[0];
        outColor.g = fBorderColorRGB[1];
        outColor.b = fBorderColorRGB[2];
    }
    if (bAreWeInMagnifierOnScreenRegion)
    {
        float2 sampleUVOffsetFromCenter = uv - uvMagnifierOnScreen;
        sampleUVOffsetFromCenter /= fMagnificationAmount;
        const float2 magnifierUV = uvMagnifiedArea + sampleUVOffsetFromCenter;
        outColor = srcColor.Load(int3(magnifierUV*ImageSize-0.5f.xx,0));
        return outColor;
    }


    // draw lines tangent to both circles
    float2 currentToCenter = uv - uvMagnifiedArea;
    float2 centerToCenterLine = uvMagnifierOnScreen - uvMagnifiedArea;


    // coorect for aspect ration
    if (ImageSize.x > ImageSize.y)
    {
        currentToCenter.x *= AspectRatio;
        centerToCenterLine.x *= AspectRatio;
    }
    else
    {
        currentToCenter.y *= AspectRatio;
        centerToCenterLine.y *= AspectRatio;
    }


    float centerToCenterLength = length(centerToCenterLine);
    centerToCenterLine /= centerToCenterLength;

    float distanceOnCenterline = dot(currentToCenter, centerToCenterLine);
    float2 projOnCenterline = distanceOnCenterline * centerToCenterLine;
    float2 perpToCenterline = currentToCenter - projOnCenterline;


    // cosine of angle between center line and tangent radius line
    float alphaFactor = (MagnifierOnScreenR - MagnifiedAreaR) / centerToCenterLength;

    float centerToCenterPlusAlphaLength = centerToCenterLength - alphaFactor * (MagnifierOnScreenR - MagnifiedAreaR);

    distanceOnCenterline += alphaFactor * MagnifiedAreaR;
    if (distanceOnCenterline > 0 && distanceOnCenterline < centerToCenterPlusAlphaLength)
    {

        float t = distanceOnCenterline / centerToCenterPlusAlphaLength;
        // the tangent line is also tangent to a circle of a radius that is a linear interpolation of the two circles and the alpha factor
        float radiusAtPoint = ((1 - t) * MagnifiedAreaR + t * MagnifierOnScreenR) * sqrt(1 - alphaFactor * alphaFactor);

        if (abs(length(perpToCenterline) - radiusAtPoint) < BORDER_THICKNESS_OFFSET)
        {
            outColor.r = fBorderColorRGB[0];
            outColor.g = fBorderColorRGB[1];
            outColor.b = fBorderColorRGB[2];
        }
    }


    return outColor;
}
