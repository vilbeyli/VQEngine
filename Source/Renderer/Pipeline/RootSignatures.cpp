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


#include "Renderer.h"
#include "Shaders/LightingConstantBufferData.h"
#include "Engine/GPUMarker.h"

using namespace Microsoft::WRL;
using namespace VQSystemInfo;


enum EDefaultSampler
{
	POINT_BORDER = 0,
	POINT_WRAP,
	POINT_CLAMP,

	BILINEAR_BORDER,
	BILINEAR_WRAP,
	BILINEAR_CLAMP,

	TRILINEAR_BORDER,
	TRILINEAR_WRAP,
	TRILINEAR_CLAMP,

	ANISOTROPIC_BORDER,
	ANISOTROPIC_WRAP,
	ANISOTROPIC_CLAMP,

	NUM_DEFAULT_SAMPLERS
};
static D3D12_STATIC_SAMPLER_DESC GetDefaultSamplerDesc(EDefaultSampler eSampler, D3D12_SHADER_VISIBILITY eShaderVisibility, UINT uShaderRegister = 0, UINT uRegisterSpace = 0)
{
	D3D12_STATIC_SAMPLER_DESC desc = {};

	switch (eSampler) // Address Mode
	{
	case POINT_BORDER:
	case BILINEAR_BORDER:
	case TRILINEAR_BORDER:
	case ANISOTROPIC_BORDER:
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		break;

	case POINT_WRAP:
	case BILINEAR_WRAP:
	case TRILINEAR_WRAP:
	case ANISOTROPIC_WRAP:
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		break;

	case POINT_CLAMP:
	case BILINEAR_CLAMP:
	case TRILINEAR_CLAMP:
	case ANISOTROPIC_CLAMP:
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		break;
	}

	switch (eSampler) // Filter
	{
	case POINT_BORDER:
	case POINT_WRAP:
	case POINT_CLAMP:
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		break;

	case BILINEAR_BORDER:
	case BILINEAR_WRAP:
	case BILINEAR_CLAMP:
		desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		break;

	case TRILINEAR_BORDER:
	case TRILINEAR_WRAP:
	case TRILINEAR_CLAMP:
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		break;

	case ANISOTROPIC_BORDER:
	case ANISOTROPIC_WRAP:
	case ANISOTROPIC_CLAMP:
		desc.Filter = D3D12_FILTER_ANISOTROPIC;
		break;
	}

	desc.MipLODBias = 0;
	desc.MaxAnisotropy = 0;
	desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;

	desc.MinLOD = 0.0f;
	desc.MaxLOD = D3D12_FLOAT32_MAX;
	desc.ShaderRegister = uShaderRegister;
	desc.RegisterSpace = uRegisterSpace;
	desc.ShaderVisibility = eShaderVisibility;
	return desc;
}
static void ReportErrorAndReleaseBlob(ComPtr<ID3DBlob>& pBlob)
{
	if (pBlob)
	{
		OutputDebugStringA((char*)pBlob->GetBufferPointer());
		pBlob->Release();
	}
}


void VQRenderer::LoadBuiltinRootSignatures()
{
	SCOPED_CPU_MARKER("RootSignatures");
	HRESULT hr = {};
	{
		SCOPED_CPU_MARKER_C("WAIT_DEVICE_CREATE", 0xFF0000FF);
		mLatchDeviceInitialized.wait();
	}
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;

	D3D12_STATIC_SAMPLER_DESC PBRsamplers[5] =
	{
		GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP , D3D12_SHADER_VISIBILITY_PIXEL, 0),
		GetDefaultSamplerDesc(EDefaultSampler::POINT_WRAP, D3D12_SHADER_VISIBILITY_PIXEL, 1),
		GetDefaultSamplerDesc(EDefaultSampler::ANISOTROPIC_WRAP, D3D12_SHADER_VISIBILITY_PIXEL, 2),
		GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_CLAMP, D3D12_SHADER_VISIBILITY_PIXEL, 3),
		GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP , D3D12_SHADER_VISIBILITY_ALL, 4)
	};


	// ROOT SIGNATURES 
	//hardcoded for now. TODO: http://simonstechblog.blogspot.com/2019/06/d3d12-root-signature-management.html
	// https://youtu.be/Wbnw87tYqVg?t=1903 : Root signature examples
	
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

	// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(pDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}
	
	// Fullscreen-Triangle Root Signature : [1]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);
		D3D12_STATIC_SAMPLER_DESC sampler = GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_BORDER, D3D12_SHADER_VISIBILITY_PIXEL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_FSTriangle");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__FullScreenTriangle] = pRS;
	}

	// Hello-World-Cube Root Signature : [2]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		//rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_VERTEX); // InitAsCBufferView?
		rootParameters[1].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_VERTEX);

		D3D12_STATIC_SAMPLER_DESC sampler = GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP, D3D12_SHADER_VISIBILITY_PIXEL);
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_HelloWorldCube");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__HelloWorldCube] = pRS;
	}

	// Tonemapper Root Signature : [3]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "CS__SRV1_UAV1_ROOTCBV1");
		mRootSignatureLookup[EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1] = pRS;
	}
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[3].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "CS__SRV2_UAV1_ROOTCBV1");
		mRootSignatureLookup[EBuiltinRootSignatures::CS__SRV2_UAV1_ROOTCBV1] = pRS;
	}

	// Object Root Signature : [4]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		D3D12_STATIC_SAMPLER_DESC samplers[2] = {
			GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP, D3D12_SHADER_VISIBILITY_PIXEL, 0),
			GetDefaultSamplerDesc(EDefaultSampler::POINT_WRAP    , D3D12_SHADER_VISIBILITY_PIXEL, 1) 
		};

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplers), &samplers[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_Object");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__Object] = pRS;
	}

	// ForwardLighting Root Signature : [5]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[9];
		// material textures
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/); // heightmap
		ranges[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 9, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/); // ssao
		
		// shadow maps
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, NUM_SHADOWING_LIGHTS__DIRECTIONAL, 13, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, NUM_SHADOWING_LIGHTS__SPOT       , 16, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, NUM_SHADOWING_LIGHTS__POINT      , 22, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 10, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 11, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 12, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		//ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC); // perView  cb's are DescRanges
		//ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC); // perFrame cb's are DescRanges

		CD3DX12_ROOT_PARAMETER1 rootParameters[13]; 
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
#if 0
		// ConstantBufferView functionality for dynamic buffer heaps (which hold constant buffer data) is currently not supported
		rootParameters[2].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL); // perView
		rootParameters[3].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL); // perFrame
#else
		// use RootConstantBufferViews for now (2 DWORDS each for this RS, = 6 DWORDS for CBVs alone)
		rootParameters[2].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[3].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
#endif
		rootParameters[12].InitAsConstantBufferView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		// Shadowmap bindings
		rootParameters[4].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[5].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[6].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);
		
		// Environment map bindings
		rootParameters[7].InitAsDescriptorTable(1, &ranges[4], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[8].InitAsDescriptorTable(1, &ranges[5], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[9].InitAsDescriptorTable(1, &ranges[6], D3D12_SHADER_VISIBILITY_PIXEL);

		// SSAO map binding
		rootParameters[10].InitAsDescriptorTable(1, &ranges[8], D3D12_SHADER_VISIBILITY_PIXEL);
		
		// Heightmap binding
		rootParameters[11].InitAsDescriptorTable(1, &ranges[7], D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(PBRsamplers), &PBRsamplers[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_ForwardLighting");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ForwardLighting] = pRS;
	}


	// Wireframe/Unlit Root Signature : [6]
	{
		//CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		//ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		
		CD3DX12_ROOT_PARAMETER1 rootParameters[1];
		rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_VERTEX);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_WireframeUnlit");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__WireframeUnlit] = pRS;
	}

	// ShadowDepthPass Root Signature [8]
	{
		D3D12_STATIC_SAMPLER_DESC samplers[2] = { 
			  GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP, D3D12_SHADER_VISIBILITY_PIXEL, 0) 
			, GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP, D3D12_SHADER_VISIBILITY_ALL  , 1) 
		};
		
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[5];
		size_t iRoot = 0;
		rootParameters[iRoot++].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[iRoot++].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
		//rootParameters[iRoot++].InitAsConstantBufferView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[iRoot++].InitAsConstantBufferView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[iRoot++].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[iRoot++].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);


		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplers), samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));

		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_MaskedDepthVSPS");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ShadowPass] = pRS;
	}
	
	// Convolution Cubemap Root Signature [10]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);

		CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		//rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_GEOMETRY);
		rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // TODO: temp
		//rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // TODO: temp
		rootParameters[2].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[3].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC samplers[1] = { GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP , D3D12_SHADER_VISIBILITY_PIXEL, 0) };

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(
			  _countof(rootParameters), rootParameters
			, _countof(samplers), samplers
			, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_ConvolutionCubemap");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ConvolutionCubemap] = pRS;
	}

	// BRDF Integration CS Root Signature : [12]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[1];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_BRDFIntegrationCS");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__BRDFIntegrationCS] = pRS;
	}
	
	// FFX-SPD CS Root Signature : [13]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[3].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_FFX-SPD_CS");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__FFX_SPD_CS] = pRS;
	}

	// DepthPrePass Root Signature : [14]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		// material textures
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/); // heightmap

		CD3DX12_ROOT_PARAMETER1 rootParameters[5];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL); // Material texture set
		rootParameters[1].InitAsConstantBufferView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // cbPerObj
		rootParameters[2].InitAsConstantBufferView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // cbTess
		rootParameters[3].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL); // Heightmap
		rootParameters[4].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // cbPerView

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(PBRsamplers), &PBRsamplers[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));

		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_DepthPrePass");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ZPrePass] = pRS;
	}

	// OutlinePass Root Signature
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		
		// material textures
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/); // heightmap

		CD3DX12_ROOT_PARAMETER1 rootParameters[5];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL); // Material texture set
		rootParameters[1].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // cbOutline
		rootParameters[2].InitAsConstantBufferView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // cbTess
		rootParameters[3].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL); // Heightmap
		rootParameters[4].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // cbPerView

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(PBRsamplers), &PBRsamplers[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));

		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_DepthPrePass");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__OutlinePass] = pRS;
	}

	// FSR1 Root Signature : [15]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		//ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
		//rootParameters[3].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);

		D3D12_STATIC_SAMPLER_DESC samplers[1] = { GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP, D3D12_SHADER_VISIBILITY_ALL, 0) };

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplers), samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_FidelityFX_SuperResolution");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__FFX_FSR1] = pRS;
	}

	// UIHDRComposite Root Signature : [16]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		//rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[2].InitAsConstants(1, 0);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_UIHDRComposite");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__UI_HDR_Composite] = pRS;
	}

	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[4];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1 , 0 , 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 13, 0 , 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1 , 13, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		// CBV ?

		CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[3].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_DownsampleDepth");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__DownsampleDepthCS] = pRS;
	}

	mLatchRootSignaturesInitialized.count_down();
}

