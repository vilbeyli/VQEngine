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

Texture2DMS<half>   texDepthMS   : register(t0);
Texture2DMS<float3> texNormalsMS : register(t1);
Texture2DMS<half4>  texSceneColorRoughnessMS: register(t2);

RWTexture2D<half>    outDepth     : register(u0);
RWTexture2D<float3>  outNormals   : register(u1);
RWTexture2D<float4>  outSceneColorRoughness : register(u2);


cbuffer DepthResolveParameters : register(b0)
{
	uint ImageSizeX;
	uint ImageSizeY;
	uint SampleCount;
}


[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{ 
	if (dispatchThreadId.x >= ImageSizeX || dispatchThreadId.y >= ImageSizeY)
	{
		return;
	}
 
	float  Depth     = 0.0f;
	float3 Normal    = 1.0f.xxx;
	float  Roughness = 0.0f;

	{ 
		// read depth samples
		half s0 = texDepthMS.Load(dispatchThreadId.xy, 0).r;
		half s1 = texDepthMS.Load(dispatchThreadId.xy, 1).r;
		half s2 = texDepthMS.Load(dispatchThreadId.xy, 2).r;
		half s3 = texDepthMS.Load(dispatchThreadId.xy, 3).r;

		// find minimum depth value among samples
		half minDepth = min(min(min(s0, s1), s2), s3);

		// find the index of the minimum sample
		int iSample = 0;
		if (minDepth == s1) iSample = 1;
		if (minDepth == s2) iSample = 2;
		if (minDepth == s3) iSample = 3;

		// --------------------- resolves ---------------------

#if OUTPUT_DEPTH
		// resolve depth
		Depth = minDepth;
#endif
		
#if OUTPUT_NORMALS
		// resolve normals
	#if 1 // avg normals
		float3 Normal0 = texNormalsMS.Load(dispatchThreadId.xy, 0).rgb * 2.0f - 1.0f;
		float3 Normal1 = texNormalsMS.Load(dispatchThreadId.xy, 1).rgb * 2.0f - 1.0f;
		float3 Normal2 = texNormalsMS.Load(dispatchThreadId.xy, 2).rgb * 2.0f - 1.0f;
		float3 Normal3 = texNormalsMS.Load(dispatchThreadId.xy, 3).rgb * 2.0f - 1.0f;
		Normal = (normalize((Normal0+Normal1+Normal2+Normal3)*0.25f) + 1.0f.xxx)*0.5f;
	#else
		Normal = texNormalsMS.Load(dispatchThreadId.xy, iSample).rgb;
	#endif
#endif

#if OUTPUT_ROUGHNESS
		// resolve roughness
		Roughness = texSceneColorRoughnessMS.Load(dispatchThreadId.xy, iSample).a;
#endif
	}

	// write out 
#if OUTPUT_DEPTH
	outDepth[dispatchThreadId.xy] = Depth;
#endif
#if OUTPUT_NORMALS
	outNormals[dispatchThreadId.xy] = Normal;
#endif
#if OUTPUT_ROUGHNESS
	outSceneColorRoughness[dispatchThreadId.xy] = float4(outSceneColorRoughness[dispatchThreadId.xy].rgb, Roughness);
#endif
}