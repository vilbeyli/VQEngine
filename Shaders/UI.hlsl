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



cbuffer vertexBuffer : register(b0) 
{
    float4x4 ProjectionMatrix; 
};

struct VS_INPUT
{
    float2 pos : POSITION;
    float2 uv  : TEXCOORD;
    uint col : COLOR;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD;
    float4 col : COLOR;
};

float4 UnpackColorRGBA8_UNORM(uint c)
{
	float4 color;
	color.r = ((c >> 0 ) & 0xFF) / 255.0f;
	color.g = ((c >> 8 ) & 0xFF) / 255.0f;
	color.b = ((c >> 16) & 0xFF) / 255.0f;
	color.a = ((c >> 24) & 0xFF) / 255.0f;
	return color;
}

sampler   sampler0;
Texture2D texture0;

PS_INPUT VSMain(VS_INPUT input)
{
	PS_INPUT output;
	output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));
	output.col = UnpackColorRGBA8_UNORM(input.col);
	output.uv = input.uv;
	return output;
};

float4 PSMain(PS_INPUT input) : SV_Target
{
	return input.col * texture0.Sample(sampler0, input.uv);;
};
