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

#include "ShadingMath.hlsl"
#include "BRDF.hlsl"

struct VSInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;  // unused
	float3 tangent  : TANGENT; // unused
	float2 uv       : TEXCOORD0;
};
struct VSOut
{
    float4 svPosition         : SV_POSITION;
	float3 localSpacePosition : COLOR;
	float2 uv                 : TEXCOORD0;
	uint instanceID           : SV_InstanceID;
};
struct GSOut
{
	float4 position             : SV_Position;
	float2 uv                   : TEXCOORD0;
	float3 CubemapLookDirection : COLOR;
	uint layer                  : SV_RenderTargetArrayIndex;
};


cbuffer CBufferGS : register(b0)
{
	matrix matViewProj[6];
}
cbuffer CBufferPS : register(b1)
{
	float2 TextureDimensionsLOD0;
	float Roughness;
	int MIP;
}

Texture2D texEquirectEnvironmentMap : register(t0);
SamplerState Sampler;

TextureCube tEnvironmentMap : register(t1);



GSOut VSMain_PerFace(VSInput VSIn)
{
	GSOut result;
	
	result.position = mul(matViewProj[0], float4(VSIn.position.xyz, 1.0f));
	result.uv = VSIn.uv;
	result.CubemapLookDirection = VSIn.position.xyz;
	result.layer = 0;
	
	return result;
}

VSOut VSMain(VSInput VSIn, uint instID : SV_InstanceID)
{
	VSOut result;
	
	result.svPosition = float4(VSIn.position.xyz, 1.0f);
	result.uv = VSIn.uv;
	result.instanceID = instID;
	result.localSpacePosition = VSIn.position.xyz;
	
	return result;
}

// Note: GS path is not preferred as each draw takes x6 longer due to instanced drawing
//       and that will limit the available time before a draw call takes too long to
//       trigger a TDR since we're making a lot of texture lookups. Hence, draw-
//       call per cube face is preferred.
//       CS is even better with 'UV -> spherical look direction' calculation.
[maxvertexcount(3)]
void GSMain(triangle VSOut input[3], inout TriangleStream<GSOut> triOutStream)
{
	GSOut output;
	
	uint instID = input[0].instanceID;
	for (int vertex = 0; vertex < 3; ++vertex)
	{
		output.position = mul(matViewProj[instID], input[vertex].svPosition);
		output.uv       = input[vertex].uv;
		output.layer    = instID;
		output.CubemapLookDirection = input[vertex].localSpacePosition;

		triOutStream.Append(output);
	}
}


// src: https://learnopengl.com/PBR/IBL/Diffuse-irradiance
// src: https://www.indiedb.com/features/using-image-based-lighting-ibl
float4 PSMain_DiffuseIrradiance(GSOut In) : SV_TARGET
{
	float3 N = normalize(In.CubemapLookDirection);
	
	// basis vectors
	float3 up = float3(0, 1, 0);
	
	// if UP and N align (top and bottom of sphere),
	// then we'll have 0 or almost 0 vector as the result of the cross product. 
	// Normalizing here avoids the above case.
	const float3 right = normalize(cross(up, N));
	
	up = normalize(cross(N, right));
	
	float3 irradiance = 0.0f.xxx;
	
	const float INTEGRATION_STEP = 0.025f;
	float numSamples = 0.0f;
	for (float phi = 0.0f; phi < TWO_PI; phi += INTEGRATION_STEP) // ~60 iterations
	{
		// theta = 0.0f doesn't yield any irradiance (sinTheta==0.0f)
		// , so start from 0.1 -> not exactly from the pole
		for (float theta = 0.10f; theta < PI_OVER_TWO; theta += INTEGRATION_STEP) // ~60 iterations
		{
			const float sinTheta = sin(theta);
			const float cosTheta = cos(theta);
			
			const float sinPhi = sin(phi);
			const float cosPhi = cos(phi);
			
			float3 tangentSample = float3(
			    sinTheta * cosPhi
			  , sinTheta * sinPhi
			  , cosTheta
			);
			
			float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
			sampleVec = normalize(sampleVec);
			
			irradiance += texEquirectEnvironmentMap.Sample(Sampler, SphericalSample(sampleVec)) * cosTheta * sinTheta;
			numSamples += 1.0f;
		}
	}
	
	irradiance = PI * irradiance / numSamples;
	
	return float4(irradiance, 1);
}

// src: https://learnopengl.com/PBR/IBL/Specular-IBL
// src: https://de45xmedrsdbp.cloudfront.net/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
float4 PSMain_SpecularIrradiance(GSOut In) : SV_TARGET
{
	// approximation: Assume viewing angle of 0 degrees.
	//                This results in the lack of isotropic (stretched out) reflections.
	float3 N = normalize(In.CubemapLookDirection);
	float3 R = N;
	float3 V = R;
	
	float3 prefilteredColor = 0.0f.xxx;
	float totalWeight = 0.0f;
	
	const uint NUM_SAMPLES = 2048u;
	for (uint i = 0; i < NUM_SAMPLES; ++i)
	{
		float2 Xi = Hammersley(i, NUM_SAMPLES);
		float3 H = ImportanceSampleGGX(Xi, N, Roughness);
		float3 L = reflect(-V, H);

		float NdotL = saturate(dot(N, L));
		if (NdotL > 0.0f)
		{
			//prefilteredColor += tEnvironmentMap.Sample(Sampler, L).rgb * NdotL;
			prefilteredColor += texEquirectEnvironmentMap.Sample(Sampler, SphericalSample(L)) * NdotL;
			totalWeight += NdotL;
		}
	}
	prefilteredColor /= totalWeight;

	return float4(prefilteredColor, 1);
}