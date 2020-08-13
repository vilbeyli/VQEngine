//	VQEngine | DirectX11 Renderer
//	Copyright(C) 2018  - Volkan Ilbeyli
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

#if 0
#include "Engine/GameObject.h"

#include "Utilities/Log.h"

#include <unordered_map>

using std::string;

// [OPTIMIZATION]
//
// caching light matrices will make the Scene calculate matrices once and 
// store them in the mViewMatrix[6] and mProjectionMatrix. This way, we avoid
// matrix creation function calls during point light shadow view culling phase
// and just return the cached float16. Saves ~25% from point light culling CPU time.
//
#define CACHE_LIGHT_MATRICES 1


// I want my parameter names, Windows...
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif


DirectX::XMMATRIX Light::CalculateSpotLightViewMatrix(const Transform& mTransform)
{
	XMVECTOR up = vec3::Up;
	XMVECTOR lookAt = vec3::Forward;	// spot light default orientation looks up
	lookAt = XMVector3TransformCoord(lookAt, mTransform.RotationMatrix());
	up = XMVector3TransformCoord(up, mTransform.RotationMatrix());
	XMVECTOR pos = mTransform._position;
	XMVECTOR taraget = pos + lookAt;
	return XMMatrixLookAtLH(pos, taraget, up);
}

DirectX::XMMATRIX Light::CalculatePointLightViewMatrix(Texture::CubemapUtility::ECubeMapLookDirections lookDir, const vec3& position)
{
	return Texture::CubemapUtility::CalculateViewMatrix(lookDir, position);
}

DirectX::XMMATRIX Light::CalculateDirectionalLightViewMatrix(const Light& mDirLight)
{
	if (mDirLight.mViewportX < 1.0f)
	{
		return XMMatrixIdentity();
	}

	const XMVECTOR up = vec3::Up;
	const XMVECTOR lookAt = vec3::Zero;
	const XMMATRIX mRot = mDirLight.mTransform.RotationMatrix();
	const vec3 direction = XMVector3Transform(vec3::Forward, mRot);
	const XMVECTOR lightPos = direction * -mDirLight.mDistanceFromOrigin;	// away from the origin along the direction vector 
	return XMMatrixLookAtLH(lightPos, lookAt, up);
}

DirectX::XMMATRIX Light::CalculateProjectionMatrix(ELightType lightType, float near, float far, vec2 viewPortSize /*= vec2(0, 0)*/)
{
	switch (lightType)
	{
	case Light::POINT:
	{
		constexpr float ASPECT_RATIO = 1.0f; // cubemap aspect ratio
		return XMMatrixPerspectiveFovLH(PI_DIV2, ASPECT_RATIO, near, far);
	}
	case Light::SPOT:
	{
		constexpr float ASPECT_RATIO = 1.0f;
		//return XMMatrixPerspectiveFovLH(mSpotOuterConeAngleDegrees * DEG2RAD, ASPECT_RATIO, mNearPlaneDistance, mFarPlaneDistance);
		return XMMatrixPerspectiveFovLH(PI_DIV2, ASPECT_RATIO, near, far);
	}
	case Light::DIRECTIONAL:
	{
		if (viewPortSize.x() < 1.0f) return XMMatrixIdentity();
		return XMMatrixOrthographicLH(viewPortSize.x(), viewPortSize.y(), near, far);
	}
	default:
		Log::Warning("GetProjectionMatrix() called on invalid light type!");
		return XMMatrixIdentity();
	}
}



DirectX::XMMATRIX Light::GetProjectionMatrix() const
{
#if CACHE_LIGHT_MATRICES
	return mProjectionMatrix;
#else
	return CalculateProjectionMatrix(mType, mNearPlaneDistance, mFarPlaneDistance, vec2(mViewportX, mViewportY));
#endif
}

DirectX::XMMATRIX Light::GetViewMatrix(Texture::CubemapUtility::ECubeMapLookDirections lookDir /*= DEFAULT_POINT_LIGHT_LOOK_DIRECTION*/) const
{
#if CACHE_LIGHT_MATRICES
	switch (mType)
	{
	case Light::POINT:       return mViewMatrix[lookDir]; 
	case Light::SPOT:        return mViewMatrix[0];
	case Light::DIRECTIONAL: return mViewMatrix[0];
	default:
		Log::Warning("GetViewMatrix() called on invalid light type!");
		return mViewMatrix[0];
	}
#else
	switch (mType)
	{
	case Light::POINT:       return CalculatePointLightViewMatrix(lookDir, mTransform._position);
	case Light::SPOT:        return CalculateSpotLightViewMatrix(mTransform);
	case Light::DIRECTIONAL: return CalculateDirectionalLightViewMatrix(*this); 
	default:
		Log::Warning("GetViewMatrix() called on invalid light type!");
		return XMMatrixIdentity();
	}
#endif
}



void Light::GetGPUData(DirectionalLightGPU& l) const
{
	assert(mType == ELightType::DIRECTIONAL);
	const XMMATRIX mRot = mTransform.RotationMatrix();
	const vec3 direction = XMVector3Transform(vec3::Forward, mRot);

	l.brightness = this->mBrightness;
	l.color = this->mColor;

	l.lightDirection = direction;
	l.depthBias = this->mDepthBias;

	l.shadowing = this->mbCastingShadows;
	l.enabled = this->mbEnabled;
}
void Light::GetGPUData(SpotLightGPU& l) const
{
	assert(mType == ELightType::SPOT);
	const vec3 spotDirection = XMVector3TransformCoord(vec3::Forward, mTransform.RotationMatrix());

	l.position = mTransform._position;
	l.halfAngle = mSpotOuterConeAngleDegrees * DEG2RAD;

	l.color = mColor.Value();
	l.brightness = mBrightness;

	l.spotDir = spotDirection;
	l.depthBias = mDepthBias;

	l.innerConeAngle = mSpotInnerConeAngleDegrees * DEG2RAD;
}
void Light::GetGPUData(PointLightGPU& l) const
{
	assert(mType == ELightType::POINT);

	l.position = mTransform._position;
	l.range = mRange;

	l.color = mColor.Value();
	l.brightness = mBrightness;
	
	l.attenuation = vec3(mAttenuationConstant, mAttenuationLinear, mAttenuationQuadratic);
	l.depthBias = mDepthBias;
}



Settings::ShadowMap Light::GetSettings() const
{
	Settings::ShadowMap settings;
	settings.directionalShadowMapDimensions = static_cast<size_t>(mViewportX);
	return settings;
}



void Light::SetMatrices()
{
#if !CACHE_LIGHT_MATRICES
	return;
#endif

	// VIEW
	switch (mType)
	{
	case Light::POINT:
	{
		for(int i=0; i<6; ++i)
			mViewMatrix[i] = CalculatePointLightViewMatrix(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(i), mTransform._position);
		break;
	}
	case Light::SPOT:         mViewMatrix[0] = CalculateSpotLightViewMatrix(mTransform);   break; 
	case Light::DIRECTIONAL:  mViewMatrix[0] = CalculateDirectionalLightViewMatrix(*this); break;
	default:
		Log::Warning("SetViewMatrix() called on invalid light type!");
		mViewMatrix[0] = XMMatrixIdentity();
		break;
	}

	// PROJ
	mProjectionMatrix = CalculateProjectionMatrix(mType, mNearPlaneDistance, mFarPlaneDistance, vec2(mViewportX, mViewportY));
}

#endif