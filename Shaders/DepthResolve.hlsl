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

Texture2DMS<float>  texDepthMS   : register(t0);
Texture2DMS<float3> texNormalsMS : register(t1);

RWTexture2D<float>  outDepth   : register(u0);
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
 
	float3 result = 1.0f.xxx;
	     if (SampleCount == 2) [unroll] for (uint i = 0; i <  2; ++i) { result.r = min(result.r, texDepthMS.Load(dispatchThreadId.xy, i).r); }
	else if (SampleCount == 4) [unroll] for (uint j = 0; j <  4; ++j) { result.r = min(result.r, texDepthMS.Load(dispatchThreadId.xy, j).r); }
	else if (SampleCount == 8) [unroll] for (uint k = 0; k <  8; ++k) { result.r = min(result.r, texDepthMS.Load(dispatchThreadId.xy, k).r); }
	else if (SampleCount == 16)[unroll] for (uint l = 0; l < 16; ++l) { result.r = min(result.r, texDepthMS.Load(dispatchThreadId.xy, l).r); }

	outDepth[dispatchThreadId.xy] = result.r;

	     if (SampleCount == 2) [unroll] for (uint i = 0; i <  2; ++i) { result.rgb = min(result.rgb, texNormalsMS.Load(dispatchThreadId.xy, i).rgb); }
	else if (SampleCount == 4) [unroll] for (uint j = 0; j <  4; ++j) { result.rgb = min(result.rgb, texNormalsMS.Load(dispatchThreadId.xy, j).rgb); }
	else if (SampleCount == 8) [unroll] for (uint k = 0; k <  8; ++k) { result.rgb = min(result.rgb, texNormalsMS.Load(dispatchThreadId.xy, k).rgb); }
	else if (SampleCount == 16)[unroll] for (uint l = 0; l < 16; ++l) { result.rgb = min(result.rgb, texNormalsMS.Load(dispatchThreadId.xy, l).rgb); }

	outNormals[dispatchThreadId.xy] = result.rgb;
}