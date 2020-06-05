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

//#include "../Application/Application.h"

#include <ctime>
#include <cstdlib>
#include <windows.h>
#include <vector>
#include <string>

#include "Libs/VQUtils/Source/Log.h"
#include "Libs/VQUtils/Source/utils.h"

struct FStartupParameters
{
	Log::LogInitializeParams LogInitParams;

};

void ParseCommandLineParameters(FStartupParameters& refStartupParams, PSTR pScmdl)
{
	const std::string StrCmdLineParams = pScmdl;
	const std::vector<std::string> params = StrUtil::split(StrCmdLineParams, ' ');
	for (const std::string& param : params)
	{
		const std::vector<std::string> paramNameValue = StrUtil::split(param, '=');
		const std::string& paramName = paramNameValue.front();
		std::string  paramValue = paramNameValue.size() > 1 ? paramNameValue[1] : "";

		if (paramName == "-LogConsole")
		{
			refStartupParams.LogInitParams.bLogConsole = true;
		}
		if (paramName == "-LogFile")
		{
			refStartupParams.LogInitParams.bLogFile = true;
			refStartupParams.LogInitParams.LogFilePath = std::move(paramValue);
		}

	}
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, PSTR pScmdl, int iCmdShow)
{
	srand(static_cast<unsigned>(time(NULL)));
	FStartupParameters StartupParameters = {};

	ParseCommandLineParameters(StartupParameters, pScmdl);

	Log::Initialize(StartupParameters.LogInitParams);

	// Launch/Run Engine

	Log::Exit();

	return 0;
}
