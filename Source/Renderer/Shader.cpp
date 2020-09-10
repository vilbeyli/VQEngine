//	VQEngine | DirectX11 Renderer
//	Copyright(C) 2018  - Volkan Ilbeyli
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

#include "Shader.h"
#include "Renderer.h"

#if 0
#include "Utilities/Log.h"
#include "Utilities/utils.h"
#include "Utilities/PerfTimer.h"
#include "Application/Application.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <functional>

//-------------------------------------------------------------------------------------------------------------
// CONSTANTS & STATICS
//-------------------------------------------------------------------------------------------------------------
static const std::unordered_map<EShaderStage, const char*> SHADER_COMPILER_VERSION_LOOKUP = 
{
	{ EShaderStage::VS, "vs_5_0" },
	{ EShaderStage::GS, "gs_5_0" },
	{ EShaderStage::DS, "ds_5_0" },
	{ EShaderStage::HS, "hs_5_0" },
	{ EShaderStage::PS, "ps_5_0" },
	{ EShaderStage::CS, "cs_5_0" },
};
static const std::unordered_map<EShaderStage, const char*> SHADER_ENTRY_POINT_LOOKUP =
{
	{ EShaderStage::VS, "VSMain" },
	{ EShaderStage::GS, "GSMain" },
	{ EShaderStage::DS, "DSMain" },
	{ EShaderStage::HS, "HSMain" },
	{ EShaderStage::PS, "PSMain" },
	{ EShaderStage::CS, "CSMain" },
};

ID3DInclude* const SHADER_INCLUDE_HANDLER = D3D_COMPILE_STANDARD_FILE_INCLUDE;		// use default include handler for using #include in shader files

#if defined( _DEBUG ) || defined ( FORCE_DEBUG )
const UINT SHADER_COMPILE_FLAGS = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
const UINT SHADER_COMPILE_FLAGS = D3DCOMPILE_ENABLE_STRICTNESS;
#endif



#ifdef _WIN64
#define CALLING_CONVENTION __cdecl
#else	// _WIN32
#define CALLING_CONVENTION __stdcall
#endif

static void(CALLING_CONVENTION ID3D11DeviceContext:: *SetShaderConstants[EShaderStage::COUNT])
(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) = 
{
	&ID3D11DeviceContext::VSSetConstantBuffers,
	&ID3D11DeviceContext::GSSetConstantBuffers,
	&ID3D11DeviceContext::DSSetConstantBuffers,
	&ID3D11DeviceContext::HSSetConstantBuffers,
	&ID3D11DeviceContext::PSSetConstantBuffers, 
	&ID3D11DeviceContext::CSSetConstantBuffers,
};

static std::unordered_map <std::string, EShaderStage > s_ShaderTypeStrLookup = 
{
	{"vs", EShaderStage::VS},
	{"gs", EShaderStage::GS},
	{"ds", EShaderStage::DS},
	{"hs", EShaderStage::HS},
	{"cs", EShaderStage::CS},
	{"ps", EShaderStage::PS}
};


//-------------------------------------------------------------------------------------------------------------
// STATIC FUNCTIONS
//-------------------------------------------------------------------------------------------------------------
std::string GetCompileError(ID3D10Blob*& errorMessage, const std::string& shdPath)
{
	if (errorMessage)
	{
		char* compileErrors = (char*)errorMessage->GetBufferPointer();
		size_t bufferSize = errorMessage->GetBufferSize();

		std::stringstream ss;
		for (unsigned int i = 0; i < bufferSize; ++i)
		{
			ss << compileErrors[i];
		}
		errorMessage->Release();
		return ss.str();
	}
	else
	{
		Log::Error(shdPath);
		return ("Error: " + shdPath);
	}
}

static std::string GetIncludeFileName(const std::string& line)
{
	const std::string str_search = "#include \"";
	const size_t foundPos = line.find(str_search);
	if (foundPos != std::string::npos)
	{
		std::string quotedFileName = line.substr(foundPos + strlen("#include "), line.size() - foundPos);// +str_search.size() - 1);
		return quotedFileName.substr(1, quotedFileName.size() - 2);
	}
	return std::string();
}

static bool AreIncludesDirty(const std::string& srcPath, const std::string& cachePath)
{

	const std::string ShaderSourceDir = DirectoryUtil::GetFolderPath(srcPath);
	const std::string ShaderCacheDir = DirectoryUtil::GetFolderPath(cachePath);

	std::stack<std::string> includeStack;
	includeStack.push(srcPath);
	while (!includeStack.empty())
	{
		const std::string includeFilePath = includeStack.top();
		includeStack.pop();
		std::ifstream src = std::ifstream(includeFilePath.c_str());
		if (!src.good())
		{
			Log::Error("[ShaderCompile] Cannot open source file: %s", includeFilePath.c_str());
			continue;
		}

		std::string line;
		while (getline(src, line))
		{
			const std::string includeFileName = GetIncludeFileName(line);
			if (includeFileName.empty()) continue;

			const std::string includeSourcePath = ShaderSourceDir + includeFileName;
			const std::string includeCachePath = ShaderCacheDir + includeFileName;

			if (DirectoryUtil::IsFileNewer(includeSourcePath, cachePath))
				return true;
			includeStack.push(includeSourcePath);
		}
		src.close();
	}
	return false;
}

bool IsCacheDirty(const std::string& sourcePath, const std::string& cachePath)
{
	if (!DirectoryUtil::FileExists(cachePath)) return true;

	return DirectoryUtil::IsFileNewer(sourcePath, cachePath) || AreIncludesDirty(sourcePath, cachePath);
}


bool Shader::CompileFromSource(const std::string& pathToFile, const EShaderStage& type, ID3D10Blob *& ref_pBob, std::string& errMsg, const std::vector<ShaderMacro>& macros)
{
	const StrUtil::UnicodeString Path = pathToFile;
	const WCHAR* PathStr = Path.GetUnicodePtr();
	ID3D10Blob* errorMessage = nullptr;

	int i = 0;
	std::vector<D3D10_SHADER_MACRO> d3dMacros(macros.size() + 1);
	std::for_each(RANGE(macros), [&](const ShaderMacro& macro)
	{
		d3dMacros[i++] = D3D10_SHADER_MACRO({ macro.name.c_str(), macro.value.c_str() });
	});
	d3dMacros[i] = { NULL, NULL };

	if (FAILED(D3DCompileFromFile(
		PathStr,
		d3dMacros.data(),
		SHADER_INCLUDE_HANDLER,
		SHADER_ENTRY_POINT_LOOKUP.at(type),
		SHADER_COMPILER_VERSION_LOOKUP.at(type),
		SHADER_COMPILE_FLAGS,
		0,
		&ref_pBob,
		&errorMessage)))
	{

		errMsg = GetCompileError(errorMessage, pathToFile);
		return false;
	}
	return true;
}

ID3D10Blob * Shader::CompileFromCachedBinary(const std::string & cachedBinaryFilePath)
{
	std::ifstream cache(cachedBinaryFilePath, std::ios::in | std::ios::binary | std::ios::ate);
	const size_t shaderBinarySize = cache.tellg();
	void* pBuffer = calloc(1, shaderBinarySize);
	cache.seekg(0);
	cache.read(reinterpret_cast<char*>(pBuffer), shaderBinarySize);
	cache.close();

	ID3D10Blob* pBlob = { nullptr };
	D3DCreateBlob(shaderBinarySize, &pBlob);
	memcpy(pBlob->GetBufferPointer(), pBuffer, shaderBinarySize);
	free(pBuffer);

	return pBlob;
}

void Shader::CacheShaderBinary(const std::string& shaderCacheFileName, ID3D10Blob * pCompiledBinary)
{
	const size_t shaderBinarySize = pCompiledBinary->GetBufferSize();

	char* pBuffer = reinterpret_cast<char*>(pCompiledBinary->GetBufferPointer());
	std::ofstream cache(shaderCacheFileName, std::ios::out | std::ios::binary);
	cache.write(pBuffer, shaderBinarySize);
	cache.close();
}

EShaderStage Shader::GetShaderTypeFromSourceFilePath(const std::string & shaderFilePath)
{
	const std::string sourceFileName = DirectoryUtil::GetFileNameWithoutExtension(shaderFilePath);
	const std::string shaderTypeStr = { *(sourceFileName.rbegin() + 1), *sourceFileName.rbegin() };
	return s_ShaderTypeStrLookup.at(shaderTypeStr);
}


//-------------------------------------------------------------------------------------------------------------
// PUBLIC INTERFACE
//-------------------------------------------------------------------------------------------------------------
const std::vector<Shader::ConstantBufferLayout>& Shader::GetConstantBufferLayouts() const { return m_CBLayouts; }
const std::vector<ConstantBufferBinding>& Shader::GetConstantBuffers() const { return mConstantBuffers; }
const TextureBinding& Shader::GetTextureBinding(const std::string& textureName) const { return mTextureBindings[mShaderTextureLookup.at(textureName)]; }
const SamplerBinding& Shader::GetSamplerBinding(const std::string& samplerName) const { return mSamplerBindings[mShaderSamplerLookup.at(samplerName)]; }
bool Shader::HasTextureBinding(const std::string& textureName) const { return mShaderTextureLookup.find(textureName) != mShaderTextureLookup.end(); }
bool Shader::HasSamplerBinding(const std::string& samplerName) const { return mShaderSamplerLookup.find(samplerName) != mShaderSamplerLookup.end(); }

Shader::Shader(const std::string& shaderFileName)
	: mName(shaderFileName)
	, mID(-1)
{}

Shader::Shader(const ShaderDesc& desc)
	: mName(desc.shaderName)
	, mDescriptor(desc)
	, mID(-1)
{}

Shader::~Shader(void)
{
#if _DEBUG 
	//Log::Info("Shader dtor: %s", m_name.c_str());
#endif

	// todo: this really could use smart pointers...

	// release constants
	ReleaseResources();
}
void Shader::ReleaseResources()
{
	for (ConstantBufferBinding& cbuf : mConstantBuffers)
	{
		if (cbuf.data)
		{
			cbuf.data->Release();
			cbuf.data = nullptr;
		}
	}
	mConstantBuffers.clear();

	for (CPUConstant cbuf : mCPUConstantBuffers)
	{
		if (cbuf._data)
		{
			delete cbuf._data;
			cbuf._data = nullptr;
		}
	}
	mCPUConstantBuffers.clear();


	if (mpInputLayout)
	{
		mpInputLayout->Release();
		mpInputLayout = nullptr;
	}

	if (mStages.mPixelShader)
	{
		mStages.mPixelShader->Release();
		mStages.mPixelShader = nullptr;
	}

	if (mStages.mVertexShader)
	{
		mStages.mVertexShader->Release();
		mStages.mVertexShader = nullptr;
	}

	if (mStages.mComputeShader)
	{
		mStages.mComputeShader->Release();
		mStages.mComputeShader = nullptr;
	}

	if (mStages.mGeometryShader)
	{
		mStages.mGeometryShader->Release();
		mStages.mGeometryShader = nullptr;
	}

	for (unsigned type = EShaderStage::VS; type < EShaderStage::COUNT; ++type)
	{
		if (mReflections.of[type])
		{
			mReflections.of[type]->Release();
			mReflections.of[type] = nullptr;
		}
	}

	m_CBLayouts.clear();
	m_constants.clear();
	mTextureBindings.clear();
	mSamplerBindings.clear();
	mShaderTextureLookup.clear();
	mShaderSamplerLookup.clear();
}




size_t Shader::GeneratePreprocessorDefinitionsHash(const std::vector<ShaderMacro>& macros) const
{
	if (macros.empty()) return 0;
	std::string concatenatedMacros;
	for (const ShaderMacro& macro : macros)
		concatenatedMacros += macro.name + macro.value;
	return std::hash<std::string>()(concatenatedMacros);
}

bool Shader::Reload(ID3D11Device* device)
{
	Shader copy(this->mDescriptor);
	copy.mID = this->mID;
	ReleaseResources();
	this->mID = copy.mID;
	return CompileShaders(device, copy.mDescriptor);
}

bool Shader::HasSourceFileBeenUpdated() const
{
	bool bUpdated = false;
	for (EShaderStage stage = EShaderStage::VS; stage < EShaderStage::COUNT; stage = (EShaderStage)(stage + 1))
	{
		if (mDirectories.find(stage) != mDirectories.end())
		{
			const std::string& path = mDirectories.at(stage).fullPath;
			const std::string& cachePath = mDirectories.at(stage).cachePath;
			bUpdated |= mDirectories.at(stage).lastWriteTime < std::experimental::filesystem::last_write_time(path);
			
			if (!bUpdated) // check include files only when source is not updated
			{
				bUpdated |= AreIncludesDirty(path, cachePath);
			}
		}
	}
	return bUpdated;
}

void Shader::ClearConstantBuffers()
{
	for (ConstantBufferBinding& cBuffer : mConstantBuffers)
	{
		cBuffer.dirty = true;
	}
}

void Shader::UpdateConstants(ID3D11DeviceContext* context)
{
	for (unsigned i = 0; i < mConstantBuffers.size(); ++i)
	{
		ConstantBufferBinding& CB = mConstantBuffers[i];
		if (CB.dirty)	// if the CPU-side buffer is updated
		{
			ID3D11Buffer* data = CB.data;
			D3D11_MAPPED_SUBRESOURCE mappedResource;

			// Map sub-resource to GPU - update contents - discard the sub-resource
			context->Map(data, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			char* bufferPos = static_cast<char*>(mappedResource.pData);	// char* so we can advance the pointer
			for (const ConstantBufferMapping& indexIDPair : m_constants)
			{
				if (indexIDPair.first != i)
				{
					continue;
				}

				const int slotIndex = indexIDPair.first;
				const CPUConstantID c_id = indexIDPair.second;
				assert(c_id < mCPUConstantBuffers.size());
				CPUConstant& c = mCPUConstantBuffers[c_id];
				memcpy(bufferPos, c._data, c._size);
				bufferPos += c._size;
			}
			context->Unmap(data, 0);

			// TODO: research update sub-resource (Setting constant buffer can be done once in setting the shader)

			// call XSSetConstantBuffers() from array using ShaderType enum
			(context->*SetShaderConstants[CB.shaderStage])(CB.bufferSlot, 1, &data);
			CB.dirty = false;
		}
	}
}




//-------------------------------------------------------------------------------------------------------------
// UTILITY FUNCTIONS
//-------------------------------------------------------------------------------------------------------------
bool Shader::CompileShaders(ID3D11Device* device, const ShaderDesc& desc)
{
	constexpr const char * SHADER_BINARY_EXTENSION = ".bin";
	mDescriptor = desc;
	HRESULT result;
	ShaderBlobs blobs;
	bool bPrinted = false;

	PerfTimer timer;
	timer.Start();

	// COMPILE SHADER STAGES
	//----------------------------------------------------------------------------
	for (const ShaderStageDesc& stageDesc : desc.stages)
	{
		if (stageDesc.fileName.empty())
			continue;

		// stage.macros
		const std::string sourceFilePath = std::string(Renderer::sShaderRoot + stageDesc.fileName);
		
		const EShaderStage stage = GetShaderTypeFromSourceFilePath(sourceFilePath);

		// USE SHADER CACHE
		//
		const size_t ShaderHash = GeneratePreprocessorDefinitionsHash(stageDesc.macros);
		const std::string cacheFileName = stageDesc.macros.empty()
			? DirectoryUtil::GetFileNameFromPath(sourceFilePath) + SHADER_BINARY_EXTENSION
			: DirectoryUtil::GetFileNameFromPath(sourceFilePath) + "_" + std::to_string(ShaderHash) + SHADER_BINARY_EXTENSION;
		const std::string cacheFilePath = Application::s_ShaderCacheDirectory + "\\" + cacheFileName;
		const bool bUseCachedShaders =
			DirectoryUtil::FileExists(cacheFilePath)
			&& !IsCacheDirty(sourceFilePath, cacheFilePath);
		//---------------------------------------------------------------------------------
		if (!bPrinted)	// quick status print here
		{
			const char* pMsgLoad = bUseCachedShaders ? "Loading cached shader binaries" : "Compiling shader from source";
			Log::Info("\t%s %s...", pMsgLoad, mName.c_str());
			bPrinted = true;
		}
		//---------------------------------------------------------------------------------
		if (bUseCachedShaders)
		{
			blobs.of[stage] = CompileFromCachedBinary(cacheFilePath);
		}
		else
		{
			std::string errMsg;
			ID3D10Blob* pBlob;
			if (CompileFromSource(sourceFilePath, stage, pBlob, errMsg, stageDesc.macros))
			{
				blobs.of[stage] = pBlob;
				CacheShaderBinary(cacheFilePath, blobs.of[stage]);
			}
			else
			{
				Log::Error(errMsg);
				return false;
			}
		}

		CreateShaderStage(device, stage, blobs.of[stage]->GetBufferPointer(), blobs.of[stage]->GetBufferSize());
		SetReflections(blobs);
		//CheckSignatures();

		mDirectories[stage] = ShaderLoadDesc(sourceFilePath, cacheFilePath);
	}

	// INPUT LAYOUT (VS)
	//---------------------------------------------------------------------------
	// src: https://stackoverflow.com/questions/42388979/directx-11-vertex-shader-reflection
	// setup the layout of the data that goes into the shader
	//
	if(mReflections.vsRefl)
	{

		D3D11_SHADER_DESC shaderDesc = {};
		mReflections.vsRefl->GetDesc(&shaderDesc);
		std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayout(shaderDesc.InputParameters);

		D3D_PRIMITIVE primitiveDesc = shaderDesc.InputPrimitive;

		for (unsigned i = 0; i < shaderDesc.InputParameters; ++i)
		{
			D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
			mReflections.vsRefl->GetInputParameterDesc(i, &paramDesc);

			// fill out input element desc
			D3D11_INPUT_ELEMENT_DESC elementDesc;
			elementDesc.SemanticName = paramDesc.SemanticName;
			elementDesc.SemanticIndex = paramDesc.SemanticIndex;
			elementDesc.InputSlot = 0;
			elementDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
			elementDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
			elementDesc.InstanceDataStepRate = 0;

			// determine DXGI format
			if (paramDesc.Mask == 1)
			{
				if      (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32_UINT;
				else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32_SINT;
				else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32_FLOAT;
			}
			else if (paramDesc.Mask <= 3)
			{
				if      (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32_UINT;
				else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32_SINT;
				else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
			}
			else if (paramDesc.Mask <= 7)
			{
				if      (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32_UINT;
				else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32_SINT;
				else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
			}
			else if (paramDesc.Mask <= 15)
			{
				if      (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
				else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32A32_SINT;
				else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			}

			inputLayout[i] = elementDesc; //save element desc
		}

		// Try to create Input Layout
		const auto* pData = inputLayout.data();
		if (pData)
		{
			result = device->CreateInputLayout(
				pData,
				shaderDesc.InputParameters,
				blobs.vs->GetBufferPointer(),
				blobs.vs->GetBufferSize(),
				&mpInputLayout);

			if (FAILED(result))
			{
				OutputDebugString("Error creating input layout");
				return false;
			}
		}
	}

	// CONSTANT BUFFERS 
	//---------------------------------------------------------------------------
	// Obtain cbuffer layout information
	for (EShaderStage type = EShaderStage::VS; type < EShaderStage::COUNT; type = (EShaderStage)(type + 1))
	{
		if (mReflections.of[type])
		{
			ReflectConstantBufferLayouts(mReflections.of[type], type);
		}
	}

	// Create CPU & GPU constant buffers
	// CPU CBuffers
	int constantBufferSlot = 0;
	for (const ConstantBufferLayout& cbLayout : m_CBLayouts)
	{
		std::vector<CPUConstantID> cpuBuffers;
		for (D3D11_SHADER_VARIABLE_DESC varDesc : cbLayout.variables)
		{
			CPUConstant c;
			CPUConstantID c_id = static_cast<CPUConstantID>(mCPUConstantBuffers.size());

			c._name = varDesc.Name;
			c._size = varDesc.Size;
			c._data = new char[c._size];
			memset(c._data, 0, c._size);
			m_constants.push_back(std::make_pair(constantBufferSlot, c_id));
			mCPUConstantBuffers.push_back(c);
		}
		++constantBufferSlot;
	}

	// GPU CBuffers
	D3D11_BUFFER_DESC cBufferDesc;
	cBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	cBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cBufferDesc.MiscFlags = 0;
	cBufferDesc.StructureByteStride = 0;
	for (const ConstantBufferLayout& cbLayout : m_CBLayouts)
	{
		ConstantBufferBinding cBuffer;
		cBufferDesc.ByteWidth = cbLayout.desc.Size;
		if (FAILED(device->CreateBuffer(&cBufferDesc, NULL, &cBuffer.data)))
		{
			OutputDebugString("Error creating constant buffer");
			return false;
		}
		cBuffer.dirty = true;
		cBuffer.shaderStage = cbLayout.stage;
		cBuffer.bufferSlot = cbLayout.bufSlot;
		mConstantBuffers.push_back(cBuffer);
	}


	// TEXTURES & SAMPLERS
	//---------------------------------------------------------------------------
	for (int shaderStage = 0; shaderStage < EShaderStage::COUNT; ++shaderStage)
	{
		unsigned texSlot = 0;	unsigned smpSlot = 0;
		unsigned uavSlot = 0;
		auto& sRefl = mReflections.of[shaderStage];
		if (sRefl)
		{
			D3D11_SHADER_DESC desc = {};
			sRefl->GetDesc(&desc);

			for (unsigned i = 0; i < desc.BoundResources; ++i)
			{
				D3D11_SHADER_INPUT_BIND_DESC shdInpDesc;
				sRefl->GetResourceBindingDesc(i, &shdInpDesc);

				switch (shdInpDesc.Type)
				{
					case D3D_SIT_SAMPLER:
					{
						SamplerBinding smp;
						smp.shaderStage = static_cast<EShaderStage>(shaderStage);
						smp.samplerSlot = smpSlot++;
						mSamplerBindings.push_back(smp);
						mShaderSamplerLookup[shdInpDesc.Name] = static_cast<int>(mSamplerBindings.size() - 1);
					} break;

					case D3D_SIT_TEXTURE:
					{
						TextureBinding tex;
						tex.shaderStage = static_cast<EShaderStage>(shaderStage);
						tex.textureSlot = texSlot++;
						mTextureBindings.push_back(tex);
						mShaderTextureLookup[shdInpDesc.Name] = static_cast<int>(mTextureBindings.size() - 1);
					} break;

					case D3D_SIT_UAV_RWTYPED:
					{
						TextureBinding tex;
						tex.shaderStage = static_cast<EShaderStage>(shaderStage);
						tex.textureSlot = uavSlot++;
						mTextureBindings.push_back(tex);
						mShaderTextureLookup[shdInpDesc.Name] = static_cast<int>(mTextureBindings.size() - 1);
					} break;

					case D3D_SIT_CBUFFER: break;


					default:
						Log::Warning("Unhandled shader input bind type in shader reflection");
						break;

				} // switch shader input type
			} // bound resource
		} // sRefl
	} // shaderStage

	// release blobs
	for (unsigned type = EShaderStage::VS; type < EShaderStage::COUNT; ++type)
	{
		if (blobs.of[type])
			blobs.of[type]->Release();
	}

	return true;
}

void Shader::CreateShaderStage(ID3D11Device* pDevice, EShaderStage stage, void* pBuffer, const size_t szShaderBinary)
{
	HRESULT result = {};
	const char* msg = "";
	switch (stage)
	{
	case EShaderStage::VS:
		if (FAILED(pDevice->CreateVertexShader(pBuffer, szShaderBinary, NULL, &mStages.mVertexShader)))
		{
			msg = "Error creating vertex shader program";
		}
		break;
	case EShaderStage::PS:
		if (FAILED(pDevice->CreatePixelShader(pBuffer, szShaderBinary, NULL, &mStages.mPixelShader)))
		{
			msg = "Error creating pixel shader program";
		}
		break;
	case EShaderStage::GS:
		if (FAILED(pDevice->CreateGeometryShader(pBuffer, szShaderBinary, NULL, &mStages.mGeometryShader)))
		{
			msg = "Error creating pixel geometry program";
		}
		break;
	case EShaderStage::CS:
		if (FAILED(pDevice->CreateComputeShader(pBuffer, szShaderBinary, NULL, &mStages.mComputeShader)))
		{
			msg = "Error creating compute shader program";
		}
		break;
	}

	if (FAILED(result))
	{
		OutputDebugString(msg);
		assert(false);
	}
}

void Shader::SetReflections(const ShaderBlobs& blobs)
{
	for(unsigned type = EShaderStage::VS; type < EShaderStage::COUNT; ++type)
	{
		if (blobs.of[type])
		{
			void** ppBuffer = reinterpret_cast<void**>(&this->mReflections.of[type]);
			if (FAILED(D3DReflect(blobs.of[type]->GetBufferPointer(), blobs.of[type]->GetBufferSize(), IID_ID3D11ShaderReflection, ppBuffer)))
			{
				Log::Error("Cannot get vertex shader reflection");
				assert(false);
			}
		}
	}
}

void Shader::CheckSignatures()
{
#if 0
	// get shader description --> input/output parameters
	std::vector<D3D11_SIGNATURE_PARAMETER_DESC> VSISignDescs, VSOSignDescs, PSISignDescs, PSOSignDescs;
	D3D11_SHADER_DESC VSDesc;
	m_vsRefl->GetDesc(&VSDesc);
	for (unsigned i = 0; i < VSDesc.InputParameters; ++i)
	{
		D3D11_SIGNATURE_PARAMETER_DESC input_desc;
		m_vsRefl->GetInputParameterDesc(i, &input_desc);
		VSISignDescs.push_back(input_desc);
	}

	for (unsigned i = 0; i < VSDesc.OutputParameters; ++i)
	{
		D3D11_SIGNATURE_PARAMETER_DESC output_desc;
		m_vsRefl->GetInputParameterDesc(i, &output_desc);
		VSOSignDescs.push_back(output_desc);
	}


	D3D11_SHADER_DESC PSDesc;
	m_psRefl->GetDesc(&PSDesc);
	for (unsigned i = 0; i < PSDesc.InputParameters; ++i)
	{
		D3D11_SIGNATURE_PARAMETER_DESC input_desc;
		m_psRefl->GetInputParameterDesc(i, &input_desc);
		PSISignDescs.push_back(input_desc);
	}

	for (unsigned i = 0; i < PSDesc.OutputParameters; ++i)
	{
		D3D11_SIGNATURE_PARAMETER_DESC output_desc;
		m_psRefl->GetInputParameterDesc(i, &output_desc);
		PSOSignDescs.push_back(output_desc);
	}

	// check VS-PS signature compatibility | wont be necessary when its 1 file.
	// THIS IS TEMPORARY
	if (VSOSignDescs.size() != PSISignDescs.size())
	{
		OutputDebugString("Error: Incompatible shader input/output signatures (sizes don't match)\n");
		assert(false);
	}
	else
	{
		for (size_t i = 0; i < VSOSignDescs.size(); ++i)
		{
			// TODO: order matters, semantic slot doesn't. check order
			;
		}
	}
#endif
	assert(false); // todo: refactor this
}


void Shader::LogConstantBufferLayouts() const
{
	char inputTable[2048];
	sprintf_s(inputTable, "\n%s ConstantBuffers: -----\n", this->mName.c_str());
	std::for_each(m_constants.begin(), m_constants.end(), [&](const ConstantBufferMapping& cMapping) {
		char entry[32];
		sprintf_s(entry, "(%d, %d)\t- %s\n", cMapping.first, cMapping.second, mCPUConstantBuffers[cMapping.second]._name.c_str());
		strcat_s(inputTable, entry);
	});
	strcat_s(inputTable, "-----\n");
	Log::Info(std::string(inputTable));
}

void Shader::ReflectConstantBufferLayouts(ID3D11ShaderReflection* sRefl, EShaderStage type)
{
	D3D11_SHADER_DESC desc;
	sRefl->GetDesc(&desc);

	unsigned bufSlot = 0;
	for (unsigned i = 0; i < desc.ConstantBuffers; ++i)
	{
		ConstantBufferLayout bufferLayout;
		bufferLayout.buffSize = 0;
		ID3D11ShaderReflectionConstantBuffer* pCBuffer = sRefl->GetConstantBufferByIndex(i);
		pCBuffer->GetDesc(&bufferLayout.desc);

		// load desc of each variable for binding on buffer later on
		for (unsigned j = 0; j < bufferLayout.desc.Variables; ++j)
		{
			// get variable and type descriptions
			ID3D11ShaderReflectionVariable* pVariable = pCBuffer->GetVariableByIndex(j);
			D3D11_SHADER_VARIABLE_DESC varDesc;
			pVariable->GetDesc(&varDesc);
			bufferLayout.variables.push_back(varDesc);

			ID3D11ShaderReflectionType* pType = pVariable->GetType();
			D3D11_SHADER_TYPE_DESC typeDesc;
			pType->GetDesc(&typeDesc);
			bufferLayout.types.push_back(typeDesc);

			// accumulate buffer size
			bufferLayout.buffSize += varDesc.Size;
		}
		bufferLayout.stage = type;
		bufferLayout.bufSlot = bufSlot;
		++bufSlot;
		m_CBLayouts.push_back(bufferLayout);
	}
}



std::array<ShaderStageDesc, EShaderStageFlags::SHADER_STAGE_COUNT> ShaderDesc::CreateStageDescsFromShaderName(const char* pShaderName, unsigned flagStages)
{
	const std::string shaderName = pShaderName;
	std::array<ShaderStageDesc, EShaderStageFlags::SHADER_STAGE_COUNT> descs;
	int idx = 0;
	if (flagStages & SHADER_STAGE_VS)
	{
		descs[idx++] = ShaderStageDesc{ shaderName + "_vs.hlsl", {} };
	}
	if (flagStages & SHADER_STAGE_GS)
	{
		descs[idx++] = ShaderStageDesc{ shaderName + "_gs.hlsl", {} };
	}
	if (flagStages & SHADER_STAGE_DS)
	{
		descs[idx++] = ShaderStageDesc{ shaderName + "_ds.hlsl", {} };
	}
	if (flagStages & SHADER_STAGE_HS)
	{
		descs[idx++] = ShaderStageDesc{ shaderName + "_hs.hlsl", {} };
	}
	if (flagStages & SHADER_STAGE_PS)
	{
		descs[idx++] = ShaderStageDesc{ shaderName + "_ps.hlsl", {} };
	}
	if (flagStages & SHADER_STAGE_CS)
	{
		descs[idx++] = ShaderStageDesc{ shaderName + "_cs.hlsl", {} };
	}
	return descs;
}
#endif