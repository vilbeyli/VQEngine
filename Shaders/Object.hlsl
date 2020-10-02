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
	float3 normal   : NORMAL;
	float3 tangent : TANGENT;
	float2 uv : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
	float3 vertNormal : COLOR0;
	float3 vertTangent : COLOR1;
	float2 uv : TEXCOORD0;
};

cbuffer CBuffer : register(b0)
{
	float4x4 matModelViewProj;
	int iTextureConfig;
	int iTextureOut;
}


Texture2D texDiffuse   : register(t0);
Texture2D texNormals   : register(t1);
Texture2D texEmissive  : register(t2);
Texture2D texAlphaMask : register(t3);
Texture2D texMetalness : register(t4);
Texture2D texRoughness : register(t5);

SamplerState LinearSampler : register(s0);
SamplerState PointSampler  : register(s1);

PSInput VSMain(VSInput vertex)
{
	PSInput result;
	
	result.position    = mul(matModelViewProj, float4(vertex.position, 1));
	result.uv          = vertex.uv;
	result.vertNormal  = vertex.normal;
	result.vertTangent = vertex.tangent;
	
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	const float2 uv = input.uv;
	
	float3 Albedo    = texDiffuse.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 Normal    = texNormals.SampleLevel(PointSampler, uv, 0).rgb;
	float3 Emissive  = texEmissive.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 AlphaMask = texAlphaMask.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 Metalness = texMetalness.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 Roughness = texRoughness.SampleLevel(LinearSampler, uv, 0).rgb;
	
	float3 OutColor = 0.0.xxx;
	switch (iTextureOut)
	{
		case 0: OutColor = Albedo; break;
		case 1: OutColor = Normal; break;
		case 2: OutColor = Emissive; break;
		case 3: OutColor = AlphaMask; break;
		case 4: OutColor = Metalness; break;
		case 5: OutColor = Roughness; break;
		default: OutColor = float3(0.8, 0, 0.8); break;
	}
	
	return float4(OutColor, 1);
}
