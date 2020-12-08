#include "Scenes.h"

#include "../Application/Input.h"

using namespace DirectX;

void GeometryUnitTestScene::UpdateScene(float dt, FSceneView& SceneView)
{
	if (mInput.IsKeyTriggered("Space"))
		bAnimateCamera = !bAnimateCamera;

	if (mInput.IsKeyTriggered("L"))
		this->mCameras[this->mIndex_SelectedCamera].LookAt(XMFLOAT3(0, 0, 0));

	if (bAnimateCamera)
	{
		Camera& cam = this->mCameras[this->mIndex_SelectedCamera];
		
		
		XMFLOAT3 posF3 = cam.GetPositionF();
		XMFLOAT3 rotAxisF3(0, 1, 0);
		XMFLOAT3 rotPoint(0, 0, 0);

		XMVECTOR pos = XMLoadFloat3(&posF3);
		XMVECTOR rotationAxis = XMLoadFloat3(&rotAxisF3);
		XMVECTOR rotationPoint = XMLoadFloat3(&rotPoint);


		Transform t = Transform(posF3);

		t.RotateAroundPointAndAxis(rotationAxis, fCameraAnimation_RotationSpeed * dt, rotationPoint);
		cam.SetPosition(t._position);
		cam.LookAt(rotationPoint);
	}

}


void GeometryUnitTestScene::InitializeScene()
{
	bAnimateCamera = true;
	fCameraAnimation_RotationSpeed = PI / 2.0f;
}


void GeometryUnitTestScene::LoadScene(FSceneRepresentation& scene)
{
}

void GeometryUnitTestScene::UnloadScene()
{

}

void GeometryUnitTestScene::RenderSceneUI() const
{
}

