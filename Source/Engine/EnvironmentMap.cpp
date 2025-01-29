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
#include "GPUMarker.h"

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
static std::string DetermineResolution_HDRI(FEnvironmentMapDescriptor& inEnvMapDesc, unsigned MonitorResolutionY)
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
static std::string FindEnvironmentMapToDownsizeFrom(const std::string& FolderPath, const std::string& EnvMapName, const std::string& TargetResolution)
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
static bool CreateEnvironmentMapTextureFromHiResAndSaveToDisk(const std::string& TargetFilePath)
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
	SCOPED_CPU_MARKER("LoadEnvironmentMap");
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
	
	std::vector<std::string>::iterator it = std::find(mResourceNames.mEnvironmentMapPresetNames.begin(), mResourceNames.mEnvironmentMapPresetNames.end(), EnvMapName);
	const size_t ActiveEnvMapIndex = it - mResourceNames.mEnvironmentMapPresetNames.begin();

	// Pick an environment map resolution based on the monitor swapchain is on
	// so we can avoid loading 8k textures for a 1080p laptop for example.
	mRenderer.WaitMainSwapchainReady(); // wait for swapchain initialization, we need it initialized at this point
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
	env.Tex_HDREnvironment = mRenderer.CreateTextureFromFile(desc.FilePath.c_str(), false, true);
	env.SRV_HDREnvironment = mRenderer.AllocateAndInitializeSRV(env.Tex_HDREnvironment);
	env.MaxContentLightLevel = static_cast<int>(desc.MaxContentLightLevel);

	// HDR Map Downsampled 
	int HDREnvironmentSizeX = 0;
	int HDREnvironmentSizeY = 0;
	mRenderer.GetTextureDimensions(env.Tex_HDREnvironment, HDREnvironmentSizeX, HDREnvironmentSizeY);

	// Create Irradiance Map Textures 
	TextureCreateDesc tdesc("EnvMap_IrradianceDiff");
	tdesc.bCubemap = true;
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
