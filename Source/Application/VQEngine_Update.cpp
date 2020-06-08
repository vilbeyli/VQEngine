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

void VQEngine::UpdateThread_Main()
{
	Log::Info("UpdateThread_Main()");

	bool bQuit = false;
	while (!mbStopAllThreads && !bQuit)
	{
		Sleep(400*2);
		Log::Info("UpdateThread::Tick()");
	}


	Log::Info("UpdateThread_Main() : Exit");
}