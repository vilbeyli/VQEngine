//	VQE
//	Copyright(C) 2024  - Volkan Ilbeyli
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

#define VQ_GPU 1
#include "LightingConstantBufferData.h"


struct VSInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR0;
};

cbuffer OutlineData : register(b0)
{
	float4x4 matWorldViewProj;
	float4x4 matNormalViewProj;
	float4 Color;
	float Scale;
}


PSInput VSMain(VSInput VSIn)
{
	PSInput result;
	
	float distance = 0.1; // *Scale;
	float3 offset = VSIn.normal * distance;
	
	result.position = mul(matWorldViewProj, float4(VSIn.position, 1));
	float3 clipOffset = normalize(mul(matNormalViewProj, float4(VSIn.normal, 0)));
	result.position = mul(matWorldViewProj, float4(VSIn.position*1.01, 1));
	//result.position.xyz += offset;
	
	result.color = Color;
	return result;
}

float4 PSMain(PSInput In) : SV_TARGET
{	
	return In.color;
}
