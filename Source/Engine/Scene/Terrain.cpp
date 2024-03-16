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

#include "Terrain.h"
#include "Camera.h"

VQ_SHADER_DATA::TerrainParams GetCBuffer_TerrainParams(const Terrain& TerrainIn, const Camera& CameraIn)
{	
	const Transform& tf = TerrainIn.RootTransform;
	const float3 CameraPosition = CameraIn.GetPositionF();
	const float3 TerrainPosition = tf._position;
	
	using namespace DirectX;
	XMVECTOR VPosCam = XMLoadFloat3(&CameraPosition);
	XMVECTOR VPosTrn = XMLoadFloat3(&TerrainPosition);
	VPosCam -= VPosTrn; // Vector Terrain --> Cam

	VQ_SHADER_DATA::TerrainParams p;
	p.fDistanceToCamera = XMVectorSqrt(XMVector3Dot(VPosCam, VPosCam)).m128_f32[0];
	p.viewProj = CameraIn.GetViewProjectionMatrix();
	p.world = tf.matWorldTransformation();
	p.worldViewProj = p.world * p.viewProj;
	p.matNormal = tf.NormalMatrix(p.world);
	p.bCullPatches = TerrainIn.bFrustumCullPatches;
	return p;
}

VQ_SHADER_DATA::TerrainTessellationParams GetCBuffer_TerrainTessellationParams(const Terrain& TerrainIn)
{
	VQ_SHADER_DATA::TerrainTessellationParams p;
	const FTessellationParameters& tess = TerrainIn.Tessellation;
	p.TriEdgeTessFactor.x = tess.TriOuter[0];
	p.TriEdgeTessFactor.y = tess.TriOuter[1];
	p.TriEdgeTessFactor.z = tess.TriOuter[2];
	p.TriInnerTessFactor  = tess.TriInner;
	p.QuadEdgeTessFactor.x = tess.QuadOuter[0];
	p.QuadEdgeTessFactor.y = tess.QuadOuter[1];
	p.QuadEdgeTessFactor.z = tess.QuadOuter[2];
	p.QuadEdgeTessFactor.w = tess.QuadOuter[3];
	p.QuadInsideFactor.x = tess.QuadInner[0];
	p.QuadInsideFactor.y = tess.QuadInner[1];
	return p;
}
