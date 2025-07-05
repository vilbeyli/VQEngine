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

#include "ShadingMath.hlsl"

#include "BRDF.hlsl"

struct PSInput
{
	float4 position             : SV_POSITION;
	float2 uv                   : TEXCOORD0;
	float3 CubemapLookDirection : TEXCOORD1;
};

cbuffer CBuffer : register(b0)
{
	float4x4 matViewProj;
}


Texture2D    texEquirectEnvironmentMap;
SamplerState Sampler;


PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD0)
{
	PSInput result;

	result.position = mul(matViewProj, position).xyww; // Z=1 for depth testing
	result.uv = uv;
	result.CubemapLookDirection = normalize(position.xyz);

	return result;
}


struct PSOutput
{
	float4 color : SV_TARGET0;
#if PS_OUTPUT_MASK
	float transparencyAndCompositionMask : SV_TARGET1;
#endif
};

PSOutput PSMain(PSInput input)
{
	PSOutput o = (PSOutput) 0;
	
	float2 uv = DirectionToEquirectUV(normalize(input.CubemapLookDirection));
	float3 ColorTex = texEquirectEnvironmentMap.SampleLevel(Sampler, uv, 0).rgb;
	o.color = float4(ColorTex, 1);
	
#if PS_OUTPUT_MASK
	o.transparencyAndCompositionMask = 1.0f;
#endif
	
	return o;
}
