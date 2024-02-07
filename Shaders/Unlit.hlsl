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

#if INSTANCED_DRAW
#ifndef INSTANCE_COUNT
#define INSTANCE_COUNT 512
#endif
#endif

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
	float4 color : COLOR0;
};

cbuffer CBuffer : register(b0)
{
#if INSTANCED_DRAW
	float4x4 matModelViewProj[INSTANCE_COUNT];
#else
	float4x4 matModelViewProj;
#endif

	float4 color;
}


PSInput VSMain(VSInput vertex, uint instID : SV_InstanceID)
{
	PSInput result;
	
#if INSTANCED_DRAW
	result.position = mul(matModelViewProj[instID], float4(vertex.position, 1));
#else
	result.position = mul(matModelViewProj, float4(vertex.position, 1));
#endif
	result.color = color;
	
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.color);
}
