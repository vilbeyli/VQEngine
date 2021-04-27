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
Texture2DMS<float> input  : register(t0);
RWTexture2D<float> output : register(u0);
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
 
	float result = 1;
	for (uint i = 0; i < SampleCount; ++i)
	{
		result = min(result, input.Load(dispatchThreadId.xy, i).r);
	}
 
	output[dispatchThreadId.xy] = result;
}