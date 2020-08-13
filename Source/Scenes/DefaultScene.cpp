#define NOMINMAX

#include "Scenes.h"

#include "../Application/Input.h"

using namespace DirectX;

static void Toggle(bool& b) { b = !b; }


void DefaultScene::UpdateScene(float dt, FSceneView& SceneView)
{
	// TODO:
	//FFrameData& FrameData = GetCurrentFrameData(hwnd);

	// handle input
	if (mInput.IsKeyTriggered('R'))
		//FrameData.SceneCamera.InitializeCamera(GenerateCameraInitializationParameters(mpWinMain));
		; // TODO: reset camera to initial stat

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

#if 0
	if (mInput.IsKeyTriggered("Space")) Toggle(FrameData.bCubeAnimating);

	constexpr float MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER = 1.0f;
	if (mInput.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   FrameData.TFCube.RotateAroundAxisRadians(ZAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
	if (mInput.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  FrameData.TFCube.RotateAroundAxisRadians(YAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
	if (mInput.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) FrameData.TFCube.RotateAroundAxisRadians(XAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);

	constexpr float DOUBLE_CLICK_MULTIPLIER = 4.0f;
	if (mInput.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   FrameData.TFCube.RotateAroundAxisRadians(ZAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	if (mInput.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  FrameData.TFCube.RotateAroundAxisRadians(YAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	if (mInput.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) FrameData.TFCube.RotateAroundAxisRadians(XAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);

	constexpr float SCROLL_SCALE_DELTA = 1.1f;
	const float CubeScale = FrameData.TFCube._scale.x;
	if (mInput.IsMouseScrollUp()) FrameData.TFCube.SetUniformScale(CubeScale * SCROLL_SCALE_DELTA);
	if (mInput.IsMouseScrollDown()) FrameData.TFCube.SetUniformScale(std::max(0.5f, CubeScale / SCROLL_SCALE_DELTA));
#endif



	// update camera
	FCameraInput camInput(LocalSpaceTranslation);
	camInput.DeltaMouseXY = mInput.GetMouseDelta();
	mCameras[mIndex_SelectedCamera].Update(dt, camInput);

#if 0
	// update scene data
	if (FrameData.bCubeAnimating)
		FrameData.TFCube.RotateAroundAxisRadians(YAxis, dt * 0.2f * PI);
#endif
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

