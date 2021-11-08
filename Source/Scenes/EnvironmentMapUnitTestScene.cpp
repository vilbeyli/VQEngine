#include "Scenes.h"

#include "../Engine/Core/Input.h"

using namespace DirectX;

void EnvironmentMapUnitTestScene::UpdateScene(float dt, FSceneView& SceneView)
{
	if (mInput.IsKeyTriggered("Space"))
		bAnimateCamera = !bAnimateCamera;

	if (mInput.IsKeyTriggered("L"))
		this->mCameras[this->mIndex_SelectedCamera].LookAt(XMFLOAT3(0, 0, 0));

	if (bAnimateCamera)
	{
		Camera& cam = this->mCameras[0];
		
		
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


void EnvironmentMapUnitTestScene::InitializeScene()
{
	bAnimateCamera = true;
	fCameraAnimation_RotationSpeed = PI / 4.0f;
}


void EnvironmentMapUnitTestScene::LoadScene(FSceneRepresentation& scene)
{
	constexpr int NUM_ROUGHNESS_INSTANCES = 8; // [0-1] in 1.0f/NUM_ROUGHNESS_INSTANCES increments
	constexpr int NUM_METALLIC_INSTANCES = 4;
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

	// gradient spheres
	{
		for (int r = 0; r < NUM_ROUGHNESS_INSTANCES; ++r)
		{
			for (int m = 0; m < NUM_METALLIC_INSTANCES; ++m)
			{
				XMFLOAT3 scale = { 5, 5, 5 };
				constexpr float posX_Begin = -50.0f;
				const float posX_Incr = r * (2 * scale.x + 2.0f);

				constexpr float posY_Begin = 50.0f;
				const float posY_Incr = m * (2 * scale.x + 2.0f);

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
}

void EnvironmentMapUnitTestScene::UnloadScene()
{

}

void EnvironmentMapUnitTestScene::RenderSceneUI() const
{
}

