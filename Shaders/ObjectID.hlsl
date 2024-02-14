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

#if INSTANCED_DRAW
	#ifndef INSTANCE_COUNT
	#define INSTANCE_COUNT 64
	#endif
#endif

struct VSInput
{
	float3 position : POSITION;
	float2 uv       : TEXCOORD0;
#if INSTANCED_DRAW
	uint instanceID : SV_InstanceID;
#endif
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
#if INSTANCED_DRAW
	uint instanceID : INSTANCEID;
#endif
};

cbuffer CBPerObject : register(b2)
{
	PerObjectData cbPerObject;
}


SamplerState LinearSampler : register(s0);

Texture2D texDiffuse : register(t0);


PSInput VSMain(VSInput VSIn)
{
	PSInput result;
	
#if INSTANCED_DRAW
	result.position = mul(cbPerObject.matWorldViewProj[VSIn.instanceID], float4(VSIn.position, 1));
#else
	result.position = mul(cbPerObject.matWorldViewProj, float4(VSIn.position, 1));
#endif
	
	result.uv = VSIn.uv;
#if INSTANCED_DRAW
	result.instanceID = VSIn.instanceID;
#endif
    return result;
}

int4 PSMain(PSInput In) : SV_TARGET
{
	const float2 uv = In.uv * cbPerObject.materialData.uvScaleOffset.xy + cbPerObject.materialData.uvScaleOffset.zw;

#if ALPHA_MASK
	float4 AlbedoAlpha = texDiffuse.Sample(AnisoSampler, uv);
	if (AlbedoAlpha.a < 0.01f)
		discard;
#endif
#if INSTANCED_DRAW
	int objID = cbPerObject.ObjID[In.instanceID].x;
#else
	int objID = cbPerObject.ObjID;
#endif
	return int4(objID, cbPerObject.meshID, cbPerObject.materialID, -111);
}
