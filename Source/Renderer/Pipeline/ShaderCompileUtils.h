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

#include "stdafx.h"
#include "Shader.h"

struct FShaderStageCompileDesc;

struct ID3D12ShaderReflection;
struct IDxcBlob;
struct ID3D10Blob;

namespace ShaderUtils
{
	struct FBlob
	{
		bool IsNull() const;
		const void* GetByteCode() const;
		size_t GetByteCodeSize() const;

		Microsoft::WRL::ComPtr<ID3D10Blob> pD3DBlob = nullptr;
		Microsoft::WRL::ComPtr<IDxcBlob> pBlobDxc = nullptr;
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


	// Compiles shader from source file with the given file path, entry point, shader model & macro definitions
	//
	FBlob CompileFromSource(const FShaderStageCompileDesc& ShaderStageCompileDesc, std::string& OutErrorString);
	
	// Reads in cached shader binary from given @ShaderBinaryFilePath 
	//
	bool CompileFromCachedBinary(const std::string& ShaderBinaryFilePath, FBlob& Blob, bool bSM6, std::string& errMsg);
	
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


	inline const char* GetShaderModel_cstr(const EShaderModel& ShaderModel, const EShaderStage ShaderStage)
	{
		static const char* ShaderModelStrings[][EShaderStage::NUM_SHADER_STAGES] = {
			{ "vs_5_0", "gs_5_0", "ds_5_0", "hs_5_0", "ps_5_0", "cs_5_0" },
			{ "vs_6_0", "gs_6_0", "ds_6_0", "hs_6_0", "ps_6_0", "cs_6_0" },
			{ "vs_6_1", "gs_6_1", "ds_6_1", "hs_6_1", "ps_6_1", "cs_6_1" },
			{ "vs_6_2", "gs_6_2", "ds_6_2", "hs_6_2", "ps_6_2", "cs_6_2" },
			{ "vs_6_3", "gs_6_3", "ds_6_3", "hs_6_3", "ps_6_3", "cs_6_3" },
			{ "vs_6_4", "gs_6_4", "ds_6_4", "hs_6_4", "ps_6_4", "cs_6_4" },
			{ "vs_6_5", "gs_6_5", "ds_6_5", "hs_6_5", "ps_6_5", "cs_6_5" },
			{ "vs_6_6", "gs_6_6", "ds_6_6", "hs_6_6", "ps_6_6", "cs_6_6" },
			{ "vs_6_7", "gs_6_7", "ds_6_7", "hs_6_7", "ps_6_7", "cs_6_7" },
			{ "vs_6_8", "gs_6_8", "ds_6_8", "hs_6_8", "ps_6_8", "cs_6_8" },
		};
		if (static_cast<int>(ShaderStage) >= 0 &&
			static_cast<int>(ShaderStage) < EShaderStage::NUM_SHADER_STAGES &&
			static_cast<int>(ShaderModel) >= 0 &&
			static_cast<int>(ShaderModel) < EShaderModel::NUM_SHADER_MODELS)
		{
			return ShaderModelStrings[static_cast<int>(ShaderModel)][static_cast<int>(ShaderStage)];
		}
		return nullptr;
	}
	inline const wchar_t* GetShaderModel_wcstr(const EShaderModel& ShaderModel, const EShaderStage ShaderStage)
	{
		static const wchar_t* ShaderModelStrings[][EShaderStage::NUM_SHADER_STAGES] = {
			{ L"vs_5_0", L"gs_5_0", L"ds_5_0", L"hs_5_0", L"ps_5_0", L"cs_5_0" },
			{ L"vs_6_0", L"gs_6_0", L"ds_6_0", L"hs_6_0", L"ps_6_0", L"cs_6_0" },
			{ L"vs_6_1", L"gs_6_1", L"ds_6_1", L"hs_6_1", L"ps_6_1", L"cs_6_1" },
			{ L"vs_6_2", L"gs_6_2", L"ds_6_2", L"hs_6_2", L"ps_6_2", L"cs_6_2" },
			{ L"vs_6_3", L"gs_6_3", L"ds_6_3", L"hs_6_3", L"ps_6_3", L"cs_6_3" },
			{ L"vs_6_4", L"gs_6_4", L"ds_6_4", L"hs_6_4", L"ps_6_4", L"cs_6_4" },
			{ L"vs_6_5", L"gs_6_5", L"ds_6_5", L"hs_6_5", L"ps_6_5", L"cs_6_5" },
			{ L"vs_6_6", L"gs_6_6", L"ds_6_6", L"hs_6_6", L"ps_6_6", L"cs_6_6" },
			{ L"vs_6_7", L"gs_6_7", L"ds_6_7", L"hs_6_7", L"ps_6_7", L"cs_6_7" },
			{ L"vs_6_8", L"gs_6_8", L"ds_6_8", L"hs_6_8", L"ps_6_8", L"cs_6_8" },
		};
		if (static_cast<int>(ShaderStage) >= 0 &&
			static_cast<int>(ShaderStage) < EShaderStage::NUM_SHADER_STAGES &&
			static_cast<int>(ShaderModel) >= 0 &&
			static_cast<int>(ShaderModel) < EShaderModel::NUM_SHADER_MODELS)
		{
			return ShaderModelStrings[static_cast<int>(ShaderModel)][static_cast<int>(ShaderStage)];
		}
		return nullptr;
	}
}


struct FShaderStageCompileDesc
{
	std::wstring FilePath = L"";
	std::string EntryPoint = "";
	EShaderStage ShaderStage = EShaderStage::NUM_SHADER_STAGES;
	EShaderModel ShaderModel = EShaderModel::SM6_0;
	std::vector<FShaderMacro> Macros;
	bool bUseNative16bit = false;
	std::vector<std::wstring> DXCompilerFlags;
};
struct FShaderStageCompileResult
{
	ShaderUtils::FBlob ShaderBlob;
	EShaderStage ShaderStage;
	std::wstring FilePath;
	bool bSM6;
};
