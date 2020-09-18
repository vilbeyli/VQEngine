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

#include "Shader.h"
#include "Renderer.h"

#include "../../Libs/VQUtils/Source/utils.h"
#include <fstream>

//-------------------------------------------------------------------------------------------------------------
// CONSTANTS & STATICS
//-------------------------------------------------------------------------------------------------------------

static const std::unordered_map<EShaderStage, const char*> DEFAULT_SHADER_ENTRY_POINT_LOOKUP =
{
	{ EShaderStage::VS, "VSMain" },
	{ EShaderStage::GS, "GSMain" },
	{ EShaderStage::DS, "DSMain" },
	{ EShaderStage::HS, "HSMain" },
	{ EShaderStage::PS, "PSMain" },
	{ EShaderStage::CS, "CSMain" },
};

ID3DInclude* const SHADER_INCLUDE_HANDLER = D3D_COMPILE_STANDARD_FILE_INCLUDE; // use default include handler for using #include in shader files

#if defined( _DEBUG ) || defined ( FORCE_DEBUG )
const UINT SHADER_COMPILE_FLAGS = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
const UINT SHADER_COMPILE_FLAGS = D3DCOMPILE_ENABLE_STRICTNESS;
#endif

static std::unordered_map <EShaderStage, std::string> SHADER_STAGE_STRING_LOOKUP =
{
	{EShaderStage::VS, "VS"},
	{EShaderStage::GS, "GS"},
	{EShaderStage::DS, "DS"},
	{EShaderStage::HS, "HS"},
	{EShaderStage::PS, "PS"},
	{EShaderStage::CS, "CS"}
};

//-------------------------------------------------------------------------------------------------------------
// SHADER UTILS
//-------------------------------------------------------------------------------------------------------------
namespace ShaderUtils
{
std::string GetCompileError(ID3DBlob*& errorMessage, const std::string& shdPath)
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

std::string GetIncludeFileName(const std::string& line)
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

bool AreIncludesDirty(const std::string& srcPath, const std::string& cachePath)
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
	if (!DirectoryUtil::FileExists(cachePath)) 
		return true;

	return DirectoryUtil::IsFileNewer(sourcePath, cachePath) || AreIncludesDirty(sourcePath, cachePath);
}

std::vector<D3D12_INPUT_ELEMENT_DESC> ReflectInputLayoutFromVS(ID3D12ShaderReflection* pReflection)
{
	D3D12_SHADER_DESC shaderDesc = {};
	pReflection->GetDesc(&shaderDesc);
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout(shaderDesc.InputParameters);

	D3D_PRIMITIVE primitiveDesc = shaderDesc.InputPrimitive;

	for (unsigned i = 0; i < shaderDesc.InputParameters; ++i)
	{
		D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
		pReflection->GetInputParameterDesc(i, &paramDesc);

		// fill out input element desc
		D3D12_INPUT_ELEMENT_DESC elementDesc;
		elementDesc.SemanticName = paramDesc.SemanticName;
		elementDesc.SemanticIndex = paramDesc.SemanticIndex;
		elementDesc.InputSlot = 0;
		elementDesc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
		elementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elementDesc.InstanceDataStepRate = 0;

		// determine DXGI format
		if (paramDesc.Mask == 1)
		{
			     if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32_FLOAT;
		}
		else if (paramDesc.Mask <= 3)
		{
			     if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		}
		else if (paramDesc.Mask <= 7)
		{
			     if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		}
		else if (paramDesc.Mask <= 15)
		{
			     if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32A32_SINT;
			else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		}

		inputLayout[i] = elementDesc; //save element desc
	}

	return inputLayout;
}


ID3DBlob* CompileFromSource(const FShaderStageCompileDesc& ShaderStageCompileDesc, std::string& OutErrorString)
{
	ID3DBlob* pShaderBlob = nullptr;
	const WCHAR* PathStr = ShaderStageCompileDesc.FilePath.data();
	ID3DBlob* pBlob_ErrMsg = nullptr;

	int i = 0;
	std::vector<D3D_SHADER_MACRO> d3dMacros(ShaderStageCompileDesc.Macros.size() + 1);
	std::for_each(RANGE(ShaderStageCompileDesc.Macros), [&](const FShaderMacro& macro)
	{
		d3dMacros[i++] = D3D_SHADER_MACRO({ macro.Name.c_str(), macro.Value.c_str() });
	});
	d3dMacros[i] = { NULL, NULL };

	Log::Info("Compiling Shader Source: %s [%s @ %s()]"
		, StrUtil::UnicodeToASCII<256>(ShaderStageCompileDesc.FilePath.c_str()).c_str()
		, SHADER_STAGE_STRING_LOOKUP.at(ShaderStageCompileDesc.ShaderStageEnum).c_str()
		, ShaderStageCompileDesc.EntryPoint.c_str()
	);
	if (FAILED(D3DCompileFromFile(
		PathStr,
		d3dMacros.data(),
		SHADER_INCLUDE_HANDLER,
		ShaderStageCompileDesc.EntryPoint.c_str(),
		ShaderStageCompileDesc.ShaderModel.c_str(),
		SHADER_COMPILE_FLAGS,
		0,
		&pShaderBlob,
		&pBlob_ErrMsg)))
	{
		OutErrorString = GetCompileError(pBlob_ErrMsg, StrUtil::UnicodeToASCII<512>(ShaderStageCompileDesc.FilePath.c_str()));
	}

	return pShaderBlob;
}


ID3DBlob* CompileFromCachedBinary(const std::string& ShaderBinaryFilePath)
{
	std::ifstream cache(ShaderBinaryFilePath, std::ios::in | std::ios::binary | std::ios::ate);
	const size_t shaderBinarySize = cache.tellg();
	void* pBuffer = calloc(1, shaderBinarySize);
	cache.seekg(0);
	cache.read(reinterpret_cast<char*>(pBuffer), shaderBinarySize);
	cache.close();

	ID3DBlob* pBlob = { nullptr };
	D3DCreateBlob(shaderBinarySize, &pBlob);
	memcpy(pBlob->GetBufferPointer(), pBuffer, shaderBinarySize);
	free(pBuffer);

	return pBlob;
}


void CacheShaderBinary(const std::string& ShaderBinaryFilePath, ID3DBlob* pCompiledBinary)
{
	const size_t shaderBinarySize = pCompiledBinary->GetBufferSize();

	char* pBuffer = reinterpret_cast<char*>(pCompiledBinary->GetBufferPointer());
	std::ofstream cache(ShaderBinaryFilePath, std::ios::out | std::ios::binary);
	cache.write(pBuffer, shaderBinarySize);
	cache.close();
}


size_t GeneratePreprocessorDefinitionsHash(const std::vector<FShaderMacro>& macros)
{
	if (macros.empty()) return 0;
	std::string concatenatedMacros;
	for (const FShaderMacro& macro : macros)
		concatenatedMacros += macro.Name + macro.Value;
	return std::hash<std::string>()(concatenatedMacros);
}


} // namespace ShaderUtils

