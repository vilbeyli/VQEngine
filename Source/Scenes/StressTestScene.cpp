#define NOMINMAX

#include "Scenes.h"

#include "../Application/Input.h"

using namespace DirectX;


void StressTestScene::UpdateScene(float dt, FSceneView& SceneView)
{

}


void StressTestScene::InitializeScene()
{

}


void StressTestScene::LoadScene(FSceneRepresentation& scene)
{
	//
	// MATERIALS
	//
	FMaterialRepresentation matRep = {};
	matRep.Alpha = 1.0f;
	matRep.DiffuseColor = { 1, 1, 1 };
	matRep.DiffuseMapFilePath = "Procedural/Checkerboard";
	matRep.Name = "Checkerboard";
	scene.Materials.push_back(matRep);
	
	matRep.DiffuseMapFilePath = "Procedural/Checkerboard_Grayscale";
	matRep.Name = "Checkerboard_Grayscale";
	scene.Materials.push_back(matRep);

	//
	// OBJECTS
	//
	
	// small cubes
	constexpr int NUM_OBJECTS = 1024;

	constexpr int DIMENSION_X = 16;
	constexpr int DIMENSION_Y = 4;
	constexpr int DIMENSION_Z = 16;
#if 0
	for (int i = 0; i < NUM_OBJECTS; ++i)
#else
	int NumObjects = 0;
	constexpr float distance = 5.0f;
	for(int x=-DIMENSION_X/2; x<DIMENSION_X/2; ++x)
	for(int y=-DIMENSION_Y/2; y<DIMENSION_Y/2; ++y)
	for(int z=-DIMENSION_Z/2; z<DIMENSION_Z/2; ++z)

#endif
	{
		FGameObjectRepresentation obj = {};

		XMFLOAT3 pos = {x*distance, y*distance, z*distance};
		XMFLOAT3 axis = UpVector;
		float rotationAngle = 15.0f; // TODO: rand?
		XMFLOAT3 scale = { 1, 1, 1 };

		obj.tf.SetPosition(pos);
		obj.tf.RotateAroundAxisDegrees(axis, rotationAngle);
		obj.tf.SetScale(scale);
		obj.BuiltinMeshName = "Cube";

		obj.MaterialName = NumObjects%2==0 ? "Checkerboard_Grayscale" : "Checkerboard";

		scene.Objects.push_back(obj);
		++NumObjects;
	}

	// gradient spheres
	{

	}

	// big cube
	{
		FGameObjectRepresentation obj = {};
		XMFLOAT3 pos = { 0, 0, 0 };
		XMFLOAT3 axis = UpVector;
		XMFLOAT3 scale = { 500, 500, 500 };
		obj.tf.SetPosition(pos);
		obj.tf.SetScale(scale);
		obj.BuiltinMeshName = "Cube";
		obj.MaterialName = "Checkerboard";
		//scene.Objects.push_back(obj);
	}

	// platform cylinder
	{
		FGameObjectRepresentation obj = {};
		XMFLOAT3 pos = { 0, -40, 0 };
		XMFLOAT3 axis = UpVector;
		XMFLOAT3 scale = { 100, 1, 100 };
		obj.tf.SetPosition(pos);
		obj.tf.SetScale(scale);
		obj.BuiltinMeshName = "Cylinder";
		obj.MaterialName = "Checkerboard_Grayscale";
		scene.Objects.push_back(obj);
	}
}

void StressTestScene::UnloadScene()
{

}

void StressTestScene::RenderSceneUI() const
{
}

