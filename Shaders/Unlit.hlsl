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
	float3 color : COLOR0;
};

cbuffer CBuffer : register(b0)
{
	float4x4 matModelViewProj;
	float3 color;
}


PSInput VSMain(VSInput vertex)
{
	PSInput result;
	
	result.position    = mul(matModelViewProj, float4(vertex.position, 1));
	result.color = color;
	
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.color, 1);
}
