#define NOMINMAX

#include "Scenes.h"

#include "../Engine/Core/Input.h"

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

	constexpr int NUM_ROUGHNESS_INSTANCES = 8; // [0-1] in 1.0f/NUM_ROUGHNESS_INSTANCES increments
	constexpr int NUM_METALLIC_INSTANCES = 10;
	auto fnGetRoughnessMetallicMaterialName = [](int r, int m)
	{
		std::string matName = "RoughnessMetallic[";
		matName += std::to_string(r);
		matName += "][" + std::to_string(m) + "]";
		return matName;
	};
	// roughness / metallic gradiant materials
	for (int r = 0; r < NUM_ROUGHNESS_INSTANCES; ++r)
	{
		for (int m = 0; m < NUM_METALLIC_INSTANCES; ++m)
		{
			const float roughness = static_cast<float>(r) / (NUM_ROUGHNESS_INSTANCES - 1);
			const float metallic  = static_cast<float>(m) / (NUM_METALLIC_INSTANCES - 1);

			FMaterialRepresentation matR = {};
			matR.DiffuseColor = XMFLOAT3(0.00f, 0.05f, 0.45f); // RGB
			matR.Roughness = roughness;
			matR.Metalness = metallic;
			matR.Name = fnGetRoughnessMetallicMaterialName(r, m);

			scene.Materials.push_back(matR);
		}
	}
	



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
		for (int r = 0; r < NUM_ROUGHNESS_INSTANCES; ++r)
		{
			for (int m = 0; m < NUM_METALLIC_INSTANCES; ++m)
			{
				XMFLOAT3 scale = { 5, 5, 5 };
				constexpr float posX_Begin = -50.0f;
				    const float posX_Incr = r * (2*scale.x + 2.0f);
				
				constexpr float posY_Begin = 200.0f;
					const float posY_Incr = m * (2*scale.x + 2.0f);

				float x = posX_Begin + posX_Incr;
				float y = posY_Begin + posY_Incr;

				XMFLOAT3 pos = XMFLOAT3(x, y, 0);

				FGameObjectRepresentation obj = {};
				obj.tf.SetPosition(pos);
				obj.tf.SetScale(scale);
				obj.BuiltinMeshName = "Sphere";

				obj.MaterialName = fnGetRoughnessMetallicMaterialName(r, m);

				scene.Objects.push_back(obj);
			}
		}
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

