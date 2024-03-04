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

struct VSInput
{
	float3 vLocalPosition : POSITION;
	float3 vNormal   : NORMAL;
	float3 vTangent : TANGENT;
	uint InstanceID : SV_InstanceID;
};

struct GSInput
{
	float4 vPosition : POSITION;
	float3 vNormal : NORMAL;
	float3 vTangent : TANGENT;
	uint InstanceID : SV_InstanceID;
};

struct PSInput
{
	float4 vPosition : SV_POSITION;
	float3 vColor : COLOR;
};

struct CB
{
	matrix matWorld;
	matrix matNormal;
	matrix matViewProj;
	float LocalAxisSize;
};

cbuffer CBPerObject : register(b2)
{
	CB cb;
}

GSInput VSMain(VSInput In)
{
	GSInput o = (GSInput) 0;
	o.vPosition = float4(In.vLocalPosition, 1);
	o.vNormal   = In.vNormal;
	o.vTangent  = In.vTangent;
	o.InstanceID = In.InstanceID;
	return o;
}

// taking in a single shaded point
// outputting 3 lines representing local space axes
static const int NUM_LINES = 3;
[maxvertexcount(2 * NUM_LINES)]
void GSMain(point GSInput input[1], inout LineStream<PSInput> outStream)
{
	const float3 tbn0 = normalize(input[0].vTangent);
	const float3 tbn1 = normalize(cross(input[0].vNormal, input[0].vTangent));
	const float3 tbn2 = normalize(input[0].vNormal);
	static const float3 colR = float3(0.6f, 0, 0);
	static const float3 colG = float3(0, 0.6f, 0);
	static const float3 colB = float3(0, 0, 0.6f);
	
	const float3 LineVertPositionWS = mul(cb.matWorld, float4(input[0].vPosition.xyz, 1)).xyz;
	const float4 LineVertPositionCS = mul(cb.matViewProj, float4(LineVertPositionWS, 1));
	
	const float3 OffsetWS0 = normalize(mul(cb.matNormal, float4(tbn0, 0)).xyz) * cb.LocalAxisSize;
	const float3 OffsetWS1 = normalize(mul(cb.matNormal, float4(tbn1, 0)).xyz) * cb.LocalAxisSize;
	const float3 OffsetWS2 = normalize(mul(cb.matNormal, float4(tbn2, 0)).xyz) * cb.LocalAxisSize;
	
	PSInput o;
	o.vPosition = LineVertPositionCS;
	o.vColor = colR;
	outStream.Append(o);
	
	o.vPosition = mul(cb.matViewProj, float4(LineVertPositionWS + OffsetWS0, 1));
	outStream.Append(o);
	outStream.RestartStrip();
	
	o.vPosition = LineVertPositionCS;
	o.vColor = colB;
	outStream.Append(o);
	
	o.vPosition = mul(cb.matViewProj, float4(LineVertPositionWS + OffsetWS1, 1));
	outStream.Append(o);
	outStream.RestartStrip();
	
	o.vPosition = LineVertPositionCS;
	o.vColor = colG;
	outStream.Append(o);
	
	o.vPosition = mul(cb.matViewProj, float4(LineVertPositionWS + OffsetWS2, 1));
	outStream.Append(o);
}

half4 PSMain(PSInput In) : SV_Target
{
	return half4(In.vColor, 1.0f);
}