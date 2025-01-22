//	VQE
//	Copyright(C) 2025  - Volkan Ilbeyli
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

#include "../Scene/Serialization.h"
#include "../../Renderer/HDR.h"
#include "../EnvironmentMap.h"

struct FStartupParameters;

namespace FileParser
{
	// Reads EngineSettings.ini from next to the executable and returns a 
	// FStartupParameters struct as it readily has override booleans for engine settings
	void                                     ParseEngineSettingsFile(FStartupParameters& StartupParameters);
	std::vector<std::pair<std::string, int>> ParseSceneIndexMappingFile();
	std::vector<FEnvironmentMapDescriptor>   ParseEnvironmentMapsFile();
	std::vector<FDisplayHDRProfile>          ParseHDRProfilesFile();
	FSceneRepresentation                     ParseSceneFile(const std::string& SceneFile);
	std::vector<FMaterialRepresentation>     ParseMaterialFile(const std::string& MaterialFilePath);
}