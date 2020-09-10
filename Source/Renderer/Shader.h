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

#pragma once

#include "../Application/Types.h"

#include <d3dcompiler.h>

#include <string>
#include <array>
#include <tuple>
#include <vector>
#include <stack>
#include <unordered_map>


enum EShaderStageFlags : unsigned
{
	SHADER_STAGE_NONE = 0x00000000,
	SHADER_STAGE_VS = 0x00000001,
	SHADER_STAGE_GS = 0x00000002,
	SHADER_STAGE_DS = 0x00000004,
	SHADER_STAGE_HS = 0x00000008,
	SHADER_STAGE_PS = 0x00000010,
	SHADER_STAGE_ALL_GRAPHICS = 0X0000001F,
	SHADER_STAGE_CS = 0x00000020,

	SHADER_STAGE_COUNT = 6
};
enum EShaderStage : unsigned	// array-index enum mapping
{	// used to map **SetShaderConstant(); function in Renderer::Apply()
	VS = 0,
	GS,
	DS,
	HS,
	PS,
	CS,

	NUM_SHADER_STAGES
};

#if 0
enum ELayoutFormat
{
	FLOAT32_2 = DXGI_FORMAT_R32G32_FLOAT,
	FLOAT32_3 = DXGI_FORMAT_R32G32B32_FLOAT,
	FLOAT32_4 = DXGI_FORMAT_R32G32B32A32_FLOAT,

	LAYOUT_FORMAT_COUNT
};
using CPUConstantID = int;
using GPU_ConstantBufferSlotIndex = int;
using ConstantBufferMapping = std::pair<GPU_ConstantBufferSlotIndex, CPUConstantID>;
//using FileTimeStamp = std::experimental::filesystem::file_time_type;

//----------------------------------------------------------------------------------------------------------------
// SHADER DATA/RESOURCE INTERFACE STRUCTS
//----------------------------------------------------------------------------------------------------------------
struct ConstantBufferBinding
{	
	EShaderStage  shaderStage;
	unsigned      bufferSlot;
	//ID3D12Buffer* data;
	//bool          dirty;
};
struct TextureBinding
{
	EShaderStage shaderStage;
	unsigned     textureSlot;
};
struct SamplerBinding
{
	EShaderStage shaderStage;
	unsigned     samplerSlot;
	std::string  name;	// TODO: move this out
};
struct InputLayout
{
	std::string		semanticName;
	ELayoutFormat	format;
};


struct ShaderMacro
{
	std::string name;
	std::string value;
};

struct ShaderStageDesc
{
	std::string fileName;
	std::vector<ShaderMacro> macros;
};

struct ShaderDesc
{
	using ShaderStageArr = std::array<ShaderStageDesc, EShaderStageFlags::SHADER_STAGE_COUNT>;
	static ShaderStageArr CreateStageDescsFromShaderName(const char* shaderName, unsigned flagStages);

	std::string shaderName;
	std::array<ShaderStageDesc, EShaderStage::NUM_SHADER_STAGES> stages;
};

struct ShaderLoadDesc
{
	ShaderLoadDesc() = default;
	ShaderLoadDesc(const std::string& path, const std::string& cachePath_);
	//	: fullPath(path), cachePath(cachePath_)
	//{
	//	this->lastWriteTime = std::experimental::filesystem::last_write_time(fullPath);
	//	this->cacheLastWriteTime = std::experimental::filesystem::last_write_time(cachePath);
	//}
	std::string fullPath;
	std::string cachePath;
	FileTimeStamp lastWriteTime;
	FileTimeStamp cacheLastWriteTime;
};


class Shader
{
	friend class Renderer;

	using ShaderArray = std::array<ShaderID, 0>;
	using ShaderTextureLookup = std::unordered_map<std::string, int>;
	using ShaderSamplerLookup = std::unordered_map<std::string, int>;
	using ShaderDirectoryLookup = std::unordered_map<EShaderStage, ShaderLoadDesc>;

public:
	// STRUCTS/ENUMS
	//
	// Current limitations for Constant Buffers: 
	//  - cbuffers with same names in different shaders (PS/VS/GS/...)
	//  - cbuffers with same names in the same shader (not tested)
	//----------------------------------------------------------------------------------------------------------------
	union ShaderBlobs
	{
		struct 
		{
			ID3D10Blob* vs;
			ID3D10Blob* gs;
			ID3D10Blob* ds;
			ID3D10Blob* hs;
			ID3D10Blob* ps;
			ID3D10Blob* cs;
		};
		ID3D10Blob* of[EShaderStage::NUM_SHADER_STAGES] = { nullptr };
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
		ID3D12ShaderReflection* of[EShaderStage::NUM_SHADER_STAGES] = { nullptr };
	};
	struct ConstantBufferLayout
	{	// information used to create GPU/CPU constant buffers
		D3D11_SHADER_BUFFER_DESC					desc;
		std::vector<D3D11_SHADER_VARIABLE_DESC>		variables;
		std::vector<D3D11_SHADER_TYPE_DESC>			types;
		unsigned									buffSize;
		EShaderStage								stage;
		unsigned									bufSlot;
	};
	struct ShaderStages
	{
		ID3D12VertexShader*    mVertexShader   = nullptr;
		ID3D12PixelShader*     mPixelShader    = nullptr;
		ID3D12GeometryShader*  mGeometryShader = nullptr;
		ID3D12HullShader*      mHullShader     = nullptr;
		ID3D12DomainShader*    mDomainShader   = nullptr;
		ID3D12ComputeShader*   mComputeShader  = nullptr;
	};

public:
	//----------------------------------------------------------------------------------------------------------------
	// MEMBER INTERFACE
	//----------------------------------------------------------------------------------------------------------------
	Shader(const ShaderDesc& desc);
	Shader(const std::string& shaderFileName);
	~Shader();

	bool Reload(ID3D12Device* device);
	void ClearConstantBuffers();
	//void UpdateConstants(ID3D12DeviceContext* context);

	//----------------------------------------------------------------------------------------------------------------
	// GETTERS
	//----------------------------------------------------------------------------------------------------------------
	const std::string& Name() const { return mName; }
	inline ShaderID    ID()   const { return mID; }
	
	const std::vector<ConstantBufferLayout>& GetConstantBufferLayouts() const;
	const std::vector<ConstantBufferBinding      >& GetConstantBuffers() const;
	
	const TextureBinding& GetTextureBinding(const std::string& textureName) const;
	const SamplerBinding& GetSamplerBinding(const std::string& samplerName) const;
	bool HasTextureBinding(const std::string& textureName) const;
	bool HasSamplerBinding(const std::string& samplerName) const;

	bool HasSourceFileBeenUpdated() const;

private:
	//----------------------------------------------------------------------------------------------------------------
	// STATIC PRIVATE INTERFACE
	//----------------------------------------------------------------------------------------------------------------
	// Compiles shader from source file with the given shader macros
	//
	static bool CompileFromSource(
		  const std::string&              pathToFile
		, const EShaderStage&             type
		, ID3D10Blob *&                   ref_pBob
		, std::string&                    outErrMsg
		, const std::vector<ShaderMacro>& macros);
	
	// Reads in cached binary from %APPDATA%/VQEngine/ShaderCache folder into ID3D10Blob 
	//
	static ID3D10Blob * CompileFromCachedBinary(const std::string& cachedBinaryFilePath);

	// Writes out compiled ID3D10Blob into %APPDATA%/VQEngine/ShaderCache folder
	//
	static void			CacheShaderBinary(const std::string& shaderCacheFileName, ID3D10Blob * pCompiledBinary);

	// example filePath: "rootPath/filename_vs.hlsl"
	//                                      ^^----- shaderTypeString
	static EShaderStage	GetShaderTypeFromSourceFilePath(const std::string& shaderFilePath);

	//----------------------------------------------------------------------------------------------------------------
	// UTILITY FUNCTIONS
	//----------------------------------------------------------------------------------------------------------------
	void ReflectConstantBufferLayouts(ID3D12ShaderReflection * sRefl, EShaderStage type);
	bool CompileShaders(ID3D12Device* device, const ShaderDesc& desc);
	void SetReflections(const ShaderBlobs& blobs);
	void CreateShaderStage(ID3D12Device* pDevice, EShaderStage stage, void* pBuffer, const size_t szShaderBinary);
	void CheckSignatures();
	void LogConstantBufferLayouts() const;
	void ReleaseResources();
	size_t GeneratePreprocessorDefinitionsHash(const std::vector<ShaderMacro>& macros) const;

private:
	//----------------------------------------------------------------------------------------------------------------
	// DATA
	//----------------------------------------------------------------------------------------------------------------
	ShaderID mID;
	ShaderStages mStages;

	ShaderReflections	mReflections;	// shader reflections, temporary?
	//ID3D12InputLayout*	mpInputLayout = nullptr;

	std::string	mName;

	std::vector<ConstantBufferBinding>	mConstantBuffers;	// https://msdn.microsoft.com/en-us/library/windows/desktop/bb509581(v=vs.85).aspx
	std::vector<ConstantBufferLayout>  m_CBLayouts;
	std::vector<ConstantBufferMapping> m_constants;// currently redundant
	//std::vector<CPUConstant> mCPUConstantBuffers;

	std::vector<TextureBinding> mTextureBindings;
	std::vector<SamplerBinding> mSamplerBindings;
	
	ShaderTextureLookup mShaderTextureLookup;
	ShaderSamplerLookup mShaderSamplerLookup;

	ShaderDesc mDescriptor;	// used for shader reloading
	ShaderDirectoryLookup mDirectories;
};
#endif