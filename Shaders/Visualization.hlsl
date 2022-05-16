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

//
// RESOURCE BINDING
//
Texture2D           texIn;
RWTexture2D<float4> texOut;

cbuffer VizParams : register(b0)
{
    int iDrawMode;
    int iUnpackNormals;
    float fInputStrength;
}

//
// ENTRY POINT
//
[numthreads(8, 8, 1)]
void CSMain(
    uint3 LocalThreadId    : SV_GroupThreadID
  , uint3 WorkGroupId      : SV_GroupID
  , uint3 DispatchThreadID : SV_DispatchThreadID
) 
{
    float4 texInA = texIn[DispatchThreadID.xy];
    float3 OutRGB = texInA.rgb;
   


/*
// In PostProcess.h
enum class EDrawMode
{
	LIT_AND_POSTPROCESSED = 0,
	WIREFRAME,
	NO_MATERIALS,

	DEPTH,
	NORMALS,
	ROUGHNESS,
	METALLIC,
	AO,

	NUM_DRAW_MODES,
};
*/

//---------------------------------------------------------------
// TODO: remove if-else blocks and use shader permutations
//       to remove the dependency of keeping track of EDrawMode
//       from the cpp side, once the hard-coded PSO usage is
//       abstracted away.
#define	LIT_AND_POSTPROCESSED 0
#define	DEPTH 1
#define	NORMALS 2
#define	ROUGHNESS 3
#define	METALLIC 4
#define	AO 5
#define ALBEDO 6
#define REFLECTIONS 7
#define	MOTION_VECTORS 8
#define	NUM_DRAW_MODES 9

    if (iDrawMode == DEPTH)
    {
        OutRGB = pow(texInA.rrr, 500);
    }
    else if (iDrawMode == NORMALS)
    {
        OutRGB = ((texInA - 0.5f.xxx) * 2.0f) * iUnpackNormals + (1 - iUnpackNormals) * texInA.rgb;
    }
    else if (iDrawMode == ROUGHNESS)
    {
        OutRGB = texInA.aaa;
    }
    else if (iDrawMode == METALLIC)
    {
        OutRGB = texInA.ggg;
    }
    else if (iDrawMode == AO)
    {
        OutRGB = texInA.rrr;
    }
    else if (iDrawMode == ALBEDO)
    {
        OutRGB = texInA.rgb;
    }
    else if (iDrawMode == REFLECTIONS)
    {
        OutRGB = texInA.rgb;
    }
    else if (iDrawMode == MOTION_VECTORS)
    {
        OutRGB = float3(texInA.rg * float2(0.5, -0.5) * fInputStrength, 0.0f) + 0.5f.rrr;
    }
    else
    {
        OutRGB = float3(1, 0, 1); // unsupported
    }
//---------------------------------------------------------------

    texOut[DispatchThreadID.xy] = float4(OutRGB, texInA.a);
}
