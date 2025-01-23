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

#include "Shader.h"
#include <vector>
#include <string>
#include <d3d12.h>

namespace ShaderUtils
{
	bool IsShaderSM5(const char* ShaderModelStr);
	bool IsShaderSM6(const char* ShaderModelStr);

	// Compiles shader from source file with the given file path, entry point, shader model & macro definitions
	//
	Shader::FBlob CompileFromSource(const FShaderStageCompileDesc& ShaderStageCompileDesc, std::string& OutErrorString);
	
	// Reads in cached shader binary from given @ShaderBinaryFilePath 
	//
	bool CompileFromCachedBinary(const std::string& ShaderBinaryFilePath, Shader::FBlob& Blob, bool bSM6, std::string& errMsg);
	
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
	bool bUseNative16bit = false;
	std::vector<std::wstring> DXCompilerFlags;
};
struct FShaderStageCompileResult
{
	Shader::FBlob ShaderBlob;
	EShaderStage ShaderStageEnum;
	std::wstring FilePath;
	bool bSM6;
};
