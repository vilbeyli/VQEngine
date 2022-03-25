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

RWTexture2D<half>   outDepth   : register(u0);
RWTexture2D<float3> outNormals : register(u1);

cbuffer DepthResolveParameters : register(b0)
{
	uint ImageSizeX;
	uint ImageSizeY;
	uint SampleCount;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{ 
	if (dispatchThreadId.x > ImageSizeX || dispatchThreadId.y > ImageSizeY)
	{
		return;
	}
 
	float result = 0.0f;
	float3 result3 = 1.0f.xxx;
//------------------------------------------------------------------------------------------------
// TODO: implement the remaining cases when the engine supports the additional PSOs
//------------------------------------------------------------------------------------------------
//	     if (SampleCount == 2)  { [unroll] for (uint i = 0; i <  2; ++i) { result = min(result, texDepthMS.Load(dispatchThreadId.xy, i).r); result3.rgb = min(result3.rgb, texNormalsMS.Load(dispatchThreadId.xy, i).rgb);} }
//	else if (SampleCount == 4)  
//------------------------------------------------------------------------------------------------
	{ 
		half s0 = texDepthMS.Load(dispatchThreadId.xy, 0).r;
		half s1 = texDepthMS.Load(dispatchThreadId.xy, 1).r;
		half s2 = texDepthMS.Load(dispatchThreadId.xy, 2).r;
		half s3 = texDepthMS.Load(dispatchThreadId.xy, 3).r;
		half minDepth = min(min(min(s0, s1), s2), s3);

		int iSample = 0;
		if (minDepth == s1) iSample = 1;
		if (minDepth == s2) iSample = 2;
		if (minDepth == s3) iSample = 3;

		result = minDepth;
		result3 = texNormalsMS.Load(dispatchThreadId.xy, iSample).rgb;
	}

	//------------------------------------------------------------------------------------------------
// TODO: implement the remaining cases when the engine supports the additional PSOs
//------------------------------------------------------------------------------------------------
//	else if (SampleCount == 8)  { [unroll] for (uint k = 0; k <  8; ++k) { result = min(result, texDepthMS.Load(dispatchThreadId.xy, k).r); result3.rgb = min(result3.rgb, texNormalsMS.Load(dispatchThreadId.xy, k).rgb);} }
//	else if (SampleCount == 16) { [unroll] for (uint l = 0; l < 16; ++l) { result = min(result, texDepthMS.Load(dispatchThreadId.xy, l).r); result3.rgb = min(result3.rgb, texNormalsMS.Load(dispatchThreadId.xy, l).rgb);} }
//------------------------------------------------------------------------------------------------

	outDepth[dispatchThreadId.xy] = result;
	outNormals[dispatchThreadId.xy] = result3.rgb;
}