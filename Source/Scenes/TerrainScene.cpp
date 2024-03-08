#define NOMINMAX

#include "Scenes.h"

#include "../Engine/Core/Input.h"

#include "../Libs/VQUtils/Source/utils.h"

using namespace DirectX;

void TerrainScene::UpdateScene(float dt, FSceneView& SceneView)
{
	if (mInput.IsKeyTriggered("Space"))
		bAnimateEnvironmentMapRotation = !bAnimateEnvironmentMapRotation;
}

static const char* pszTerrainMaterialName = "TerrainMaterial0";
void TerrainScene::InitializeScene()
{
	for (Terrain& t : mTerrains)
	{
		t.MaterialId = CreateMaterial(pszTerrainMaterialName);
	}
}

void TerrainScene::LoadScene(FSceneRepresentation& scene)
{
	FMaterialRepresentation mr;
	mr.Name                 = pszTerrainMaterialName;
	mr.Alpha                = 1.0f;
	mr.DiffuseColor         = { 1, 1, 1 };
	mr.EmissiveColor        = { 0, 1, 1 };
	mr.EmissiveIntensity    = 0.0f;
	mr.Displacement         = 100.0f;
	mr.DiffuseMapFilePath   = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_BaseColor.png";
	mr.NormalMapFilePath    = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_Normal.png";
	mr.RoughnessMapFilePath = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_Roughness.png";
	mr.AOMapFilePath        = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_AO.png";
	mr.HeightMapFilePath    = "Data/Textures/PBR/PavingStone05_4K/PavingStone05_4K_Height.png";
	scene.Materials.push_back(mr);
		
	// terrains
	{
		Terrain t;
		t.MeshId = EBuiltInMeshes::GRID_DETAILED_QUAD2;

		const float fTerrainScale = 500.f;
		t.RootTransform.SetPosition(0, 0, 0);
		t.RootTransform.SetScale(fTerrainScale, 1, fTerrainScale);
		mTerrains.push_back(t);
	}
	
	if constexpr (false)
	{
		Terrain t;
		t.MeshId = EBuiltInMeshes::GRID_DETAILED_QUAD2;
		t.RootTransform.SetPosition(-10, 100, 0);
		t.RootTransform.SetScale(100, 1, 100);
		mTerrains.push_back(t);
	}
}

void TerrainScene::UnloadScene()
{

}

void TerrainScene::RenderSceneUI() const
{
}