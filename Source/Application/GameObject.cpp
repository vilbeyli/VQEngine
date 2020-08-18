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

#include "GameObject.h"
#if 0
#include "Renderer/Renderer.h"
#include "RenderPasses/RenderPasses.h"
#include "SceneResourceView.h"
#include "SceneView.h"

#include "Engine.h"

GameObject::GameObject(Scene* pScene) : mpScene(pScene) {};

void GameObject::AddMesh(MeshID meshID)
{
	//if (meshID >= 0 && meshID < EGeometry::MESH_TYPE_COUNT)
		mModel.mbLoaded = true;

	mModel.mData.mMeshIDs.push_back(meshID);
}


void GameObject::AddMesh(MeshID meshID, const MeshRenderSettings& renderSettings)
{
	mModel.mbLoaded = true;
	mModel.mData.mMeshRenderSettingsLookup[meshID] = renderSettings;
	mModel.mData.mMeshIDs.push_back(meshID);
}

void GameObject::AddMaterial(Material * pMat)
{
	if (mModel.mbLoaded)
		mModel.AddMaterialToMesh(mModel.mData.mMeshIDs.back(), pMat->ID, pMat->IsTransparent());
	else
		mModel.mMaterialAssignmentQueue.push(pMat->ID);
}


void GameObject::SetMeshMaterials(const Material* pMat)
{
	if (mModel.mbLoaded)
		mModel.OverrideMaterials(pMat->ID);
	else
		assert(false); // TODO: impl mesh override queue or resolve the ambiguity between material assignment queue and this case
}

void GameObject::RenderTransparent(
	  Renderer * pRenderer
	, const FSceneView& sceneView
	, bool UploadMaterialDataToGPU
	, const MaterialPool& materialBuffer) const
{
	const EShaders shader = static_cast<EShaders>(pRenderer->GetActiveShader());
	const XMMATRIX world = mTransform.WorldTransformationMatrix();
	const XMMATRIX wvp = world * sceneView.viewProj;

	// SET MATRICES
	switch (shader)
	{
	case EShaders::TBN:
		pRenderer->SetConstant4x4f("world", world);
		pRenderer->SetConstant4x4f("viewProj", sceneView.viewProj);
		pRenderer->SetConstant4x4f("normalMatrix", mTransform.NormalMatrix(world));
		break;
	case EShaders::NORMAL:
		pRenderer->SetConstant4x4f("normalMatrix", mTransform.NormalMatrix(world));
	case EShaders::UNLIT:
	case EShaders::TEXTURE_COORDINATES:
		pRenderer->SetConstant4x4f("worldViewProj", wvp);
		break;
	default:	// lighting shaders
		pRenderer->SetConstant4x4f("world", world);
		pRenderer->SetConstant4x4f("normalMatrix", mTransform.NormalMatrix(world));
		pRenderer->SetConstant4x4f("worldViewProj", wvp);
		break;
	}

	// SET GEOMETRY & MATERIAL, THEN DRAW
	for(MeshID id : mModel.mData.mTransparentMeshIDs)
	{
		const auto IABuffer = SceneResourceView::GetVertexAndIndexBufferIDsOfMesh(mpScene, id, this);

		// SET MATERIAL CONSTANTS
		if (UploadMaterialDataToGPU)
		{
			const bool bMeshHasMaterial = mModel.mData.mMaterialLookupPerMesh.find(id) != mModel.mData.mMaterialLookupPerMesh.end();

			if (bMeshHasMaterial)
			{
				const MaterialID materialID = mModel.mData.mMaterialLookupPerMesh.at(id);
				const Material* pMat = materialBuffer.GetMaterial_const(materialID);
				pMat->SetMaterialConstants(pRenderer, shader, sceneView.bIsDeferredRendering && shader != FORWARD_BRDF);
			}
			else
			{
				materialBuffer.GetDefaultMaterial(GGX_BRDF)->SetMaterialConstants(pRenderer, shader, sceneView.bIsDeferredRendering);
			}
		}

		pRenderer->SetVertexBuffer(IABuffer.first);
		pRenderer->SetIndexBuffer(IABuffer.second);
		pRenderer->Apply();
		pRenderer->DrawIndexed();
	};
}

void GameObject::Clear()
{
	mTransform = Transform();
	mModel = Model();
	mBoundingBox = BoundingBox();
	mRenderSettings = GameObjectRenderSettings();
}




DirectX::XMMATRIX BoundingBox::GetWorldTransformationMatrix() const
{
	Transform tf;
	const vec3 diag = this->hi - this->low;
	const vec3 pos = (this->hi + this->low) * 0.5f;
	tf.SetScale(diag * 0.5f);
	tf.SetPosition(pos);
	return tf.WorldTransformationMatrix();
}
#endif