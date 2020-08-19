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

#include "VQEngine.h"

#include "Libs/VQUtils/Source/utils.h"
#include "Libs/VQUtils/Libs/tinyxml2/tinyxml2.h"

#include <fstream>
#include <cassert>

#ifdef _DEBUG
constexpr char* BUILD_CONFIG = "-Debug";
#else
constexpr char* BUILD_CONFIG = "";
#endif
constexpr char* VQENGINE_VERSION = "v0.5.0";


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
	  { "Fullscreen"           , EDisplayMode::EXCLUSIVE_FULLSCREEN   }
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

FSceneRepresentation VQEngine::ParseSceneFile(const std::string& SceneFile)
{
	using namespace DirectX;
	using namespace tinyxml2;
	//-----------------------------------------------------------------
	constexpr char* XML_TAG__SCENE                  = "Scene";
	constexpr char* XML_TAG__ENVIRONMENT_MAP        = "EnvironmentMap";
	constexpr char* XML_TAG__ENVIRONMENT_MAP_PRESET = "Preset";
	constexpr char* XML_TAG__CAMERA                 = "Camera";
	constexpr char* XML_TAG__GAMEOBJECT             = "GameObject";
	//-----------------------------------------------------------------
	constexpr char* SCENE_FILES_DIRECTORY           = "Data/Levels/";
	//-----------------------------------------------------------------
	//      std::vector<FSceneRepresentation> SceneRepresentations;
	//const std::vector<std::string>          SceneFiles = DirectoryUtil::ListFilesInDirectory(SCENE_FILES_DIRECTORY, ".xml");
	//-----------------------------------------------------------------

	// parse vectors --------------------------------------------------
	// e.g." 0.0 9 -1.0f" -> [0.0f, 9.0f, -1.0f]
	auto fnParseF3 = [](const std::string& xyz) -> XMFLOAT3
	{
		XMFLOAT3 f3;
		std::vector<std::string> tokens = StrUtil::split(xyz, ' ');
		assert(tokens.size() == 3);
		f3.x = StrUtil::ParseFloat(tokens[0]);
		f3.y = StrUtil::ParseFloat(tokens[1]);
		f3.z = StrUtil::ParseFloat(tokens[2]);
		return f3;
	};
	auto fnParseF4 = [](const std::string& xyzw) -> XMFLOAT4
	{
		XMFLOAT4 f4;
		std::vector<std::string> tokens = StrUtil::split(xyzw, ' ');
		assert(tokens.size() == 4);
		f4.x = StrUtil::ParseFloat(tokens[0]);
		f4.y = StrUtil::ParseFloat(tokens[1]);
		f4.z = StrUtil::ParseFloat(tokens[2]);
		f4.w = StrUtil::ParseFloat(tokens[3]);
		return f4;
	};
	// parse xml elements ---------------------------------------------
	auto fnParseXMLStringVal = [](XMLElement* pEle, std::string& dest)
	{
		XMLNode* pNode = pEle->FirstChild();
		if (pNode)
		{
			dest = pNode->Value();
		}
	};
	auto fnParseXMLFloatVal = [](XMLElement* pEle, float& dest)
	{
		XMLNode* pNode = pEle->FirstChild();
		if (pNode)
		{
			dest = StrUtil::ParseFloat(pNode->Value());
		}
	};
	auto fnParseXMLFloat3Val = [&](XMLElement* pEle, XMFLOAT3& f3)
	{
		XMLNode* pNode = pEle->FirstChild();
		if (pNode)
		{
			f3 = fnParseF3(pNode->Value());
		}
	};
	auto fnParseXMLFloat4Val = [&](XMLElement* pEle, XMFLOAT4& f4)
	{
		XMLNode* pNode = pEle->FirstChild();
		if (pNode)
		{
			f4 = fnParseF4(pNode->Value());
		}
	};
	// parse engine stuff -------------------------------------------
	auto fnParseTransform = [&](XMLElement* pTransform) -> Transform
	{
		Transform tf;

		XMLElement* pPos  = pTransform->FirstChildElement("Position");
		XMLElement* pQuat = pTransform->FirstChildElement("Quaternion");
		XMLElement* pRot  = pTransform->FirstChildElement("Rotation");
		XMLElement* pScl  = pTransform->FirstChildElement("Scale");
		if (pPos) fnParseXMLFloat3Val(pPos, tf._position);
		if (pScl) fnParseXMLFloat3Val(pScl, tf._scale);
		if (pQuat)
		{
			XMFLOAT4 qf4; fnParseXMLFloat4Val(pQuat, qf4);
			tf._rotation = Quaternion(qf4.w, XMFLOAT3(qf4.x, qf4.y, qf4.z));
		}
		if (pRot)
		{
			XMFLOAT3 f3; fnParseXMLFloat3Val(pRot, f3);
			tf.RotateAroundGlobalXAxisDegrees(f3.x);
			tf.RotateAroundGlobalYAxisDegrees(f3.y);
			tf.RotateAroundGlobalZAxisDegrees(f3.z);
		}
		return tf;
	};

	//-----------------------------------------------------------------

	// Start reading scene XML file
	FSceneRepresentation SceneRep = {};

	// parse XML
	tinyxml2::XMLDocument doc;
	doc.LoadFile(SceneFile.c_str());

	// scene name
	SceneRep.SceneName = DirectoryUtil::GetFileNameWithoutExtension(SceneFile);


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
					fnParseXMLStringVal(pPreset, SceneRep.EnvironmentMapPreset);
				}
			}

			// Cameras
			else if (XML_TAG__CAMERA == CurrEle)
			{
				FCameraParameters cam = {};
				XMLElement*& pCam = pCurrentSceneElement;

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
					fnParseXMLFloat3Val(pPos, xyz);
					cam.x = xyz.x;
					cam.y = xyz.y;
					cam.z = xyz.z;
				}
				if (pPitch) fnParseXMLFloatVal(pPitch, cam.Pitch); 
				if (pYaw)   fnParseXMLFloatVal(pYaw, cam.Yaw);

				// projection----------------------------------------
				if(pProj)
				{
					std::string projVal;
					fnParseXMLStringVal(pProj, projVal);
					cam.ProjectionParams.bPerspectiveProjection = projVal == "Perspective";
				}
				if(pFoV ) fnParseXMLFloatVal(pFoV , cam.ProjectionParams.FieldOfView);
				if(pNear) fnParseXMLFloatVal(pNear, cam.ProjectionParams.NearZ);
				if(pFar ) fnParseXMLFloatVal(pFar , cam.ProjectionParams.FarZ);
						

				// attributes----------------------------------------
				if (pFP)
				{
					cam.bInitializeCameraController = true;
					cam.bFirstPerson = true;
					if(pTSpeed)  fnParseXMLFloatVal(pTSpeed, cam.TranslationSpeed);
					if(pASpeed)  fnParseXMLFloatVal(pASpeed, cam.AngularSpeed);
					if(pDrag  )  fnParseXMLFloatVal(pDrag  , cam.Drag);
				}
				if (pOrbit)
				{
					cam.bInitializeCameraController = true;
					cam.bFirstPerson = false;

				}

				SceneRep.Cameras.push_back(cam);
			}


			// Game Objects
			else if (XML_TAG__GAMEOBJECT == CurrEle)
			{
				GameObjectRepresentation obj;

				XMLElement*& pObj = pCurrentSceneElement;
				XMLElement* pTransform = pObj->FirstChildElement("Transform");
				XMLElement* pModel     = pObj->FirstChildElement("Model");
					
				// Transform
				if (pTransform)
				{
					obj.tf = fnParseTransform(pTransform);
				}

				// Model (WIP)
				if (pModel)
				{
					XMLElement* pMesh = pModel->FirstChildElement("Mesh");
					XMLElement* pMaterial = pModel->FirstChildElement("Material");
					XMLElement* pModelPath = pModel->FirstChildElement("Path");
					XMLElement* pModelName = pModel->FirstChildElement("Name");
						
						
					if (pMesh) fnParseXMLStringVal(pMesh, obj.BuiltinMeshName);
					if (pMaterial)
					{
						// TODO
					}
					if (pModelPath) fnParseXMLStringVal(pModelPath, obj.ModelFilePath);
					if (pModelName) fnParseXMLStringVal(pModelName, obj.ModelName);
				}


				SceneRep.Objects.push_back(obj);
			}

			pCurrentSceneElement = pCurrentSceneElement->NextSiblingElement();
		} while (pCurrentSceneElement);
	}

	return SceneRep;
}
