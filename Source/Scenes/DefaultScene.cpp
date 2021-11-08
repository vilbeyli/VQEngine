#define NOMINMAX

#include "Scenes.h"

#include "../Engine/Core/Input.h"

using namespace DirectX;

static void Toggle(bool& b) { b = !b; }


#include "Libs/imgui/imgui.h"

void DefaultScene::UpdateScene(float dt, FSceneView& SceneView)
{
	assert(pObject);
	assert(mIndex_SelectedCamera < mCameras.size());
	ImGuiIO& io = ImGui::GetIO();
	const bool& bMouseInputUsedByUI = io.WantCaptureMouse;

	Camera& cam = mCameras[mIndex_SelectedCamera];

	// handle input
	if (mInput.IsKeyTriggered('R'))
	{
		FCameraParameters params = mSceneRepresentation.Cameras[mIndex_SelectedCamera];
		params.ProjectionParams.ViewportWidth  = static_cast<float>(mpWindow->GetWidth() );
		params.ProjectionParams.ViewportHeight = static_cast<float>(mpWindow->GetHeight());
		cam.InitializeCamera(params);
	}

	if (mInput.IsKeyTriggered("Space")) Toggle(this->bObjectAnimation);

	Transform* pTF = mpTransforms[pObject->mTransformID];

	// update scene data
	if (this->bObjectAnimation)
		pTF->RotateAroundAxisRadians(YAxis, dt * 0.2f * PI);
}


void DefaultScene::InitializeScene()
{
	assert(!mpObjects.empty());
	this->pObject = mpObjects[1];
	this->bObjectAnimation = true;
}


void DefaultScene::LoadScene(FSceneRepresentation& scene)
{
}

void DefaultScene::UnloadScene()
{

}

void DefaultScene::RenderSceneUI() const
{
}

