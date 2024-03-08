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

void TerrainScene::InitializeScene()
{
}

void TerrainScene::LoadScene(FSceneRepresentation& scene)
{
	// terrains
	{
		const char* pszTerrainHeightMapTexturePath = "Data/Textures/Terrain/world.png";
		Terrain t;
		t.MeshId = EBuiltInMeshes::GRID_DETAILED_QUAD2;
		t.HeightMap = mRenderer.CreateTextureFromFile(pszTerrainHeightMapTexturePath, false); // TODO: fix errors creating mips
		t.SRVHeightMap = mRenderer.AllocateSRV(1);
		mRenderer.InitializeSRV(t.SRVHeightMap, 0, t.HeightMap);

		const float fTerrainScale = 500.f;
		t.HeightmapScale = 5.5f;
		t.RootTransform.SetPosition(0, 0, 0);
		t.RootTransform.SetScale(fTerrainScale, 1, fTerrainScale);
		mTerrains.push_back(t);
	}
	
	if constexpr (false)
	{
		Terrain t;
		t.MeshId = EBuiltInMeshes::GRID_DETAILED_QUAD2;
		t.HeightMap = -1; // TODO
		t.HeightmapScale = 100.0f;
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