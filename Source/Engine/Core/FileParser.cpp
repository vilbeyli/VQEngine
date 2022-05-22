//	VQE
//	Copyright(C) 2020  - Volkan Ilbeyli
//
//	This program is free software : you can redistribute it and / or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.If not, see <http://www.gnu.org/licenses/>.
//
//	Contact: volkanilbeyli@gmail.com

#include "../VQEngine.h"

#include "Libs/VQUtils/Source/utils.h"
#include "Libs/VQUtils/Libs/tinyxml2/tinyxml2.h"

#include <fstream>
#include <cassert>

using namespace DirectX;

static std::pair<std::string, std::string> ParseLineINI(const std::string& iniLine, bool* pbSectionTag)
{
	assert(!iniLine.empty());
	std::pair<std::string, std::string> SettingNameValuePair;

	const bool bSectionTag = iniLine[0] == '[';
	if(pbSectionTag) 
		*pbSectionTag = bSectionTag;

	if (bSectionTag)
	{
		auto vecSettingNameValue = StrUtil::split(iniLine.substr(1), ']');
		SettingNameValuePair.first = vecSettingNameValue[0];
	}
	else
	{
		auto vecSettingNameValue = StrUtil::split(iniLine, '=');
		assert(vecSettingNameValue.size() >= 2);
		SettingNameValuePair.first = vecSettingNameValue[0];
		SettingNameValuePair.second = vecSettingNameValue[1];
	}

	return SettingNameValuePair;
}

static std::unordered_map<std::string, EDisplayMode> S_LOOKUP_STR_TO_DISPLAYMODE =
{
	  //{ "Fullscreen"           , EDisplayMode::EXCLUSIVE_FULLSCREEN   }
	  { "Fullscreen"           , EDisplayMode::BORDERLESS_FULLSCREEN  }
	, { "Borderless"           , EDisplayMode::BORDERLESS_FULLSCREEN  }
	, { "BorderlessFullscreen" , EDisplayMode::BORDERLESS_FULLSCREEN  }
	, { "BorderlessWindowed"   , EDisplayMode::BORDERLESS_FULLSCREEN  }
	, { "Windowed"             , EDisplayMode::WINDOWED               }
};



FStartupParameters VQEngine::ParseEngineSettingsFile()
{
	constexpr char* ENGINE_SETTINGS_FILE_NAME = "Data/EngineSettings.ini";
	FStartupParameters params = {};

	std::ifstream file(ENGINE_SETTINGS_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		std::string currSection;
		bool bReadingSection = false;
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") continue; // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line, &bReadingSection);
			const std::string& SettingName  = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (bReadingSection)
			{
				currSection = SettingName;
				continue;
			}


			// 
			// Graphics
			//
			if (SettingName == "VSync")
			{
				params.bOverrideGFXSetting_bVSync = true;
				params.EngineSettings.gfx.bVsync = StrUtil::ParseBool(SettingValue);
			}
			if (SettingName == "RenderScale")
			{
				params.bOverrideGFXSetting_RenderScale = true;
				params.EngineSettings.gfx.RenderScale = StrUtil::ParseFloat(SettingValue);
			}
			if (SettingName == "TripleBuffer")
			{
				params.bOverrideGFXSetting_bUseTripleBuffering = true;
				params.EngineSettings.gfx.bUseTripleBuffering = StrUtil::ParseBool(SettingValue);
			}
			if (SettingName == "AntiAliasing" || SettingName == "AA")
			{
				params.bOverrideGFXSetting_bAA = true;
				params.EngineSettings.gfx.bAntiAliasing = StrUtil::ParseBool(SettingValue);
			}
			if (SettingName == "MaxFrameRate" || SettingName == "MaxFPS")
			{
				params.bOverrideGFXSetting_bMaxFrameRate = true;
				if (SettingValue == "Unlimited" || SettingValue == "0")
					params.EngineSettings.gfx.MaxFrameRate = 0;
				else if (SettingValue == "Auto" || SettingValue == "Automatic" || SettingValue == "-1")
					params.EngineSettings.gfx.MaxFrameRate = -1;
				else
					params.EngineSettings.gfx.MaxFrameRate = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "EnvironmentMapResolution")
			{
				params.EngineSettings.gfx.EnvironmentMapResolution = StrUtil::ParseInt(SettingValue);
				params.bOverrideGFXSetting_EnvironmentMapResolution = true;
			}
			if (SettingName == "Reflections")
			{
				params.bOverrideGFXSettings_Reflections = true;
				params.EngineSettings.gfx.Reflections = static_cast<EReflections>(StrUtil::ParseInt(SettingValue));
			}

			// 
			// Engine
			//
			if (SettingName == "Width")
			{
				params.bOverrideENGSetting_MainWindowWidth = true;
				params.EngineSettings.WndMain.Width = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "Height")
			{
				params.bOverrideENGSetting_MainWindowHeight = true;
				params.EngineSettings.WndMain.Height = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "DisplayMode")
			{
				if (S_LOOKUP_STR_TO_DISPLAYMODE.find(SettingValue) != S_LOOKUP_STR_TO_DISPLAYMODE.end())
				{
					params.bOverrideENGSetting_bDisplayMode = true;
					params.EngineSettings.WndMain.DisplayMode = S_LOOKUP_STR_TO_DISPLAYMODE.at(SettingValue);
				}
			}
			if (SettingName == "PreferredDisplay")
			{
				params.bOverrideENGSetting_PreferredDisplay = true;
				params.EngineSettings.WndMain.PreferredDisplay = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "HDR")
			{
				params.bOverrideGFXSetting_bHDR = true;
				params.EngineSettings.WndMain.bEnableHDR = StrUtil::ParseBool(SettingValue);
			}


			if (SettingName == "DebugWindow")
			{
				params.bOverrideENGSetting_bDebugWindowEnable = true;
				params.EngineSettings.bShowDebugWindow = StrUtil::ParseBool(SettingValue);
			}
			if (SettingName == "DebugWindowWidth")
			{
				params.bOverrideENGSetting_DebugWindowWidth = true;
				params.EngineSettings.WndDebug.Width = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "DebugWindowHeight")
			{
				params.bOverrideENGSetting_DebugWindowHeight = true;
				params.EngineSettings.WndDebug.Height = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "DebugWindowDisplayMode")
			{
				if (S_LOOKUP_STR_TO_DISPLAYMODE.find(SettingValue) != S_LOOKUP_STR_TO_DISPLAYMODE.end())
				{
					params.bOverrideENGSetting_DebugWindowDisplayMode = true;
					params.EngineSettings.WndDebug.DisplayMode = S_LOOKUP_STR_TO_DISPLAYMODE.at(SettingValue);
				}
			}
			if (SettingName == "DebugWindowPreferredDisplay")
			{
				params.bOverrideENGSetting_DebugWindowPreferredDisplay = true;
				params.EngineSettings.WndDebug.PreferredDisplay = StrUtil::ParseInt(SettingValue);
			}

			if (SettingName == "Scene")
			{
				params.bOverrideENGSetting_StartupScene = true;
				params.EngineSettings.StartupScene = SettingValue;
			}
			
		}
	}
	else
	{
		Log::Warning("Cannot find settings file %s in current directory: %s", ENGINE_SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
		Log::Warning("Will use default settings for Engine & Graphics.", ENGINE_SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
	}

	file.close();

	return params;
}

std::vector<std::pair<std::string, int>> VQEngine::ParseSceneIndexMappingFile()
{
	constexpr char* SCENE_MAPPING_FILE_NAME = "Data/Scenes.ini";

	std::vector<std::pair<std::string, int>> SceneIndexMappings;

	std::ifstream file(SCENE_MAPPING_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		std::string currSection;
		bool bReadingSection = false;
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") continue; // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line, &bReadingSection);
			const std::string& SettingName = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (bReadingSection)
			{
				currSection = SettingName;
				continue;
			}

			const int          SceneIndex = StrUtil::ParseInt(SettingValue);
			const std::string& SceneName = SettingName;
			SceneIndexMappings.push_back(std::make_pair(SceneName, SceneIndex));
		}
	}
	else
	{
		Log::Warning("Cannot find settings file %s in current directory: %s", SCENE_MAPPING_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
		Log::Warning("Default scene mapping will be used.");
	}

	std::sort(SceneIndexMappings.begin(), SceneIndexMappings.end(), [](const auto& l, const auto& r) { return l.second < r.second; });

	return SceneIndexMappings;
}

std::vector<FEnvironmentMapDescriptor> VQEngine::ParseEnvironmentMapsFile()
{
	constexpr char* SETTINGS_FILE_NAME = "Data/EnvironmentMaps.ini";

	std::vector<FEnvironmentMapDescriptor> EnvironmentMapDescriptors;

	std::ifstream file(SETTINGS_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		bool bReadingSection = false;
		bool bRecentlyReadEmptyLine = false;
		bool bCurrentlyReadingEnvMap = false;
		FEnvironmentMapDescriptor desc = {};
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") { bRecentlyReadEmptyLine = true; continue; } // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line, &bReadingSection);
			const std::string& SettingName = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (bReadingSection)
			{
				bCurrentlyReadingEnvMap = true;
				if (bRecentlyReadEmptyLine)
				{
					EnvironmentMapDescriptors.push_back(desc);
					desc = {};
					bRecentlyReadEmptyLine = false;
				}
				desc.Name = SettingName;
				continue;
			}

			if (SettingName == "Path")
			{
				desc.FilePath = SettingValue;
			}
			if (SettingName == "MaxCLL")
			{
				desc.MaxContentLightLevel = StrUtil::ParseFloat(SettingValue);
			}
			bRecentlyReadEmptyLine = false;
		}
		if (bCurrentlyReadingEnvMap)
		{
			EnvironmentMapDescriptors.push_back(desc);
			desc = {};
			bCurrentlyReadingEnvMap = false;
		}
	}
	else
	{ 
		Log::Error("Cannot find settings file %s in current directory: %s", SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
	}

	return EnvironmentMapDescriptors;
}

std::vector<FDisplayHDRProfile> VQEngine::ParseHDRProfilesFile()
{
	constexpr char* SETTINGS_FILE_NAME = "Data/HDRDisplayProfiles.ini";

	std::vector<FDisplayHDRProfile> HDRProfiles;

	std::ifstream file(SETTINGS_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		bool bReadingSection = false; // Section is an .ini term
		bool bRecentlyReadEmptyLine = false;
		bool bCurrentlyReadingProfile = false;
		FDisplayHDRProfile profile = {};
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") { bRecentlyReadEmptyLine = true; continue; } // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line, &bReadingSection);
			const std::string& SettingName = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (bReadingSection)
			{
				bCurrentlyReadingProfile = true;
				if (bRecentlyReadEmptyLine)
				{
					HDRProfiles.push_back(profile);
					profile = {};
					bRecentlyReadEmptyLine = false;
				}
				profile.DisplayName = SettingName;
				continue;
			}

			if (SettingName == "MaxBrightness")
			{
				profile.MaxBrightness = StrUtil::ParseFloat(SettingValue);
			}
			if (SettingName == "MinBrightness")
			{
				profile.MinBrightness = StrUtil::ParseFloat(SettingValue);
			}
			bRecentlyReadEmptyLine = false;
		}
		if (bCurrentlyReadingProfile) // Take into account the last item we're reading (.push_back() isn't called above for the last item)
		{
			HDRProfiles.push_back(profile);
			profile = {};
			bCurrentlyReadingProfile = false;
		}
	}
	else
	{
		Log::Error("Cannot find settings file %s in current directory: %s", SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
	}
	return HDRProfiles;
}




// xml parsing --------------------------------------------------
using namespace tinyxml2;


constexpr char* XML_TAG__MATERIAL = "Material";

template<class TVec> 
static TVec XMLParseFVec(const std::string& xyzw)
{
	constexpr bool bHas3Elements = std::is_same<TVec, XMFLOAT3>() || std::is_same<TVec, XMFLOAT4>();
	constexpr bool bHas4Elements = std::is_same<TVec, XMFLOAT4>();
	
	// e.g." 0.0 9 -1.0f" -> [0.0f, 9.0f, -1.0f]
	const std::vector<std::string> tokens = StrUtil::split(xyzw, ' ');
	
	TVec f;
	f.x = StrUtil::ParseFloat(tokens[0]);
	f.y = StrUtil::ParseFloat(tokens[1]);
	if constexpr(bHas3Elements) { assert(tokens.size() >= 3); f.z = StrUtil::ParseFloat(tokens[2]); }
	if constexpr(bHas4Elements) { assert(tokens.size() >= 4); f.w = StrUtil::ParseFloat(tokens[3]); }
	return f;
}
template<class TVec> 
static void XMLParseFVecVal(tinyxml2::XMLElement* pEle, TVec& vec)
{
	tinyxml2::XMLNode* pNode = pEle->FirstChild();
	if (pNode)
	{
		vec = XMLParseFVec<TVec>(pNode->Value());
	}
}
static void XMLParseStringVal(tinyxml2::XMLElement* pEle, std::string& dest)
{
	tinyxml2::XMLNode* pNode = pEle->FirstChild();
	if (pNode)
	{
		dest = pNode->Value();
		dest = StrUtil::trim(dest);
	}
}
static void XMLParseFloatVal(tinyxml2::XMLElement* pEle, float& dest)
{
	tinyxml2::XMLNode* pNode = pEle->FirstChild();
	if (pNode)
	{
		dest = StrUtil::ParseFloat(pNode->Value());
	}
}

static FMaterialRepresentation XMLParseMaterial(tinyxml2::XMLElement* pMat)
{
	FMaterialRepresentation mat;

	XMLElement* pName    = pMat->FirstChildElement("Name");
	//------------------------------------------------------------------
	XMLElement* pDiff    = pMat->FirstChildElement("Diffuse");
	XMLElement* pAlph    = pMat->FirstChildElement("Alpha");
	XMLElement* pEmsv    = pMat->FirstChildElement("Emissive");
	XMLElement* pEmsI    = pMat->FirstChildElement("EmissiveIntensity");
	XMLElement* pRgh     = pMat->FirstChildElement("Roughness");
	XMLElement* pMtl     = pMat->FirstChildElement("Metalness");
	//------------------------------------------------------------------
	XMLElement* pDiffMap = pMat->FirstChildElement("DiffuseMap");
	XMLElement* pNrmlMap = pMat->FirstChildElement("NormalMap");
	XMLElement* pEmsvMap = pMat->FirstChildElement("EmissiveMap");
	XMLElement* pAlphMap = pMat->FirstChildElement("AlphaMaskMap");
	XMLElement* pMtlMap  = pMat->FirstChildElement("MetallicMap");
	XMLElement* pRghMap  = pMat->FirstChildElement("RoughnessMap");
	XMLElement* pAOMap   = pMat->FirstChildElement("AOMap");

	if (pName) XMLParseStringVal(pName, mat.Name);
	//------------------------------------------------------------------
	if (pDiff) XMLParseFVecVal<XMFLOAT3>(pDiff, mat.DiffuseColor);
	if (pAlph) XMLParseFloatVal(pAlph, mat.Alpha);
	if (pEmsv) XMLParseFVecVal<XMFLOAT3>(pEmsI, mat.EmissiveColor);
	if (pEmsI) XMLParseFloatVal(pEmsI, mat.EmissiveIntensity);
	if (pRgh ) XMLParseFloatVal(pRgh , mat.Roughness);
	if (pMtl ) XMLParseFloatVal(pMtl , mat.Metalness);
	//-------------------------------------------------------------------
	if (pDiffMap) XMLParseStringVal(pDiffMap, mat.DiffuseMapFilePath  );
	if (pNrmlMap) XMLParseStringVal(pNrmlMap, mat.NormalMapFilePath   );
	if (pEmsvMap) XMLParseStringVal(pEmsvMap, mat.EmissiveMapFilePath );
	if (pAlphMap) XMLParseStringVal(pAlphMap, mat.AlphaMaskMapFilePath);
	if (pMtlMap ) XMLParseStringVal(pMtlMap , mat.MetallicMapFilePath );
	if (pRghMap ) XMLParseStringVal(pRghMap , mat.RoughnessMapFilePath);
	if (pAOMap  ) XMLParseStringVal(pAOMap  , mat.AOMapFilePath);

	return mat;
}

unsigned GetNumSiblings(XMLElement* pEle)
{
	assert(pEle);
	unsigned numSiblings = 0;
	while (pEle != nullptr)
	{
		numSiblings += static_cast<int>(StrUtil::split(pEle->FirstChild()->Value()).size());
		//++numSiblings;
		pEle = pEle->NextSiblingElement();
	}
	return numSiblings;
}

FSceneRepresentation VQEngine::ParseSceneFile(const std::string& SceneFile)
{
	using namespace tinyxml2;
	//-----------------------------------------------------------------
	constexpr char* XML_TAG__SCENE                  = "Scene";
	constexpr char* XML_TAG__ENVIRONMENT_MAP        = "EnvironmentMap";
	constexpr char* XML_TAG__ENVIRONMENT_MAP_PRESET = "Preset";
	constexpr char* XML_TAG__CAMERA                 = "Camera";
	constexpr char* XML_TAG__GAMEOBJECT             = "GameObject";
	constexpr char* XML_TAG__LIGHT                  = "Light";
	//-----------------------------------------------------------------
	constexpr char* SCENE_FILES_DIRECTORY           = "Data/Levels/";
	//-----------------------------------------------------------------

	// functions for parsing engine stuff -----------------------------
	auto fnParseTransform = [&](XMLElement* pTransform) -> Transform
	{
		Transform tf;

		XMLElement* pPos  = pTransform->FirstChildElement("Position");
		XMLElement* pQuat = pTransform->FirstChildElement("Quaternion");
		XMLElement* pRot  = pTransform->FirstChildElement("Rotation");
		XMLElement* pScl  = pTransform->FirstChildElement("Scale");
		const uint NumScaleValues = pScl ? GetNumSiblings(pScl) : 0;
		
		if (pPos) XMLParseFVecVal<XMFLOAT3>(pPos, tf._position);
		if (pScl)
		{
			if (NumScaleValues == 3) XMLParseFVecVal<XMFLOAT3>(pScl, tf._scale);
			else
			{
				float s; XMLParseFloatVal(pScl, s);
				tf._scale = DirectX::XMFLOAT3(s, s, s);
			}
		}
		if (pQuat)
		{
			XMFLOAT4 qf4; XMLParseFVecVal<XMFLOAT4>(pQuat, qf4);
			tf._rotation = Quaternion(qf4.w, XMFLOAT3(qf4.x, qf4.y, qf4.z));
		}
		if (pRot)
		{
			XMFLOAT3 f3; XMLParseFVecVal<XMFLOAT3>(pRot, f3);
			tf.RotateAroundGlobalXAxisDegrees(f3.x);
			tf.RotateAroundGlobalYAxisDegrees(f3.y);
			tf.RotateAroundGlobalZAxisDegrees(f3.z);
		}
		return tf;
	};
	auto fnParseGameObject= [&](XMLElement* pObj) -> FGameObjectRepresentation
	{
		FGameObjectRepresentation obj = {};
		XMLElement* pTransform = pObj->FirstChildElement("Transform");
		XMLElement* pModel     = pObj->FirstChildElement("Model");

		// Transform
		if (pTransform)
		{
			obj.tf = fnParseTransform(pTransform);
		}

		// Model
		if (pModel)
		{
			XMLElement* pMesh      = pModel->FirstChildElement("Mesh");
			XMLElement* pMaterial  = pModel->FirstChildElement("MaterialName");
			XMLElement* pModelPath = pModel->FirstChildElement("Path");
			XMLElement* pModelName = pModel->FirstChildElement("Name");

			if (pMesh)      XMLParseStringVal(pMesh     , obj.BuiltinMeshName);
			if (pMaterial)  XMLParseStringVal(pMaterial , obj.MaterialName);
			if (pModelPath) XMLParseStringVal(pModelPath, obj.ModelFilePath);
			if (pModelName) XMLParseStringVal(pModelName, obj.ModelName);
		}

		return obj;
	};
	auto fnParseCamera    = [&](XMLElement* pCam) ->FCameraParameters
	{
		FCameraParameters cam = {};

		// transform
		XMLElement* pPos   = pCam->FirstChildElement("Position");
		XMLElement* pPitch = pCam->FirstChildElement("Pitch");
		XMLElement* pYaw   = pCam->FirstChildElement("Yaw");

		// projection
		XMLElement* pProj = pCam->FirstChildElement("Projection");
		XMLElement* pFoV  = pCam->FirstChildElement("FoV");
		XMLElement* pNear = pCam->FirstChildElement("Near");
		XMLElement* pFar  = pCam->FirstChildElement("Far");

		// attributes
		XMLElement* pFP     = pCam->FirstChildElement("FirstPerson");
		XMLElement* pTSpeed = pFP ? pFP->FirstChildElement("TranslationSpeed") : nullptr;
		XMLElement* pASpeed = pFP ? pFP->FirstChildElement("AngularSpeed")     : nullptr;
		XMLElement* pDrag   = pFP ? pFP->FirstChildElement("Drag")             : nullptr;

		XMLElement* pOrbit = pCam->FirstChildElement("Orbit");

		// transform ----------------------------------------
		if (pPos)
		{
			XMFLOAT3 xyz;
			XMLParseFVecVal<XMFLOAT3>(pPos, xyz);
			cam.x = xyz.x;
			cam.y = xyz.y;
			cam.z = xyz.z;
		}
		if (pPitch) XMLParseFloatVal(pPitch, cam.Pitch); 
		if (pYaw)   XMLParseFloatVal(pYaw, cam.Yaw);

		// projection----------------------------------------
		if(pProj)
		{
			std::string projVal;
			XMLParseStringVal(pProj, projVal);
			cam.ProjectionParams.bPerspectiveProjection = projVal == "Perspective";
		}
		if(pFoV ) XMLParseFloatVal(pFoV , cam.ProjectionParams.FieldOfView);
		if(pNear) XMLParseFloatVal(pNear, cam.ProjectionParams.NearZ);
		if(pFar ) XMLParseFloatVal(pFar , cam.ProjectionParams.FarZ);
				

		// attributes----------------------------------------
		if (pFP)
		{
			cam.bInitializeCameraController = true;
			cam.ControllerType = ECameraControllerType::FIRST_PERSON;
			if(pTSpeed)  XMLParseFloatVal(pTSpeed, cam.TranslationSpeed);
			if(pASpeed)  XMLParseFloatVal(pASpeed, cam.AngularSpeed);
			if(pDrag  )  XMLParseFloatVal(pDrag  , cam.Drag);
		}
		if (pOrbit)
		{
			cam.bInitializeCameraController = true;
			cam.ControllerType = ECameraControllerType::ORBIT;
		}

		return cam;
	};
	auto fnParseLight     = [&](XMLElement* pLight) -> Light
	{
		Light l;

		XMLElement* pTransform = pLight->FirstChildElement("Transform");
		XMLElement* pColor = pLight->FirstChildElement("Color");
		XMLElement* pRange = pLight->FirstChildElement("Range");
		XMLElement* pBrightness = pLight->FirstChildElement("Brightness");
		XMLElement* pMobility = pLight->FirstChildElement("Mobility");
		XMLElement* pEnabled = pLight->FirstChildElement("Enabled");

		XMLElement* pShadows = pLight->FirstChildElement("Shadows");
		XMLElement* pNear = pShadows ? pShadows->FirstChildElement("NearPlane") : nullptr;
		XMLElement* pFar  = pShadows ? pShadows->FirstChildElement("FarPlane")  : nullptr;
		XMLElement* pBias = pShadows ? pShadows->FirstChildElement("DepthBias")  : nullptr;

		XMLElement* pSpot = pLight->FirstChildElement("Spot");
		XMLElement* pConeOuter = pSpot ? pSpot->FirstChildElement("OuterConeAngleDegrees") : nullptr;
		XMLElement* pConeInner = pSpot ? pSpot->FirstChildElement("InnerConeAngleDegrees") : nullptr;

		XMLElement* pDirectional = pLight->FirstChildElement("Directional");
		XMLElement* pViewPortX = pDirectional ? pDirectional->FirstChildElement("ViewPortX") : nullptr;
		XMLElement* pViewPortY = pDirectional ? pDirectional->FirstChildElement("ViewPortY") : nullptr;
		XMLElement* pDistance  = pDirectional ? pDirectional->FirstChildElement("Distance") : nullptr;

		XMLElement* pPoint = pLight->FirstChildElement("Point");
		XMLElement* pAttenuation = pPoint ? pPoint->FirstChildElement("Attenuation") : nullptr;

		XMLElement* pArea = pLight->FirstChildElement("Area");
		// TODO: area light implementation

		//---------------------------------------------------

		if (pTransform)
		{
			const Transform tf = fnParseTransform(pTransform);
			l.Position = tf._position;
			l.RenderScale = tf._scale;
			l.RotationQuaternion = tf._rotation;
		}

		if(pColor)      { XMLParseFVecVal<XMFLOAT3>(pColor, l.Color); }
		if(pRange)      { XMLParseFloatVal(pRange         , l.Range); }
		if(pBrightness) { XMLParseFloatVal(pBrightness    , l.Brightness); }
		if(pNear)       { XMLParseFloatVal(pNear          , l.ShadowData.NearPlane); }
		if(pFar )       { XMLParseFloatVal(pFar           , l.ShadowData.FarPlane); }
		if(pBias)       { XMLParseFloatVal(pBias          , l.ShadowData.DepthBias); }
		if(pConeOuter)  { XMLParseFloatVal(pConeOuter     , l.SpotOuterConeAngleDegrees); }
		if(pConeInner)  { XMLParseFloatVal(pConeInner     , l.SpotInnerConeAngleDegrees); }
		if(pViewPortX)  { XMLParseFloatVal(pViewPortX     , l.ViewportX); }
		if(pViewPortY)  { XMLParseFloatVal(pViewPortY     , l.ViewportY); }
		if(pDistance)   { XMLParseFloatVal(pDistance      , l.DistanceFromOrigin); }
		if(pMobility)   
		{
			std::string mobilityEnumStr;
			XMLParseStringVal(pMobility, mobilityEnumStr);
			std::transform(mobilityEnumStr.begin(), mobilityEnumStr.end(), mobilityEnumStr.begin(), [](char& c) { return std::tolower(c); });
			if (mobilityEnumStr == "static")     l.Mobility = Light::EMobility::STATIC;
			if (mobilityEnumStr == "dynamic")    l.Mobility = Light::EMobility::DYNAMIC;
			if (mobilityEnumStr == "stationary") l.Mobility = Light::EMobility::STATIONARY;
		}
		if(pEnabled)    
		{ 
			std::string val; XMLParseStringVal(pEnabled, val);
			l.bEnabled = StrUtil::ParseBool(val); 
		}
		if (pShadows)
		{
			l.bCastingShadows = true;
		}
		if(pAttenuation)
		{ 
			XMFLOAT3 attn; XMLParseFVecVal(pAttenuation, attn); 
			l.AttenuationConstant  = attn.x;
			l.AttenuationLinear    = attn.y;
			l.AttenuationQuadratic = attn.z;
		}
		// TODO: area light impl
		//if (pArea)        l.Type = Light::EType::AREA;
		if (pSpot)        l.Type = Light::EType::SPOT;
		if (pDirectional) l.Type = Light::EType::DIRECTIONAL;
		if (pPoint)       l.Type = Light::EType::POINT;

		return l;
	};
	//-----------------------------------------------------------------


	// Start reading scene XML file
	FSceneRepresentation SceneRep = {};

	// parse XML
	tinyxml2::XMLDocument doc;
	doc.LoadFile(SceneFile.c_str());

	// scene name
	SceneRep.SceneName = DirectoryUtil::GetFileNameWithoutExtension(SceneFile);

	// parse scene
	XMLElement* pScene = doc.FirstChildElement(XML_TAG__SCENE);
	if (pScene)
	{
		XMLElement* pCurrentSceneElement = pScene->FirstChildElement();
		if (!pCurrentSceneElement)
		{
			return SceneRep;
		}

		do
		{
			// Environment Map
			const std::string CurrEle = pCurrentSceneElement->Value();
			if (XML_TAG__ENVIRONMENT_MAP == CurrEle)
			{
				XMLElement* pPreset = pCurrentSceneElement->FirstChildElement(XML_TAG__ENVIRONMENT_MAP_PRESET);
				if (pPreset)
				{
					XMLParseStringVal(pPreset, SceneRep.EnvironmentMapPreset);
				}
			}

			// Cameras
			else if (XML_TAG__CAMERA == CurrEle)
			{
				FCameraParameters cam = fnParseCamera(pCurrentSceneElement);
				SceneRep.Cameras.push_back(cam);
			}

			// Materials
			else if (XML_TAG__MATERIAL == CurrEle)
			{
				FMaterialRepresentation mat = XMLParseMaterial(pCurrentSceneElement);
				SceneRep.Materials.push_back(mat);
			}

			// Lights
			else if (XML_TAG__LIGHT == CurrEle)
			{
				Light light = fnParseLight(pCurrentSceneElement);
				SceneRep.Lights.push_back(light);
			}

			// Game Objects
			else if (XML_TAG__GAMEOBJECT == CurrEle)
			{
				FGameObjectRepresentation obj = fnParseGameObject(pCurrentSceneElement);
				SceneRep.Objects.push_back(obj);
			}


			pCurrentSceneElement = pCurrentSceneElement->NextSiblingElement();
		} while (pCurrentSceneElement);
	} // if (pScene)

	return SceneRep;
}


std::vector<FMaterialRepresentation> VQEngine::ParseMaterialFile(const std::string& MaterialFilePath)
{
	std::vector<FMaterialRepresentation> matReps;

	// open xml file
	tinyxml2::XMLDocument doc;
	doc.LoadFile(MaterialFilePath.c_str());

	XMLElement* pRoot = doc.FirstChildElement();

	if (!pRoot)
	{
		Log::Error("ParseMaterialFile() : Err");
		return matReps;
	}

	assert(pRoot);

	XMLElement* pCurrentSceneElement = pRoot->FirstChildElement();
	if (!pCurrentSceneElement)
	{
		return matReps;
	}

	do
	{
		const std::string CurrEle = pCurrentSceneElement->Value();
		if (XML_TAG__MATERIAL == CurrEle)
		{
			FMaterialRepresentation mat = XMLParseMaterial(pCurrentSceneElement);
			matReps.push_back(mat);
		}
		pCurrentSceneElement = pCurrentSceneElement->NextSiblingElement();
	} while (pCurrentSceneElement);

	return matReps;
}

