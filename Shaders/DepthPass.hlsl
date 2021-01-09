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

struct VSInput
{
	float3 position : POSITION;
#if ALPHA_MASK
	float2 uv       : TEXCOORD0;
#endif
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 worldPosition : COLOR0;
#if ALPHA_MASK
	float2 uv       : TEXCOORD0;
#endif
};

cbuffer CBuffer : register(b0)
{
	float4x4 matModelViewProj;
	float4x4 matWorld;
}

cbuffer CBufferPx : register(b1)
{
	float3 vLightPosition;
	float fFarPlane;
}

PSInput VSMain(VSInput vertex)
{
	PSInput result;
	
	result.position = mul(matModelViewProj, float4(vertex.position, 1));
	result.worldPosition = mul(matWorld, float4(vertex.position, 1));
	
#if ALPHA_MASK
	result.uv = vertex.uv;
#endif
	
    return result;
}

#if ALPHA_MASK
Texture2D    texDiffuseAlpha : register(t0);
SamplerState LinearSampler   : register(s0);
#endif
float PSMain(PSInput In) : SV_DEPTH
{
#if ALPHA_MASK
	float alpha = texDiffuseAlpha.SampleLevel(LinearSampler, In.uv, 0).a;
	if(alpha < 0.01f)
		discard;
#endif
	
	const float depth = length(vLightPosition - In.worldPosition);
	return depth / fFarPlane;
}
