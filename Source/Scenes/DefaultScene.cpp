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
	constexpr float MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER = 1.0f;
	if (!bMouseInputUsedByUI)
	{
		if (mInput.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   pTF->RotateAroundAxisRadians(ZAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
		if (mInput.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  pTF->RotateAroundAxisRadians(YAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
		if (mInput.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) pTF->RotateAroundAxisRadians(XAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);

		constexpr float DOUBLE_CLICK_MULTIPLIER = 4.0f;
		if (mInput.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   pTF->RotateAroundAxisRadians(ZAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
		if (mInput.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  pTF->RotateAroundAxisRadians(YAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
		if (mInput.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) pTF->RotateAroundAxisRadians(XAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);

		constexpr float SCROLL_SCALE_DELTA = 1.1f;
		const float CubeScale = pTF->_scale.x;
		if (mInput.IsMouseScrollUp())
		{
			Log::Info("ScrollUp");
			pTF->SetUniformScale(CubeScale * SCROLL_SCALE_DELTA);
		}
		if (mInput.IsMouseScrollDown()) pTF->SetUniformScale(std::max(0.5f, CubeScale / SCROLL_SCALE_DELTA));
	}

	// update scene data
	if (this->bObjectAnimation)
		pTF->RotateAroundAxisRadians(YAxis, dt * 0.2f * PI);
}


void DefaultScene::InitializeScene()
{
	assert(!mpObjects.empty());
	this->pObject = mpObjects.front();
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

