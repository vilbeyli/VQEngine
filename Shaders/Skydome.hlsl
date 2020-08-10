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

#define PI 3.14159265359f
#define TWO_PI 6.28318530718f
#define PI_OVER_TWO 1.5707963268f
#define PI_SQUARED 9.86960440109f
// additional sources: 
// - Converting to/from cubemaps: http://paulbourke.net/miscellaneous/cubemaps/
// - Convolution: https://learnopengl.com/#!PBR/IBL/Diffuse-irradiance
// - Projections: https://gamedev.stackexchange.com/questions/114412/how-to-get-uv-coordinates-for-sphere-cylindrical-projection
float2 SphericalSample(float3 v)
{
	// https://msdn.microsoft.com/en-us/library/windows/desktop/bb509575(v=vs.85).aspx
	// The signs of the x and y parameters are used to determine the quadrant of the return values 
	// within the range of -PI to PI. The atan2 HLSL intrinsic function is well-defined for every point 
	// other than the origin, even if y equals 0 and x does not equal 0.
	float2 uv = float2(atan2(v.z, v.x), asin(-v.y));
	uv /= float2(-TWO_PI, PI);
	uv += float2(0.5, 0.5);
	return uv;
}


struct PSInput
{
    float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
	float3 CubemapLookDirection : TEXCOORD1;
};

cbuffer CBuffer : register(b0)
{
	float4x4 matViewProj;
}


Texture2D    texEquirectEnvironmentMap;
SamplerState Sampler;

PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD0)
{
    PSInput result;

	result.position = mul(matViewProj, position).xyww; // Z=1 for depth testing
	result.uv = uv;
	result.CubemapLookDirection = normalize(position.xyz);

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 uv = SphericalSample(normalize(input.CubemapLookDirection));
	float3 ColorTex = texEquirectEnvironmentMap.SampleLevel(Sampler, uv, 0).rgb;
	return float4(ColorTex, 1);
}
