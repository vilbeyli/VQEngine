#define NOMINMAX

#include "Scenes.h"

#include "../Application/Input.h"

using namespace DirectX;

static void Toggle(bool& b) { b = !b; }


void DefaultScene::UpdateScene(float dt, FSceneView& SceneView)
{
	assert(pObject);
	assert(mIndex_SelectedCamera < mCameras.size());

	Camera& cam = mCameras[mIndex_SelectedCamera];

	// handle input
	if (mInput.IsKeyTriggered('R'))
	{
		FCameraParameters params = mSceneRepresentation.Cameras[mIndex_SelectedCamera];
		params.Width  = static_cast<float>(mpWindow->GetWidth() );
		params.Height = static_cast<float>(mpWindow->GetHeight());
		cam.InitializeCamera(params);
	}

	constexpr float CAMERA_MOVEMENT_SPEED_MULTIPLER = 0.75f;
	constexpr float CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER = 2.0f;
	XMVECTOR LocalSpaceTranslation = XMVectorSet(0, 0, 0, 0);
	if (mInput.IsKeyDown('A'))      LocalSpaceTranslation += XMLoadFloat3(&LeftVector);
	if (mInput.IsKeyDown('D'))      LocalSpaceTranslation += XMLoadFloat3(&RightVector);
	if (mInput.IsKeyDown('W'))      LocalSpaceTranslation += XMLoadFloat3(&ForwardVector);
	if (mInput.IsKeyDown('S'))      LocalSpaceTranslation += XMLoadFloat3(&BackVector);
	if (mInput.IsKeyDown('E'))      LocalSpaceTranslation += XMLoadFloat3(&UpVector);
	if (mInput.IsKeyDown('Q'))      LocalSpaceTranslation += XMLoadFloat3(&DownVector);
	if (mInput.IsKeyDown(VK_SHIFT)) LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER;
	LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_MULTIPLER;

	if (mInput.IsKeyTriggered("Space")) Toggle(this->bObjectAnimation);

	Transform* pTF = mpTransforms[pObject->mTransformID];
	constexpr float MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER = 1.0f;
	if (mInput.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   pTF->RotateAroundAxisRadians(ZAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
	if (mInput.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  pTF->RotateAroundAxisRadians(YAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
	if (mInput.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) pTF->RotateAroundAxisRadians(XAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);

	constexpr float DOUBLE_CLICK_MULTIPLIER = 4.0f;
	if (mInput.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   pTF->RotateAroundAxisRadians(ZAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	if (mInput.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  pTF->RotateAroundAxisRadians(YAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	if (mInput.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) pTF->RotateAroundAxisRadians(XAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);

	constexpr float SCROLL_SCALE_DELTA = 1.1f;
	const float CubeScale = pTF->_scale.x;
	if (mInput.IsMouseScrollUp()) pTF->SetUniformScale(CubeScale * SCROLL_SCALE_DELTA);
	if (mInput.IsMouseScrollDown()) pTF->SetUniformScale(std::max(0.5f, CubeScale / SCROLL_SCALE_DELTA));


	// update camera
	FCameraInput camInput(LocalSpaceTranslation);
	camInput.DeltaMouseXY = mInput.GetMouseDelta();
	cam.Update(dt, camInput);

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

