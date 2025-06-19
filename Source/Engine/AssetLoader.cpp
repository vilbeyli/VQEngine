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

#include "AssetLoader.h"

#include "GPUMarker.h"

#include "Scene/Mesh.h"
#include "Scene/Material.h"
#include "Scene/Scene.h"

#include "../Renderer/Renderer.h"

#include "Libs/VQUtils/Include/utils.h"
#include "Libs/VQUtils/Include/Image.h"
#include "Libs/VQUtils/Include/Timer.h"
#include "Libs/VQUtils/Include/Log.h"

#define CGLTF_IMPLEMENTATION
#ifdef matrix
#undef matrix
#endif

#pragma warning (disable: 4996) // 'This function or variable may be unsafe': strcpy, strdup, sprintf, vsnprintf, sscanf, fopen
#include "Libs/cgltf/cgltf.h"

using namespace DirectX;

static ModelID ImportOBJ(Scene* pScene, AssetLoader* pAssetLoader, VQRenderer* pRenderer, const std::string& objFilePath, std::string ModelName)
{
	assert(false);
	return INVALID_ID;
}
static const std::unordered_map<std::string, AssetLoader::FModelLoadParams::pfnImportModel_t> ImportModelFunctions = 
{
	{ "gltf", AssetLoader::ImportGLTF },
	{ "obj",  ImportOBJ },
	// { "glb",  ImportGLB },
	// { "fbx",  ImportFBX },
	// add more formats here
};

TaskID AssetLoader::GenerateModelLoadTaskID()
{
	static std::atomic<TaskID> LOAD_TASK_ID = 0;
	TaskID id = LOAD_TASK_ID.fetch_add(1);
	return id;
}

AssetLoader::AssetLoader(ThreadPool& WorkerThreads_Model, ThreadPool& WorkerThreads_Mesh, VQRenderer& renderer)
	: mWorkers_ModelLoad(WorkerThreads_Model)
	, mWorkers_MeshLoad(WorkerThreads_Mesh)
	, mRenderer(renderer)
{}


//----------------------------------------------------------------------------------------------------------------
// MODEL LOADER
//----------------------------------------------------------------------------------------------------------------
void AssetLoader::QueueModelLoad(GameObject* pObject, const std::string& ModelPath, const std::string& ModelName)
{
	const std::string FileExtension = DirectoryUtil::GetFileExtension(ModelPath);
	{
		std::unique_lock<std::mutex> lk(mMtxQueue_ModelLoad);
		mModelLoadQueue.push({ pObject, ModelPath, ModelName, ImportModelFunctions.at(StrUtil::GetLowercased(FileExtension)) });
	}
}

AssetLoader::ModelLoadResults_t AssetLoader::StartLoadingModels(Scene* pScene)
{
	SCOPED_CPU_MARKER("AssetLoader::StartLoadingModels()");
	VQRenderer* pRenderer = &mRenderer;
	ModelLoadResults_t ModelLoadResults;

	if (mModelLoadQueue.empty())
	{
		Log::Warning("AssetLoader::StartLoadingModels(): no models to load");
		return ModelLoadResults;
	}

	// process model load queue
	std::unordered_map<std::string, std::shared_future<ModelID>> ModelLoadResultMap;
	std::unique_lock<std::mutex> lk(mMtxQueue_ModelLoad);
	do
	{
		SCOPED_CPU_MARKER("ProcessQueueItem");

		FModelLoadParams ModelLoadParams = std::move(mModelLoadQueue.front());
		const std::string& ModelPath = ModelLoadParams.ModelPath;
		mModelLoadQueue.pop();

		// queue unique model paths for loading
		std::shared_future<ModelID> modelLoadResult;
		if (mUniqueModelPaths.find(ModelPath) == mUniqueModelPaths.end())
		{
			mUniqueModelPaths.insert(ModelPath);

			// check whether Exit signal is given to the app before dispatching workers
			if (mWorkers_ModelLoad.IsExiting())
			{
				break;
			}

			// start loading the model
			modelLoadResult = std::move(mWorkers_ModelLoad.AddTask([=]()
			{
				SCOPED_CPU_MARKER_C("ModelLoadWorker", 0xFFDDAA00);
				return ModelLoadParams.pfnImportModel(pScene, this, pRenderer, ModelLoadParams.ModelPath, ModelLoadParams.ModelName);
			}));
			ModelLoadResultMap[ModelLoadParams.ModelPath] = modelLoadResult;
		}
		else
		{
			modelLoadResult = ModelLoadResultMap.at(ModelLoadParams.ModelPath);
		}

		ModelLoadResults.emplace(std::make_pair(ModelLoadParams.pObject, modelLoadResult));

	} while (!mModelLoadQueue.empty());

	mUniqueModelPaths.clear();
	return ModelLoadResults;
}

//----------------------------------------------------------------------------------------------------------------
// TEXTURE LOADER
//----------------------------------------------------------------------------------------------------------------
void AssetLoader::QueueTextureLoad(TaskID taskID, const FTextureLoadParams& TexLoadParam)
{
	FLoadTaskContext<FTextureLoadParams>& ctx = mLookup_TextureLoadContext[taskID];
	ctx.LoadQueue.push(TexLoadParam);
}

static AssetLoader::ECustomMapType DetermineCustomMapType(const std::string& FilePath)
{
	SCOPED_CPU_MARKER("DetermineCustomMapType()");
	AssetLoader::ECustomMapType t = AssetLoader::ECustomMapType::UNKNOWN;

	// parse @CustomMapFileName 
	std::string fileName = StrUtil::split(FilePath, { '/', '\\' }).back();
	StrUtil::MakeLowercase(fileName);

	// e.g. fileName=metal_tiles_02_2k_diffuse.png
	//      case: FileName has word 'metal' in it, but isn't actually a metalness texture
	// we're interested in this region: before extension, after last occurance of '_'
	//   fileName=metal_tiles_02_2k_diffuse.png
	//                              ^^^^^^^
	// 
	auto vTokens = StrUtil::split(fileName, { '.', '_' });
	if (vTokens.size() < 3) // we expect at least 3 tokens: MAT_NAME, TEXTYPE, EXTENSION
		return t; // not a proper texture type string token, such as procedural texture names

	const std::string& texTypeToken = vTokens[vTokens.size()-2];

	if (texTypeToken.empty())
		return t;

	auto itOccl = texTypeToken.find("occlu");
	auto itRghn = texTypeToken.find("roughness");
	auto itRgh  = texTypeToken.find("rough");
	auto itMtlc = texTypeToken.find("metallic");
	auto itMtln = texTypeToken.find("metalness");
	auto itMtl  = texTypeToken.find("metal");

	bool bContainsAO = itOccl != std::string::npos;
	bool bContainsRoughness = itRghn != std::string::npos || itRgh != std::string::npos;
	bool bCountainsMetalness = itMtlc != std::string::npos 
		|| itMtln != std::string::npos
		|| itMtl  != std::string::npos;

	bool bRoughMetal = false;
	bool bMetalRough = false;

	// determine type
	if (bContainsAO && bContainsRoughness && bCountainsMetalness)
	{
		// TODO: ensure order
		t = AssetLoader::ECustomMapType::OCCLUSION_ROUGHNESS_METALNESS;
	}
	else if (!bContainsAO && bContainsRoughness && bCountainsMetalness)
	{
		// determine rough/metal order
		auto& itMtl_valid = itMtlc != std::string::npos 
			? itMtlc 
			: (itMtln != std::string::npos ? itMtln : itMtl);
		auto& itRgh_valid = itRghn != std::string::npos 
			? itRghn 
			: itRgh;
		bRoughMetal = itRgh_valid < itMtl_valid;
		bMetalRough = itMtl_valid < itRgh_valid;

		assert(bRoughMetal || bMetalRough);
		if(bRoughMetal) t = AssetLoader::ECustomMapType::ROUGHNESS_METALNESS;
		if(bMetalRough) t = AssetLoader::ECustomMapType::METALNESS_ROUGHNESS;
	}

	return t;
}

AssetLoader::TextureLoadResults_t AssetLoader::StartLoadingTextures(TaskID taskID)
{
	SCOPED_CPU_MARKER("AssetLoader.StartLoadingTextures()");
	TextureLoadResults_t TextureLoadResults;
	
	if (mLookup_TextureLoadContext.find(taskID) == mLookup_TextureLoadContext.end())
	{
		Log::Warning("AssetLoader::StartLoadingTextures(taskID=%d): no Textures to load", taskID);
		return TextureLoadResults;
	}

	FLoadTaskContext<FTextureLoadParams>& ctx = mLookup_TextureLoadContext.at(taskID);

	if (ctx.LoadQueue.empty())
	{
		Log::Warning("AssetLoader::StartLoadingTextures(taskID=%d): no Textures to load", taskID);
		return TextureLoadResults;
	}
	
	std::unordered_map<std::string, TextureID> Lookup_TextureLoadResult;

	// process texture load queue
	{
		SCOPED_CPU_MARKER("DispatchTextureWorkers");
		while (!ctx.LoadQueue.empty())
		{
			FTextureLoadParams TexLoadParams = std::move(ctx.LoadQueue.front());
			ctx.LoadQueue.pop();

			// handle duplicate texture load paths
			if (ctx.UniquePaths.find(TexLoadParams.TexturePath) == ctx.UniquePaths.end())
			{
				ctx.UniquePaths.insert(TexLoadParams.TexturePath);

				// determine whether we'll load file OR use a procedurally generated texture
				auto vPathTokens = StrUtil::split(TexLoadParams.TexturePath, '/');
				assert(!vPathTokens.empty());
				const bool bProceduralTexture = vPathTokens[0] == "Procedural";

				TextureID texID = INVALID_ID;
				TextureManager& mTextureManager = mRenderer.GetTextureManager();
				if (bProceduralTexture)
				{
					texID = mRenderer.GetProceduralTexture(
						VQRenderer::GetProceduralTextureEnumFromName(vPathTokens[1])
					);
				}
				else
				{
					FTextureRequest Request;
					Request.Name = DirectoryUtil::GetFileNameFromPath(TexLoadParams.TexturePath);
					Request.FilePath = TexLoadParams.TexturePath;
					Request.bGenerateMips = true;
					Request.bCPUReadback = false;
					Request.InitialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

					const bool bCheckAlphaMask = (TexLoadParams.TexType == ETextureType::DIFFUSE) || TexLoadParams.TexType == ETextureType::ALPHA_MASK;
					texID = mTextureManager.CreateTexture(Request, bCheckAlphaMask);
				}

				// update results lookup for the shared textures (among different materials)
				Lookup_TextureLoadResult[TexLoadParams.TexturePath] = texID;

				// record load results
				TextureLoadResults.emplace(std::make_pair(TexLoadParams.MatID, FTextureLoadResult{ TexLoadParams.TexType, TexLoadParams.TexturePath, texID }));
			}

			// shared textures among materials
			else
			{
				TextureLoadResults.emplace(std::make_pair(TexLoadParams.MatID, FTextureLoadResult{ TexLoadParams.TexType, TexLoadParams.TexturePath, Lookup_TextureLoadResult.at(TexLoadParams.TexturePath) }));
			}
		}
	}

	ctx.UniquePaths.clear();

	mLookup_TextureLoadContext.erase(taskID);

	return std::move(TextureLoadResults);
}

static AssetLoader::ETextureType GetTextureTypeFromGLTF(const cgltf_texture_view* texture_view, const cgltf_material* material)
{
	// Map glTF texture roles to engine's ETextureType
	if (texture_view == &material->pbr_metallic_roughness.base_color_texture)
		return AssetLoader::ETextureType::DIFFUSE;
	if (texture_view == &material->normal_texture)
		return AssetLoader::ETextureType::NORMALS;
	if (texture_view == &material->emissive_texture)
		return AssetLoader::ETextureType::EMISSIVE;
	if (texture_view == &material->occlusion_texture)
		return AssetLoader::ETextureType::AMBIENT_OCCLUSION;
	if (texture_view == &material->pbr_metallic_roughness.metallic_roughness_texture)
		return AssetLoader::ETextureType::CUSTOM_MAP; // Likely OCCLUSION_ROUGHNESS_METALNESS
	return AssetLoader::ETextureType::NUM_TEXTURE_TYPES;
}


void AssetLoader::FMaterialTextureAssignments::DoAssignments(Scene* pScene, std::mutex& mtxTexturePaths, std::unordered_map<TextureID, std::string>& TexturePaths, VQRenderer* pRenderer)
{
	SCOPED_CPU_MARKER("MaterialTextureAssignments");
	for (FMaterialTextureAssignment& assignment : mAssignments)
	{
		Material& mat = pScene->GetMaterial(assignment.matID);
		std::string log = "DoAssignments for mat: " + std::to_string(assignment.matID) + ":\n";
		const bool bLoadedTextures = mTextureLoadResults.find(assignment.matID) != mTextureLoadResults.end();
		
		UINT OcclRoughMtlMap_ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		if(bLoadedTextures)
		{
			auto pair_itBeginEnd = mTextureLoadResults.equal_range(assignment.matID);
			// wait for textures, assign IDs, cache texture path
			for (auto it = pair_itBeginEnd.first; it != pair_itBeginEnd.second; ++it)
			{
				const MaterialID& matID = it->first;
				FTextureLoadResult& result = it->second;

				const TextureID loadedTextureID = result.TexID;
				pRenderer->GetTextureManager().WaitForTexture(loadedTextureID);
				log += " - texID: " + std::to_string(loadedTextureID) + "\n";
				switch (result.type)
				{
				case DIFFUSE           : mat.TexDiffuseMap          = loadedTextureID; break;
				case NORMALS           : mat.TexNormalMap           = loadedTextureID; break;
				case ALPHA_MASK        : mat.TexAlphaMaskMap        = loadedTextureID; break;
				case EMISSIVE          : mat.TexEmissiveMap         = loadedTextureID; break;
				case METALNESS         : mat.TexMetallicMap         = loadedTextureID; mat.metalness = 1.0f; break;
				case ROUGHNESS         : mat.TexRoughnessMap        = loadedTextureID; mat.roughness = 1.0f; break;
				case HEIGHT            : mat.TexHeightMap           = loadedTextureID; break;
				case AMBIENT_OCCLUSION : mat.TexAmbientOcclusionMap = loadedTextureID; break;
				case SPECULAR          : assert(false); /*mat.TexSpecularMap  = result.texLoadResult.get();*/ break;
				case CUSTOM_MAP        :
				{
					const ECustomMapType customMapType = DetermineCustomMapType(result.TexturePath);
					switch (customMapType)
					{
					case OCCLUSION_ROUGHNESS_METALNESS:
						OcclRoughMtlMap_ComponentMapping; // leave as is
						mat.TexOcclusionRoughnessMetalnessMap = result.TexID;
						mat.metalness = 1.0f;
						mat.roughness = 1.0f;
						break;
					case METALNESS_ROUGHNESS:
						// turns out: even the file is named in reverse order, the common thing to do is to store
						//            roughness in G and metalness in B channels. Hence we're not handling this case 
						//            here.
	#if 0 
						OcclRoughMtlMap_ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
							  D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1
							, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2
							, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1
							, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0
						);
						mat.TexOcclusionRoughnessMetalnessMap = result.TexID;
						break;
	#endif
					case ROUGHNESS_METALNESS:
						OcclRoughMtlMap_ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
							D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1
							, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1
							, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2
							, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0
						);
						mat.TexOcclusionRoughnessMetalnessMap = result.TexID;
						mat.metalness = 1.0f;
						mat.roughness = 1.0f;
						break;
					default:
					case UNKNOWN:
						//Log::Warning("Unknown custom map (%s) type for material MatID=%d!", result.TexturePath.c_str(), assignment.matID);
						DetermineCustomMapType(result.TexturePath);
						break;
					}
				} break;
				default:
					Log::Warning("UNHANDLED CUSTOM_MAP TEXTURE ASSIGNMENT");
					break;
				}
			
				// store the loaded texture path if we have a successful texture creation
				if (loadedTextureID != INVALID_ID)
				{
					std::lock_guard<std::mutex> lk(mtxTexturePaths);
					TexturePaths[loadedTextureID] = result.TexturePath;
				}
			}
		}

		//Log::Info(log);

		if (mat.SRVMaterialMaps == INVALID_ID)
		{
			SCOPED_CPU_MARKER("SRVs");
			mat.SRVMaterialMaps = pRenderer->AllocateSRV(NUM_MATERIAL_TEXTURE_MAP_BINDINGS - 1);
			mat.SRVHeightMap = pRenderer->AllocateSRV(1);

			pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ALBEDO, mat.TexDiffuseMap);
			pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::NORMALS, mat.TexNormalMap);
			pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::EMISSIVE, mat.TexEmissiveMap);
			pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ALPHA_MASK, mat.TexAlphaMaskMap);
			pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::METALLIC, mat.TexMetallicMap);
			pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ROUGHNESS, mat.TexRoughnessMap);
			pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::OCCLUSION_ROUGHNESS_METALNESS, mat.TexOcclusionRoughnessMetalnessMap, OcclRoughMtlMap_ComponentMapping);
			pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::AMBIENT_OCCLUSION, mat.TexAmbientOcclusionMap);
			pRenderer->InitializeSRV(mat.SRVHeightMap, 0, mat.TexHeightMap);
		}
		else
		{
			Log::Warning("Material (%d) texture map SRV (%d) already initialized: %s", assignment.matID, mat.SRVMaterialMaps, pScene->GetMaterialName(assignment.matID).c_str());
		}
	}
}

//----------------------------------------------------------------------------------------------------------------
// HELPER FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
static std::vector<AssetLoader::FTextureLoadParams> GenerateTextureLoadParams(
	const cgltf_material* material,
	MaterialID matID,
	const std::string& modelDirectory)
{
	SCOPED_CPU_MARKER("GenerateTextureLoadParams()");
	std::vector<AssetLoader::FTextureLoadParams> TexLoadParams;

	// List of texture views to check
	const cgltf_texture_view* texture_views[] = 
	{
		&material->pbr_metallic_roughness.base_color_texture,
		&material->pbr_metallic_roughness.metallic_roughness_texture,
		&material->normal_texture,
		&material->occlusion_texture,
		&material->emissive_texture
	};

	for (const cgltf_texture_view* view : texture_views)
	{
		if (view->texture && view->texture->image && view->texture->image->uri)
		{
			AssetLoader::FTextureLoadParams params = {};
			params.TexturePath = modelDirectory + view->texture->image->uri;
			params.MatID = matID;
			params.TexType = GetTextureTypeFromGLTF(view, material);
			if (params.TexType != AssetLoader::ETextureType::NUM_TEXTURE_TYPES)
			{
				TexLoadParams.push_back(params);
			}
		}
	}

	return TexLoadParams;
}

static Mesh ProcessGLTFMesh(
	VQRenderer* pRenderer,
	const cgltf_primitive* prim,
	const cgltf_data* data,
	const std::string& ModelName
)
{
	SCOPED_CPU_MARKER("ProcessGLTFMesh()");
	GeometryData<FVertexWithNormalAndTangent, unsigned> GeometryData(1);
	std::vector<FVertexWithNormalAndTangent>& Vertices = GeometryData.LODVertices[0];
	std::vector<unsigned>& Indices = GeometryData.LODIndices[0];

	// Load buffers if not already loaded
	if (prim->attributes_count > 0 && prim->attributes[0].data->buffer_view && !prim->attributes[0].data->buffer_view->buffer->data)
	{
		// Buffers should be loaded by cgltf_load_buffers in the main function
		Log::Error("Buffer data not loaded for mesh %s", ModelName.c_str());
		return Mesh(nullptr, std::move(GeometryData), ModelName);
	}

	// Count total indices
	size_t NumIndices = 0;
	if (prim->indices)
	{
		if (prim->type == cgltf_primitive_type_triangles)
		{
			NumIndices = prim->indices->count;
		}
		else if (prim->type == cgltf_primitive_type_triangle_strip)
		{
			NumIndices = (prim->indices->count >= 3) ? (prim->indices->count - 2) * 3 : 0;
		}
		else if (prim->type == cgltf_primitive_type_triangle_fan)
		{
			NumIndices = (prim->indices->count >= 3) ? (prim->indices->count - 2) * 3 : 0;
		}
		else
		{
			Log::Error("Unsupported primitive type %d in mesh %s", prim->type, ModelName.c_str());
			return Mesh(nullptr, std::move(GeometryData), ModelName);
		}
	}

	// Count vertices
	size_t NumVertices = 0;
	for (size_t i = 0; i < prim->attributes_count; ++i)
	{
		if (prim->attributes[i].type == cgltf_attribute_type_position)
		{
			NumVertices = prim->attributes[i].data->count;
			break;
		}
	}

	{
		SCOPED_CPU_MARKER("MemAlloc");
		Indices.resize(NumIndices);
		Vertices.resize(NumVertices);
	}

	bool bTangentDataExists = false;
	{
		SCOPED_CPU_MARKER("Verts");
		for (size_t i = 0; i < NumVertices; ++i)
		{
			FVertexWithNormalAndTangent& Vert = Vertices[i];

			// Process attributes
			for (size_t j = 0; j < prim->attributes_count; ++j)
			{
				const cgltf_attribute* attr = &prim->attributes[j];
				const cgltf_accessor* acc = attr->data;

				if (acc->is_sparse) 
				{
					Log::Warning("Sparse accessors not supported for mesh %s", ModelName.c_str());
					continue;
				}

				switch (attr->type)
				{
				case cgltf_attribute_type_invalid:
					Log::Warning("Invalid attribute type for mesh %s", ModelName.c_str());
					break;
				
				case cgltf_attribute_type_position:
					assert(acc->type == cgltf_type_vec3);
					if (!cgltf_accessor_read_float(acc, i, Vert.position, 3))
					{
						Log::Warning("Failed to read position for vertex %zu in mesh %s", i, ModelName.c_str());
						break;
					}
					Vert.position[2] = -Vert.position[2]; // Convert to left-handed coordinate system
					break;

				case cgltf_attribute_type_normal:
					assert(acc->type == cgltf_type_vec3);
					if (!cgltf_accessor_read_float(acc, i, Vert.normal, 3))
					{
						Log::Warning("Failed to read normal for vertex %zu in mesh %s", i, ModelName.c_str());
						break;
					}
					Vert.normal[2] = -Vert.normal[2]; // Convert to left-handed coordinate system
					break;

				case cgltf_attribute_type_tangent:
				{
					if (acc->type == cgltf_type_vec4)
					{
						float tan[4];
						if (!cgltf_accessor_read_float(acc, i, tan, 4))
						{
							Log::Warning("Failed to read tangent for vertex %zu in mesh %s", i, ModelName.c_str());
							break;
						}
						Vert.tangent[0] = tan[0];
						Vert.tangent[1] = tan[1];
						Vert.tangent[2] = -tan[2]; // Convert to left-handed coordinate system
						bTangentDataExists = true;
					}
					else if (acc->type == cgltf_type_vec3)
					{
						if (!cgltf_accessor_read_float(acc, i, Vert.tangent, 3))
						{
							Log::Warning("Failed to read tangent for vertex %zu in mesh %s", i, ModelName.c_str());
							break;
						}
						Vert.tangent[2] = -Vert.tangent[2]; // Convert to left-handed coordinate system
						bTangentDataExists = true;
					}
					else
					{
						Log::Warning("Unsupported tangent type for vertex %zu in mesh %s", i, ModelName.c_str());
					}
					break;
				}

				case cgltf_attribute_type_texcoord:
					assert(acc->type == cgltf_type_vec2);
					if (!cgltf_accessor_read_float(acc, i, Vert.uv, 2))
					{
						Log::Warning("Failed to read UV for vertex %zu in mesh %s", i, ModelName.c_str());
					}
					break;

				case cgltf_attribute_type_color:
				case cgltf_attribute_type_joints:
				case cgltf_attribute_type_weights:
				case cgltf_attribute_type_custom:
				case cgltf_attribute_type_max_enum:
					Log::Warning("Unhandled attribute type %d for vertex %zu in mesh %s", attr->type, i, ModelName.c_str());
					break;
				}
			}
		}
	}

	if (prim->indices)
	{
		SCOPED_CPU_MARKER("Indices");
		if (prim->type == cgltf_primitive_type_triangles)
		{
			for (size_t i = 0; i < NumIndices; i += 3)
			{
				Indices[i] = static_cast<unsigned>(cgltf_accessor_read_index(prim->indices, i));
				Indices[i + 1] = static_cast<unsigned>(cgltf_accessor_read_index(prim->indices, i + 2)); // Flip winding
				Indices[i + 2] = static_cast<unsigned>(cgltf_accessor_read_index(prim->indices, i + 1));
			}
		}
		else if (prim->type == cgltf_primitive_type_triangle_strip)
		{
			for (size_t i = 2; i < prim->indices->count; ++i)
			{
				size_t idx0 = cgltf_accessor_read_index(prim->indices, i - 2);
				size_t idx1 = cgltf_accessor_read_index(prim->indices, i - 1);
				size_t idx2 = cgltf_accessor_read_index(prim->indices, i);
				size_t base = (i - 2) * 3;
				if (i % 2 == 0)
				{
					Indices[base] = static_cast<unsigned>(idx0);
					Indices[base + 1] = static_cast<unsigned>(idx2); // Flip winding
					Indices[base + 2] = static_cast<unsigned>(idx1);
				}
				else
				{
					Indices[base] = static_cast<unsigned>(idx0);
					Indices[base + 1] = static_cast<unsigned>(idx1);
					Indices[base + 2] = static_cast<unsigned>(idx2);
				}
			}
		}
		else if (prim->type == cgltf_primitive_type_triangle_fan)
		{
			size_t idx0 = cgltf_accessor_read_index(prim->indices, 0);
			for (size_t i = 2; i < prim->indices->count; ++i)
			{
				size_t idx1 = cgltf_accessor_read_index(prim->indices, i - 1);
				size_t idx2 = cgltf_accessor_read_index(prim->indices, i);
				size_t base = (i - 2) * 3;
				Indices[base] = static_cast<unsigned>(idx0);
				Indices[base + 1] = static_cast<unsigned>(idx2); // Flip winding
				Indices[base + 2] = static_cast<unsigned>(idx1);
			}
		}
	}

	constexpr bool CALCULATE_TANGENTS = true;
	if constexpr (CALCULATE_TANGENTS)
	{
		if (!bTangentDataExists)
		{
			SCOPED_CPU_MARKER("CalculateTangents");

			size_t numTriangles = Indices.empty() ? NumVertices / 3 : Indices.size() / 3;
			for (size_t i = 0; i < numTriangles; ++i) // Process triangles
			{
				// Get vertex indices for the triangle
				size_t i0 = Indices.empty() ? (i * 3 + 0) : Indices[i * 3 + 0];
				size_t i1 = Indices.empty() ? (i * 3 + 1) : Indices[i * 3 + 1];
				size_t i2 = Indices.empty() ? (i * 3 + 2) : Indices[i * 3 + 2];

				// Ensure indices are valid
				if (i0 >= NumVertices || i1 >= NumVertices || i2 >= NumVertices)
				{
					Log::Warning("Invalid triangle indices for mesh %s", ModelName.c_str());
					continue;
				}

				const FVertexWithNormalAndTangent& v0 = Vertices[i0];
				const FVertexWithNormalAndTangent& v1 = Vertices[i1];
				const FVertexWithNormalAndTangent& v2 = Vertices[i2];

				// Compute edges
				float e1[3] = { v1.position[0] - v0.position[0], v1.position[1] - v0.position[1], v1.position[2] - v0.position[2] };
				float e2[3] = { v2.position[0] - v0.position[0], v2.position[1] - v0.position[1], v2.position[2] - v0.position[2] };

				// Compute UV differences
				float du1 = v1.uv[0] - v0.uv[0];
				float dv1 = v1.uv[1] - v0.uv[1];
				float du2 = v2.uv[0] - v0.uv[0];
				float dv2 = v2.uv[1] - v0.uv[1];

				// Compute determinant
				float det = du1 * dv2 - dv1 * du2;
				if (fabs(det) < 1e-6f)
				{
					// Degenerate UVs; skip or use fallback
					continue;
				}

				float invDet = 1.0f / det;

				// Compute tangent
				float tangent[3];
				tangent[0] = invDet * (dv2 * e1[0] - dv1 * e2[0]);
				tangent[1] = invDet * (dv2 * e1[1] - dv1 * e2[1]);
				tangent[2] = invDet * (dv2 * e1[2] - dv1 * e2[2]);

				// Adjust for left-handed coordinate system (negate z)
				tangent[2] = -tangent[2];

				// Accumulate tangents for each vertex
				for (size_t j : {i0, i1, i2})
				{
					FVertexWithNormalAndTangent& vert = Vertices[j];
					vert.tangent[0] += tangent[0];
					vert.tangent[1] += tangent[1];
					vert.tangent[2] += tangent[2];
				}
			}

			// Normalize tangents and orthogonalize against normals
			for (size_t i = 0; i < NumVertices; ++i)
			{
				FVertexWithNormalAndTangent& vert = Vertices[i];

				// Normalize tangent
				float len = sqrtf(vert.tangent[0] * vert.tangent[0] + vert.tangent[1] * vert.tangent[1] + vert.tangent[2] * vert.tangent[2]);
				if (len > 1e-6f) {
					vert.tangent[0] /= len;
					vert.tangent[1] /= len;
					vert.tangent[2] /= len;
				}
				else 
				{
					// Fallback: use arbitrary tangent perpendicular to normal
					float absNx = fabs(vert.normal[0]);
					float absNy = fabs(vert.normal[1]);
					float absNz = fabs(vert.normal[2]);
					if (absNx <= absNy && absNx <= absNz)
					{
						vert.tangent[0] = 1.0f;
						vert.tangent[1] = 0.0f;
						vert.tangent[2] = 0.0f;
					}
					else if (absNy <= absNx && absNy <= absNz)
					{
						vert.tangent[0] = 0.0f;
						vert.tangent[1] = 1.0f;
						vert.tangent[2] = 0.0f;
					}
					else
					{
						vert.tangent[0] = 0.0f;
						vert.tangent[1] = 0.0f;
						vert.tangent[2] = 1.0f;
					}
				}

				// Orthogonalize tangent against normal (Gram-Schmidt)
				float dot = vert.normal[0] * vert.tangent[0] + vert.normal[1] * vert.tangent[1] + vert.normal[2] * vert.tangent[2];
				vert.tangent[0] -= dot * vert.normal[0];
				vert.tangent[1] -= dot * vert.normal[1];
				vert.tangent[2] -= dot * vert.normal[2];

				// Re-normalize
				len = sqrtf(vert.tangent[0] * vert.tangent[0] + vert.tangent[1] * vert.tangent[1] + vert.tangent[2] * vert.tangent[2]);
				if (len > 1e-6f)
				{
					vert.tangent[0] /= len;
					vert.tangent[1] /= len;
					vert.tangent[2] /= len;
				}
			}

			// Log::Info("Calculated tangents for mesh %s with %zu vertices", ModelName.c_str(), NumVertices);
		}
	}

	return Mesh(nullptr, std::move(GeometryData), ModelName);
}

static std::vector<Mesh> ProcessGLTFMeshPrimitives
(
	VQRenderer* pRenderer,
	const cgltf_mesh* mesh,
	const cgltf_data* data,
	const std::string& ModelName
)
{
	SCOPED_CPU_MARKER("ProcessGLTFMeshPrimitives()");

	std::vector<Mesh> meshes;
	meshes.reserve(mesh->primitives_count);

	for (size_t i = 0; i < mesh->primitives_count; ++i)
	{
		meshes.push_back(ProcessGLTFMesh(pRenderer, &mesh->primitives[i], data, ModelName));
	}

	return meshes;
}

static std::string CreateUniqueMaterialName(const cgltf_material* material, size_t iMat, const std::string& modelDirectory)
{
	std::string uniqueMatName;
	std::string matName = material->name ? material->name : ("Material#" + std::to_string(iMat));

	// Data/Models/%MODEL_NAME%/... : index 2 will give model name
	auto vFolders = DirectoryUtil::GetFlattenedFolderHierarchy(modelDirectory);
	assert(vFolders.size() > 2);
	const std::string ModelFolderName = vFolders[2];
	uniqueMatName = ModelFolderName + "/" + matName;

	return uniqueMatName;
}

static void QueueUpTextureLoadRequests(
	AssetLoader* pAssetLoader,
	const std::string& modelDirectory,
	const cgltf_material* material,
	MaterialID matID,
	TaskID taskID)
{
	SCOPED_CPU_MARKER("QueueUpTextureLoadRequests");
	std::vector<AssetLoader::FTextureLoadParams> TexLoadParams = GenerateTextureLoadParams(material, matID, modelDirectory);
	for (const auto& param : TexLoadParams) 
	{
		pAssetLoader->QueueTextureLoad(taskID, param);
	}
}

static MaterialID ProcessGLTFMaterial(
	const cgltf_material* material,
	size_t matIndex,
	const std::string& modelDirectory,
	Scene* pScene,
	AssetLoader* pAssetLoader,
	AssetLoader::FMaterialTextureAssignments& MaterialTextureAssignments,
	TaskID taskID)
{
	SCOPED_CPU_MARKER("ProcessGLTFMaterial");
	const std::string matName = CreateUniqueMaterialName(material, matIndex, modelDirectory);
	MaterialID matID = pScene->CreateMaterial(matName);
	Material& mat = pScene->GetMaterial(matID);

	QueueUpTextureLoadRequests(pAssetLoader, modelDirectory, material, matID, taskID);
	MaterialTextureAssignments.mAssignments.push_back({ matID });

	// Set material properties (PBR metallic-roughness model)
	if (material->has_pbr_metallic_roughness) {
		const auto& pbr = material->pbr_metallic_roughness;
		mat.diffuse = XMFLOAT3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
		mat.metalness = pbr.metallic_factor;
		mat.roughness = pbr.roughness_factor;
		mat.alpha = pbr.base_color_factor[3];
	}

	if (material->emissive_factor[0] != 0.0f || material->emissive_factor[1] != 0.0f || material->emissive_factor[2] != 0.0f) {
		mat.emissiveIntensity = std::max({ material->emissive_factor[0], material->emissive_factor[1], material->emissive_factor[2] });
	}

	// Handle alpha mode
	if (material->alpha_mode == cgltf_alpha_mode_mask) {
		// mat.alphaCutoff = material->alpha_cutoff; // TODO:
	}

	// Note: glTF doesn't directly provide specular or shininess like Assimp; we rely on PBR properties
	// If specular-glossiness is needed, check material->has_pbr_specular_glossiness and convert

	return matID;
}


#define THREADED_MESH_LOAD 1
static Model::Data ImportGLTFAllMeshes
(
	const std::string& ModelName,
	cgltf_data* data,
	const std::string& modelDirectory,
	AssetLoader* pAssetLoader,
	Scene* pScene,
	VQRenderer* pRenderer,
	AssetLoader::FMaterialTextureAssignments& MaterialTextureAssignments,
	TaskID taskID
)
{
	SCOPED_CPU_MARKER("ImportGLTFAllMeshes()");
	Model::Data modelData;

	ThreadPool& WorkerThreadPool = pAssetLoader->mWorkers_MeshLoad;

	// Count total primitives across all meshes
	size_t total_primitives = 0;
	for (size_t i = 0; i < data->meshes_count; ++i)
	{
		total_primitives += data->meshes[i].primitives_count;
	}

	// Process all meshes in cgltf_data->meshes
	std::vector<Mesh> MeshData(total_primitives);
	std::vector<MaterialID> MaterialIDs(total_primitives, INVALID_ID);
	const bool bThreaded = THREADED_MESH_LOAD && total_primitives > 1;

	if (bThreaded)
	{
		constexpr size_t NumMinWorkItemsPerThread = 1;
		const size_t NumWorkItems = total_primitives;
		const size_t NumThreadsToUse = CalculateNumThreadsToUse(NumWorkItems, WorkerThreadPool.GetThreadPoolSize() + 1, NumMinWorkItemsPerThread);
		const size_t NumWorkerThreadsToUse = NumThreadsToUse - 1;
		auto vRanges = PartitionWorkItemsIntoRanges(NumWorkItems, NumThreadsToUse);

		// Synchronization
		std::atomic<int> WorkerCounter(static_cast<int>(std::min(vRanges.size() - 1, NumWorkerThreadsToUse)));
		EventSignal WorkerSignal;
		
		// Map primitive indices to mesh/primitive pairs
		std::vector<std::pair<size_t, size_t>> primitive_map(total_primitives);
		size_t prim_idx = 0;
		for (size_t mesh_idx = 0; mesh_idx < data->meshes_count; ++mesh_idx)
		{
			for (size_t p = 0; p < data->meshes[mesh_idx].primitives_count; ++p)
			{
				primitive_map[prim_idx++] = { mesh_idx, p };
			}
		}

		{
			SCOPED_CPU_MARKER("DispatchMeshWorkers");
			for (size_t iRange = 1; iRange < vRanges.size(); ++iRange)
			{
				WorkerThreadPool.AddTask([=, &MeshData, &WorkerSignal, &WorkerCounter]()
				{
					SCOPED_CPU_MARKER_C("MeshWorker", 0xFF0000FF);
					for (size_t i = vRanges[iRange].first; i <= vRanges[iRange].second; ++i)
					{
						auto [mesh_idx, prim_idx] = primitive_map[i];
						MeshData[i] = ProcessGLTFMesh(pRenderer, &data->meshes[mesh_idx].primitives[prim_idx], data, ModelName);
					}
					WorkerCounter.fetch_sub(1);
					WorkerSignal.NotifyOne();
				});
			}
		}
		{
			SCOPED_CPU_MARKER("ThisThread_Mesh");
			for (size_t i = vRanges[0].first; i <= vRanges[0].second; ++i)
			{
				auto [mesh_idx, prim_idx] = primitive_map[i];
				MeshData[i] = ProcessGLTFMesh(pRenderer, &data->meshes[mesh_idx].primitives[prim_idx], data, ModelName);
			}
		}
		{
			SCOPED_CPU_MARKER("ProcessMaterials");
			size_t idx = 0;
			for (size_t mesh_idx = 0; mesh_idx < data->meshes_count; ++mesh_idx)
			{
				for (size_t prim_idx = 0; prim_idx < data->meshes[mesh_idx].primitives_count; ++prim_idx)
				{
					if (data->meshes[mesh_idx].primitives[prim_idx].material)
					{
						MaterialIDs[idx] = ProcessGLTFMaterial(
							data->meshes[mesh_idx].primitives[prim_idx].material, idx,
							modelDirectory, pScene, pAssetLoader, MaterialTextureAssignments, taskID
						);
					}
					++idx;
				}
			}
		}
		{
			SCOPED_CPU_MARKER_C("WAIT_MESH_WORKERS", 0xFFAA0000);
			WorkerSignal.Wait([&]() { return WorkerCounter.load() == 0; });
		}
		{
			SCOPED_CPU_MARKER("UpdateSceneData");
			for (size_t i = 0; i < total_primitives; ++i)
			{
				if (MaterialIDs[i] != INVALID_ID)
				{
					MeshID id = pScene->AddMesh(std::move(MeshData[i]));
					modelData.AddMesh(id, MaterialIDs[i], Model::Data::EMeshType::OPAQUE_MESH);
				}
			}
		}
	}
	else // single thread
	{
		size_t idx = 0;
		for (size_t mesh_idx = 0; mesh_idx < data->meshes_count; ++mesh_idx)
		{
			for (size_t prim_idx = 0; prim_idx < data->meshes[mesh_idx].primitives_count; ++prim_idx)
			{
				Mesh mesh = ProcessGLTFMesh(pRenderer, &data->meshes[mesh_idx].primitives[prim_idx], data, ModelName);
				MaterialID mat_id = INVALID_ID;
				if (data->meshes[mesh_idx].primitives[prim_idx].material)
				{
					mat_id = ProcessGLTFMaterial(
						data->meshes[mesh_idx].primitives[prim_idx].material, idx,
						modelDirectory, pScene, pAssetLoader, MaterialTextureAssignments, taskID);
				}
				MeshID id = pScene->AddMesh(std::move(mesh));
				if (mat_id != INVALID_ID)
				{
					modelData.AddMesh(id, mat_id, Model::Data::EMeshType::OPAQUE_MESH);
				}
				++idx;
			}
		}
	}

	return modelData;
}


static Model::Data ProcessGLTFNode(
	const std::string& ModelName,
	cgltf_node* node,
	const cgltf_data* data,
	const std::string& modelDirectory,
	AssetLoader* pAssetLoader,
	Scene* pScene,
	VQRenderer* pRenderer,
	AssetLoader::FMaterialTextureAssignments& MaterialTextureAssignments,
	TaskID taskID)
{
	SCOPED_CPU_MARKER("ProcessGLTFNode()");
	Model::Data modelData;

	ThreadPool& WorkerThreadPool = pAssetLoader->mWorkers_MeshLoad;

	std::vector<Mesh> NodeMeshData;
	std::vector<MaterialID> NodeMaterialIDs;
	if (node->mesh) {
		NodeMeshData.resize(1); // One mesh per node for simplicity
		NodeMaterialIDs.resize(1, INVALID_ID);
	}
	const bool bThreaded = THREADED_MESH_LOAD && NodeMeshData.size() > 1; // Adjust if supporting multiple primitives

	if (node->mesh)
	{
		if (bThreaded)
		{
			// Threaded processing (though unlikely for single mesh per node)
			constexpr size_t NumMinWorkItemsPerThread = 1;
			const size_t NumWorkItems = NodeMeshData.size();
			const size_t NumThreadsToUse = CalculateNumThreadsToUse(NumWorkItems, WorkerThreadPool.GetThreadPoolSize() + 1, NumMinWorkItemsPerThread);
			const size_t NumWorkerThreadsToUse = NumThreadsToUse - 1;
			auto vRanges = PartitionWorkItemsIntoRanges(NumWorkItems, NumThreadsToUse);

			std::atomic<int> WorkerCounter(static_cast<int>(std::min(vRanges.size() - 1, NumWorkerThreadsToUse)));
			EventSignal WorkerSignal;
			{
				SCOPED_CPU_MARKER("DispatchMeshWorkers");
				for (size_t iRange = 1; iRange < vRanges.size(); ++iRange)
				{
					WorkerThreadPool.AddTask([=, &NodeMeshData, &WorkerSignal, &WorkerCounter]()
					{
						SCOPED_CPU_MARKER_C("MeshWorker", 0xFF0000FF);
						for (size_t i = vRanges[iRange].first; i <= vRanges[iRange].second; ++i)
						{
							NodeMeshData[i] = ProcessGLTFMesh(pRenderer, &node->mesh->primitives[i], data, ModelName);
						}
						WorkerCounter.fetch_sub(1);
						WorkerSignal.NotifyOne();
					});
				}
			}
			{
				SCOPED_CPU_MARKER("ThisThread_Mesh");
				for (size_t i = vRanges[0].first; i <= vRanges[0].second; ++i) 
				{
					NodeMeshData[i] = ProcessGLTFMesh(pRenderer, &node->mesh->primitives[i], data, ModelName);
				}
			}
			{
				SCOPED_CPU_MARKER("ProcessMaterials");
				if (node->mesh->primitives_count > 0 && node->mesh->primitives[0].material)
				{
					NodeMaterialIDs[0] = ProcessGLTFMaterial(node->mesh->primitives[0].material, 0, modelDirectory, pScene, pAssetLoader, MaterialTextureAssignments, taskID);
				}
			}
			{
				SCOPED_CPU_MARKER_C("WAIT_MESH_WORKERS", 0xFFAA0000);
				WorkerSignal.Wait([&]() { return WorkerCounter.load() == 0; });
			}
			{
				SCOPED_CPU_MARKER("UpdateSceneData");
				if (node->mesh->primitives_count > 0 && node->mesh->primitives[0].material)
				{
					MeshID id = pScene->AddMesh(std::move(NodeMeshData[0]));
					MaterialID matID = NodeMaterialIDs[0];
					modelData.AddMesh(id, matID, Model::Data::EMeshType::OPAQUE_MESH);
				}
			}
		}
		else // Non-threaded processing
		{
			for (size_t i = 0; i < NodeMeshData.size(); ++i)
			{
				MaterialID mat_id = INVALID_ID;
				if (node->mesh->primitives[i].material)
				{
					mat_id = ProcessGLTFMaterial(
						node->mesh->primitives[i].material, i,
						modelDirectory, pScene, pAssetLoader, MaterialTextureAssignments, taskID);
				}
				MeshID id = pScene->AddMesh(std::move(NodeMeshData[i]));
				if (mat_id != INVALID_ID)
				{
					modelData.AddMesh(id, mat_id, Model::Data::EMeshType::OPAQUE_MESH);
				}
			}
		}
	}

	{
		SCOPED_CPU_MARKER("Children");
		for (size_t i = 0; i < node->children_count; ++i)
		{
			Model::Data childModelData = ProcessGLTFNode(ModelName, node->children[i], data, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments, taskID);
			const std::vector<std::pair<MeshID, MaterialID>>& ChildMeshes = childModelData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH);
			std::copy(ChildMeshes.begin(), ChildMeshes.end(), std::back_inserter(modelData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH)));
			std::unordered_set<MaterialID>& childMats = childModelData.GetMaterials();
			modelData.GetMaterials().insert(childMats.begin(), childMats.end());
		}
	}

	return modelData;
}

static Model::Data ImportGLTFScene
(
	const std::string& ModelName,
	cgltf_data* data,
	const std::string& modelDirectory,
	AssetLoader* pAssetLoader,
	Scene* pScene,
	VQRenderer* pRenderer,
	AssetLoader::FMaterialTextureAssignments& MaterialTextureAssignments,
	TaskID taskID
)
{
	SCOPED_CPU_MARKER("ImportGLTFScene()");
	Model::Data modelData;

	// Process the default scene's root node or fallback to the first node
	if (data->scene && data->scene->nodes_count > 0)
	{
		modelData = ProcessGLTFNode(ModelName, data->scene->nodes[0], data, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments, taskID);
	}
	else if (data->nodes_count > 0)
	{
		modelData = ProcessGLTFNode(ModelName, &data->nodes[0], data, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments, taskID);
	}
	else
	{
		Log::Warning("No nodes found in glTF file for scene import: %s", ModelName.c_str());
	}

	return modelData;
}


//----------------------------------------------------------------------------------------------------------------
// IMPORT MODEL FUNCTION FOR WORKER THREADS
//----------------------------------------------------------------------------------------------------------------
ModelID AssetLoader::ImportGLTF(Scene* pScene, AssetLoader* pAssetLoader, VQRenderer* pRenderer, const std::string& objFilePath, std::string ModelName)
{
	SCOPED_CPU_MARKER("AssetLoader::ImportGLTF()");

	TaskID taskID = AssetLoader::GenerateModelLoadTaskID();
	const std::string modelDirectory = DirectoryUtil::GetFolderPath(objFilePath);

	Log::Info("ImportGLTF: %s - %s", ModelName.c_str(), objFilePath.c_str());
	Timer t;
	t.Start();

	// Initialize cgltf options
	cgltf_options options = {};
	options.file.read = [](const struct cgltf_memory_options* memory_options, const struct cgltf_file_options* file_options, const char* path, cgltf_size* size, void** data)
	{
		FILE* file = fopen(path, "rb");
		if (!file)
			return cgltf_result_file_not_found;
		fseek(file, 0, SEEK_END);
		long length = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (length < 0)
		{
			fclose(file);
			return cgltf_result_io_error;
		}
		*size = (cgltf_size)length;
		char* file_data = (char*)memory_options->alloc_func(memory_options->user_data, *size);
		if (!file_data)
		{
			fclose(file);
			return cgltf_result_out_of_memory;
		}
		size_t read_size = fread(file_data, 1, *size, file);
		fclose(file);
		if (read_size != *size)
		{
			memory_options->free_func(memory_options->user_data, file_data);
			return cgltf_result_io_error;
		}
		*data = file_data;
		return cgltf_result_success;
	};
	options.file.release = [](const struct cgltf_memory_options* memory_options, const struct cgltf_file_options* file_options, void* data)
	{
		memory_options->free_func(memory_options->user_data, data);
	};
	options.memory.alloc_func = [](void* user, cgltf_size size) { return malloc(size); };
	options.memory.free_func = [](void* user, void* ptr) { free(ptr); };

	// Parse glTF file
	cgltf_data* data = nullptr;
	cgltf_result result = cgltf_parse_file(&options, objFilePath.c_str(), &data);
	if (result != cgltf_result_success)
	{
		Log::Error("cgltf_parse_file failed: %d", result);
		return INVALID_ID;
	}

	t.Tick();
	float fTimeReadFile = t.DeltaTime();
	Log::Info("   [%.2fs] ReadFile=%s ", fTimeReadFile, objFilePath.c_str());

	// Load buffers
	result = cgltf_load_buffers(&options, data, objFilePath.c_str());
	if (result != cgltf_result_success)
	{
		Log::Error("cgltf_load_buffers failed: %d", result);
		cgltf_free(data);
		return INVALID_ID;
	}

	// Validate data
	result = cgltf_validate(data);
	if (result != cgltf_result_success) 
	{
		Log::Warning("cgltf_validate failed: %d", result);
		// Proceed anyway, as some issues might be non-critical
	}


	// Process scene or all meshes based on mode
	bool bImportAllMeshes = true; // Default to importing all meshes
	// Process scene
	AssetLoader::FMaterialTextureAssignments MaterialTextureAssignments;
	Model::Data modelData;
	if (bImportAllMeshes)
	{
		modelData = ImportGLTFAllMeshes(ModelName, data, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments, taskID);
	}
	else
	{
		modelData = ImportGLTFScene(ModelName, data, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments, taskID);
	}


	pRenderer->WaitHeapsInitialized();

	{
		SCOPED_CPU_MARKER("UploadVertexAndIndexBufferHeaps()");
		pRenderer->UploadVertexAndIndexBufferHeaps();
	}

	if (!MaterialTextureAssignments.mAssignments.empty()) 
	{
		MaterialTextureAssignments.mTextureLoadResults = pAssetLoader->StartLoadingTextures(taskID);
	}

	// Cache the imported model
	ModelID mID = pScene->CreateModel();
	Model& model = pScene->GetModel(mID);
	model = Model(objFilePath, ModelName, std::move(modelData));

	// Async cleanup
	pAssetLoader->mWorkers_MeshLoad.AddTask([=]() 
	{
		SCOPED_CPU_MARKER("CleanUpGLTFData");
		cgltf_free(data);
	});

	// Assign texture IDs
	MaterialTextureAssignments.DoAssignments(pScene, pScene->mMtxTexturePaths, pScene->mTexturePaths, pRenderer);

	pRenderer->WaitHeapsInitialized();
	for (const auto& [meshID, matID] : model.mData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH))
	{
		Mesh& mesh = pScene->GetMesh(meshID);
		mesh.CreateBuffers(pRenderer);
	}
	//pObject->mModelID = modelID;

	pRenderer->UploadVertexAndIndexBufferHeaps();

	t.Stop();
	Log::Info("   [%.2fs] Loaded Model '%s': %d meshes, %d materials",
		fTimeReadFile + t.DeltaTime(),
		ModelName.c_str(),
		model.mData.GetNumMeshesOfAllTypes(),
		model.mData.GetMaterials().size());

	return mID;
}