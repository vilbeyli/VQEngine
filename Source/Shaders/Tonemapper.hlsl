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


Texture2D           texColorInput;
RWTexture2D<float4> texColorOutput;

float3 ApplyGamma(float3 rgb, float gamma)
{
	return pow(rgb, gamma);
}

[numthreads(8, 8, 1)]
void CSMain(
 	uint3 LocalThreadId    : SV_GroupThreadID
  , uint3 WorkGroupId      : SV_GroupID
  , uint3 DispatchThreadID : SV_DispatchThreadID
) 
{
	float4 InRGBA = texColorInput[DispatchThreadID.xy];
	float3 OutRGB = ApplyGamma(InRGBA.rgb, 1.0 / 2.2);
	
	texColorOutput[DispatchThreadID.xy] = float4(OutRGB, InRGBA.a);
}
