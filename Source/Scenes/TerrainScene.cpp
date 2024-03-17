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
	mr.TilingX              = 10.f;
	mr.TilingY              = 10.f;

	FTessellationParameters& tess = mr.Tessellation;
	tess.bEnableTessellation = true;
	tess.TriInner = 4.0f;
	tess.TriOuter[0] = tess.TriOuter[1] = tess.TriOuter[2] = 4.0f;
	tess.QuadInner[0] = tess.QuadInner[1] = 4.0f;
	tess.QuadOuter[0] = tess.QuadOuter[1] = tess.QuadOuter[2] = tess.QuadOuter[3] = 4.0f;
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