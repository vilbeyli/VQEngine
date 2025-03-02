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

#include "Renderer.h"

#include "Engine/GPUMarker.h"
#include "Engine/Culling.h"
#include "Engine/Scene/SceneViews.h"

using namespace DirectX;

#define UPDATE_THREAD__ENABLE_WORKERS 1  // TODO: rename to render thread


template<class TMeshInstanceDataArray>
static void CountInstanceData(
	  std::unordered_map<uint64, TMeshInstanceDataArray>& drawParamLookup
	, const std::vector<FVisibleMeshData>& vVisibleMeshData
	, std::function<uint64(MaterialID, MeshID, int, bool)> FnGetKey
)
{
	SCOPED_CPU_MARKER("CountInstanceData");
	for (int i = 0; i < vVisibleMeshData.size(); ++i)
	{
		TMeshInstanceDataArray& d = drawParamLookup[FnGetKey(vVisibleMeshData[i].hMaterial, vVisibleMeshData[i].hMesh, vVisibleMeshData[i].SelectedLOD, vVisibleMeshData[i].bTessellated)];
		d.NumValidData++;
	}
}

template<class TMeshInstanceDataArray, class TMeshRenderCommand>
static void CountNResizeRenderCmds(
	std::unordered_map<uint64, TMeshInstanceDataArray>& drawParamLookup,
	std::vector<TMeshRenderCommand>& MeshRenderCommands,
	int MAX_INSTANCE_COUNT
)
{
	SCOPED_CPU_MARKER("CountNResizeDrawParams");
	int NumInstancedRenderCommands = 0;
	{
		SCOPED_CPU_MARKER("Count");
		for (auto it = drawParamLookup.begin(); it != drawParamLookup.end(); ++it)
		{
			TMeshInstanceDataArray& MeshInstanceData = it->second;
			int NumInstancesToProces = (int)MeshInstanceData.NumValidData;
			int iInst = 0;
			while (NumInstancesToProces > 0)
			{
				const int ThisBatchSize = std::min(MAX_INSTANCE_COUNT, NumInstancesToProces);
				int iBatch = 0;
				while (iBatch < MAX_INSTANCE_COUNT && iInst < MeshInstanceData.NumValidData)
				{
					++iBatch;
					++iInst;
				}
				NumInstancesToProces -= iBatch;
				++NumInstancedRenderCommands;
			}
		}
	}
	{
		SCOPED_CPU_MARKER("Resize");
		MeshRenderCommands.resize(NumInstancedRenderCommands);
	}
}


template<class TMeshInstanceDataArray>
static void AllocInstanceData(std::unordered_map<uint64, TMeshInstanceDataArray>& drawParamLookup)
{
	SCOPED_CPU_MARKER("AllocInstData");
	std::vector<typename std::unordered_map<uint64, TMeshInstanceDataArray>::iterator> deleteList;
	for (auto it = drawParamLookup.begin(); it != drawParamLookup.end(); ++it)
	{
		TMeshInstanceDataArray& a = it->second;
		if (a.NumValidData == 0)
		{
			deleteList.push_back(it);
			continue;
		}
		a.Data.resize(a.NumValidData);
		//Log::Info("AllocInstanceData: %llu -> resize(%d)", it->first, a.NumValidData);
		a.NumValidData = 0; // we'll use NumValidData for indexing after this call.
	}

	for (auto it : deleteList)
	{
		drawParamLookup.erase(it);
	}
}

static void SetParamData(MeshRenderData_t& cmd,
	int iInst,
	const XMMATRIX& viewProj,
	const XMMATRIX& viewProjPrev,
	const FVisibleMeshData& cullResult
)
{
	const XMMATRIX matWorld = cullResult.Transform.matWorldTransformation();
	const XMMATRIX matWorldHistory = cullResult.Transform.matWorldTransformationPrev();
	const XMMATRIX matNormal = cullResult.Transform.NormalMatrix(matWorld);
	const XMMATRIX matWVP = matWorld * viewProj;

	assert(iInst >= 0 && cmd.matWorld.size() > iInst);
	assert(cmd.matWorld.size() <= MAX_INSTANCE_COUNT__SCENE_MESHES);

	cmd.matWorld[iInst] = matWorld;
	cmd.matWorldViewProj[iInst] = matWVP;
	cmd.matWorldViewProjPrev[iInst] = matWorldHistory * viewProjPrev;
	cmd.matNormal[iInst] = matNormal;
	cmd.objectID[iInst] = (int)cullResult.hGameObject + 1; // 0 means empty. offset by 1 now, and undo it when reading back
	cmd.projectedArea[iInst] = cullResult.fBBArea;
	cmd.vertexIndexBuffer = cullResult.VBIB;
	cmd.numIndices = cullResult.NumIndices;
	cmd.matID = cullResult.hMaterial;
	cmd.pMaterial = &cullResult.Material; // TODO: use std::move

#if 0
	auto fnAllZero = [](const XMMATRIX& m)
		{
			for (int hObj = 0; hObj < 4; ++hObj)
				for (int j = 0; j < 4; ++j)
					if (m.r[hObj].m128_f32[j] != 0.0f)
						return false;
			return true;
		};
	if (fnAllZero(matWorld))
	{
		Log::Warning("All zero matrix");
	}
#endif
}
static void SetParamData(FInstancedShadowMeshRenderData& cmd,
	int iInst,
	const XMMATRIX& viewProj,
	const XMMATRIX& viewProjPrev,
	const FVisibleMeshData& cullResult
)
{
	const XMMATRIX matWorld = cullResult.Transform.matWorldTransformation();
	const XMMATRIX matWVP = matWorld * viewProj;

	assert(iInst >= 0 && cmd.matWorldViewProj.size() > iInst);
	assert(iInst >= 0 && cmd.matWorld.size() > iInst);
	assert(cmd.matWorldViewProj.size() <= MAX_INSTANCE_COUNT__SHADOW_MESHES);
	assert(cmd.matWorld.size() <= MAX_INSTANCE_COUNT__SHADOW_MESHES);

	cmd.matWorld[iInst] = matWorld;
	cmd.matWorldViewProj[iInst] = matWVP;
	cmd.vertexIndexBuffer = cullResult.VBIB;
	cmd.numIndices = cullResult.NumIndices;
	cmd.matID = cullResult.hMaterial;
	cmd.pMaterial = &cullResult.Material; // TODO: std::move()
}

template<class TMeshRenderCmd>
static void CollectViewInstanceData(
	  const std::vector<FVisibleMeshData>& vVisibleMeshData
	, size_t iBegin
	, size_t iEnd
	, const XMMATRIX& viewProj
	, const XMMATRIX& viewProjPrev
	, const std::vector<FInstanceDataWriteParam>& vOutputWriteParams // {iDraw, iInst}
	, std::vector<TMeshRenderCmd>& meshRenderParams
)
{
	SCOPED_CPU_MARKER("CollectViewInstanceData");
	for (size_t i = iBegin; i <= iEnd; ++i)
	{
		const int iDraw = vOutputWriteParams[i].iDraw;
		const int iInst = vOutputWriteParams[i].iInst;

		assert(meshRenderParams.size() > iDraw);
		TMeshRenderCmd& cmd = meshRenderParams[iDraw];
		//Log::Info("meshRenderParams[%d] NumInst=%d , iFirst=%d", iCmdParam, NumInstances, range.first);
		SetParamData(cmd
			, iInst
			, viewProj
			, viewProjPrev
			, vVisibleMeshData[i]
		);
	}
}

static void ResizeDrawInstanceArrays(FInstancedMeshRenderData& cmd, size_t sz, size_t iCmd)
{
	//Log::Info("ResizeDrawParamInstanceArrays(cmds[%d], %d);", sz, iCmd);
	cmd.matWorld.resize(sz);
	cmd.matWorldViewProj.resize(sz);
	cmd.matWorldViewProjPrev.resize(sz);
	cmd.matNormal.resize(sz);
	cmd.objectID.resize(sz);
	cmd.projectedArea.resize(sz);
}
static void ResizeDrawInstanceArrays(FInstancedShadowMeshRenderData& cmd, size_t sz, size_t iCmd)
{
	//Log::Info("ResizeDrawParamInstanceArrays(cmds[%d], %d);", sz, iCmd);
	cmd.matWorld.resize(sz);
	cmd.matWorldViewProj.resize(sz);
}

template<class TMeshRenderCmd>
static void CountNResizeRenderCmdInstanceArrays(
	  const std::vector<FVisibleMeshData>& vCullResults
	, std::vector<TMeshRenderCmd>& meshRenderParams
	, int MAX_INSTANCE_COUNT
	, std::function<uint64(MaterialID, MeshID, int, bool)> FnGetKey
)
{
	SCOPED_CPU_MARKER("CountNResizeRenderCmdInstanceArrays");
	int iDraw = -1;
	int iInst = -1;
	uint64 keyPrev = -1;
	for (int i = 0; i < vCullResults.size(); ++i)
	{
		const uint64 key = FnGetKey(vCullResults[i].hMaterial, vCullResults[i].hMesh, vCullResults[i].SelectedLOD, vCullResults[i].bTessellated);
		if (keyPrev != key)
		{
			if (iDraw != -1)
			{
				ResizeDrawInstanceArrays(meshRenderParams[iDraw], iInst, iDraw);
			}
			keyPrev = key;
			++iDraw;
			iInst = 0;
		}
		if (iInst == MAX_INSTANCE_COUNT)
		{
			ResizeDrawInstanceArrays(meshRenderParams[iDraw], iInst, iDraw);
			++iDraw;
			iInst = 0;
		}
		++iInst; // TODO: check correctness for after iInst = 0 executions
	}
	if (iDraw != -1 && iInst != -1)
	{
		ResizeDrawInstanceArrays(meshRenderParams[iDraw], iInst, iDraw);
	}
}

static void CalculateInstanceDataWriteIndices(
	  const std::vector<FVisibleMeshData>& vViewCullResults
	, std::vector<FInstanceDataWriteParam>& vOutputWriteParams // {iDraw, iInst}
	, std::function<uint64(MaterialID, MeshID, int, bool)> FnGetKey
	, int MAX_INSTANCE_COUNT
)
{
	{
		SCOPED_CPU_MARKER("AllocInstanceParams");
		vOutputWriteParams.resize(vViewCullResults.size());
	}
	{
		SCOPED_CPU_MARKER("WriteOutputIndices");
		int iDraw = -1;
		int iInst = -1;
		uint64 keyPrev = -1;
		for (size_t i = 0; i < vViewCullResults.size(); ++i)
		{
			const int lod = vViewCullResults[i].SelectedLOD;
			const uint64 key = FnGetKey(vViewCullResults[i].hMaterial, vViewCullResults[i].hMesh, lod, vViewCullResults[i].bTessellated);
			if (keyPrev != key)
			{
				keyPrev = key;
				++iDraw;
				iInst = 0;
			}
			if (iInst == MAX_INSTANCE_COUNT)
			{
				++iDraw;
				iInst = 0;
			}

			// i,iBB --> key --> { iDraw, iInst }
			// Log::Info("[%-4d] %-6d --> %-13llu --> { %d , %d }", i, iBB, key, iDraw, iInst);

			vOutputWriteParams[i] = FInstanceDataWriteParam{ iDraw, iInst };
			++iInst;
		}
	}
}

template<class TMeshInstanceDataArray, class TMeshRenderCmd>
static void ReserveWorkerMemory(
	  const std::vector<FVisibleMeshData>& visibleMeshDataList
	, std::vector<TMeshRenderCmd>& meshRenderParams
	, std::unordered_map<uint64, TMeshInstanceDataArray>& drawParamLookup
	, std::function<uint64(MaterialID, MeshID, int, bool)> FnGetKey
	, std::vector<FInstanceDataWriteParam>& outWriteParams
	, int MAX_INSTANCE_COUNT
)
{
	SCOPED_CPU_MARKER("ReserveWorkerMemory");
	CountInstanceData(drawParamLookup, visibleMeshDataList, FnGetKey);
	CountNResizeRenderCmds(drawParamLookup, meshRenderParams, MAX_INSTANCE_COUNT);
	CountNResizeRenderCmdInstanceArrays(visibleMeshDataList, meshRenderParams, MAX_INSTANCE_COUNT, FnGetKey);
	AllocInstanceData<TMeshInstanceDataArray>(drawParamLookup);
	CalculateInstanceDataWriteIndices(visibleMeshDataList, outWriteParams, FnGetKey, MAX_INSTANCE_COUNT);
}


static void DispatchMainViewInstanceDataWorkers(
	  const FSceneView& SceneView
	, FSceneDrawData& SceneDrawData
	, const std::vector<FVisibleMeshData>& vMainViewCullResults
	, ThreadPool& RenderWorkerThreadPool
	, std::atomic<int>& MainViewThreadDone
	, std::vector<FInstanceDataWriteParam>& vOutputWriteParams // {iDraw, iInst}
)
{
	SCOPED_CPU_MARKER("DispatchWorker_MainView");
	const size_t NumWorkerThreads = RenderWorkerThreadPool.GetThreadPoolSize();
	const size_t NumThreads = NumWorkerThreads + 1;
	const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(vMainViewCullResults.size(), NumThreads);

	ReserveWorkerMemory(
		vMainViewCullResults,
		SceneDrawData.meshRenderParams,
		SceneDrawData.drawParamLookup,
		FSceneDrawData::GetKey,
		SceneDrawData.mRenderCmdInstanceDataWriteIndex,
		MAX_INSTANCE_COUNT__SCENE_MESHES
	);

	for (int iR = 0; !vRanges.empty() && iR < vRanges.size(); ++iR) // offload all tasks to workers
	{
		RenderWorkerThreadPool.AddTask([=, &SceneView, &SceneDrawData, &vMainViewCullResults, &MainViewThreadDone]()
		{
			SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
			CollectViewInstanceData(
				vMainViewCullResults,
				vRanges[iR].first,
				vRanges[iR].second,
				SceneView.viewProj,
				SceneView.viewProjPrev,
				vOutputWriteParams,
				SceneDrawData.meshRenderParams
			);
			MainViewThreadDone++;
		});
	}
}


static void DispatchWorkers_ShadowViews(
	  size_t NumShadowMeshFrustums
	, std::vector<FFrustumRenderCommandRecorderContext>& WorkerContexts
	, const bool bForceLOD0
	, ThreadPool& RenderWorkerThreadPool
	, std::vector<FFrustumRenderList>& mFrustumRenderLists
)
{
	SCOPED_CPU_MARKER("DispatchWorkers_ShadowViews");
	constexpr size_t NUM_MIN_SHADOW_MESHES_FOR_THREADING = 1; // TODO: tweak this when thread work count is divided equally instead of per frustum

	size_t NumShadowFrustumsWithNumMeshesLargerThanMinNumMeshesPerThread = 0;
	size_t NumShadowMeshes = 0;
	size_t NumShadowMeshes_Threaded = 0;
	{
		SCOPED_CPU_MARKER("PrepareShadowViewWorkerContexts");
		WorkerContexts.resize(NumShadowMeshFrustums);
		for (size_t iFrustum = 1; iFrustum <= NumShadowMeshFrustums; ++iFrustum) // iFrustum==0 is for mainView, start from 1
		{
			FShadowView* pShadowView = static_cast<FShadowView*>(mFrustumRenderLists[iFrustum].ViewRef.pViewData);
			size_t shadowIndex = iFrustum - 1; // Offset by 1 since index 0 is main view

			assert(pShadowView);
			FFrustumRenderList* FrustumRenderList = &mFrustumRenderLists[iFrustum];
			//const std::vector<FVisibleMeshData>* ViewCullResults = &(*mFrustumCullWorkerContext.pVisibleMeshListPerView)[iFrustum];
			WorkerContexts[iFrustum - 1] = { iFrustum, &FrustumRenderList->Data, pShadowView };

			FrustumRenderList->DataReadySignal.Wait();
			const size_t NumMeshes = FrustumRenderList->Data.size();
			NumShadowMeshes += NumMeshes;
			NumShadowFrustumsWithNumMeshesLargerThanMinNumMeshesPerThread += NumMeshes >= NUM_MIN_SHADOW_MESHES_FOR_THREADING ? 1 : 0;
			NumShadowMeshes_Threaded += NumMeshes >= NUM_MIN_SHADOW_MESHES_FOR_THREADING ? NumMeshes : 0;
		}
	}
	const size_t NumShadowMeshesRemaining = NumShadowMeshes - NumShadowMeshes_Threaded;
	const size_t NumWorkersForFrustumsBelowThreadingThreshold = DIV_AND_ROUND_UP(NumShadowMeshesRemaining, NUM_MIN_SHADOW_MESHES_FOR_THREADING);
	const size_t NumShadowFrustumBatchWorkers = NumShadowFrustumsWithNumMeshesLargerThanMinNumMeshesPerThread;
	const bool bUseWorkerThreadsForShadowViews = NumShadowFrustumBatchWorkers >= 1;
	const size_t NumShadowFrustumsThisThread = bUseWorkerThreadsForShadowViews 
		? std::max((size_t)0, NumShadowMeshFrustums - NumShadowFrustumBatchWorkers) 
		: NumShadowMeshFrustums;

	if (bUseWorkerThreadsForShadowViews)
	{
		for (size_t iFrustum = 1 + NumShadowFrustumsThisThread; iFrustum <= NumShadowMeshFrustums; ++iFrustum)
		{
			SCOPED_CPU_MARKER("Dispatch");
			RenderWorkerThreadPool.AddTask([=]() // dispatch workers
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				FFrustumRenderCommandRecorderContext ctx = WorkerContexts[iFrustum - 1]; // operate on a copy
				if (ctx.pCullResults->empty())
					return;

				ReserveWorkerMemory(
					*ctx.pCullResults,
					ctx.pShadowView->meshRenderParams,
					ctx.pShadowView->drawParamLookup,
					FShadowView::GetKey,
					ctx.pShadowView->mRenderCmdInstanceDataWriteIndex,
					MAX_INSTANCE_COUNT__SHADOW_MESHES
				);
				
				CollectViewInstanceData(
					*ctx.pCullResults,
					0,
					ctx.pCullResults->size()-1,
					ctx.pShadowView->matViewProj,
					ctx.pShadowView->matViewProj,
					ctx.pShadowView->mRenderCmdInstanceDataWriteIndex,
					ctx.pShadowView->meshRenderParams
				);
			});
		}
	}

	return;
}



static void BatchBoundingBoxRenderCommandData(
	std::vector<FInstancedBoundingBoxRenderData>& cmds
	, const std::vector<FBoundingBox>& BBs
	, const XMMATRIX viewProj
	, const XMFLOAT4 Color
	, size_t iBegin
	, BufferID VB
	, BufferID IB
)
{
	SCOPED_CPU_MARKER("BatchBoundingBoxRenderCommandData");
	int NumBBsToProcess = (int)BBs.size();
	size_t i = 0;
	int iBB = 0;
	while (NumBBsToProcess > 0)
	{
		FInstancedBoundingBoxRenderData& cmd = cmds[iBegin + i];
		cmd.matWorldViewProj.resize(std::min(MAX_INSTANCE_COUNT__UNLIT_SHADER, (size_t)NumBBsToProcess));
		cmd.vertexIndexBuffer = { VB, IB };
		cmd.numIndices = 36; // TODO: remove magic number
		cmd.color = Color;

		int iBatch = 0;
		while (iBatch < MAX_INSTANCE_COUNT__UNLIT_SHADER && iBB < BBs.size())
		{
			cmd.matWorldViewProj[iBatch] = BBs[iBB].GetWorldTransformMatrix() * viewProj;
			++iBatch;
			++iBB;
		}

		NumBBsToProcess -= iBatch;
		++i;
	}
}
static void BatchInstanceData_BoundingBox(FSceneDrawData& SceneDrawData
	, const FSceneView& SceneView
	, ThreadPool& UpdateWorkerThreadPool
	, const DirectX::XMMATRIX matViewProj)
{
	SCOPED_CPU_MARKER("BoundingBox");
	const bool bDrawGameObjectBBs = SceneView.sceneRenderOptions.bDrawGameObjectBoundingBoxes;
	const bool bDrawMeshBBs = SceneView.sceneRenderOptions.bDrawMeshBoundingBoxes;

	const float Transparency = 0.75f;
	const XMFLOAT4 BBColor_GameObj = XMFLOAT4(0.0f, 0.2f, 0.8f, Transparency);
	const XMFLOAT4 BBColor_Mesh = XMFLOAT4(0.0f, 0.8f, 0.2f, Transparency);

	auto fnBatch = [&UpdateWorkerThreadPool, &SceneView](
		std::vector<FInstancedBoundingBoxRenderData>& cmds
		, const std::vector<FBoundingBox>& BBs
		, size_t iBoundingBox
		, const XMFLOAT4 BBColor
		, const XMMATRIX matViewProj
		, const char* strMarker = ""
	)
	{
		SCOPED_CPU_MARKER(strMarker);

		constexpr size_t MIN_NUM_BOUNDING_BOX_FOR_THREADING = 128;
		if (BBs.size() < MIN_NUM_BOUNDING_BOX_FOR_THREADING || !UPDATE_THREAD__ENABLE_WORKERS)
		{
			BatchBoundingBoxRenderCommandData(cmds
				, BBs
				, matViewProj
				, BBColor
				, iBoundingBox
				, SceneView.cubeVB
				, SceneView.cubeIB
			);
		}
		else
		{
			SCOPED_CPU_MARKER("Dispatch");
			UpdateWorkerThreadPool.AddTask([=, &BBs, &cmds, &SceneView]()
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				BatchBoundingBoxRenderCommandData(cmds
					, BBs
					, matViewProj
					, BBColor
					, iBoundingBox
					, SceneView.cubeVB
					, SceneView.cubeIB
				);
			});
		}
	};


	{
		SCOPED_CPU_MARKER("AllocMem");
		SceneDrawData.boundingBoxRenderParams.resize(SceneView.NumGameObjectBBRenderCmds + SceneView.NumMeshBBRenderCmds);
	}
	// --------------------------------------------------------------
	// Game Object Bounding Boxes
	// --------------------------------------------------------------
	size_t iBoundingBox = 0;
	if (SceneView.sceneRenderOptions.bDrawGameObjectBoundingBoxes)
	{
#if RENDER_INSTANCED_BOUNDING_BOXES 
		fnBatch(SceneDrawData.boundingBoxRenderParams
			, *SceneView.pGameObjectBoundingBoxList
			, iBoundingBox
			, BBColor_GameObj
			, matViewProj
			, "GameObj"
		);
#else
	BatchBoundingBoxRenderCommandData(SceneView.boundingBoxRenderParams, SceneView, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, BBColor_GameObj, 0);
#endif
	}


	// --------------------------------------------------------------
	// Mesh Bounding Boxes
	// --------------------------------------------------------------
	if (SceneView.sceneRenderOptions.bDrawMeshBoundingBoxes)
	{
		iBoundingBox = SceneView.NumGameObjectBBRenderCmds;

#if RENDER_INSTANCED_BOUNDING_BOXES 
		fnBatch(SceneDrawData.boundingBoxRenderParams
			, *SceneView.pMeshBoundingBoxList
			, iBoundingBox
			, BBColor_Mesh
			, matViewProj
			, "Meshes"
		);
#else
		BatchBoundingBoxRenderCommandData(SceneView.boundingBoxRenderParams, SceneView, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, BBColor_GameObj, 0);
#endif
	}
}



void VQRenderer::BatchDrawCalls(ThreadPool& RenderWorkerThreadPool, FSceneView& SceneView)
{
	SCOPED_CPU_MARKER("BatchInstanceData");
	FSceneDrawData& DrawData = this->GetSceneDrawData(0);

	constexpr size_t NUM_MIN_SCENE_MESHES_FOR_THREADING = 128;
	const size_t NumWorkerThreads = RenderWorkerThreadPool.GetThreadPoolSize();

	assert(SceneView.FrustumRenderLists.size() >= 1);
	FFrustumRenderList& MainViewFrustumRenderList = SceneView.FrustumRenderLists[0];
	const std::vector<FVisibleMeshData>& MainViewRenderList = MainViewFrustumRenderList.Data;

	// ---------------------------------------------------SYNC ---------------------------------------------------
	{
		SCOPED_CPU_MARKER_C("WAIT_WORKER_CULL", 0xFFAA0000); // wait for frustum cull workers to finish
		MainViewFrustumRenderList.DataReadySignal.Wait();
	}
	// --------------------------------------------------- SYNC ---------------------------------------------------

	const size_t NumSceneViewMeshes = MainViewRenderList.size();
	const bool bUseWorkerThreadForMainView = NUM_MIN_SCENE_MESHES_FOR_THREADING <= NumSceneViewMeshes;
	const bool bForceLOD0_SceneView = SceneView.sceneRenderOptions.bForceLOD0_SceneView;
	const bool bForceLOD0_ShadowView = SceneView.sceneRenderOptions.bForceLOD0_ShadowView;

	std::vector< FFrustumRenderCommandRecorderContext> WorkerContexts;
	const size_t NumShadowMeshFrustums = SceneView.FrustumRenderLists.size() - 1; // exclude main view

	// main view
	std::atomic<int> MainViewThreadDone = 0;
	DispatchMainViewInstanceDataWorkers(
		SceneView,
		DrawData,
		MainViewRenderList,
		RenderWorkerThreadPool,
		MainViewThreadDone,
		DrawData.mRenderCmdInstanceDataWriteIndex
	);

	// collect isntance data on this thread
	{
		const size_t NumThreads = NumWorkerThreads + 1;
		auto vRanges = PartitionWorkItemsIntoRanges(MainViewRenderList.size(), NumThreads);
		if (!vRanges.empty())
		{
			const std::pair<size_t, size_t> vRange_ThisThread = vRanges.back();
			CollectViewInstanceData(
				MainViewRenderList
				, vRange_ThisThread.first
				, vRange_ThisThread.second
				, SceneView.viewProj
				, SceneView.viewProjPrev
				, DrawData.mRenderCmdInstanceDataWriteIndex
				, DrawData.meshRenderParams
			);
		}
	}

	// shadow views
	DispatchWorkers_ShadowViews(
		NumShadowMeshFrustums,
		WorkerContexts,
		bForceLOD0_ShadowView,
		RenderWorkerThreadPool,
		SceneView.FrustumRenderLists
	);


	// -------------------------------------------------------------------------------------------------------------------

	BatchInstanceData_BoundingBox(this->GetSceneDrawData(0), SceneView, RenderWorkerThreadPool, SceneView.viewProj);

	{
		SCOPED_CPU_MARKER_C("ClearLocalContext", 0xFF880000);
		WorkerContexts.clear();
	}

	RenderWorkerThreadPool.RunRemainingTasksOnThisThread();
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000);
		while (RenderWorkerThreadPool.GetNumActiveTasks() != 0);
	}
}