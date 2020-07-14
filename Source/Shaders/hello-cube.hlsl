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

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
	float2 uv : TEXCOORD0;
};

struct PerFrame{};  // TODO
struct PerView {};  // TODO
struct PerObject{}; // TODO

cbuffer CBuffer : register(b0)
{
	float4x4 matModelViewProj;
}


Texture2D    texColor;
SamplerState Sampler;

PSInput VSMain(float4 position : POSITION, float4 color : COLOR, float2 uv : TEXCOORD0)
{
    PSInput result;

	result.position = mul(matModelViewProj, position);
    result.color = color;
	result.uv = uv;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 ColorTex  = texColor.SampleLevel(Sampler, input.uv, 0).rgb;
	float3 ColorVert = input.color * 0.8f; // Dim the vert colors a bit... they're too bright and look uglier.
	float3 Color = ColorVert * ColorTex;
	return float4(Color, 1);
}
