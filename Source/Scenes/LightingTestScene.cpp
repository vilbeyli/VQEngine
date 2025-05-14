#include "Scenes.h"

#include "Engine/Scene/SceneViews.h"
#include "Engine/Core/Input.h"

#include "Libs/VQUtils/Source/utils.h"

using namespace DirectX;

static const char* pszTerrainMaterialName = "TerrainMaterial0";

void LightingTestScene::UpdateScene(float dt, FSceneView& SceneView)
{
	if (mInput.IsKeyTriggered("Space"))
		bRotateLights = !bRotateLights;

	if (bRotateLights)
	{
		const float ROTATION_SPEED = 0.3f * PI;
		std::vector<Light*> lights = this->GetLightsOfType(Light::EType::LINEAR);
		for (Light* pL : lights)
		{
			Quaternion rotQ = Quaternion::FromAxisAngle(XMFLOAT3(1, 0, 0), ROTATION_SPEED * dt);
			pL->RotationQuaternion = rotQ * pL->RotationQuaternion;
		}
	}
}

void LightingTestScene::InitializeScene()
{
	this->GetSceneView(0).sceneRenderOptions.fAmbientLightingFactor = 0.009f;
}

void LightingTestScene::LoadScene(FSceneRepresentation& scene)
{
	FMaterialRepresentation mr;
	mr.Name                 = pszTerrainMaterialName;
	mr.Alpha                = 1.0f;
	mr.DiffuseColor         = { 1, 1, 1 };
	mr.EmissiveColor        = { 0.f, 0.235f, 1.0f };
	mr.EmissiveIntensity    = 0.0f;
	mr.DiffuseMapFilePath   = "Data/Textures/PBR/BlackTiles07_MR_2K/BlackTiles07_2K_BaseColor.png";
	mr.NormalMapFilePath    = "Data/Textures/PBR/BlackTiles07_MR_2K/BlackTiles07_2K_Normal.png";
	mr.RoughnessMapFilePath = "Data/Textures/PBR/BlackTiles07_MR_2K/BlackTiles07_2K_Roughness.png";
	mr.AOMapFilePath        = "Data/Textures/PBR/BlackTiles07_MR_2K/BlackTiles07_2K_AO.png";
	mr.HeightMapFilePath    = "Data/Textures/PBR/BlackTiles07_MR_2K/BlackTiles07_2K_Height.png";
	mr.TilingX              = 1.0f;
	mr.TilingY              = 1.0f;
	mr.Displacement         = 0.25f;

	mr.TessellationEnabled = false;
	mr.Tessellation.SetAllTessellationFactors(4.0f);
	scene.Materials.push_back(mr);
	
	const float fTerrainScale = 1000.f;
	FGameObjectRepresentation obj;
	obj.tf.SetPosition(0, 0, 0);
	obj.tf.SetScale(fTerrainScale, 1, fTerrainScale);
	obj.BuiltinMeshName = "DetaildGrid1";
	obj.ModelName = "TerrainModel0";
	obj.MaterialName = pszTerrainMaterialName;
	scene.Objects.emplace_back(obj);
}

void LightingTestScene::UnloadScene()
{

}

void LightingTestScene::RenderSceneUI() const
{
}