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

#include "Light.h"

#include "Libs/VQUtils/Source/Log.h"

using namespace DirectX;

Light Light::MakePointLight()
{
	Light l; // default ctor takes care of most
	l.AttenuationConstant  = 1.0f;
	l.AttenuationLinear    = 1.0f;
	l.AttenuationQuadratic = 1.0f;
	l.Type = Light::EType::POINT;
	return l;
}

Light Light::MakeDirectionalLight()
{
	Light l; // default ctor takes care of most

	l.ViewportX = 2048;
	l.ViewportY = 2048;
	l.DistanceFromOrigin = 500.0f;
	l.Type = Light::EType::DIRECTIONAL;

	return l;
}

Light Light::MakeSpotLight()
{
	Light l; // default ctor takes care of most

	l.SpotInnerConeAngleDegrees = 25.0f;
	l.SpotOuterConeAngleDegrees = 35.0f;
	l.Type = Light::EType::SPOT;
	return l;
}


Light::Light()
	: Position(XMFLOAT3(0,0,0))
	, Range(1000.0f)
	, RotationQuaternion(Quaternion::Identity())
	, RenderScale(XMFLOAT3(0.1f,0.1f,0.1f))
	, bEnabled(true)
	, bCastingShadows(false)
	, Mobility(EMobility::DYNAMIC)
	, Color(XMFLOAT3(1,1,1))
	, Brightness(300.0f)
	, ShadowData(FShadowData(0.00005f, 0.01f, 1500.0f))
	// assumed point light by default
	, AttenuationConstant(1)
	, AttenuationLinear(1)
	, AttenuationQuadratic(1)
{}

#define COPY_COMMON_LIGHT_DATA(pDst, pSrc)\
pDst->brightness = pSrc->Brightness;\
pDst->color      = pSrc->Color;\
pDst->depthBias  = pSrc->ShadowData.DepthBias;


void Light::GetGPUData(VQ_SHADER_DATA::DirectionalLight * pLight) const
{
	assert(pLight);
	COPY_COMMON_LIGHT_DATA(pLight, this);
	pLight->enabled = this->bEnabled;
	pLight->shadowing = this->bCastingShadows;

	Transform tf;
	tf._position = this->Position;
	tf._rotation = this->RotationQuaternion;
	XMFLOAT3 FWD_F3(0, -1, 0); // default orientation looks down for directional lights
	XMVECTOR FWD = XMLoadFloat3(&FWD_F3);
	FWD = XMVector3Transform(FWD, tf.NormalMatrix(tf.matWorldTransformation()));

	XMStoreFloat3(&pLight->lightDirection, FWD);
}

void Light::GetGPUData(VQ_SHADER_DATA::PointLight* pLight) const
{
	assert(pLight);
	COPY_COMMON_LIGHT_DATA(pLight, this);

	pLight->position = this->Position;
	pLight->range = this->Range;
	// pLight->attenuation
}

void Light::GetGPUData(VQ_SHADER_DATA::SpotLight* pLight) const
{
	assert(pLight);
	COPY_COMMON_LIGHT_DATA(pLight, this);
	
	XMFLOAT3 FWD_F3(0, 0, 1);  // default orientation looks forward for spot lights
	XMVECTOR FWD = XMLoadFloat3(&FWD_F3);
	FWD = XMVector3Transform(FWD, Transform::NormalMatrix(this->GetWorldTransformationMatrix()));

	XMStoreFloat3(&pLight->spotDir, FWD);
	pLight->position = this->Position;
	pLight->innerConeAngle = this->SpotInnerConeAngleDegrees * DEG2RAD;
	pLight->outerConeAngle = this->SpotOuterConeAngleDegrees * DEG2RAD;
}

DirectX::XMMATRIX Light::GetWorldTransformationMatrix() const
{
	constexpr float LightMeshScale = 0.1f;
	Transform tf;
	tf._position = this->Position;
	tf._rotation = this->RotationQuaternion;
	tf._scale = XMFLOAT3(LightMeshScale, LightMeshScale, LightMeshScale);
	return tf.matWorldTransformation();
}

DirectX::XMMATRIX Light::GetViewProjectionMatrix(Texture::CubemapUtility::ECubeMapLookDirections PointLightFace) const
{
	XMFLOAT2 ViewportSize = XMFLOAT2(this->ViewportX, this->ViewportY);

	XMMATRIX matView = XMMatrixIdentity();
	XMMATRIX matProj = CalculateProjectionMatrix(this->Type, this->ShadowData.NearPlane, this->ShadowData.FarPlane, ViewportSize);

	switch (this->Type)
	{
	case Light::EType::POINT      : matView = Light::CalculatePointLightViewMatrix(PointLightFace, this->Position); break;
	case Light::EType::SPOT       : matView = Light::CalculateSpotLightViewMatrix(this->GetTransform()); break;
	case Light::EType::DIRECTIONAL: matView = Light::CalculateDirectionalLightViewMatrix(*this);  break;
	default:
		break;
	}

	return matView * matProj;
}

Transform Light::GetTransform() const
{
	Transform tf;
	tf._position = this->Position;
	tf._rotation = this->RotationQuaternion;
	return tf;
}

DirectX::XMMATRIX Light::CalculateSpotLightViewMatrix(const Transform& mTransform)
{
	XMVECTOR up = XMLoadFloat3(&UpVector);
	XMVECTOR lookAt = XMLoadFloat3(&ForwardVector); // spot light default orientation looks fwd
	XMMATRIX RotMatrix = mTransform.RotationMatrix();

	lookAt = XMVector3TransformCoord(lookAt, RotMatrix);
	up = XMVector3TransformCoord(up, RotMatrix);
	XMVECTOR pos = XMLoadFloat3(&mTransform._position);
	XMVECTOR taraget = pos + lookAt;
	return XMMatrixLookAtLH(pos, taraget, up);
}

DirectX::XMMATRIX Light::CalculatePointLightViewMatrix(Texture::CubemapUtility::ECubeMapLookDirections lookDir, const DirectX::XMFLOAT3& position)
{
	return Texture::CubemapUtility::CalculateViewMatrix(lookDir, position);
}

DirectX::XMMATRIX Light::CalculateDirectionalLightViewMatrix(const Light& mDirLight)
{
	if (mDirLight.ViewportX < 1.0f)
	{
		return XMMatrixIdentity();
	}

	XMVECTOR up      = XMLoadFloat3(&UpVector);
	XMVECTOR forward = XMLoadFloat3(&DownVector); // directional light looks down by default
	XMFLOAT3 f3Zero = XMFLOAT3(0, 0, 0);

	const XMVECTOR lookAt = XMLoadFloat3(&f3Zero);
	const XMMATRIX mRot = mDirLight.RotationQuaternion.Matrix();
	const XMVECTOR direction = XMVector4Transform(forward, mRot);
	const XMVECTOR lightPos = direction * -mDirLight.DistanceFromOrigin; // away from the origin along the direction vector 

	// check for edge cases: when upVector and light vector align (0deg) or oppose (180deg),
	//                       they become become linearly dependent and the cross product will become zero.
	//                       This happens when <Rotation> is given as 0 0 0, we end up with up(0,1,0) and down(0,-1,0)
	XMVECTOR L = XMVector3Normalize(lookAt - lightPos);
	XMVECTOR LDotUp = XMVector3Dot(L, up);
	if (LDotUp.m128_f32[0] == 1.0f || LDotUp.m128_f32[0] == -1.0f)
	{
		// nudge the up vector a bit to prevent the vectors from being linearly dependent
		up.m128_f32[0] += 0.001f;
		up = XMVector3Normalize(up);
	}

	return XMMatrixLookAtLH(lightPos, lookAt, up);
}

DirectX::XMMATRIX Light::CalculateProjectionMatrix(EType lightType, float fNear, float fFar, const DirectX::XMFLOAT2 viewPortSize /*= vec2(0, 0)*/)
{
	switch (lightType)
	{
	case Light::POINT:
	{
		constexpr float ASPECT_RATIO = 1.0f; // cubemap aspect ratio
		return XMMatrixPerspectiveFovLH(PI_DIV2, ASPECT_RATIO, fNear, fFar);
	}
	case Light::SPOT:
	{
		constexpr float ASPECT_RATIO = 1.0f;
		//return XMMatrixPerspectiveFovLH(mSpotOuterConeAngleDegrees * DEG2RAD, ASPECT_RATIO, mNearPlaneDistance, mFarPlaneDistance);
		return XMMatrixPerspectiveFovLH(PI_DIV2, ASPECT_RATIO, fNear, fFar);
	}
	case Light::DIRECTIONAL:
	{
		if (viewPortSize.x < 1.0f) return XMMatrixIdentity();
		return XMMatrixOrthographicLH(viewPortSize.x, viewPortSize.y, fNear, fFar);
	}
	default:
		Log::Warning("GetProjectionMatrix() called on invalid light type!");
		return XMMatrixIdentity();
	}
}