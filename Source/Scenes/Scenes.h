// TBA

#include "../Application/Scene.h"

#pragma once

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
	)\
		: Scene(engine, NumFrameBuffers, input, pWin)\
	{}\


class DefaultScene : public Scene
{
	DECLARE_SCENE_INTERFACE()
	DECLARE_CTOR(DefaultScene)

private:
	GameObject* pObject = nullptr;
	bool        bObjectAnimation = false;
};

class SponzaScene : public Scene
{
	DECLARE_SCENE_INTERFACE()

	DECLARE_CTOR(SponzaScene)
};

class GeometryUnitTestScene : public Scene
{
	DECLARE_SCENE_INTERFACE()

	DECLARE_CTOR(GeometryUnitTestScene)
};

class StressTestScene : public Scene
{
	DECLARE_SCENE_INTERFACE()

	DECLARE_CTOR(StressTestScene)
};