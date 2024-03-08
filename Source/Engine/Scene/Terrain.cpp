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
	p.fHeightScale = TerrainIn.HeightmapScale;
	p.viewProj = CameraIn.GetViewProjectionMatrix();
	p.world = tf.matWorldTransformation();
	p.worldViewProj = p.world * p.viewProj;
	return p;
}