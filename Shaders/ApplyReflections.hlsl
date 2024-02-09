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

Texture2D<float4> TexReflectionRadiance;
#if COMPOSITE_BOUNDING_VOLUMES
Texture2D<float4> TexBoundingVolumes;
#endif

RWTexture2D<float4> TexSceneColor;

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
    const float3 ReflectionRadiance = TexReflectionRadiance[DispatchThreadID.xy].rgb;
    const float4 SceneRadianceAndRoughness = TexSceneColor[DispatchThreadID.xy];
    const float3 SceneRadiance = SceneRadianceAndRoughness.rgb;

    float3 FinalComposite = SceneRadiance + ReflectionRadiance;
	float FinalAlpha = SceneRadianceAndRoughness.a;
#if COMPOSITE_BOUNDING_VOLUMES
    float4 BVColor = TexBoundingVolumes[DispatchThreadID.xy].rgba;
    FinalComposite = BVColor.rgb * BVColor.a + FinalComposite * (1.0f - BVColor.a);
    FinalAlpha = BVColor.a;
#endif

    TexSceneColor[DispatchThreadID.xy] = float4(FinalComposite, FinalAlpha);
}