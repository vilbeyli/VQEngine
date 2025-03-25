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
using namespace VQ_SHADER_DATA;

#define UPDATE_THREAD__ENABLE_WORKERS 1  // TODO: rename to render thread


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
	//const size_t NumElements = (ViewVisibleMeshes.*SortKeyArray).size();
	const size_t NumElements = ViewVisibleMeshes.NumValidElements;
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



static void BatchShadowViewDrawCalls(
	std::vector<FInstancedDrawParameters>& drawParams,
	const FVisibleMeshDataSoA& ViewVisibleMeshes,
	const XMMATRIX viewProj,     // take in copy for less cache thrashing
	const XMMATRIX viewProjPrev, // take in copy for less cache thrashing
	DynamicBufferHeap& CBHeap,
	const VQRenderer* pRenderer
)
{
	SCOPED_CPU_MARKER_C("BatchShadowViewDrawCalls", 0xFF005500);

	const std::vector<FDrawCallInputDataRange> drawCallRanges = CalcInstancedDrawCommandDataRangesSoA<&FVisibleMeshDataSoA::ShadowSortKey>(ViewVisibleMeshes, MAX_INSTANCE_COUNT__SHADOW_MESHES);
	const size_t NumInstancedDrawCalls = drawCallRanges.size();
	if (NumInstancedDrawCalls == 0)
		return;

	std::vector<D3D12_GPU_VIRTUAL_ADDRESS> cbAddr(NumInstancedDrawCalls);
	std::vector<PerObjectShadowData*> pPerObj(NumInstancedDrawCalls);
	for (size_t i = 0; i < NumInstancedDrawCalls; ++i)
	{
		CBHeap.AllocConstantBuffer_MT(sizeof(PerObjectShadowData), (void**)(&pPerObj[i]), &cbAddr[i]);
	}
	{
		SCOPED_CPU_MARKER("ResizeDrawParams");
		drawParams.resize(NumInstancedDrawCalls);
	}
	{
		SCOPED_CPU_MARKER("SetDrawData");
		{
			SCOPED_CPU_MARKER("matWorldViewProj");
			size_t iDraw = 0;
			for (const FDrawCallInputDataRange& r : drawCallRanges)
			{
				assert(r.Stride <= MAX_INSTANCE_COUNT__SHADOW_MESHES);
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
			SCOPED_CPU_MARKER("PerDraw");
			size_t iDraw = 0;
			for (const FDrawCallInputDataRange& r : drawCallRanges)
			{
				const size_t iMesh = r.iStart;
				const FPerDrawData& drawData = ViewVisibleMeshes.PerDrawData[iMesh];
				FInstancedDrawParameters& draw = drawParams[iDraw];

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
				FInstancedDrawParameters& draw = drawParams[iDraw];

				draw.IATopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

				const Material& mat = ViewVisibleMeshes.Material[iMesh];
				pPerObj[iDraw]->texScaleBias = float4(mat.tiling.x, mat.tiling.y, mat.uv_bias.x, mat.uv_bias.y);
				pPerObj[iDraw]->displacement = mat.displacement;
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
					CBHeap.AllocConstantBuffer_MT(sizeof(TessellationParams), (void**)(&pTessParams), &draw.cbAddr_Tessellation);
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

static void BatchMainViewDrawCalls(
	std::vector<FInstancedDrawParameters>& drawParams,
	const FVisibleMeshDataSoA& ViewVisibleMeshes,
	const XMMATRIX viewProj,     // take in copy for less cache thrashing
	const XMMATRIX viewProjPrev, // take in copy for less cache thrashing
	DynamicBufferHeap& CBHeap,
	const VQRenderer* pRenderer
)
{
	SCOPED_CPU_MARKER_C("BatchMainViewDrawCalls", 0xFF00AA00);

	const std::vector<FDrawCallInputDataRange> drawCallRanges = CalcInstancedDrawCommandDataRangesSoA<&FVisibleMeshDataSoA::SceneSortKey>(ViewVisibleMeshes, MAX_INSTANCE_COUNT__SCENE_MESHES);
	const size_t NumInstancedDrawCalls = drawCallRanges.size();
	if (NumInstancedDrawCalls == 0)
		return;

	std::vector<D3D12_GPU_VIRTUAL_ADDRESS> cbAddr(NumInstancedDrawCalls);
	std::vector<PerObjectLightingData*> pPerObj(NumInstancedDrawCalls);
	for (size_t i = 0; i < NumInstancedDrawCalls; ++i)
	{
		CBHeap.AllocConstantBuffer(sizeof(PerObjectLightingData), (void**)(&pPerObj[i]), &cbAddr[i]);
	}
	{
		SCOPED_CPU_MARKER("ResizeDrawParams");
		drawParams.resize(NumInstancedDrawCalls);
	}
	{
		SCOPED_CPU_MARKER("SetDrawData");
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
				FInstancedDrawParameters& draw = drawParams[iDraw];

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
				FInstancedDrawParameters& draw = drawParams[iDraw];

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

static DynamicBufferHeap& GetThreadConstantBufferHeap(
	FFrustumRenderList::EFrustumType FrustumType,
	uint FrustumIndex,
	FWindowRenderContext& ctx,
	const FSceneShadowViews& SceneShadowView
)
{
	constexpr size_t iCmdZPrePassThread = 0;
	constexpr size_t iCmdObjIDPassThread = iCmdZPrePassThread + 1;
	constexpr size_t iCmdPointLightsThread = iCmdObjIDPassThread + 1;
	const     size_t iCmdSpots = iCmdPointLightsThread + SceneShadowView.NumPointShadowViews;
	const     size_t iCmdDirectional = iCmdSpots + (SceneShadowView.NumSpotShadowViews > 0 ? 1 : 0);
	const     size_t iCmdRenderThread = iCmdDirectional + SceneShadowView.NumDirectionalViews;

	switch (FrustumType)
	{
	case FFrustumRenderList::EFrustumType::MainView          : return ctx.GetConstantBufferHeap(iCmdRenderThread);
	case FFrustumRenderList::EFrustumType::SpotShadow        : return ctx.GetConstantBufferHeap(iCmdSpots);
	case FFrustumRenderList::EFrustumType::PointShadow       : return ctx.GetConstantBufferHeap(iCmdPointLightsThread + FrustumIndex/6);
	case FFrustumRenderList::EFrustumType::DirectionalShadow : return ctx.GetConstantBufferHeap(iCmdDirectional);
	}
	return ctx.GetConstantBufferHeap(iCmdRenderThread);
}

static void DispatchWorkers_ShadowViews(FWindowRenderContext& ctx,
	const FSceneShadowViews& SceneShadowView, 
	ThreadPool& RenderWorkerThreadPool, 
	const std::vector<FFrustumRenderList>& mFrustumRenderLists, 
	VQRenderer* pRenderer
)
{
	SCOPED_CPU_MARKER("DispatchWorkers_ShadowViews");
	constexpr size_t NUM_MIN_SHADOW_MESHES_FOR_THREADING = 1; // TODO: tweak this when thread work count is divided equally instead of per frustum

	const size_t NumShadowMeshFrustums = SceneShadowView.NumSpotShadowViews 
		+ SceneShadowView.NumPointShadowViews * 6
		+ (SceneShadowView.NumDirectionalViews
	);

	FSceneDrawData& DrawData = pRenderer->GetSceneDrawData(0);
	{
		SCOPED_CPU_MARKER("ResizeShadowDrawDataContainers");
		DrawData.pointShadowDrawParams.resize(SceneShadowView.NumPointShadowViews*6);
		DrawData.spotShadowDrawParams.resize(SceneShadowView.NumSpotShadowViews);
	}

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
			const FFrustumRenderList* pFrustumRenderList = &mFrustumRenderLists[iFrustum];
			//const std::vector<FVisibleMeshData>* ViewvisibleMeshDatas = &(*mFrustumCullWorkerContext.pVisibleMeshListPerView)[iFrustum];
			WorkerContexts[iFrustum - 1] = { /*iFrustum,*/ pFrustumRenderList, pShadowView };

			// -------------------------------------------------- SYNC ---------------------------------------------------
			//Log::Info("WaitDataCountReady[%d]", iFrustum);
			const size_t NumMeshes = pFrustumRenderList->DataCountReadySignal.Wait();
			// -------------------------------------------------- SYNC ---------------------------------------------------

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
		constexpr size_t NUM_NON_SHADOW_FRUSTUMS = 1; // TODO: dont assume a single main/scene view
		const size_t iFrustumBegin = NUM_NON_SHADOW_FRUSTUMS + NumShadowFrustumsThisThread;
		const size_t iFrustumEnd = iFrustumBegin + NumShadowMeshFrustums;
		
		for (size_t iFrustum = NUM_NON_SHADOW_FRUSTUMS + NumShadowFrustumsThisThread; iFrustum <= NumShadowMeshFrustums; ++iFrustum)
		{
			SCOPED_CPU_MARKER("Dispatch");
			const FFrustumRenderList* pFrustumRenderList = &mFrustumRenderLists[iFrustum];
			assert(pFrustumRenderList);
			if (pFrustumRenderList->Data.Size() == 0)
			{
				//Log::Info("Empty pFrustumRenderList %d", iFrustum);
				continue;
			}

			const size_t iContext = iFrustum - NUM_NON_SHADOW_FRUSTUMS;
			FFrustumRenderCommandRecorderContext wctx = WorkerContexts[iContext]; // copy so we dont have to worry about freed memory since contexts are within the scope of this function
			RenderWorkerThreadPool.AddTask([&, wctx, iFrustum, pRenderer]() // dispatch workers
			{
				RENDER_WORKER_CPU_MARKER;
				assert(wctx.pFrustumRenderList);
				const FFrustumRenderList* pFrustumRenderList = wctx.pFrustumRenderList;

				DynamicBufferHeap& CBHeap = GetThreadConstantBufferHeap(
					pFrustumRenderList->Type,
					pFrustumRenderList->TypeIndex,
					ctx,
					SceneShadowView
				);

				std::vector<FInstancedDrawParameters>* pDrawParams = nullptr;
				switch (pFrustumRenderList->Type)
				{
				case FFrustumRenderList::EFrustumType::DirectionalShadow: pDrawParams = &DrawData.directionalShadowDrawParams; break;
				case FFrustumRenderList::EFrustumType::SpotShadow       : pDrawParams = &DrawData.spotShadowDrawParams[pFrustumRenderList->TypeIndex]; break;
				case FFrustumRenderList::EFrustumType::PointShadow      : pDrawParams = &DrawData.pointShadowDrawParams[pFrustumRenderList->TypeIndex]; break;
				}
				assert(pDrawParams);

				// -------------------------------------------------- SYNC ---------------------------------------------------
				pFrustumRenderList->DataReadySignal.Wait(); 
				// -------------------------------------------------- SYNC ---------------------------------------------------
				//const std::vector<FDrawCallInputDataRange> drawCallRanges = CalcInstancedDrawCommandDataRangesSoA<&FVisibleMeshDataSoA::ShadowSortKey>(
				//	wctx.pFrustumRenderList->Data, MAX_INSTANCE_COUNT__SHADOW_MESHES);
				//const size_t NumInstancedDrawCalls = drawCallRanges.size();
				//Log::Info("Frustum[%d] : %d", iFrustum, NumInstancedDrawCalls);

				BatchShadowViewDrawCalls(
					*pDrawParams,
					wctx.pFrustumRenderList->Data,
					wctx.pShadowView->matViewProj,
					wctx.pShadowView->matViewProj,
					CBHeap,
					pRenderer
				);
				wctx.pFrustumRenderList->BatchDoneSignal.Notify();
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

	// ---------------------------------------------------SYNC ---------------------------------------------------
	const size_t NumSceneViewMeshes = MainViewFrustumRenderList.DataCountReadySignal.Wait();
	// -------------------------------------------------- SYNC ---------------------------------------------------

	const bool bUseWorkerThreadForMainView = NUM_MIN_SCENE_MESHES_FOR_THREADING <= NumSceneViewMeshes;
	
	RenderWorkerThreadPool.AddTask([&]() 
	{
		RENDER_WORKER_CPU_MARKER;
		DynamicBufferHeap& CBHeap = GetThreadConstantBufferHeap(MainViewFrustumRenderList.Type, MainViewFrustumRenderList.TypeIndex, ctx, SceneShadowView);

		// ---------------------------------------------------SYNC ---------------------------------------------------
		MainViewFrustumRenderList.DataReadySignal.Wait();
		// -------------------------------------------------- SYNC ---------------------------------------------------

		BatchMainViewDrawCalls(
			DrawData.mainViewDrawParams,
			MainViewFrustumRenderList.Data,
			SceneView.viewProj,
			SceneView.viewProjPrev,
			CBHeap, 
			this
		);
		MainViewFrustumRenderList.BatchDoneSignal.Notify();
	});

	DispatchWorkers_ShadowViews(
		ctx,
		SceneShadowView,
		RenderWorkerThreadPool,
		SceneView.FrustumRenderLists,
		this
	);

	BatchInstanceData_BoundingBox(DrawData, SceneView, RenderWorkerThreadPool, SceneView.viewProj);

	RenderWorkerThreadPool.RunRemainingTasksOnThisThread();
}