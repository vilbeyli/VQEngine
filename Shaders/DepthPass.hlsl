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
#if ALPHA_MASK
	float2 uv       : TEXCOORD0;
#endif
};

cbuffer CBuffer : register(b0)
{
	float4x4 matModelViewProj;
}


PSInput VSMain(VSInput vertex)
{
	PSInput result;
	
	result.position = mul(matModelViewProj, float4(vertex.position, 1));
#if ALPHA_MASK
	result.uv = vertex.uv;
#endif
	
    return result;
}

#if ALPHA_MASK
Texture2D    texDiffuseAlpha : register(t0);
SamplerState LinearSampler   : register(s0);
float4 PSMain(PSInput input) : SV_TARGET
{
	float alpha = texDiffuseAlpha.SampleLevel(LinearSampler, input.uv, 0).a;
	if(alpha < 0.01f)
		discard;
	return 0.0f.xxxx;
}
#endif
