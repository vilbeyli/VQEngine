#define NOMINMAX

#include "Scenes.h"

#include "../Engine/Core/Input.h"

#include "../Libs/VQUtils/Source/utils.h"

using namespace DirectX;



constexpr bool ENABLE_MATERIALS = true;
constexpr bool ENABLE_RANDOM_COLORS_IN_RANDOM_MATERIALS    = true;
constexpr bool ENABLE_RANDOM_NORMALS_IN_RANDOM_MATERIALS   = true;
constexpr bool ENABLE_RANDOM_ROUGHNESS_IN_RANDOM_MATERIALS = true;
constexpr bool ENABLE_RANDOM_ROUGHNESS_TEXTURES_IN_RANDOM_MATERIALS = true && ENABLE_RANDOM_ROUGHNESS_IN_RANDOM_MATERIALS;

constexpr int    NUM_ROUGHNESS_INSTANCES = 8; // [0-1] in 1.0f/NUM_ROUGHNESS_INSTANCES increments
constexpr int    NUM_METALLIC_INSTANCES = 10;
constexpr size_t NUM_RND_COLORS = 5;
static const std::array< XMFLOAT3, NUM_RND_COLORS> Colors =
{
	  XMFLOAT3(0.90f, 0.90f, 0.90f) // white
	, XMFLOAT3(0.40f, 0.15f, 0.00f) // orange
	, XMFLOAT3(0.00f, 0.05f, 0.65f) // blue
	, XMFLOAT3(0.05f, 0.05f, 0.05f) // black
	, XMFLOAT3(0.15f, 0.15f, 0.15f) // gray
};


void StressTestScene::UpdateScene(float dt, FSceneView& SceneView)
{
	if (mInput.IsKeyTriggered("Space"))
		bAnimateEnvironmentMapRotation = !bAnimateEnvironmentMapRotation;

	constexpr float HDRI_ROTATION_SPEED = 0.01f;
	if (bAnimateEnvironmentMapRotation)
	{
		SceneView.sceneParameters.fYawSliderValue += HDRI_ROTATION_SPEED * dt;
		if (SceneView.sceneParameters.fYawSliderValue > 1.0f)
			SceneView.sceneParameters.fYawSliderValue = 0.0f;
	}
}


void StressTestScene::InitializeScene()
{

}

void StressTestScene::LoadScene(FSceneRepresentation& scene)
{
	//
	// LAMBDA FUNCTION DEFINITIONS
	//
	auto fnGetInstanceCloudMaterialName = [](int r, int m, int c) -> std::string
	{
		std::string matName = "RoughnessMetallicColor[";
		matName += std::to_string(r);
		matName += "][" + std::to_string(m) + "]";
		matName += "][" + std::to_string(c) + "]";
		return matName;
	};
	auto fnGetRoughnessMetallicMaterialName = [](int r, int m)
	{
		std::string matName = "RoughnessMetallic[";
		matName += std::to_string(r);
		matName += "][" + std::to_string(m) + "]";
		return matName;
	};

	auto fnGetRandomNormalMapFile = []() -> std::string
	{
		const size_t NUM_RND_MAPS = 5;
		std::array<std::string, NUM_RND_MAPS> maps =
		{
			  "Data/Textures/PBR/Black_herringbone_tiles_01/Black_herringbone_tiles_01_2K_Normal.png"
			, "Data/Textures/PBR/Marble_08/Marble_08_2K_Normal.png"
			, "Data/Textures/PBR/Metal_tiles_03/Metal_tiles_03_2K_Normal.png"
			, "Data/Textures/PBR/Painted_metal_02/Painted_metal_02_2K_Normal.png"
			, "Data/Textures/PBR/Small_tiles_01/Small_tiles_01_2K_Normal.png"
		};
		return maps[MathUtil::RandI(0, NUM_RND_MAPS)];
	};
	auto fnGetRandomRoughnessMetallicMapFile = []() -> std::string
	{
		const size_t NUM_RND_MAPS = 10;
		std::array<std::string, NUM_RND_MAPS> maps =
		{
			  "Data/Textures/PBR/Black_herringbone_tiles_01/Black_herringbone_tiles_01_2K_Roughness.png"
			, "Data/Textures/PBR/Marble_08/Marble_08_2K_Roughness.png"
			, "Data/Textures/PBR/Metal_tiles_03/Metal_tiles_03_2K_Roughness.png"
			, "Data/Textures/PBR/Painted_metal_02/Painted_metal_02_2K_Roughness.png"
			, "Data/Textures/PBR/Small_tiles_01/Small_tiles_01_2K_Roughness.png"
			, "Data/Textures/PBR/Black_herringbone_tiles_01/Black_herringbone_tiles_01_2K_Metallic.png"
			, "Data/Textures/PBR/Marble_08/Marble_08_2K_Metallic.png"
			, "Data/Textures/PBR/Metal_tiles_03/Metal_tiles_03_2K_Metallic.png"
			, "Data/Textures/PBR/Painted_metal_02/Painted_metal_02_2K_Metallic.png"
			, "Data/Textures/PBR/Small_tiles_01/Small_tiles_01_2K_Metallic.png"
		};
		return maps[MathUtil::RandI(0, NUM_RND_MAPS)];
	};


	//
	// MATERIALS
	//
	if constexpr (ENABLE_MATERIALS)
	{
		FMaterialRepresentation matRep = {};
		matRep.Alpha = 1.0f;
		matRep.DiffuseColor = { 1, 1, 1 };
		matRep.DiffuseMapFilePath = "Procedural/Checkerboard";
		matRep.RoughnessMapFilePath = "Procedural/Checkerboard";
		matRep.Name = "Checkerboard";
		scene.Materials.push_back(matRep);
	
		matRep.DiffuseMapFilePath = "Procedural/Checkerboard_Grayscale";
		matRep.RoughnessMapFilePath = "Procedural/Checkerboard";
		matRep.Name = "Checkerboard_Grayscale";
		scene.Materials.push_back(matRep);

		// roughness / metallic gradiant materials for spheres
		for (int r = 0; r < NUM_ROUGHNESS_INSTANCES; ++r)
		{
			for (int m = 0; m < NUM_METALLIC_INSTANCES; ++m)
			{
				const float roughness = static_cast<float>(r) / (NUM_ROUGHNESS_INSTANCES - 1);
				const float metallic  = static_cast<float>(m) / (NUM_METALLIC_INSTANCES - 1);

				FMaterialRepresentation matR = {};
				matR.DiffuseColor = XMFLOAT3(0.00f, 0.05f, 0.45f); // blue spheres
				matR.Roughness = roughness;
				matR.Metalness = metallic;
				matR.Name = fnGetRoughnessMetallicMaterialName(r, m);

				scene.Materials.push_back(matR);
			}
		}

		for (int r = 0; r < NUM_ROUGHNESS_INSTANCES; ++r)
		{
			for (int m = 0; m < NUM_METALLIC_INSTANCES; ++m)
			{
				for (int c = 0; c < NUM_RND_COLORS; ++c)
				{
					const float roughness = static_cast<float>(r) / (NUM_ROUGHNESS_INSTANCES - 1);
					const float metallic = static_cast<float>(m) / (NUM_METALLIC_INSTANCES - 1);

					FMaterialRepresentation matR = {};
					matR.DiffuseColor = Colors[c];
					matR.Roughness = roughness;
					matR.Metalness = metallic;
					if constexpr (ENABLE_RANDOM_NORMALS_IN_RANDOM_MATERIALS)
					{
						matR.NormalMapFilePath = (MathUtil::RandF(0.0f, 1.0f) > 0.50f) ? fnGetRandomNormalMapFile() : "";
					}
					if constexpr (ENABLE_RANDOM_ROUGHNESS_TEXTURES_IN_RANDOM_MATERIALS)
					{ 
						matR.RoughnessMapFilePath = (MathUtil::RandF(0.0f, 1.0f) < 0.30f) ? fnGetRandomRoughnessMetallicMapFile() : "";
					}

					//matR.MetallicMapFilePath  = (MathUtil::RandF(0.0f, 1.0f) < 0.30f) ? fnGetRandomRoughnessMetallicMapFile() : "";
					matR.Name = fnGetInstanceCloudMaterialName(r, m, c);
					scene.Materials.push_back(matR);
				}
			}
		}
	}


	//
	// OBJECTS
	//
	
	// small cubes
	constexpr int DIMENSION_X = 64;
	constexpr int DIMENSION_Y = 4;
	constexpr int DIMENSION_Z = 48;

	int NumObjects = 0;
	constexpr float distance = 10.0f;
	constexpr float yOffset = 25.0f;
	constexpr float fScaleBaseValue = 2.5f;
	constexpr float fScaleNegativeOffsetMax = fScaleBaseValue / 3.f;

	for(int x=-DIMENSION_X/2; x<DIMENSION_X/2; ++x)
	for(int y=-DIMENSION_Y/2; y<DIMENSION_Y/2; ++y)
	for(int z=-DIMENSION_Z/2; z<DIMENSION_Z/2; ++z)
	{
		FGameObjectRepresentation obj = {};

		XMFLOAT3 pos = {x*distance, yOffset + y*distance + MathUtil::RandF(-4.0f, 4.0f), z * distance};
		XMFLOAT3 scaleRndXYZ = { 
			  fScaleBaseValue - MathUtil::RandF(0.f, fScaleNegativeOffsetMax)
			, fScaleBaseValue - MathUtil::RandF(0.f, fScaleNegativeOffsetMax)
			, fScaleBaseValue - MathUtil::RandF(0.f, fScaleNegativeOffsetMax)
		};
		float rndOff = MathUtil::RandF(0.f, fScaleNegativeOffsetMax);
		XMFLOAT3 scaleRndXXX = {
			  fScaleBaseValue - rndOff
			, fScaleBaseValue - rndOff
			, fScaleBaseValue - rndOff
		};

		const bool bIsCube = MathUtil::RandI(0, 2) == 0;

		obj.tf.SetPosition(pos);
		obj.tf.RotateAroundAxisDegrees(UpVector   , MathUtil::RandF(0.0f, 360.f));
		obj.tf.RotateAroundAxisDegrees(RightVector, MathUtil::RandF(-15.0f, 15.f));
		obj.tf.SetScale(bIsCube ? scaleRndXYZ : scaleRndXXX);
		obj.BuiltinMeshName = (bIsCube ? "Cube" : "Sphere");

		if constexpr (ENABLE_MATERIALS)
		{
			obj.MaterialName = (MathUtil::RandF(0.0f, 1.0f) < 0.05f)
				? (NumObjects % 2 == 0 ? "Checkerboard_Grayscale" : "Checkerboard")
				: fnGetInstanceCloudMaterialName(
					  ENABLE_RANDOM_ROUGHNESS_TEXTURES_IN_RANDOM_MATERIALS ? MathUtil::RandI(0, NUM_ROUGHNESS_INSTANCES) : NUM_ROUGHNESS_INSTANCES-1
					, MathUtil::RandI(0, NUM_METALLIC_INSTANCES)
					, ENABLE_RANDOM_COLORS_IN_RANDOM_MATERIALS ? MathUtil::RandI(0, NUM_RND_COLORS) : 0
				);
		}

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
				if constexpr (ENABLE_MATERIALS)
				{
					obj.MaterialName = fnGetRoughnessMetallicMaterialName(r, m);
				}

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
		if constexpr (ENABLE_MATERIALS)
		{
			obj.MaterialName = "Checkerboard";
		}
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
		if constexpr (ENABLE_MATERIALS)
		{
			obj.MaterialName = "Checkerboard_Grayscale";
		}
		scene.Objects.push_back(obj);
	}
}

void StressTestScene::UnloadScene()
{

}

void StressTestScene::RenderSceneUI() const
{
}

