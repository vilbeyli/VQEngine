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

#include <functional>


void VQEngine::LoadThread_Main()
{
	Log::Info("LoadThread_Main()");
	
	while (!this->mbStopAllThreads)
	{
		LoadThread_WaitForLoadTask();
		
		// If we decided to quit while waiting, do not process any more tasks
		if (this->mbStopAllThreads) break;


		Log::Info("LoadThread::Tick()");
		// TODO: Queue<LoadTask>
	}

	Log::Info("LoadThread_Main() : Exit");
}

void VQEngine::LoadThread_WaitForLoadTask()
{
	mSignalLoadTaskReadyForProcess.Wait();
}
