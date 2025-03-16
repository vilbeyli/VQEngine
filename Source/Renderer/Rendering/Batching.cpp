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
#include "Shaders/LightingConstantBufferData.h"

using namespace DirectX;

#define UPDATE_THREAD__ENABLE_WORKERS 1  // TODO: rename to render thread

static void SetParamData(MeshRenderData_t& cmd,
	int iInst,
	const XMMATRIX& viewProj,
	const XMMATRIX& viewProjPrev,
	const FVisibleMeshData& visibleMeshData
)
{
	XMMATRIX matWorld = visibleMeshData.Transform.matWorldTransformation();
	XMMATRIX matWorldHistory = visibleMeshData.Transform.matWorldTransformationPrev();
	XMMATRIX matNormal = visibleMeshData.Transform.NormalMatrix(matWorld);
	XMMATRIX matWVP = matWorld * viewProj;

	assert(iInst >= 0 && cmd.matWorld.size() > iInst);
	assert(cmd.matWorld.size() <= MAX_INSTANCE_COUNT__SCENE_MESHES);

	cmd.matWorldViewProjPrev[iInst] = std::move(matWorldHistory * viewProjPrev);
	cmd.matWorldViewProj[iInst] = std::move(matWVP);
	cmd.matNormal[iInst] = std::move(matNormal);
	cmd.matWorld[iInst] = std::move(matWorld);
	cmd.objectID[iInst] = (int)visibleMeshData.hGameObject + 1; // 0 means empty. offset by 1 now, and undo it when reading back
	cmd.projectedArea[iInst] = visibleMeshData.fBBArea;
	cmd.vertexIndexBuffer = visibleMeshData.VBIB;
	cmd.numIndices = visibleMeshData.NumIndices;
	cmd.matID = visibleMeshData.hMaterial;
	cmd.material = std::move(visibleMeshData.Material);

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
	const XMMATRIX& viewProjPrev, // unused, keeping here to match signature of SetParamData() for non-shadow meshes
	const FVisibleMeshData& visibleMeshData
)
{
	assert(iInst >= 0 && cmd.matWorldViewProj.size() > iInst);
	assert(iInst >= 0 && cmd.matWorld.size() > iInst);
	assert(cmd.matWorldViewProj.size() <= MAX_INSTANCE_COUNT__SHADOW_MESHES);
	assert(cmd.matWorld.size() <= MAX_INSTANCE_COUNT__SHADOW_MESHES);

	cmd.matWorld[iInst] = visibleMeshData.Transform.matWorldTransformation();
	cmd.material = visibleMeshData.Material;
	cmd.matID = visibleMeshData.hMaterial;
	cmd.numIndices = visibleMeshData.NumIndices;
	cmd.vertexIndexBuffer = visibleMeshData.VBIB;
	cmd.matWorldViewProj[iInst] = cmd.matWorld[iInst] * viewProj;
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


template <size_t FVisibleMeshData::*SortKey>
static size_t CountInstancedDrawCommands(
	const std::vector<FVisibleMeshData>& ViewVisibleMeshes,
	const size_t MAX_INSTANCES
)
{
	SCOPED_CPU_MARKER("CountInstancedDraws");
	if (ViewVisibleMeshes.empty())
		return 0;

	size_t numDrawCommands = 0;
	uint64 currentKey = ViewVisibleMeshes[0].*SortKey;
	size_t count = 1;
	for (size_t i = 1; i < ViewVisibleMeshes.size(); ++i)
	{
		const uint64 key = ViewVisibleMeshes[i].*SortKey;
		if (key != currentKey || count == MAX_INSTANCES)
		{
			numDrawCommands += 1;
			currentKey = key;
			count = 1;
		}
		else
		{
			++count;
		}
	}
	if (count > 0)
	{
		numDrawCommands += 1;
	}

	return numDrawCommands;
}

struct FDrawCallInputDataRange
{
	size_t iStart;
	size_t Stride; // NumElements
};
template <typename std::vector<size_t> FVisibleMeshDataSoA::* SortKeyArray>
static std::vector<FDrawCallInputDataRange> CalcInstancedDrawCommandDataRangesSoA(
	const FVisibleMeshDataSoA& ViewVisibleMeshes,
	const size_t MAX_INSTANCES
)
{
	SCOPED_CPU_MARKER("CalcInstancedDrawDataRangesSoA");
	const size_t NumElements = (ViewVisibleMeshes.*SortKeyArray).size();
	if (NumElements == 0)
		return std::vector<FDrawCallInputDataRange>();

	std::vector<FDrawCallInputDataRange> drawCalls;
	drawCalls.reserve(NumElements);
	uint64 currentKey = (ViewVisibleMeshes.*SortKeyArray)[0];
	size_t count = 1;
	size_t iStart = 0;
	for (size_t i = 1; i < NumElements; ++i)
	{
		const uint64 key = (ViewVisibleMeshes.*SortKeyArray)[i];
		if (key != currentKey || (count-1) == MAX_INSTANCES-1)
		{
			drawCalls.push_back({ .iStart = iStart, .Stride = count });
			currentKey = key;
			count = 0;
			iStart = i;
		}
		++count;
	}
	drawCalls.push_back({ .iStart = iStart, .Stride = count });

	return drawCalls;
}

template <typename std::vector<size_t> FVisibleMeshDataSoA::*SortKeyArray>
static size_t CountInstancedDrawCommandsSoA(
	const FVisibleMeshDataSoA& ViewVisibleMeshes,
	const size_t MAX_INSTANCES
)
{
	SCOPED_CPU_MARKER("CountInstancedDraws");
	const size_t NumElements = (ViewVisibleMeshes.*SortKeyArray).size();
	if (NumElements == 0)
		return 0;

	size_t numDrawCommands = 0;
	uint64 currentKey = (ViewVisibleMeshes.*SortKeyArray)[0];
	size_t count = 1;
	for (size_t i = 1; i < NumElements; ++i)
	{
		const uint64 key = (ViewVisibleMeshes.*SortKeyArray)[i];
		if (key != currentKey || count == MAX_INSTANCES)
		{
			numDrawCommands += 1;
			currentKey = key;
			count = 1;
		}
		else
		{
			++count;
		}
	}
	if (count > 0)
	{
		numDrawCommands += 1;
	}

	return numDrawCommands;
}


template<typename RenderDataT, typename ContainerT, size_t MAX_INSTANCES, size_t FVisibleMeshData::*SortKey>
static void BatchViewDrawCalls(
	ContainerT& renderParamsContainer,
	const std::vector<FVisibleMeshData>& ViewVisibleMeshes,
	const XMMATRIX& viewProj,
	const XMMATRIX& viewProjPrev // used for main view
)
{
	SCOPED_CPU_MARKER("BatchViewDrawCalls");

	if (ViewVisibleMeshes.empty()) 
	{
		return;
	}

	size_t numDrawCommands = CountInstancedDrawCommands<SortKey>(ViewVisibleMeshes, MAX_INSTANCES);

	{
		SCOPED_CPU_MARKER("ResizeDrawData");
		renderParamsContainer.resize(numDrawCommands);
	}
	{
		SCOPED_CPU_MARKER("ResizeInstanceData");
		uint64 currentKey = ViewVisibleMeshes[0].*SortKey;
		size_t numInstances = 1;
		size_t iDraw = 0;
		for (size_t iMesh = 1; iMesh < ViewVisibleMeshes.size(); ++iMesh) 
		{
			const FVisibleMeshData& meshData = ViewVisibleMeshes[iMesh];
			const uint64 key = meshData.*SortKey;
			if (key != currentKey || numInstances == MAX_INSTANCES)
			{
				assert(iDraw < renderParamsContainer.size());
				ResizeDrawInstanceArrays(renderParamsContainer[iDraw], numInstances, iDraw);
				++iDraw;
				numInstances = 1;
				currentKey = key;
			}
			else 
			{
				++numInstances;
			}
		}
		if (numInstances > 0) 
		{
			assert(iDraw < renderParamsContainer.size());
			ResizeDrawInstanceArrays(renderParamsContainer[iDraw], numInstances, iDraw);
		}
	}
	{
		SCOPED_CPU_MARKER("SetDrawData");
		int iInstance = 0;
		int iDraw = 0;
		size_t iMesh = 0;
		uint64 currentKey = ViewVisibleMeshes[iMesh].*SortKey;

		assert(iDraw < renderParamsContainer.size());
		SetParamData(renderParamsContainer[iDraw], iInstance, viewProj, viewProjPrev, ViewVisibleMeshes[iMesh]);
		++iInstance;

		for (iMesh = 1; iMesh < ViewVisibleMeshes.size(); ++iMesh) 
		{
			const FVisibleMeshData& meshData = ViewVisibleMeshes[iMesh];
			const uint64 key = meshData.*SortKey;

			if (iInstance >= MAX_INSTANCES || key != currentKey) 
			{
				++iDraw;
				iInstance = 0;
				currentKey = key;
			}

			assert(iDraw < renderParamsContainer.size());
			SetParamData(renderParamsContainer[iDraw], iInstance, viewProj, viewProjPrev, meshData);
			++iInstance;
		}
	}
}

static void BatchMainViewDrawCalls(
	FSceneDrawData& SceneDrawData,
	const FVisibleMeshDataSoA& ViewVisibleMeshes,
	const XMMATRIX viewProj,     // take in copy for less cache thrashing
	const XMMATRIX viewProjPrev, // take in copy for less cache thrashing
	DynamicBufferHeap& CBHeap,
	VQRenderer* pRenderer,
	const FGraphicsSettings& GFXSettings,
	const FPostProcessParameters& PPParams
)
{
	SCOPED_CPU_MARKER_C("BatchMainViewDrawCalls", 0xFF00AA00);
	
	using namespace VQ_SHADER_DATA;

	const std::vector<FDrawCallInputDataRange> drawCallRanges = CalcInstancedDrawCommandDataRangesSoA<&FVisibleMeshDataSoA::SceneSortKey>(ViewVisibleMeshes, MAX_INSTANCE_COUNT__SCENE_MESHES);
	//const size_t NumInstancedDrawCalls2 = CountInstancedDrawCommandsSoA<&FVisibleMeshDataSoA::SceneSortKey>(ViewVisibleMeshes, MAX_INSTANCE_COUNT__SCENE_MESHES);
	const size_t NumInstancedDrawCalls = drawCallRanges.size();
	if (NumInstancedDrawCalls == 0)
		return;

	std::vector<D3D12_GPU_VIRTUAL_ADDRESS> cbAddr(NumInstancedDrawCalls);
	std::vector<PerObjectData*> pPerObj(NumInstancedDrawCalls);
	for (size_t i = 0; i < NumInstancedDrawCalls; ++i)
	{
		CBHeap.AllocConstantBuffer(sizeof(PerObjectData), (void**)(&pPerObj[i]), &cbAddr[i]);
	}
	{
		SCOPED_CPU_MARKER("ResizeDrawParams");
		SceneDrawData.mainViewDrawParams.resize(NumInstancedDrawCalls);
	}
	{
		SCOPED_CPU_MARKER("SetDrawData");

		// render settings
		const bool bRenderMotionVectors = VQRenderer::ShouldUseMotionVectorsTarget(GFXSettings);
		const bool bUseVisualizationRenderTarget = VQRenderer::ShouldUseVisualizationTarget(PPParams);

		// PSO_ID query indices
		const size_t iOutMoVec = bRenderMotionVectors ? 1 : 0;
		const size_t iOutRough = bUseVisualizationRenderTarget ? 1 : 0;
		const size_t iMSAA = GFXSettings.bAntiAliasing ? 1 : 0;
		{
			SCOPED_CPU_MARKER("matWorldViewProj");
			size_t iDraw = 0;
			for (const FDrawCallInputDataRange& r : drawCallRanges)
			{
				assert(r.Stride <= MAX_INSTANCE_COUNT__SCENE_MESHES);
				size_t iInstance = 0;
				for (size_t i = r.iStart; i < r.iStart + r.Stride; ++i)
				{
					const Transform& tf = ViewVisibleMeshes.Transform[i];
					pPerObj[iDraw]->matWorldViewProj[iInstance++] = tf.matWorldTransformation() * viewProj;
				}
				++iDraw;
			}
		}
		{
			SCOPED_CPU_MARKER("matWorld");
			size_t iDraw = 0;
			for (const FDrawCallInputDataRange& r : drawCallRanges)
			{
				size_t iInstance = 0;
				for (size_t i = r.iStart; i < r.iStart + r.Stride; ++i)
				{
					const Transform& tf = ViewVisibleMeshes.Transform[i];
					pPerObj[iDraw]->matWorld[iInstance++] = tf.matWorldTransformation();
				}
				++iDraw;
			}
		}
		{
			SCOPED_CPU_MARKER("matWorldViewProjPrev");
			size_t iDraw = 0;
			for (const FDrawCallInputDataRange& r : drawCallRanges)
			{
				size_t iInstance = 0;
				for (size_t i = r.iStart; i < r.iStart + r.Stride; ++i)
				{
					const Transform& tf = ViewVisibleMeshes.Transform[i];
					pPerObj[iDraw]->matWorldViewProjPrev[iInstance++] = tf.matWorldTransformationPrev() * viewProjPrev;
				}
				++iDraw;
			}
		}
		{
			SCOPED_CPU_MARKER("matNormal");
			size_t iDraw = 0;
			for (const FDrawCallInputDataRange& r : drawCallRanges)
			{
				size_t iInstance = 0;
				for (size_t i = r.iStart; i < r.iStart + r.Stride; ++i)
				{
					const Transform& tf = ViewVisibleMeshes.Transform[i];
					pPerObj[iDraw]->matNormal[iInstance++] = tf.RotationMatrix();
				}
				++iDraw;
			}
		}
		{
			SCOPED_CPU_MARKER("objID");
			size_t iDraw = 0;
			for (const FDrawCallInputDataRange& r : drawCallRanges)
			{
				size_t iInstance = 0;
				for (size_t iMesh = r.iStart; iMesh < r.iStart + r.Stride; ++iMesh)
				{
					pPerObj[iDraw]->ObjID[iInstance].x = (int)ViewVisibleMeshes.PerInstanceData[iMesh].hGameObject;
					pPerObj[iDraw]->ObjID[iInstance].y = -222; //debug val
					pPerObj[iDraw]->ObjID[iInstance].z = -333; //debug val
					pPerObj[iDraw]->ObjID[iInstance].w = (int)(ViewVisibleMeshes.PerInstanceData[iMesh].fBBArea * 10000); // float value --> int render target
					++iInstance;
				}
				++iDraw;
			}
		}
		{
			SCOPED_CPU_MARKER("PerDraw");
			size_t iDraw = 0;
			for (const FDrawCallInputDataRange& r : drawCallRanges)
			{
				const size_t iMesh = r.iStart;
				const FPerDrawData& drawData = ViewVisibleMeshes.PerDrawData[iMesh];
				FInstancedDrawParameters& draw = SceneDrawData.mainViewDrawParams[iDraw];

				pPerObj[iDraw]->materialID = drawData.hMaterial;
				pPerObj[iDraw]->meshID = drawData.hMesh;
				draw.VB = drawData.VBIB.first;
				draw.IB = drawData.VBIB.second;
				draw.numIndices = drawData.NumIndices;
				draw.numInstances = r.Stride;

				++iDraw;
			}
		}
		{
			SCOPED_CPU_MARKER("Material");
			size_t iDraw = 0;
			for (const FDrawCallInputDataRange& r : drawCallRanges)
			{
				const size_t iMesh = r.iStart;
				FInstancedDrawParameters& draw = SceneDrawData.mainViewDrawParams[iDraw];

				draw.IATopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

				const Material& mat = ViewVisibleMeshes.Material[iMesh];
				mat.GetCBufferData(pPerObj[iDraw]->materialData);
				draw.SRVMaterialMaps = mat.SRVMaterialMaps;
				draw.SRVHeightMap = mat.SRVHeightMap;

				const bool bWireframe = mat.bWireframe;
				draw.cbAddr = cbAddr[iDraw];

				if (mat.IsTessellationEnabled())
				{
					draw.IATopology = mat.GetTessellationDomain() == ETessellationDomain::QUAD_PATCH
						? D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST
						: D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;

					uint8 iTess = 0;
					uint8 iDomain = 0;
					uint8 iPart = 0;
					uint8 iOutTopo = 0;
					uint8 iTessCull = 0;
					mat.GetTessellationPSOConfig(iTess, iDomain, iPart, iOutTopo, iTessCull);
					assert(iTess == 1);

					draw.PackTessellationConfig(iTess, (ETessellationDomain)iDomain, (ETessellationPartitioning)iPart, (ETessellationOutputTopology)iOutTopo, iTessCull);

					TessellationParams* pTessParams = nullptr;
					CBHeap.AllocConstantBuffer(sizeof(TessellationParams), (void**)(&pTessParams), &draw.cbAddr_Tessellation);
					*pTessParams = mat.GetTessellationCBufferData();
				}
				else
				{
					draw.cbAddr_Tessellation = 0;
					draw.PackedTessellationConfig = 0;
				}

				const uint8 iAlpha = mat.IsAlphaMasked(*pRenderer) ? 1 : 0;
				const uint8 iRaster = mat.bWireframe ? 1 : 0;
				const uint8 iFaceCull = 2; // 2:back
				draw.PackMaterialConfig(iAlpha, bWireframe, iFaceCull);
				
				++iDraw;
			}
		}
	}

}

static void BatchShadowViewDrawCalls(
	FShadowView* pShadowView,
	const std::vector<FVisibleMeshData>& ViewVisibleMeshes
)
{
	SCOPED_CPU_MARKER("BatchShadowViewDrawCalls");
	BatchViewDrawCalls<FInstancedShadowMeshRenderData, std::vector<FInstancedShadowMeshRenderData>&, MAX_INSTANCE_COUNT__SHADOW_MESHES, &FVisibleMeshData::ShadowSortKey>(
		pShadowView->meshRenderParams,
		ViewVisibleMeshes,
		pShadowView->matViewProj,
		pShadowView->matViewProj // No viewProjPrev for shadow
	);
}

static void DispatchWorkers_ShadowViews(
	  size_t NumShadowMeshFrustums
	, ThreadPool& RenderWorkerThreadPool
	, const std::vector<FFrustumRenderList>& mFrustumRenderLists
)
{
	SCOPED_CPU_MARKER("DispatchWorkers_ShadowViews");
	constexpr size_t NUM_MIN_SHADOW_MESHES_FOR_THREADING = 1; // TODO: tweak this when thread work count is divided equally instead of per frustum

	std::vector< FFrustumRenderCommandRecorderContext> WorkerContexts;

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
			const FFrustumRenderList* FrustumRenderList = &mFrustumRenderLists[iFrustum];
			//const std::vector<FVisibleMeshData>* ViewvisibleMeshDatas = &(*mFrustumCullWorkerContext.pVisibleMeshListPerView)[iFrustum];
			WorkerContexts[iFrustum - 1] = { iFrustum, FrustumRenderList, pShadowView };

			const size_t NumMeshes = FrustumRenderList->DataCountReadySignal.Wait(); // sync

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
		constexpr size_t NUM_NON_SHADOW_FRUSTUMS = 1;
		for (size_t iFrustum = NUM_NON_SHADOW_FRUSTUMS + NumShadowFrustumsThisThread; iFrustum <= NumShadowMeshFrustums; ++iFrustum)
		{
			SCOPED_CPU_MARKER("Dispatch");
			RenderWorkerThreadPool.AddTask([=]() // dispatch workers
			{
				RENDER_WORKER_CPU_MARKER;
				const size_t iContext = iFrustum - NUM_NON_SHADOW_FRUSTUMS;
				FFrustumRenderCommandRecorderContext ctx = WorkerContexts[iFrustum - NUM_NON_SHADOW_FRUSTUMS]; // copy so we dont have to worry about freed memory since contexts are within the scope of this function
				assert(ctx.pFrustumRenderList);
				
				ctx.pFrustumRenderList->DataReadySignal.Wait(); // sync
				if (ctx.pFrustumRenderList->Data.empty())
					return;

				BatchShadowViewDrawCalls(ctx.pShadowView, ctx.pFrustumRenderList->Data);
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
				RENDER_WORKER_CPU_MARKER;
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



void VQRenderer::BatchDrawCalls(
	ThreadPool& RenderWorkerThreadPool,
	const FSceneView& SceneView,
	const FSceneShadowViews& SceneShadowView,
	FWindowRenderContext& ctx, 
	const FPostProcessParameters& PPParams,
	const FGraphicsSettings& GFXSettings
)
{
	SCOPED_CPU_MARKER("BatchInstanceData");

	FSceneDrawData& DrawData = this->GetSceneDrawData(0);

	constexpr size_t NUM_MIN_SCENE_MESHES_FOR_THREADING = 128;
	const size_t NumWorkerThreads = RenderWorkerThreadPool.GetThreadPoolSize();

	assert(SceneView.FrustumRenderLists.size() >= 1);
	const FFrustumRenderList& MainViewFrustumRenderList = SceneView.FrustumRenderLists[0];
	const std::vector<FVisibleMeshData>& MainViewRenderList = MainViewFrustumRenderList.Data;
	const FVisibleMeshDataSoA& MainViewRenderList2 = MainViewFrustumRenderList.Data2;

	// ---------------------------------------------------SYNC ---------------------------------------------------
	{
		SCOPED_CPU_MARKER_C("WAIT_WORKER_CULL", 0xFFAA0000); // wait for main view frustum cull worker to finish
		MainViewFrustumRenderList.DataReadySignal.Wait();
	}
	// --------------------------------------------------- SYNC ---------------------------------------------------

	const size_t NumSceneViewMeshes = MainViewRenderList2.Size();
	const bool bUseWorkerThreadForMainView = NUM_MIN_SCENE_MESHES_FOR_THREADING <= NumSceneViewMeshes;

	const size_t NumShadowMeshFrustums = SceneShadowView.NumSpotShadowViews 
		+ SceneShadowView.NumPointShadowViews * 6
		+ (SceneShadowView.NumDirectionalViews
	);

	constexpr size_t iCmdZPrePassThread = 0;
	constexpr size_t iCmdObjIDPassThread = iCmdZPrePassThread + 1;
	constexpr size_t iCmdPointLightsThread = iCmdObjIDPassThread + 1;
	const     size_t iCmdSpots = iCmdPointLightsThread + SceneShadowView.NumPointShadowViews;
	const     size_t iCmdDirectional = iCmdSpots + (SceneShadowView.NumSpotShadowViews > 0 ? 1 : 0);
	const     size_t iCmdRenderThread = iCmdDirectional + (SceneShadowView.NumDirectionalViews);

	ID3D12GraphicsCommandList* pCmd_ThisThread = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, iCmdRenderThread);
	DynamicBufferHeap& CBHeap = ctx.GetConstantBufferHeap(iCmdRenderThread);

	RenderWorkerThreadPool.AddTask([&]() 
	{
		RENDER_WORKER_CPU_MARKER;
		BatchMainViewDrawCalls(DrawData, MainViewFrustumRenderList.Data2, SceneView.viewProj, SceneView.viewProjPrev, CBHeap, this, GFXSettings, PPParams);
	});

	DispatchWorkers_ShadowViews(
		NumShadowMeshFrustums,
		RenderWorkerThreadPool,
		SceneView.FrustumRenderLists
	);


	// -------------------------------------------------------------------------------------------------------------------

	BatchInstanceData_BoundingBox(this->GetSceneDrawData(0), SceneView, RenderWorkerThreadPool, SceneView.viewProj);

	RenderWorkerThreadPool.RunRemainingTasksOnThisThread();
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000);
		while (RenderWorkerThreadPool.GetNumActiveTasks() != 0);
	}

}