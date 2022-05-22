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

#define NOMINMAX

#include "VQEngine.h"

#include "Libs/VQUtils/Source/utils.h"

#include <algorithm>

#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

using namespace DirectX;

static const FEnvironmentMapDescriptor DEFAULT_ENV_MAP_DESC = { "ENV_MAP_NOT_FOUND", "", 0.0f };


int FEnvironmentMapRenderingResources::GetNumSpecularIrradianceCubemapLODLevels(const VQRenderer& Renderer) const
{
	return Renderer.GetTextureMips(Tex_IrradianceSpec);
}


//-------------------------------------------------------------------------------------------------------------------------------------------------
//
// UPDATE THREAD
//
//-------------------------------------------------------------------------------------------------------------------------------------------------
const FEnvironmentMapDescriptor& VQEngine::GetEnvironmentMapDesc(const std::string& EnvMapName) const
{
	const bool bEnvMapNotFound = mLookup_EnvironmentMapDescriptors.find(EnvMapName) == mLookup_EnvironmentMapDescriptors.end();
	if (bEnvMapNotFound)
	{
		Log::Error("Environment Map %s not found.", EnvMapName.c_str());
	}
	return bEnvMapNotFound
		? DEFAULT_ENV_MAP_DESC
		: mLookup_EnvironmentMapDescriptors.at(EnvMapName);
}

FEnvironmentMapDescriptor VQEngine::GetEnvironmentMapDescCopy(const std::string& EnvMapName) const
{
	const bool bEnvMapNotFound = mLookup_EnvironmentMapDescriptors.find(EnvMapName) == mLookup_EnvironmentMapDescriptors.end();
	if (bEnvMapNotFound)
	{
		Log::Error("Environment Map %s not found.", EnvMapName.c_str());
		return DEFAULT_ENV_MAP_DESC;
	}
	return mLookup_EnvironmentMapDescriptors.at(EnvMapName);
}



// replaces %resolution% with the actual file name
// assumes no other '%' in environment map file path
std::string DetermineResolution_HDRI(FEnvironmentMapDescriptor& inEnvMapDesc, unsigned MonitorResolutionY)
{
	std::string resolution = "1k";
	const size_t iReplacementToken = inEnvMapDesc.FilePath.find('%');
	const bool bHasReplacementToken = iReplacementToken != std::string::npos;

	if (bHasReplacementToken)
	{
		     if (MonitorResolutionY <  720) { resolution = "1k"; }
		else if (MonitorResolutionY < 1080) { resolution = "2k"; }
		else if (MonitorResolutionY <= 1440) { resolution = "4k"; }
		else if (MonitorResolutionY < 2160) { resolution = "8k"; }
		else { resolution = "8k"; }

		const size_t lenReplacementToken = inEnvMapDesc.FilePath.find_last_of('%') - iReplacementToken + 1; // +1 to include the last '%' in "%resolution%'
		inEnvMapDesc.FilePath.replace(iReplacementToken, lenReplacementToken, resolution.c_str());
	}

	return resolution;
}
std::string FindEnvironmentMapToDownsizeFrom(const std::string& FolderPath, const std::string& EnvMapName, const std::string& TargetResolution)
{
	assert(TargetResolution.size() >= 2);
	const unsigned NextHigherResolution = (TargetResolution[0] - '0') * 2; // downsample from next higher resolution (resolutions scale *2 each time)
	if (NextHigherResolution > 8) // up to 8k env map supported
	{
		Log::Warning("FindEnvironmentMapToDownsizeFrom() NextHigherResolution requested is larger than 8k: %s", EnvMapName.c_str());
		return "";
	}

	
	// walk in HDRI/ directory
	std::vector<std::string> files = DirectoryUtil::GetFilesInPath(FolderPath);
	for (const std::string& FilePath : files) 
	{
		const std::string FileName = DirectoryUtil::GetFileNameWithoutExtension(FilePath);
#if 1
		// find the '_8k.hdr' file with matching env map name
		// assumes DownloadAssets.bat packages the project with 8k HDRI textures
		if (FileName.find(EnvMapName) != std::string::npos)
		{
			// validate file name: 'FILE_NAME_8k' -> ensure the last bit is the resolution
			auto vStrTokens = StrUtil::split(FileName, '_');
			const std::string& resolutionToken = vStrTokens.back();
			const bool bIsValidFileName = resolutionToken.size() >= 2 
				&& std::isalnum(resolutionToken[resolutionToken.size() - 2]) 
				&& std::isalnum(resolutionToken[0]) 
				&& resolutionToken.back() == 'k';
			
			if(bIsValidFileName)
				return FilePath;
		}
#else
		// find next higher resolution (e.g. 1k -> 2k, or 2k -> 4k)
		const std::string HiResResolution = std::to_string(NextHigherResolution) + "k";

		const bool bEnvMapFileWithDifferentResolutionFound = FileName.find(EnvMapName) != std::string::npos;
		if (bEnvMapFileWithDifferentResolutionFound)
		{
			const std::string FileResolution = StrUtil::split(FileName, '_').back(); 
			
			// we have a candidate env map file, but it may not be the resolution we're looking for
			const bool bEnvMapFileWithTargetResolutionFound = FileResolution[0] == HiResResolution[0];
			if (bEnvMapFileWithTargetResolutionFound)
			{
				return FilePath;
			}
		}
#endif
	}

	return "";
}
bool CreateEnvironmentMapTextureFromHiResAndSaveToDisk(const std::string& TargetFilePath)
{
	const std::string EnvMapFolder = DirectoryUtil::GetFolderPath(TargetFilePath);
	const std::string EnvMapNameWithDesiredResolution = DirectoryUtil::GetFileNameWithoutExtension(TargetFilePath);                    // "file_name_4k"
	const std::string EnvMapName = std::string(EnvMapNameWithDesiredResolution, 0, EnvMapNameWithDesiredResolution.find_last_of('_')); // "file_name"
	const std::string EnvMapDesiredResolution = StrUtil::split(EnvMapNameWithDesiredResolution, '_').back();                           // "4k"
	const std::string EnvMapFilePath_HiRes = FindEnvironmentMapToDownsizeFrom(EnvMapFolder, EnvMapName, EnvMapDesiredResolution);
	const std::string EnvMapSourceResolution = !EnvMapFilePath_HiRes.empty() ? StrUtil::split(EnvMapFilePath_HiRes, '_').back() : "";
	
	if (!EnvMapFilePath_HiRes.empty())
	{
		Log::Info("[EnvironmentMap] Downsizing from (%s) for target resolution (%s)", EnvMapFilePath_HiRes.c_str(), EnvMapDesiredResolution.c_str());

		Image LoadedHiResEnvMapImage = Image::LoadFromFile(EnvMapFilePath_HiRes.c_str());
		if (LoadedHiResEnvMapImage.IsValid())
		{
			const int ResSrc = EnvMapSourceResolution[0] - '0';
			const int ResDst = EnvMapDesiredResolution[0] - '0';

#if 1	// call 1x resize to the source image
			static const std::unordered_map<int, unsigned> LookupResolutionX { {8, 8192}, {4, 4096}, {2, 2048}, {1, 1024} };
			static const std::unordered_map<int, unsigned> LookupResolutionY { {8, 4096}, {4, 2048}, {2, 1024}, {1, 512 } };

			const unsigned TargetWidth  = LookupResolutionX.at(ResDst);
			const unsigned TargetHeight = LookupResolutionY.at(ResDst);
			Image DownsizedImage = Image::CreateResizedImage(LoadedHiResEnvMapImage, TargetWidth, TargetHeight);

			if (DownsizedImage.IsValid() && DownsizedImage.SaveToDisk(TargetFilePath.c_str()))
			{
				Log::Info("[EnvironmentMap] Saved to disk: %s", TargetFilePath.c_str());
			}
			else
			{
				Log::Error("Error saving to file: %s", TargetFilePath.c_str());
			}
			DownsizedImage.Destroy();

#else	// Halve the resolution on each iteration until we reach to target resolution
			const int NumDownsize = std::log2f((float)ResSrc / ResDst);

			// do the chain downsizing
			std::vector<Image> DownSizedImages(NumDownsize);
			DownSizedImages[0] = Image::CreateHalfResolutionFromImage(LoadedHiResEnvMapImage);
			for (int i = 1; i < NumDownsize; ++i) { DownSizedImages[i] = Image::CreateHalfResolutionFromImage(DownSizedImages[i - 1]); }

			// only write out the last one
			const Image& TargetDownsizedImage = DownSizedImages.back();
			if (TargetDownsizedImage.IsValid())
			{
				const bool bSaveSuccess = TargetDownsizedImage.SaveToDisk(TargetFilePath.c_str());
				if (bSaveSuccess) { Log::Info("[EnvironmentMap] Saved to disk: %s", TargetFilePath.c_str()); }
				else { Log::Error("Error saving to file: %s", TargetFilePath.c_str()); }
			}

			// cleanup
			for (int i = 0; i < NumDownsize; ++i) { DownSizedImages[i].Destroy(); }
#endif
		}
		LoadedHiResEnvMapImage.Destroy();
	}
	else
	{
		Log::Error("[EnvironmentMap] EnvMapFile to downsize from is not found: %s", EnvMapName.c_str());
		return false;
	}

	return true;
}
void VQEngine::LoadEnvironmentMap(const std::string& EnvMapName, int SpecularMapMip0Resolution)
{
	assert(EnvMapName.size() != 0);
	constexpr int DIFFUSE_IRRADIANCE_CUBEMAP_RESOLUTION = 64;
	FEnvironmentMapRenderingResources& env = mResources_MainWnd.EnvironmentMap;

	// if already loaded, unload it
	if (env.Tex_HDREnvironment != INVALID_ID)
	{
		assert(env.SRV_HDREnvironment != INVALID_ID);
		UnloadEnvironmentMap();
	}

	FEnvironmentMapDescriptor desc = this->GetEnvironmentMapDescCopy(EnvMapName); // copy because we'll update the FilePath down below
	std::vector<std::string>::iterator it = std::find(mResourceNames.mEnvironmentMapPresetNames.begin(), mResourceNames.mEnvironmentMapPresetNames.end(), EnvMapName);
	const size_t ActiveEnvMapIndex = it - mResourceNames.mEnvironmentMapPresetNames.begin();
	
	if (desc.FilePath.empty()) // check whether the env map was found or not
	{
		Log::Error("Environment Map file path empty");
		return;
	}
	
	if (!DirectoryUtil::FileExists(desc.FilePath)) // check whether the env map was found or not
	{
		Log::Error("Couldn't find Environment Map %s: %s", EnvMapName.c_str(), desc.FilePath.c_str());
		Log::Warning("Have you run Scripts/DownloadAssets.bat?");
		return;
	}

	// Pick an environment map resolution based on the monitor swapchain is on
	// so we can avoid loading 8k textures for a 1080p laptop for example.
	const unsigned MonitorResolutionY = mRenderer.GetWindowSwapChain(mpWinMain->GetHWND()).GetContainingMonitorDesc().DesktopCoordinates.bottom;
	DetermineResolution_HDRI(desc, MonitorResolutionY);
	const std::string EnvMapResolution = StrUtil::split(DirectoryUtil::GetFileNameWithoutExtension(desc.FilePath), '_').back(); // file_name_4k.png -> "4k"
	Log::Info("Loading Environment Map: %s (%s | Diff:%dx%d | Spec:%dx%d)", EnvMapName.c_str(), EnvMapResolution.c_str(), DIFFUSE_IRRADIANCE_CUBEMAP_RESOLUTION, DIFFUSE_IRRADIANCE_CUBEMAP_RESOLUTION, SpecularMapMip0Resolution, SpecularMapMip0Resolution);

	// if the lowres texture doesn't exist, run a downsample pass (on CPU) on the available texture and save to disk
	if (!DirectoryUtil::FileExists(desc.FilePath)) // desc.FilePath: "FolderPath/file_name_4k.hdr"
	{
		Log::Info("[EnvironmentMap] Target resolution texture (%s) doesn't exist on disk. ", desc.FilePath.c_str());

		// Note: Downsizing the 8K source texture doesn't look well, a native version of the
		//       down-sized HDRI should be downloaded, either through packaging or during execution.
		//       VQE will keep using 8K source textures for now while limiting the number of them
		//       to reduce download size.
		CreateEnvironmentMapTextureFromHiResAndSaveToDisk(desc.FilePath);
	}


	// Load environment map resources ------------------------------------------------------------


	// HDR map
	env.Tex_HDREnvironment = mRenderer.CreateTextureFromFile(desc.FilePath.c_str(), true);
	env.SRV_HDREnvironment = mRenderer.AllocateAndInitializeSRV(env.Tex_HDREnvironment);
	env.MaxContentLightLevel = static_cast<int>(desc.MaxContentLightLevel);

	// HDR Map Downsampled 
	int HDREnvironmentSizeX = 0;
	int HDREnvironmentSizeY = 0;
	mRenderer.GetTextureDimensions(env.Tex_HDREnvironment, HDREnvironmentSizeX, HDREnvironmentSizeY);

	// Create Irradiance Map Textures 
	TextureCreateDesc tdesc("EnvMap_IrradianceDiff");
	tdesc.bCubemap = true;
	tdesc.pData = nullptr;
	tdesc.d3d12Desc.Height = DIFFUSE_IRRADIANCE_CUBEMAP_RESOLUTION; // TODO: drive with gfx settings?
	tdesc.d3d12Desc.Width  = DIFFUSE_IRRADIANCE_CUBEMAP_RESOLUTION; // TODO: drive with gfx settings?
	tdesc.d3d12Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	tdesc.d3d12Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	tdesc.d3d12Desc.DepthOrArraySize = 6;
	tdesc.d3d12Desc.MipLevels = 1;
	tdesc.d3d12Desc.SampleDesc = { 1, 0 };
	tdesc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	tdesc.ResourceState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_RENDER_TARGET;
	env.Tex_IrradianceDiff = mRenderer.CreateTexture(tdesc);

	tdesc.TexName = "EnvMap_IrradianceDiffBlurred";
	tdesc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	tdesc.ResourceState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	env.Tex_IrradianceDiffBlurred = mRenderer.CreateTexture(tdesc);

	tdesc.TexName = "EnvMap_BlurImmediateTemp";
	tdesc.bCubemap = false;
	tdesc.d3d12Desc.DepthOrArraySize = 1;
	env.Tex_BlurTemp = mRenderer.CreateTexture(tdesc);

	tdesc.TexName = "EnvMap_IrradianceSpec";
	tdesc.d3d12Desc.DepthOrArraySize = 6;
	tdesc.bGenerateMips = true;
	tdesc.bCubemap = true;
	tdesc.d3d12Desc.Height = SpecularMapMip0Resolution;
	tdesc.d3d12Desc.Width  = SpecularMapMip0Resolution;
	tdesc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	tdesc.ResourceState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_RENDER_TARGET;
	tdesc.d3d12Desc.MipLevels = Image::CalculateMipLevelCount(tdesc.d3d12Desc.Width, tdesc.d3d12Desc.Height) - 1; // 2x2 for the last mip level
	env.Tex_IrradianceSpec = mRenderer.CreateTexture(tdesc);

	const int& NUM_MIPS = tdesc.d3d12Desc.MipLevels;

	// Create Irradiance Map SRVs 
	env.SRV_IrradianceDiff = mRenderer.AllocateSRV();
	env.SRV_IrradianceSpec = mRenderer.AllocateSRV();
	env.SRV_BlurTemp = mRenderer.AllocateSRV();
	mRenderer.InitializeSRV(env.SRV_IrradianceDiff, 0, env.Tex_IrradianceDiff, false, true);
	mRenderer.InitializeSRV(env.SRV_IrradianceSpec, 0, env.Tex_IrradianceSpec, false, true);
	mRenderer.InitializeSRV(env.SRV_BlurTemp, 0, env.Tex_BlurTemp);
	for (int face = 0; face < 6; ++face)
	{
		env.SRV_IrradianceDiffFaces[face] = mRenderer.AllocateSRV();
		mRenderer.InitializeSRV(env.SRV_IrradianceDiffFaces[face], face, env.Tex_IrradianceDiff, false, false);
	}
	env.SRV_IrradianceDiffBlurred = mRenderer.AllocateSRV();
	mRenderer.InitializeSRV(env.SRV_IrradianceDiffBlurred, 0, env.Tex_IrradianceDiffBlurred, false, true);


	// Create Irradiance Map RTVs & UAVs
	env.RTV_IrradianceDiff = mRenderer.AllocateRTV(6);
	env.RTV_IrradianceSpec = mRenderer.AllocateRTV(6 * NUM_MIPS);
	env.UAV_IrradianceDiffBlurred = mRenderer.AllocateUAV(6);
	env.UAV_BlurTemp = mRenderer.AllocateUAV();
	for (int face = 0; face < 6; ++face)
	{
		constexpr int MIP_LEVEL = 0;
		mRenderer.InitializeRTV(env.RTV_IrradianceDiff, face, env.Tex_IrradianceDiff, face, MIP_LEVEL);
		mRenderer.InitializeUAV(env.UAV_IrradianceDiffBlurred, face, env.Tex_IrradianceDiffBlurred, face, MIP_LEVEL);
	}
	mRenderer.InitializeUAV(env.UAV_BlurTemp, 0, env.Tex_BlurTemp, 0, 0);

	for (int  mip = 0; mip<NUM_MIPS; ++mip ) 
	for (int face = 0; face < 6    ; ++face)  
		mRenderer.InitializeRTV(env.RTV_IrradianceSpec, mip*6+face, env.Tex_IrradianceSpec, face, mip);


	// Queue irradiance cube face rendering
	mbEnvironmentMapPreFilter.store(true);

	//assert(mpScene->mIndex_ActiveEnvironmentMapPreset == static_cast<int>(ActiveEnvMapIndex)); // Only false durin initialization
	mpScene->mIndex_ActiveEnvironmentMapPreset = static_cast<int>(ActiveEnvMapIndex);

	// Update HDRMetaData when the environment map is loaded
	HWND hwnd = mpWinMain->GetHWND();

	mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetStaticHDRMetaDataEvent>(hwnd, this->GatherHDRMetaDataParameters(hwnd)));
}

void VQEngine::UnloadEnvironmentMap()
{
	FEnvironmentMapRenderingResources& env = mResources_MainWnd.EnvironmentMap;
	if (env.Tex_HDREnvironment != INVALID_ID)
	{
		// GPU-sync assumed
		mRenderer.GetWindowSwapChain(mpWinMain->GetHWND()).WaitForGPU();
		
		mRenderer.DestroySRV(env.SRV_HDREnvironment);
		mRenderer.DestroySRV(env.SRV_IrradianceDiff);
		for (int face = 0; face < 6; ++face) mRenderer.DestroySRV(env.SRV_IrradianceDiffFaces[face]);
		mRenderer.DestroySRV(env.SRV_IrradianceSpec);
		mRenderer.DestroySRV(env.SRV_BlurTemp);
		mRenderer.DestroySRV(env.SRV_IrradianceDiffBlurred);
		// mRenderer.DestroyUAV(); // TODO:?
		mRenderer.DestroyTexture(env.Tex_HDREnvironment);
		mRenderer.DestroyTexture(env.Tex_IrradianceDiff);
		mRenderer.DestroyTexture(env.Tex_IrradianceSpec);
		mRenderer.DestroyTexture(env.Tex_IrradianceDiffBlurred);

		env.SRV_HDREnvironment = env.Tex_HDREnvironment = INVALID_ID;
		env.SRV_IrradianceDiff = env.SRV_IrradianceSpec = INVALID_ID;
		env.MaxContentLightLevel = 0;
	}
}



//-------------------------------------------------------------------------------------------------------------------------------------------------
//
// RENDER THREAD
//
//-------------------------------------------------------------------------------------------------------------------------------------------------
#include "GPUMarker.h"
void VQEngine::PreFilterEnvironmentMap(ID3D12GraphicsCommandList* pCmd, FEnvironmentMapRenderingResources& env)
{
	Log::Info("Environment Map: PreFilterEnvironmentMap");
	using namespace DirectX;

	FWindowRenderContext& ctx = mRenderer.GetWindowRenderContext(mpWinMain->GetHWND());
	DynamicBufferHeap& cbHeap = ctx.GetConstantBufferHeap(0);

	if (env.SRV_BRDFIntegrationLUT == INVALID_ID)
	{
		ComputeBRDFIntegrationLUT(pCmd, env.SRV_BRDFIntegrationLUT);
	}

	SCOPED_GPU_MARKER(pCmd, "RenderEnvironmentMapCubeFaces");

	constexpr int NUM_CUBE_FACES = 6;
	
	const SRV& srvEnv = mRenderer.GetSRV(env.SRV_HDREnvironment);
	const SRV& srvIrrDiffuse   = mRenderer.GetSRV(env.SRV_IrradianceDiff);
	const SRV& srvIrrSpcecular = mRenderer.GetSRV(env.SRV_IrradianceSpec);

	const XMFLOAT4X4 f16proj = MakePerspectiveProjectionMatrix(PI_DIV2, 1.0f, 0.1f, 10.0f);
	const XMMATRIX proj = XMLoadFloat4x4(&f16proj);
	
	struct cb0_t { XMMATRIX viewProj[NUM_CUBE_FACES]; };
	struct cb1_t { float ViewDimX; float ViewDimY; float Roughness; int MIP; };

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

		const RTV& rtv = mRenderer.GetRTV(env.RTV_IrradianceDiff);

		// Viewport & Scissors
		int w, h, d;
		mRenderer.GetTextureDimensions(env.Tex_IrradianceDiff, w, h, d);
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f };
		D3D12_RECT     scissorsRect{ 0, 0, (LONG)w, (LONG)h };
		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);

		// geometry input
		const Mesh& mesh = mBuiltinMeshes[EBuiltInMeshes::CUBE];
		const auto VBIBIDs = mesh.GetIABufferIDs();
		const uint32 NumIndices = mesh.GetNumIndices();
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
		const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		if constexpr (DRAW_CUBE_FACES_SEPARATELY)
		{
			pCmd->SetPipelineState(mRenderer.GetPSO(CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO));
			pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap));
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
				pCB0->viewProj[0] = Texture::CubemapUtility::CalculateViewMatrix(face) * proj;

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
				pCB0->viewProj[face] = Texture::CubemapUtility::CalculateViewMatrix(face) * proj;
			}

			pCmd->SetPipelineState(mRenderer.GetPSO(CUBEMAP_CONVOLUTION_DIFFUSE_PSO));
			pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap));
			pCmd->SetGraphicsRootDescriptorTable(2, srvEnv.GetGPUDescHandle());
			pCmd->SetGraphicsRootDescriptorTable(3, srvEnv.GetGPUDescHandle());
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr0);
			pCmd->SetGraphicsRootConstantBufferView(1, cbAddr1);
			pCmd->OMSetRenderTargets(1, &rtvHandle, TRUE, NULL);
			pCmd->DrawIndexedInstanced(NumIndices, NUM_CUBE_FACES, 0, 0, 0);
		}

		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			  CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(env.Tex_IrradianceDiff), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
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
		mRenderer.GetTextureDimensions(env.Tex_IrradianceDiff, InputImageWidth, InputImageHeight, InputImageNumSlices);
		assert(InputImageNumSlices == NUM_CUBE_FACES);

		const SRV& srv = mRenderer.GetSRV(env.SRV_IrradianceDiffFaces[face]);

		constexpr int DispatchGroupDimensionX = 8;
		constexpr int DispatchGroupDimensionY = 8;
		const     int DispatchX = (InputImageWidth  + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX;
		const     int DispatchY = (InputImageHeight + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY;
		constexpr int DispatchZ = 1;

		const UAV& uav_BlurIntermediate = mRenderer.GetUAV(env.UAV_BlurTemp);
		const UAV& uav_BlurOutput       = mRenderer.GetUAV(env.UAV_IrradianceDiffBlurred);
		const SRV& srv_BlurIntermediate = mRenderer.GetSRV(env.SRV_BlurTemp);
		ID3D12Resource* pRscBlurIntermediate = mRenderer.GetTextureResource(env.Tex_BlurTemp);
		ID3D12Resource* pRscBlurOutput       = mRenderer.GetTextureResource(env.Tex_IrradianceDiffBlurred);

		struct FBlurParams { int iImageSizeX; int iImageSizeY; };
		FBlurParams* pBlurParams = nullptr;

		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		cbHeap.AllocConstantBuffer(sizeof(FBlurParams), (void**)&pBlurParams, &cbAddr);
		pBlurParams->iImageSizeX = InputImageWidth;
		pBlurParams->iImageSizeY = InputImageHeight;

		{
			SCOPED_GPU_MARKER(pCmd, "BlurX");
			pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO));
			pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));

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
			pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO));
			pCmd->SetComputeRootDescriptorTable(0, srv_BlurIntermediate.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_BlurOutput.GetGPUDescHandle(face));
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

			const CD3DX12_RESOURCE_BARRIER pBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			};
			
			if(face != NUM_CUBE_FACES-1) // skip the last barrier as its not necessary (also causes DXGI error)
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
		}
	} // for_each face
	
	// transition blurred diffuse irradiance map resource
	{
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(env.Tex_IrradianceDiffBlurred), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		  , CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(env.Tex_IrradianceDiff), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}


	// Specular Irradiance
	if constexpr (true)
	{
		Log::Info("Environment Map:   SpecularIrradianceCubemap");
		SCOPED_GPU_MARKER(pCmd, "SpecularIrradianceCubemap");

		pCmd->SetPipelineState(mRenderer.GetPSO(CUBEMAP_CONVOLUTION_SPECULAR_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap));
		pCmd->SetGraphicsRootDescriptorTable(2, srvEnv.GetGPUDescHandle());
		pCmd->SetGraphicsRootDescriptorTable(3, srvIrrDiffuse.GetGPUDescHandle());

		int w, h, d, MIP_LEVELS;
		mRenderer.GetTextureDimensions(env.Tex_IrradianceSpec, w, h, d, MIP_LEVELS);

		int inpTexW, inpTexH;
		mRenderer.GetTextureDimensions(env.Tex_HDREnvironment, inpTexW, inpTexH);

		for (int mip = 0; mip < MIP_LEVELS; ++mip)
		{
			for (int face = 0; face < NUM_CUBE_FACES; ++face)
			{
				std::string marker = "CubeFace["; marker += std::to_string(face); marker += "]";
				marker += ", MIP=" + std::to_string(mip);
				SCOPED_GPU_MARKER(pCmd, marker.c_str());

				const RTV& rtv = mRenderer.GetRTV(env.RTV_IrradianceSpec);
				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle(mip * 6 + face);

				D3D12_GPU_VIRTUAL_ADDRESS cbAddr0 = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr1 = {};
				cb0_t* pCB0 = {};
				cb1_t* pCB1 = {};
				cbHeap.AllocConstantBuffer(sizeof(cb0_t), (void**)(&pCB0), &cbAddr0);
				cbHeap.AllocConstantBuffer(sizeof(cb1_t), (void**)(&pCB1), &cbAddr1);
				
				pCB0->viewProj[0] = Texture::CubemapUtility::CalculateViewMatrix(face) * proj;
				pCB1->Roughness = static_cast<float>(mip) / (MIP_LEVELS-1); // min(0.04, ) ?
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

				const Mesh& mesh = mBuiltinMeshes[EBuiltInMeshes::CUBE];
				const auto VBIBIDs = mesh.GetIABufferIDs();
				const uint32 NumIndices = mesh.GetNumIndices();
				const BufferID& VB_ID = VBIBIDs.first;
				const BufferID& IB_ID = VBIBIDs.second;
				const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
				const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

				pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				pCmd->IASetVertexBuffers(0, 1, &vb);
				pCmd->IASetIndexBuffer(&ib);

				pCmd->OMSetRenderTargets(1, &rtvHandle, TRUE, NULL);
				pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
			}
		}

		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(env.Tex_IrradianceSpec), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}
}

void VQEngine::ComputeBRDFIntegrationLUT(ID3D12GraphicsCommandList* pCmd, SRV_ID& outSRV_ID)
{
	Log::Info("Environment Map:   ComputeBRDFIntegrationLUT");
	SCOPED_GPU_MARKER(pCmd, "CreateBRDFIntegralLUT");

	// Texture resource is created (on Renderer::LoadDefaultResources()) but not initialized at this point.
	const TextureID TexBRDFLUT = mRenderer.GetProceduralTexture(EProceduralTextures::IBL_BRDF_INTEGRATION_LUT);
	ID3D12Resource* pRscBRDFLUT = mRenderer.GetTextureResource(TexBRDFLUT);

	int W, H;
	mRenderer.GetTextureDimensions(TexBRDFLUT, W, H);

	// Create & Initialize a UAV for the LUT 
	UAV_ID uavBRDFLUT_ID = mRenderer.AllocateUAV();
	mRenderer.InitializeUAV(uavBRDFLUT_ID, 0, TexBRDFLUT);
	const UAV& uavBRDFLUT = mRenderer.GetUAV(uavBRDFLUT_ID);

	// Dispatch
	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::BRDF_INTEGRATION_CS_PSO));
	pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__BRDFIntegrationCS));
	pCmd->SetComputeRootDescriptorTable(0, uavBRDFLUT.GetGPUDescHandle());

	constexpr int THREAD_GROUP_X = 8;
	constexpr int THREAD_GROUP_Y = 8;
	const int DISPATCH_DIMENSION_X = (W + (THREAD_GROUP_X - 1)) / THREAD_GROUP_X;
	const int DISPATCH_DIMENSION_Y = (H + (THREAD_GROUP_Y - 1)) / THREAD_GROUP_Y;
	constexpr int DISPATCH_DIMENSION_Z = 1;
	pCmd->Dispatch(DISPATCH_DIMENSION_X, DISPATCH_DIMENSION_Y, DISPATCH_DIMENSION_Z);

	const CD3DX12_RESOURCE_BARRIER pBarriers[] =
	{
		  CD3DX12_RESOURCE_BARRIER::Transition(pRscBRDFLUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};
	pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	
	outSRV_ID = mRenderer.GetProceduralTextureSRV_ID(EProceduralTextures::IBL_BRDF_INTEGRATION_LUT);
}

