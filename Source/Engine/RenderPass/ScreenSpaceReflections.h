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
#pragma once

#include "RenderPass.h"

#include <unordered_map>
#include <DirectXMath.h>

class DynamicBufferHeap;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandSignature;

class ScreenSpaceReflectionsPass : public RenderPassBase
{
public:
	struct FResourceParameters : public IRenderPassResourceCollection
	{
		DXGI_FORMAT NormalBufferFormat = DXGI_FORMAT_UNKNOWN;
		TextureID TexSceneColorRoughness = INVALID_ID;
		TextureID TexHierarchicalDepthBuffer = INVALID_ID;
		TextureID TexNormals = INVALID_ID;
		TextureID TexSceneColor = INVALID_ID;
		TextureID TexMotionVectors = INVALID_ID;
	};

	struct FFX_SSSRConstants
	{
		DirectX::XMMATRIX /*XMFLOAT4X4*/ invViewProjection;
		DirectX::XMMATRIX /*XMFLOAT4X4*/ projection;
		DirectX::XMMATRIX /*XMFLOAT4X4*/ invProjection;
		DirectX::XMMATRIX /*XMFLOAT4X4*/ view;
		DirectX::XMMATRIX /*XMFLOAT4X4*/ invView;
		DirectX::XMMATRIX /*XMFLOAT4X4*/ prevViewProjection;
		uint32 bufferDimensions[2];
		float inverseBufferDimensions[2];
		float temporalStabilityFactor;
		float depthBufferThickness;
		float roughnessThreshold;
		float varianceThreshold;
		uint32 frameIndex;
		uint32 maxTraversalIntersections;
		uint32 minTraversalOccupancy;
		uint32 mostDetailedMip;
		uint32 samplesPerQuad;
		uint32 temporalVarianceGuidedTracingEnabled;
		uint32 envMapSpecularIrradianceCubemapMipLevelCount;
	};
	struct FDrawParameters : public IRenderPassDrawParameters
	{
		ID3D12GraphicsCommandList* pCmd = nullptr;
		DynamicBufferHeap* pCBufferHeap = nullptr;
		FFX_SSSRConstants ffxCBuffer;
		TextureID TexDepthHierarchy = INVALID_ID;
		TextureID TexNormals = INVALID_ID;
		SRV_ID SRVEnvironmentSpecularIrradianceCubemap = INVALID_ID;
		SRV_ID SRVBRDFIntegrationLUT = INVALID_ID;
	};


	ScreenSpaceReflectionsPass(VQRenderer& Renderer);
	ScreenSpaceReflectionsPass() = delete;
	~ScreenSpaceReflectionsPass() override;

	virtual bool Initialize() override;
	virtual void Destroy() override;

	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) override;
	virtual void OnDestroyWindowSizeDependentResources() override;

	virtual void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) override;

	virtual std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() override;

	SRV_ID GetPassOutputSRV(int iOutput = 0) const;
	void ClearHistoryBuffers(ID3D12GraphicsCommandList* pCmd);
private:
	void LoadRootSignatures();
	void CreateResources();
	void DestroyResources();
	void AllocateResourceViews();
	void DeallocateResourceViews();
	void InitializeResourceViews(const FResourceParameters* pParams);

	enum ESubpass
	{
		CLASSIFY_TILES,
		PREPARE_INDIRECT_ARGS,
		INTERSECTION,
		RESOLVE_TEMPORAL,
		PREFILTER,
		REPROJECT,
		BLUE_NOISE,

		NUM_SUBPASSES
	};

	std::vector<TextureID*> GetWindowSizeDependentDenoisingTextures();

private:
	// resources
	TextureID TexRayCounter;
	TextureID TexIntersectionPassIndirectArgs;
	TextureID TexBlueNoiseSobolBuffer;
	TextureID TexBlueNoiseRankingTileBuffer;
	TextureID TexBlueNoiseScramblingTileBuffer;
	TextureID TexReflectionDenoiserBlueNoise;
	
	TextureID TexRayList;
	TextureID TexDenoiserTileList;
	TextureID TexExtractedRoughness;
	TextureID TexDepthHistory;
	TextureID TexNormalsHistory;
	TextureID TexRoughnessHistory;
	std::array<TextureID, 2> TexRadiance;
	std::array<TextureID, 2> TexVariance;
	std::array<TextureID, 2> TexSampleCount;
	std::array<TextureID, 2> TexAvgRadiance;
	TextureID TexReprojectedRadiance;

	// R/W ping-pong resource views for all subpasses
	short iBuffer;
	std::array<UAV_ID, 2> UAVClassifyTilesOutputs;
	std::array<SRV_ID, 2> SRVClassifyTilesInputs;
	std::array<UAV_ID, 2> UAVPrepareIndirectArgsPass;
	std::array<UAV_ID, 2> UAVIntersectionOutputs;
	std::array<SRV_ID, 2> SRVIntersectionInputs;
	std::array<UAV_ID, 2> UAVTemporalResolveOutputs;
	std::array<SRV_ID, 2> SRVTemporalResolveInputs;
	std::array<UAV_ID, 2> UAVPrefilterPassOutputs;
	std::array<SRV_ID, 2> SRVPrefilterPassInputs;
	std::array<UAV_ID, 2> UAVReprojectPassOutputs;
	std::array<SRV_ID, 2> SRVReprojectPassInputs;
	std::array<UAV_ID, 2> UAVBlueNoisePassOutputs;
	std::array<SRV_ID, 2> SRVBlueNoisePassInputs;

	std::array<SRV_ID, 2> SRVScreenSpaceReflectionOutput; // ~ Radiance[]

	// root signatures, pipeline state objects, command signatures
	ID3D12CommandSignature* mpCommandSignature = nullptr;
	std::unordered_map<ESubpass, ID3D12RootSignature*> mSubpassRootSignatureLookup;
	PSO_ID PSOClassifyTilesPass = INVALID_ID;
	PSO_ID PSOBlueNoisePass = INVALID_ID;
	PSO_ID PSOPrepareIndirectArgsPass = INVALID_ID;
	PSO_ID PSOReprojectPass = INVALID_ID;
	PSO_ID PSOPrefilterPass = INVALID_ID;
	PSO_ID PSOResolveTemporalPass = INVALID_ID;
	PSO_ID PSOIntersectPass = INVALID_ID;

	// data
	DirectX::XMMATRIX MatPreviousViewProjection;
	bool bClearHistoryBuffers;
};