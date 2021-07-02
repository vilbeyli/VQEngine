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

#include "../Engine/Core/Types.h"

#include <atlbase.h>
#include <d3dcompiler.h>
#include <d3d12.h>

#include "../../Libs/DirectXCompiler/inc/dxcapi.h"

#include <string>
#include <array>
#include <tuple>
#include <vector>
#include <stack>
#include <unordered_map>
#include <filesystem>

struct ID3D12Device;

using FileTimeStamp = std::filesystem::file_time_type;

//using namespace;

//
// HELPER STRUCTS/ENUMS
//
enum EShaderStageFlags : unsigned
{
	SHADER_STAGE_NONE         = 0x00000000,
	SHADER_STAGE_VS           = 0x00000001,
	SHADER_STAGE_GS           = 0x00000002,
	SHADER_STAGE_DS           = 0x00000004,
	SHADER_STAGE_HS           = 0x00000008,
	SHADER_STAGE_PS           = 0x00000010,
	SHADER_STAGE_ALL_GRAPHICS = 0X0000001F,
	SHADER_STAGE_CS           = 0x00000020,

	SHADER_STAGE_COUNT = 6
};
enum EShaderStage : unsigned // use this enum for array indexing
{	
	VS = 0,
	GS,
	DS,
	HS,
	PS,
	CS,

	NUM_SHADER_STAGES // =6
	, UNINITIALIZED = NUM_SHADER_STAGES
};

struct FShaderMacro
{
	std::string Name;
	std::string Value;
};

struct FShaderStageCompileDesc;


//
// SHADER
//
class Shader
{
	friend class Renderer;

public:
	struct FBlob
	{
		inline bool IsNull() const { return !pD3DBlob && !pBlobDxc; }
		const void* GetByteCode() const;
		size_t GetByteCodeSize() const;

		CComPtr<ID3DBlob> pD3DBlob = nullptr;
		CComPtr<IDxcBlob> pBlobDxc = nullptr;
	};
	union ShaderBlobs
	{
		struct
		{
			FBlob* vs;
			FBlob* gs;
			FBlob* ds;
			FBlob* hs;
			FBlob* ps;
			FBlob* cs;
		};
		FBlob* ShaderBlobs[EShaderStage::NUM_SHADER_STAGES];
	};
	union ShaderReflections
	{
		struct
		{
			ID3D12ShaderReflection* vsRefl;
			ID3D12ShaderReflection* gsRefl;
			ID3D12ShaderReflection* dsRefl;
			ID3D12ShaderReflection* hsRefl;
			ID3D12ShaderReflection* psRefl;
			ID3D12ShaderReflection* csRefl;
		};
		ID3D12ShaderReflection* Reflections[EShaderStage::NUM_SHADER_STAGES] = { nullptr };
	};
};

namespace ShaderUtils
{
	// Compiles shader from source file with the given file path, entry point, shader model & macro definitions
	//
	Shader::FBlob CompileFromSource(const FShaderStageCompileDesc& ShaderStageCompileDesc, std::string& OutErrorString);
	
	// Reads in cached shader binary from given @ShaderBinaryFilePath 
	//
	Shader::FBlob CompileFromCachedBinary(const std::string& ShaderBinaryFilePath);
	
	// Writes out compiled ID3DBlob into @ShaderBinaryFilePath
	//
	void CacheShaderBinary(const std::string& ShaderBinaryFilePath, size_t ShaderBinarySize, const void* pShaderBinary);

	// Concatenates given FShaderMacros and generates a hash from the resulting string
	//
	size_t GeneratePreprocessorDefinitionsHash(const std::vector<FShaderMacro>& Macros);

	std::string  GetCompileError(ID3DBlob*& errorMessage, const std::string& shdPath);
	std::string  GetIncludeFileName(const std::string& line);
	bool         AreIncludesDirty(const std::string& srcPath, const std::string& cachePath);
	bool         IsCacheDirty(const std::string& sourcePath, const std::string& cachePath);

	std::vector<D3D12_INPUT_ELEMENT_DESC> ReflectInputLayoutFromVS(ID3D12ShaderReflection* pReflection);

	EShaderStage GetShaderStageEnumFromShaderModel(const std::string& ShaderModel);
}


struct FShaderStageCompileDesc
{
	std::wstring FilePath;
	std::string EntryPoint;
	std::string ShaderModel;
	std::vector<FShaderMacro> Macros;
};
struct FShaderStageCompileResult
{
	Shader::FBlob ShaderBlob;
	EShaderStage ShaderStageEnum;
};
