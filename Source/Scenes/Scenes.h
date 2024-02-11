#pragma once

#include "../Engine/Scene/Scene.h"

class VQEngine;

#define DECLARE_SCENE_INTERFACE()\
protected:\
	void InitializeScene() override;\
	void UpdateScene(float dt, FSceneView& SceneView) override;\
	void LoadScene(FSceneRepresentation& scene) override;\
	void UnloadScene() override;\
	void RenderSceneUI() const override;\

#define DECLARE_CTOR(TypeName)\
public:\
	TypeName(VQEngine& engine\
		, int NumFrameBuffers\
		, const Input& input\
		, const std::unique_ptr<Window>& pWin\
		, VQRenderer& renderer\
	)\
		: Scene(engine, NumFrameBuffers, input, pWin, renderer)\
	{}\


class DefaultScene : public Scene
{
	DECLARE_SCENE_INTERFACE()
	DECLARE_CTOR(DefaultScene)

private:
	size_t hObject = -1;
	bool bObjectAnimation = false;
};

class SponzaScene : public Scene
{
	DECLARE_SCENE_INTERFACE()

	DECLARE_CTOR(SponzaScene)
};

class EnvironmentMapUnitTestScene : public Scene
{
	DECLARE_SCENE_INTERFACE()

	DECLARE_CTOR(EnvironmentMapUnitTestScene)

private:
	bool bAnimateCamera = false;
	float fCameraAnimation_RotationSpeed = 0.0f; // rad/sec
};

class StressTestScene : public Scene
{
	DECLARE_SCENE_INTERFACE()

	DECLARE_CTOR(StressTestScene)
	bool bAnimateEnvironmentMapRotation = false;
};