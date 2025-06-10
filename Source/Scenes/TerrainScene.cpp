#include "Scenes.h"

#include "Engine/Scene/SceneViews.h"
#include "Engine/Core/Input.h"

#include "Libs/VQUtils/Include/utils.h"

using namespace DirectX;

void TerrainScene::UpdateScene(float dt, FSceneView& SceneView)
{
	if (mInput.IsKeyTriggered("Space"))
		bAnimateEnvironmentMapRotation = !bAnimateEnvironmentMapRotation;
}

static const char* pszTerrainMaterialName = "TerrainMaterial0";
void TerrainScene::InitializeScene()
{

}

void TerrainScene::LoadScene(FSceneRepresentation& scene)
{
	FMaterialRepresentation mr;
	mr.Name                 = pszTerrainMaterialName;
	mr.Alpha                = 1.0f;
	mr.DiffuseColor         = { 1, 1, 1 };
	mr.EmissiveColor        = { 0.f, 0.235f, 1.0f };
	mr.EmissiveIntensity    = 0.0f;
	mr.Displacement         = 100.0f;
	mr.DiffuseMapFilePath   = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_BaseColor.png";
	mr.NormalMapFilePath    = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_Normal.png";
	mr.RoughnessMapFilePath = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_Roughness.png";
	mr.AOMapFilePath        = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_AO.png";
	mr.HeightMapFilePath    = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_Height.png";
	mr.TilingX              = 1.0f;
	mr.TilingY              = 1.0f;

	mr.TessellationEnabled = (true);
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

void TerrainScene::UnloadScene()
{

}

void TerrainScene::RenderSceneUI() const
{
}