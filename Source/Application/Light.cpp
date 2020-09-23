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
pDst->depthBias  = 0.0f;


void Light::GetGPUData(VQ_SHADER_DATA::DirectionalLight * pLight) const
{
	assert(pLight);
	COPY_COMMON_LIGHT_DATA(pLight, this);
	pLight->enabled = this->bEnabled;
	pLight->shadowing = this->bCastingShadows;
	pLight->lightDirection = { 0, -1, 0 }; // TODO
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
	
	pLight->position = this->Position;
	//pLight->spotDir = 
	pLight->innerConeAngle = this->SpotInnerConeAngleDegrees;
	pLight->outerConeAngle = this->SpotOuterConeAngleDegrees;
}
