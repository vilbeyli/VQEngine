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

Texture2D TexIn;
RWTexture2D<float4> TexOut;

cbuffer ApplyReflectionParams : register(b0)
{
    int iDummy;
}

[numthreads(8, 8, 1)]
void CSMain(
      uint3 LocalThreadId    : SV_GroupThreadID
    , uint3 WorkGroupId      : SV_GroupID
    , uint3 DispatchThreadID : SV_DispatchThreadID
)
{
    float3 ReflectionRadiance = TexIn [DispatchThreadID.xy].rgb;
    float4 SceneRadianceAndRoughness = TexOut[DispatchThreadID.xy];
    float3 SceneRadiance = SceneRadianceAndRoughness.rgb;
    TexOut[DispatchThreadID.xy] = float4(SceneRadiance + ReflectionRadiance, SceneRadianceAndRoughness.a);
}