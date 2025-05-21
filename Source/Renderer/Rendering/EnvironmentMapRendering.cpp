#include "EnvironmentMapRendering.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/CubemapUtility.h"
#include "Libs/VQUtils/Source/Image.h"
#include "Libs/VQUtils/Source/Log.h"

#include "Engine/EnvironmentMap.h"
#include "Engine/GPUMarker.h"
#include "Engine/Math.h"
#include "Engine/Scene/Mesh.h"

int FEnvironmentMapRenderingResources::GetNumSpecularIrradianceCubemapLODLevels(const VQRenderer& Renderer) const
{
	if (Tex_IrradianceSpec == INVALID_ID)
		return 0;
	Renderer.WaitForTexture(Tex_IrradianceSpec);
	return Renderer.GetTextureMips(Tex_IrradianceSpec);
}

void FEnvironmentMapRenderingResources::CreateRenderingResources(VQRenderer& Renderer, const FEnvironmentMapDescriptor& desc, int DiffuseIrradianceCubemapResolution, int SpecularMapMip0Resolution)
{
	// HDR map
	this->Tex_HDREnvironment = Renderer.CreateTextureFromFile(desc.FilePath.c_str(), false, true);
	this->MaxContentLightLevel = static_cast<int>(desc.MaxContentLightLevel);
	
	// HDR Map Downsampled 
	int HDREnvironmentSizeX = 0;
	int HDREnvironmentSizeY = 0;
	Renderer.GetTextureDimensions(this->Tex_HDREnvironment, HDREnvironmentSizeX, HDREnvironmentSizeY);

	// Create Irradiance Map Textures 
	FTextureRequest tdesc("EnvMap_IrradianceDiff");
	tdesc.bCubemap = true;
	tdesc.D3D12Desc.Height = DiffuseIrradianceCubemapResolution; // TODO: drive with gfx settings?
	tdesc.D3D12Desc.Width = DiffuseIrradianceCubemapResolution; // TODO: drive with gfx settings?
	tdesc.D3D12Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	tdesc.D3D12Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	tdesc.D3D12Desc.DepthOrArraySize = 6;
	tdesc.D3D12Desc.MipLevels = 1;
	tdesc.D3D12Desc.SampleDesc = { 1, 0 };
	tdesc.D3D12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	tdesc.InitialState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_RENDER_TARGET;
	this->Tex_IrradianceDiff = Renderer.CreateTexture(tdesc);

	tdesc.Name = "EnvMap_IrradianceDiffBlurred";
	tdesc.D3D12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	tdesc.InitialState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	this->Tex_IrradianceDiffBlurred = Renderer.CreateTexture(tdesc);

	tdesc.Name = "EnvMap_BlurImmediateTemp";
	tdesc.bCubemap = false;
	tdesc.D3D12Desc.DepthOrArraySize = 1;
	this->Tex_BlurTemp = Renderer.CreateTexture(tdesc);

	tdesc.Name = "EnvMap_IrradianceSpec";
	tdesc.D3D12Desc.DepthOrArraySize = 6;
	tdesc.bGenerateMips = true;
	tdesc.bCubemap = true;
	tdesc.D3D12Desc.Height = SpecularMapMip0Resolution;
	tdesc.D3D12Desc.Width = SpecularMapMip0Resolution;
	tdesc.D3D12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	tdesc.InitialState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_RENDER_TARGET;
	tdesc.D3D12Desc.MipLevels = Image::CalculateMipLevelCount(tdesc.D3D12Desc.Width, tdesc.D3D12Desc.Height) - 1; // 2x2 for the last mip level
	this->Tex_IrradianceSpec = Renderer.CreateTexture(tdesc);

	const int& NUM_MIPS = tdesc.D3D12Desc.MipLevels;

	Renderer.WaitHeapsInitialized();

	// Create HDR Map SRV
	this->SRV_HDREnvironment = Renderer.AllocateAndInitializeSRV(this->Tex_HDREnvironment);

	// Create Irradiance Map SRVs 
	this->SRV_IrradianceDiff = Renderer.AllocateSRV();
	this->SRV_IrradianceSpec = Renderer.AllocateSRV();
	this->SRV_BlurTemp = Renderer.AllocateSRV();
	Renderer.InitializeSRV(this->SRV_IrradianceDiff, 0, this->Tex_IrradianceDiff, false, true);
	Renderer.InitializeSRV(this->SRV_IrradianceSpec, 0, this->Tex_IrradianceSpec, false, true);
	Renderer.InitializeSRV(this->SRV_BlurTemp, 0, this->Tex_BlurTemp);
	for (int face = 0; face < 6; ++face)
	{
		this->SRV_IrradianceDiffFaces[face] = Renderer.AllocateSRV();
		Renderer.InitializeSRV(this->SRV_IrradianceDiffFaces[face], face, this->Tex_IrradianceDiff, false, false);
	}
	this->SRV_IrradianceDiffBlurred = Renderer.AllocateSRV();
	Renderer.InitializeSRV(this->SRV_IrradianceDiffBlurred, 0, this->Tex_IrradianceDiffBlurred, false, true);


	// Create Irradiance Map RTVs & UAVs
	this->RTV_IrradianceDiff = Renderer.AllocateRTV(6);
	this->RTV_IrradianceSpec = Renderer.AllocateRTV(6 * NUM_MIPS);
	this->UAV_IrradianceDiffBlurred = Renderer.AllocateUAV(6);
	this->UAV_BlurTemp = Renderer.AllocateUAV();
	for (int face = 0; face < 6; ++face)
	{
		constexpr int MIP_LEVEL = 0;
		Renderer.InitializeRTV(this->RTV_IrradianceDiff, face, this->Tex_IrradianceDiff, face, MIP_LEVEL);
		Renderer.InitializeUAV(this->UAV_IrradianceDiffBlurred, face, this->Tex_IrradianceDiffBlurred, face, MIP_LEVEL);
	}
	Renderer.InitializeUAV(this->UAV_BlurTemp, 0, this->Tex_BlurTemp, 0, 0);

	for (int mip = 0; mip < NUM_MIPS; ++mip)
	for (int face = 0; face < 6; ++face)
		Renderer.InitializeRTV(this->RTV_IrradianceSpec, mip * 6 + face, this->Tex_IrradianceSpec, face, mip);

}

void FEnvironmentMapRenderingResources::DestroyRenderingResources(VQRenderer& Renderer, HWND hwnd)
{
	if (this->Tex_HDREnvironment == INVALID_ID)
	{
		return;
	}

	// GPU-sync assumed
	Renderer.GetWindowSwapChain(hwnd).WaitForGPU();

	Renderer.DestroySRV(this->SRV_HDREnvironment);
	Renderer.DestroySRV(this->SRV_IrradianceDiff);
	for (int face = 0; face < 6; ++face) Renderer.DestroySRV(this->SRV_IrradianceDiffFaces[face]);
	Renderer.DestroySRV(this->SRV_IrradianceSpec);
	Renderer.DestroySRV(this->SRV_BlurTemp);
	Renderer.DestroySRV(this->SRV_IrradianceDiffBlurred);
	// Renderer.DestroyUAV(); // TODO:?
	Renderer.DestroyTexture(this->Tex_HDREnvironment);
	Renderer.DestroyTexture(this->Tex_IrradianceDiff);
	Renderer.DestroyTexture(this->Tex_IrradianceSpec);
	Renderer.DestroyTexture(this->Tex_IrradianceDiffBlurred);

	this->SRV_HDREnvironment = this->Tex_HDREnvironment = INVALID_ID;
	this->SRV_IrradianceDiff = this->SRV_IrradianceSpec = INVALID_ID;
	this->MaxContentLightLevel = 0;
}


//
// Rendering
//
void VQRenderer::PreFilterEnvironmentMap(const Mesh& CubeMesh)
{
	Log::Info("Environment Map: PreFilterEnvironmentMap");
	using namespace DirectX;

	ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)mpBackgroundTaskCmds[GFX][0];
	DynamicBufferHeap& cbHeap = mDynamicHeap_BackgroundTaskConstantBuffer[0];
	FEnvironmentMapRenderingResources& env = mResources_MainWnd.EnvironmentMap;

	// sync for PSO initialization 
	this->WaitHeapsInitialized();
	{
		SCOPED_CPU_MARKER("WAIT_PSO_WORKER_DISPATCH");
		mLatchPSOLoaderDispatched.wait();
	}
	
	auto it = mPSOs.find(EBuiltinPSOs::CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO);
	if (it == mPSOs.end() || it->second == nullptr)
	{
		const FPSOCompileResult result = this->WaitPSOReady(EBuiltinPSOs::CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO);
		mPSOs[result.id] = result.pPSO;
	}

	SCOPED_GPU_MARKER(pCmd, "RenderEnvironmentMapCubeFaces");

	constexpr int NUM_CUBE_FACES = 6;

	const SRV& srvEnv = this->GetSRV(env.SRV_HDREnvironment);
	const SRV& srvIrrDiffuse = this->GetSRV(env.SRV_IrradianceDiff);
	const SRV& srvIrrSpcecular = this->GetSRV(env.SRV_IrradianceSpec);

	const XMFLOAT4X4 f16proj = MakePerspectiveProjectionMatrix(PI_DIV2, 1.0f, 0.1f, 10.0f);
	const XMMATRIX proj = XMLoadFloat4x4(&f16proj);

	struct cb0_t { XMMATRIX viewProj[NUM_CUBE_FACES]; };
	struct cb1_t { float ViewDimX; float ViewDimY; float Roughness; int MIP; };

	pCmd->Reset(mBackgroundTaskCommandAllocators[GFX][0], nullptr);

	// Diffuse Irradiance Convolution
	{
		Log::Info("Environment Map:   DiffuseIrradianceCubemap");
		SCOPED_GPU_MARKER(pCmd, "DiffuseIrradianceCubemap");

		// Since calculating the diffuse irradiance integral takes a long time,
		// we opt-in for drawing cube faces separately instead of doing all at one 
		// go using GS for RT slicing. Otherwise, a TDR can occur when drawing all
		// 6 faces per draw call as the integration takes long for each face.
		constexpr bool DRAW_CUBE_FACES_SEPARATELY = true;
		//------------------------------------------------------------------------

		const RTV& rtv = this->GetRTV(env.RTV_IrradianceDiff);

		// Viewport & Scissors
		int w, h, d;
		this->GetTextureDimensions(env.Tex_IrradianceDiff, w, h, d);
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f };
		D3D12_RECT     scissorsRect{ 0, 0, (LONG)w, (LONG)h };
		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);

		// geometry input
		const auto VBIBIDs = CubeMesh.GetIABufferIDs();
		const uint32 NumIndices = CubeMesh.GetNumIndices();
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = this->GetVertexBufferView(VB_ID);
		const IBV& ib = this->GetIndexBufferView(IB_ID);

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		if constexpr (DRAW_CUBE_FACES_SEPARATELY)
		{
			pCmd->SetPipelineState(this->GetPSO(CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO));
			pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap));
			pCmd->SetGraphicsRootDescriptorTable(2, srvEnv.GetGPUDescHandle());
			pCmd->SetGraphicsRootDescriptorTable(3, srvEnv.GetGPUDescHandle());

			for (int face = 0; face < NUM_CUBE_FACES; ++face)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle(face);

				D3D12_GPU_VIRTUAL_ADDRESS cbAddr0 = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr1 = {};
				cb0_t* pCB0 = {};
				cb1_t* pCB1 = {};
				cbHeap.AllocConstantBuffer(sizeof(cb0_t), (void**)(&pCB0), &cbAddr0);
				cbHeap.AllocConstantBuffer(sizeof(cb1_t), (void**)(&pCB1), &cbAddr1);
				pCB1->ViewDimX = static_cast<float>(w);
				pCB1->ViewDimY = static_cast<float>(h);
				pCB1->Roughness = 0.0f;
				pCB0->viewProj[0] = CubemapUtility::CalculateViewMatrix(face) * proj;

				pCmd->SetGraphicsRootConstantBufferView(1, cbAddr1);
				pCmd->SetGraphicsRootConstantBufferView(0, cbAddr0);
				pCmd->OMSetRenderTargets(1, &rtvHandle, TRUE, NULL);
				pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
			}
		}

		else // Draw Instanced Cubes (1x instance / cube face) w/ GS RT slicing
		{
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle();

			D3D12_GPU_VIRTUAL_ADDRESS cbAddr0 = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr1 = {};
			cb0_t* pCB0 = {};
			cb1_t* pCB1 = {};
			cbHeap.AllocConstantBuffer(sizeof(cb0_t), (void**)(&pCB0), &cbAddr0);
			cbHeap.AllocConstantBuffer(sizeof(cb1_t), (void**)(&pCB1), &cbAddr1);
			pCB1->ViewDimX = static_cast<float>(w);
			pCB1->ViewDimY = static_cast<float>(h);
			pCB1->MIP = 0;
			pCB1->Roughness = 0.0f;
			for (int face = 0; face < NUM_CUBE_FACES; ++face)
			{
				pCB0->viewProj[face] = CubemapUtility::CalculateViewMatrix(face) * proj;
			}

			pCmd->SetPipelineState(this->GetPSO(CUBEMAP_CONVOLUTION_DIFFUSE_PSO));
			pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap));
			pCmd->SetGraphicsRootDescriptorTable(2, srvEnv.GetGPUDescHandle());
			pCmd->SetGraphicsRootDescriptorTable(3, srvEnv.GetGPUDescHandle());
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr0);
			pCmd->SetGraphicsRootConstantBufferView(1, cbAddr1);
			pCmd->OMSetRenderTargets(1, &rtvHandle, TRUE, NULL);
			pCmd->DrawIndexedInstanced(NumIndices, NUM_CUBE_FACES, 0, 0, 0);
		}

		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			  CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(env.Tex_IrradianceDiff), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}

	// Blur Diffuse Irradiance
	for (int face = 0; face < NUM_CUBE_FACES; ++face)
	{
		std::string marker = "CubeFace[";
		marker += std::to_string(face);
		marker += "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());


		// compute dispatch dimensions
		int InputImageWidth = 0;
		int InputImageHeight = 0;
		int InputImageNumSlices = 0;
		this->GetTextureDimensions(env.Tex_IrradianceDiff, InputImageWidth, InputImageHeight, InputImageNumSlices);
		assert(InputImageNumSlices == NUM_CUBE_FACES);

		const SRV& srv = this->GetSRV(env.SRV_IrradianceDiffFaces[face]);

		constexpr int DispatchGroupDimensionX = 8;
		constexpr int DispatchGroupDimensionY = 8;
		const     int DispatchX = (InputImageWidth + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX;
		const     int DispatchY = (InputImageHeight + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY;
		constexpr int DispatchZ = 1;

		const UAV& uav_BlurIntermediate = this->GetUAV(env.UAV_BlurTemp);
		const UAV& uav_BlurOutput = this->GetUAV(env.UAV_IrradianceDiffBlurred);
		const SRV& srv_BlurIntermediate = this->GetSRV(env.SRV_BlurTemp);
		ID3D12Resource* pRscBlurIntermediate = this->GetTextureResource(env.Tex_BlurTemp);
		ID3D12Resource* pRscBlurOutput = this->GetTextureResource(env.Tex_IrradianceDiffBlurred);

		struct FBlurParams { int iImageSizeX; int iImageSizeY; };
		FBlurParams* pBlurParams = nullptr;

		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		cbHeap.AllocConstantBuffer(sizeof(FBlurParams), (void**)&pBlurParams, &cbAddr);
		pBlurParams->iImageSizeX = InputImageWidth;
		pBlurParams->iImageSizeY = InputImageHeight;

		if (!mPSOCompileResults.empty())
		{
			{
				std::shared_future<FPSOCompileResult>& future = mPSOCompileResults[EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO];
				if (future.valid())
				{
					SCOPED_CPU_MARKER("WAIT_PSO_COMPILE");
					future.wait();
					const FPSOCompileResult& result = future.get();
					mPSOs[result.id] = result.pPSO;
				}
			}
			{
				std::shared_future<FPSOCompileResult>& future = mPSOCompileResults[EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO];
				if (future.valid())
				{
					SCOPED_CPU_MARKER("WAIT_PSO_COMPILE");
					future.wait();
					const FPSOCompileResult& result = future.get();
					mPSOs[result.id] = result.pPSO;
				}
			}
		}

		{
			SCOPED_GPU_MARKER(pCmd, "BlurX");
			pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO));
			pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));

			pCmd->SetComputeRootDescriptorTable(0, srv.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_BlurIntermediate.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

			const CD3DX12_RESOURCE_BARRIER pBarriers[] =
			{
				  CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			};
			pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
		}
		{
			SCOPED_GPU_MARKER(pCmd, "BlurY");
			pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO));
			pCmd->SetComputeRootDescriptorTable(0, srv_BlurIntermediate.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_BlurOutput.GetGPUDescHandle(face));
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

			const CD3DX12_RESOURCE_BARRIER pBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			};

			if (face != NUM_CUBE_FACES - 1) // skip the last barrier as its not necessary (also causes DXGI error)
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
		}
	} // for_each face

	// transition blurred diffuse irradiance map resource
	{
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(env.Tex_IrradianceDiffBlurred), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		  , CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(env.Tex_IrradianceDiff), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}


	// Specular Irradiance
	if constexpr (true)
	{
		Log::Info("Environment Map:   SpecularIrradianceCubemap");
		SCOPED_GPU_MARKER(pCmd, "SpecularIrradianceCubemap");
		if (!mPSOCompileResults.empty())
		{
			std::shared_future<FPSOCompileResult>& future = mPSOCompileResults[EBuiltinPSOs::CUBEMAP_CONVOLUTION_SPECULAR_PSO];
			if (future.valid())
			{
				SCOPED_CPU_MARKER("WAIT_PSO_COMPILE");
				future.wait();
				const FPSOCompileResult& result = future.get();
				mPSOs[result.id] = result.pPSO;
			}
		}
		pCmd->SetPipelineState(this->GetPSO(CUBEMAP_CONVOLUTION_SPECULAR_PSO));
		pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap));
		pCmd->SetGraphicsRootDescriptorTable(2, srvEnv.GetGPUDescHandle());
		pCmd->SetGraphicsRootDescriptorTable(3, srvIrrDiffuse.GetGPUDescHandle());

		int w, h, d, MIP_LEVELS;
		this->GetTextureDimensions(env.Tex_IrradianceSpec, w, h, d, MIP_LEVELS);

		int inpTexW, inpTexH;
		this->GetTextureDimensions(env.Tex_HDREnvironment, inpTexW, inpTexH);

		for (int mip = 0; mip < MIP_LEVELS; ++mip)
		{
			for (int face = 0; face < NUM_CUBE_FACES; ++face)
			{
				std::string marker = "CubeFace["; marker += std::to_string(face); marker += "]";
				marker += ", MIP=" + std::to_string(mip);
				SCOPED_GPU_MARKER(pCmd, marker.c_str());

				const RTV& rtv = this->GetRTV(env.RTV_IrradianceSpec);
				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle(mip * 6 + face);

				D3D12_GPU_VIRTUAL_ADDRESS cbAddr0 = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr1 = {};
				cb0_t* pCB0 = {};
				cb1_t* pCB1 = {};
				cbHeap.AllocConstantBuffer(sizeof(cb0_t), (void**)(&pCB0), &cbAddr0);
				cbHeap.AllocConstantBuffer(sizeof(cb1_t), (void**)(&pCB1), &cbAddr1);

				pCB0->viewProj[0] = CubemapUtility::CalculateViewMatrix(face) * proj;
				pCB1->Roughness = static_cast<float>(mip) / (MIP_LEVELS - 1); // min(0.04, ) ?
				pCB1->ViewDimX = static_cast<float>(inpTexW);
				pCB1->ViewDimY = static_cast<float>(inpTexH);
				pCB1->MIP = mip;

				assert(pCB1->ViewDimX > 0);
				assert(pCB1->ViewDimY > 0);

				pCmd->SetGraphicsRootConstantBufferView(0, cbAddr0);
				pCmd->SetGraphicsRootConstantBufferView(1, cbAddr1);

				LONG Viewport[2] = {};
				Viewport[0] = w >> mip;
				Viewport[1] = h >> mip;
				D3D12_VIEWPORT viewport{ 0.0f, 0.0f, (FLOAT)Viewport[0], (FLOAT)Viewport[1], 0.0f, 1.0f };
				D3D12_RECT     scissorsRect{ 0, 0, Viewport[0], Viewport[1] };
				pCmd->RSSetViewports(1, &viewport);
				pCmd->RSSetScissorRects(1, &scissorsRect);

				const auto VBIBIDs = CubeMesh.GetIABufferIDs();
				const uint32 NumIndices = CubeMesh.GetNumIndices();
				const BufferID& VB_ID = VBIBIDs.first;
				const BufferID& IB_ID = VBIBIDs.second;
				const VBV& vb = this->GetVertexBufferView(VB_ID);
				const IBV& ib = this->GetIndexBufferView(IB_ID);

				pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				pCmd->IASetVertexBuffers(0, 1, &vb);
				pCmd->IASetIndexBuffer(&ib);

				pCmd->OMSetRenderTargets(1, &rtvHandle, TRUE, NULL);
				pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
			}
		}

		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(env.Tex_IrradianceSpec), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}
}
