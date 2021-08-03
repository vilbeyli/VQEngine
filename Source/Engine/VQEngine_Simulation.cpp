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
#include "Geometry.h"
#include "GPUMarker.h"

#include <d3d12.h>
#include <dxgi.h>

#include "RenderPass/AmbientOcclusion.h"
#include "RenderPass/DepthPrePass.h"

#include "VQEngine_RenderCommon.h"

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// MAIN
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::SimulationThread_Main()
{
	Log::Info("SimulationThread Created.");

	SimulationThread_Initialize();

	float dt = 0.0f;
	bool bQuit = false;
	while (!mbStopAllThreads && !bQuit)
	{
		dt = mTimer.Tick(); // update timer

		SimulationThread_Tick(dt);

		float FrameLimiterTimeSpent = FramePacing(dt);

		// SimulationThread_Logging()
		constexpr int LOGGING_PERIOD = 4; // seconds
		static float LAST_LOG_TIME = mTimer.TotalTime();
		const float TotalTime = mTimer.TotalTime();
		if (TotalTime - LAST_LOG_TIME > 4)
		{
			Log::Info("SimulationThread_Tick() : dt=%.2f ms (Sleep=%.2f)", dt * 1000.0f, FrameLimiterTimeSpent);
			LAST_LOG_TIME = TotalTime;
		}
	}

	SimulationThread_Exit();
}

#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
void VQEngine::SimulationThread_Initialize()
{
	mNumSimulationTicks = 0;

	RenderThread_Inititalize();

	// --- needs renderer ready
	UpdateThread_Inititalize();

	Log::Info("SimulationThread Initialized.");
}

void VQEngine::SimulationThread_Exit()
{
	UpdateThread_Exit();
	RenderThread_Exit();
	Log::Info("SimulationThread Exit.");
}

void VQEngine::SimulationThread_Tick(const float dt)
{
	SCOPED_CPU_MARKER_C("SimulationThread_Tick()", 0xFF007777);

	// world update
	UpdateThread_Tick(dt);

	// ui
	if (!(mbLoadingLevel || mbLoadingEnvironmentMap))
	{
		UpdateUIState(mpWinMain->GetHWND(), dt);
	}

	// render
	RenderThread_Tick();

	++mNumSimulationTicks;
}
#else
void VQEngine::SimulationThread_Initialize(){}
void VQEngine::SimulationThread_Exit(){}
void VQEngine::SimulationThread_Tick(float dt){}
#endif