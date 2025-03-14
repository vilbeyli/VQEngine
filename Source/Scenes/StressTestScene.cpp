#include "Scenes.h"
#include "Engine/Scene/SceneViews.h"
#include "Engine/Core/Input.h"
#include "Libs/VQUtils/Source/utils.h"

using namespace DirectX;

// randomized object grid
#if _DEBUG
constexpr int DIMENSION_X = 16;
constexpr int DIMENSION_Y = 2;
constexpr int DIMENSION_Z = 12;
#else
constexpr int DIMENSION_X = 64;
constexpr int DIMENSION_Y = 4;
constexpr int DIMENSION_Z = 48;
#endif

constexpr size_t NUM_TESSELLATION_SPHERES_ROW = 2;
constexpr size_t NUM_TESSELLATION_SPHERES_COL = 2;
constexpr size_t NUM_TESSELLATION_SPHERES = NUM_TESSELLATION_SPHERES_COL * NUM_TESSELLATION_SPHERES_ROW;



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
		SceneView.sceneRenderOptions.fYawSliderValue += HDRI_ROTATION_SPEED * dt;
		if (SceneView.sceneRenderOptions.fYawSliderValue > 1.0f)
			SceneView.sceneRenderOptions.fYawSliderValue = 0.0f;
	}

	// animation
	if (bEnableGeneratedObjectAnimation)
	{
		for (int i=0; i< mAnimatiedObjectHandles.size(); ++i)
		{
			const size_t hObj = mAnimatiedObjectHandles[i];
			Transform* pTf = GetGameObjectTransform(hObj);

			XMVECTOR vAxis;
			
			if (bEnableOrbit)
			{
				vAxis = XMLoadFloat3(&mOrbitAxes[i]);
				XMVECTOR vPoint = XMLoadFloat3(&mOrbitRotationPoint);
				vPoint.m128_f32[3] = 1.0f;
				pTf->RotateAroundPointAndAxis(vAxis, mOrbitSpeeds[i] * dt, vPoint);
			}
			
			if (bEnableRotation)
			{
				vAxis = XMLoadFloat3(&mRotationAxes[i]);
				pTf->RotateAroundAxisDegrees(vAxis, mRotationSpeeds[i] * dt);
			}
		}
	}
}


constexpr float ROTATION_SPEED_MIN = 0.0f;
constexpr float ROTATION_SPEED_MAX = 400.0f;
constexpr float ROTATION_SPEED_DEFAULT = 0.0f;
constexpr float ORBIT_SPEED_MIN = 0.1f;
constexpr float ORBIT_SPEED_MAX = 0.8f;
constexpr float ORBIT_SPEED_DEFAULT = 0.25f;
void StressTestScene::InitializeScene()
{
	bEnableGeneratedObjectAnimation = false;
	bEnableOrbit = true;
	bEnableRotation = true;

	bRandomizeRotationSpeeds = true;
	bRandomizeOrbitSpeeds = true;
	bRandomizeRotationAxes = true;

	mAnimatiedObjectHandles.clear();
	mRotationAxes.clear();
	mRotationSpeeds.clear();
	mOrbitAxes.clear();
	mOrbitSpeeds.clear();
	for (size_t hObj : this->mGameObjectHandles)
	{
		const GameObject* pObj = GetGameObject(hObj);
		const ModelID iModel = pObj->mModelID;
		auto it = mModels.find(iModel);
		if (it == mModels.end())
		{
			continue;
		}

		const Model& model = it->second;
		
		const bool bGeneratedObject = model.mModelName.empty();
		if (bGeneratedObject)
		{
			mAnimatiedObjectHandles.push_back(hObj);

			XMFLOAT3 f3RotationAxis = UpVector;
			if (bRandomizeRotationAxes)
			{
				const float fAzimuth = MathUtil::RandF(0.0f, 360.0f ) * DEG2RAD;
				const float fZenith  = MathUtil::RandF(-90.0f, 90.0f) * DEG2RAD;
				f3RotationAxis.x = cosf(fAzimuth);
				f3RotationAxis.y = sinf(fZenith);
				f3RotationAxis.z = sinf(fAzimuth);
				
				XMVECTOR vAxis = XMLoadFloat3(&f3RotationAxis);
				vAxis = XMVector3Normalize(vAxis);
				XMStoreFloat3(&f3RotationAxis, vAxis);
			}

			mRotationAxes.push_back(f3RotationAxis);
			mOrbitAxes.push_back(f3RotationAxis);
			mOrbitSpeeds.push_back(bRandomizeOrbitSpeeds ? MathUtil::RandF(ORBIT_SPEED_MIN   , ORBIT_SPEED_MAX   ) : ORBIT_SPEED_DEFAULT   );
			mRotationSpeeds.push_back(bRandomizeRotationSpeeds ? MathUtil::RandF(ROTATION_SPEED_MIN, ROTATION_SPEED_MAX) : ROTATION_SPEED_DEFAULT);
		}
	}
	mOrbitRotationPoint = XMFLOAT3(0, 0, 0); // origin
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
		static std::array<const char*, NUM_RND_MAPS> maps =
		{
			  "Data/Textures/PBR/BlackHerringboneTiles01_MR_2K/BlackHerringboneTiles01_2K_Normal.png"
			, "Data/Textures/PBR/Marble08_MR_2K/Marble08_2K_Normal.png"
			, "Data/Textures/PBR/BlackTiles07_MR_2K/BlackTiles07_2K_Normal.png"
			, "Data/Textures/PBR/PaintedMetal02_MR_2K/PaintedMetal02_2K_Normal.png"
			, "Data/Textures/PBR/BlackTiles01_MR_2K/BlackTiles01_2K_Normal.png"
		};
		return maps[MathUtil::RandI(0, NUM_RND_MAPS)];
	};
	auto fnGetRandomRoughnessMetallicMapFile = []() -> std::string
	{
		const size_t NUM_RND_MAPS = 8;
		static std::array<const char*, NUM_RND_MAPS> maps =
		{
			  "Data/Textures/PBR/BlackHerringboneTiles01_MR_2K/BlackHerringboneTiles01_2K_Roughness.png"
			, "Data/Textures/PBR/Marble08_MR_2K/Marble08_2K_Roughness.png"
			, "Data/Textures/PBR/BlackTiles07_MR_2K/BlackTiles07_2K_Roughness.png"
			, "Data/Textures/PBR/PaintedMetal02_MR_2K/PaintedMetal02_2K_Roughness.png"
			, "Data/Textures/PBR/BlackTiles01_MR_2K/BlackTiles01_2K_Roughness.png"
			, "Data/Textures/PBR/BlackTiles07_MR_2K/BlackTiles07_2K_BaseColor.png"
			, "Data/Textures/PBR/PaintedMetal02_MR_2K/PaintedMetal02_2K_Metallic.png"
			, "Data/Textures/PBR/BlackTiles01_MR_2K/BlackTiles01_2K_BaseColor.png"
		};
		return maps[MathUtil::RandI(0, NUM_RND_MAPS)];
	};


	//
	// MATERIALS
	//
	{
		FMaterialRepresentation matR;
		matR.TessellationEnabled = (true);
		matR.TessellationDomain = (ETessellationDomain::QUAD_PATCH);
		matR.TessellationOutputTopology = (ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CW);
		matR.TessellationPartitioning = (ETessellationPartitioning::INTEGER);
		constexpr float TERRAIN_TESS_FACTOR = Tessellation::MAX_TESSELLATION_FACTOR;
		matR.Tessellation.SetAllTessellationFactors(TERRAIN_TESS_FACTOR);

		matR.EmissiveIntensity    = 0.0f;
		matR.NormalMapFilePath    = "Data/Textures/PBR/Pebbles02_MR_2K/Pebbles02_2K_Normal.png";
		matR.DiffuseMapFilePath   = "Data/Textures/PBR/Pebbles02_MR_2K/Pebbles02_2K_BaseColor.png";
		matR.RoughnessMapFilePath = "Data/Textures/PBR/Pebbles02_MR_2K/Pebbles02_2K_Roughness.png";
		matR.HeightMapFilePath    = "Data/Textures/PBR/Pebbles02_MR_2K/Pebbles02_2K_Height.png";
		matR.AOMapFilePath        = "Data/Textures/PBR/Pebbles02_MR_2K/Pebbles02_2K_AO.png";
		matR.EmissiveMapFilePath  = "Data/Textures/PBR/Pebbles02_MR_2K/Pebbles02_2K_Normal.png";
		matR.Displacement         = 75.0f;
		matR.Name                 = "GroundPlatformMaterial";
		scene.Materials.push_back(matR);
	}
	if constexpr (ENABLE_MATERIALS)
	{
		const size_t NumMaterials = 2
			+ NUM_ROUGHNESS_INSTANCES * NUM_METALLIC_INSTANCES
			+ NUM_ROUGHNESS_INSTANCES * NUM_METALLIC_INSTANCES * NUM_RND_COLORS;

		size_t iMat = scene.Materials.size();
		scene.Materials.resize(scene.Materials.size() + NumMaterials);

		{
			FMaterialRepresentation& matR = scene.Materials[iMat++];
			matR.Alpha = 1.0f;
			matR.DiffuseColor = { 1, 1, 1 };
			matR.DiffuseMapFilePath = "Procedural/Checkerboard";
			matR.RoughnessMapFilePath = "Procedural/Checkerboard";
			matR.Name = "Checkerboard";
		}
		{
			FMaterialRepresentation& matR = scene.Materials[iMat++];
			matR.Alpha = 1.0f;
			matR.DiffuseColor = { 1, 1, 1 };
			matR.DiffuseMapFilePath = "Procedural/Checkerboard_Grayscale";
			matR.RoughnessMapFilePath = "Procedural/Checkerboard";
			matR.Name = "Checkerboard_Grayscale";
		}

		// roughness / metallic gradiant materials for spheres
		for (int r = 0; r < NUM_ROUGHNESS_INSTANCES; ++r)
		{
			for (int m = 0; m < NUM_METALLIC_INSTANCES; ++m)
			{
				const float roughness = static_cast<float>(r) / (NUM_ROUGHNESS_INSTANCES - 1);
				const float metallic  = static_cast<float>(m) / (NUM_METALLIC_INSTANCES - 1);

				FMaterialRepresentation& matR = scene.Materials[iMat++];
				matR.DiffuseColor = XMFLOAT3(0.00f, 0.05f, 0.45f); // blue spheres
				matR.Roughness = roughness;
				matR.Metalness = metallic;
				matR.Name = fnGetRoughnessMetallicMaterialName(r, m);
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

					FMaterialRepresentation& matR = scene.Materials[iMat++];
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
				}
			}
		}
	}


	//
	// OBJECTS
	//
	size_t iObj = scene.Objects.size();
	scene.Objects.resize(scene.Objects.size() + 1);

	// ground platform
	{
		FGameObjectRepresentation& obj = scene.Objects[iObj++];
		XMFLOAT3 pos = { 0, -200, 0 };
		XMFLOAT3 axis = UpVector;
		XMFLOAT3 scale = { 2000, 1, 2000 };
		obj.tf.SetPosition(pos);
		obj.tf.SetScale(scale);
		obj.BuiltinMeshName = "TessellationGrid_Quad25";
		obj.ModelName = "GroundPlatform";
		if constexpr (ENABLE_MATERIALS)
		{
			obj.MaterialName = "GroundPlatformMaterial";
		}
	}

	// TODO: pre-alloc

	// object chunk
	const size_t NumObjectsToAllocate
		= DIMENSION_X * DIMENSION_Y * DIMENSION_Z // randomized objects 
		+ NUM_ROUGHNESS_INSTANCES * NUM_METALLIC_INSTANCES // gradient spheres
		+ NUM_TESSELLATION_SPHERES;
	
	scene.Objects.resize(scene.Objects.size() + NumObjectsToAllocate);

	int NumObjects = 0;
	constexpr float distance = 10.0f;
	constexpr float yOffset = 25.0f;
	constexpr float fScaleBaseValue = 2.5f;
	constexpr float fScaleNegativeOffsetMax = fScaleBaseValue / 3.f;

	for(int x=-DIMENSION_X/2; x<=(DIMENSION_X-1)/2; ++x)
	for(int y=-DIMENSION_Y/2; y<=(DIMENSION_Y-1)/2; ++y)
	for(int z=-DIMENSION_Z/2; z<=(DIMENSION_Z-1)/2; ++z)
	{
		FGameObjectRepresentation& obj = scene.Objects[iObj++];

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

				FGameObjectRepresentation& obj = scene.Objects[iObj++];
				obj.tf.SetPosition(pos);
				obj.tf.SetScale(scale);
				obj.BuiltinMeshName = "Sphere";
				if constexpr (ENABLE_MATERIALS)
				{
					obj.MaterialName = fnGetRoughnessMetallicMaterialName(r, m);
				}
			}
		}

		for (int r = 0; r < NUM_TESSELLATION_SPHERES_ROW; ++r)
		{
			for (int m = 0; m < NUM_TESSELLATION_SPHERES_COL; ++m)
			{
				XMFLOAT3 scale = { 7, 7, 7 };
				constexpr float posX_Begin = +50.0f;
				const float posX_Incr = r * (2 * scale.x + 2.0f);

				constexpr float posY_Begin = 200.0f;
				const float posY_Incr = m * (2 * scale.x + 2.0f);

				float x = posX_Begin + posX_Incr;
				float y = posY_Begin + posY_Incr;

				XMFLOAT3 pos = XMFLOAT3(x, y, -50);

				FGameObjectRepresentation& obj = scene.Objects[iObj++];
				obj.tf.SetPosition(pos);
				obj.tf.SetScale(scale);
				obj.BuiltinMeshName = "Sphere";
				//if constexpr (ENABLE_MATERIALS)
				{
					FMaterialRepresentation matR;
					matR.TessellationEnabled = (true);
					matR.TessellationDomain = (ETessellationDomain::TRIANGLE_PATCH);
					matR.TessellationOutputTopology = (ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CW);
					matR.TessellationPartitioning = (ETessellationPartitioning::INTEGER);
					constexpr float TESSELLATION_FACTOR = 10.0f;
					matR.Tessellation.SetAllTessellationFactors(TESSELLATION_FACTOR);
					
					const size_t i = r * NUM_TESSELLATION_SPHERES_COL + m;
					matR.EmissiveIntensity = static_cast<float>(i);
					matR.Name = "TessellatedDisplacementMaterial[" + std::to_string(i) + "]";
					scene.Materials.push_back(matR);

					obj.MaterialName = matR.Name;
				}
			}
		}
	}

	// big cube
	if constexpr (false)
	{
		FGameObjectRepresentation& obj = scene.Objects[iObj++];
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
		//scene.Objects.[iObj++]= (obj);
	}

}

void StressTestScene::UnloadScene()
{

}

void StressTestScene::RenderSceneUI() const
{
}