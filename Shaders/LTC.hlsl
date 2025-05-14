//	VQE
//	Copyright(C) 2025  - Volkan Ilbeyli
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

#define PI          3.14159265359f

// ------------------------------------------------------------------------------------------------------------------------
// REFERENCES
// ------------------------------------------------------------------------------------------------------------------------
// author page   : https://eheitzresearch.wordpress.com/415-2/
// white paper   : https://drive.google.com/file/d/0BzvWIdpUpRx_d09ndGVjNVJzZjA/view?resourcekey=0-21tmiqk55JIZU8UoeJatXQ
// siggraph 2016 : https://advances.realtimerendering.com/s2016/s2016_ltc_rnd.pdf
// siggraph 2017 : https://blog.selfshadow.com/publications/s2017-shading-course/heitz/s2017_pbs_ltc_lines_disks.pdf
// GPU Zen 1     : https://hal.science/hal-02155101v1/file/LTC_linearlights_GPUZen.pdf
// slides        : https://sgvr.kaist.ac.kr/~sungeui/ICG_F18/Students/Real-Time%20Polygonal-Light%20Shading%20with%20Linearly%20Transformed%20Cosines.pdf
// polygon lights: https://www.jcgt.org/published/0011/01/01/paper.pdf
// github        : https://github.com/selfshadow/ltc_code/tree/master/webgl/shaders/ltc


// ------------------------------------------------------------------------------------------------------------------------
// UTILITIES
// ------------------------------------------------------------------------------------------------------------------------
float3x3 inverse(float3x3 m)
{
	// Compute cofactors using cross products of columns
	float3 r0 = cross(m[1], m[2]);
	float3 r1 = cross(m[2], m[0]);
	float3 r2 = cross(m[0], m[1]);

	// Compute determinant & inverse
	float det = dot(m[0], r0);
	if (abs(det) < 1e-6) // Check for singular or near-singular matrix
	{
		return float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
	}
	float invDet = 1.0 / det;

	// Construct cofactor matrix scaled by 1/det
	float3x3 adjugate = float3x3(
		r0 * invDet,
		r1 * invDet,
		r2 * invDet
	);
	
	return transpose(adjugate); // Transpose to get the inverse (adjugate / det)
}
float2 LTCGetUVs(float roughness, float NdotV)
{
	return float2(roughness, 1.0f - sqrt(1.0f - NdotV));
}
float3x3 LTCMinv(float4 t1) // t1: samples from LTC texture 1 (compacted Minv)
{
	return float3x3(
		t1.x, 0.0f, t1.y,
		0.0f, 1.0f, 0.0f,
		t1.z, 0.0f, t1.w
	);
}
float3x3 LTCMinv(Texture2D LTC1, SamplerState LinearSampler, float roughness, float NdotV)
{
	float2 uvLTC = LTCGetUVs(roughness, NdotV);
	float4 t1 = LTC1.Sample(LinearSampler, uvLTC);
	return LTCMinv(t1);
}

// ------------------------------------------------------------------------------------------------------------------------
// EDGE INTEGRAL
// ------------------------------------------------------------------------------------------------------------------------
// float3 IntegrateEdge(float3 v1, float3 v2, float3 N) 
// {
// 	float theta = acos(dot(v1, v2));
// 	float3 u = normalize(cross(v1, v2)); // causes major artefacts
// 	return theta * dot(u, N);
// }
//
// acos() can cause ringing artefacts in high-intensity lighting & smooth receiver cases.
// mathematically equivalent to above, works around some of the artefacts 
// although there's still ringing artefacts present in this form
// float3 IntegrateEdge(float3 v1, float3 v2, float3 N) 
// {
// 	float theta = acos(dot(v1, v2));
// 	float3 u = cross(v1, v2) / sin(theta); 
// 	return theta * dot(u, N); 
// }
//
// solution is to fit theta / sin(theta)
// See: https://advances.realtimerendering.com/s2016/s2016_ltc_rnd.pdf
float EdgeIntegral_0(float3 v1, float3 v2, float3 N)
{
	float x = dot(v1, v2);
	float y = abs(x);

	float a = 5.42031f + (3.12829f + 0.0902326f * y) * y;
	float b = 3.45068f + (4.18814f + y) * y;
	float theta_sintheta = a / b;
	
	if (x < 0.0f)
		theta_sintheta = PI * rsqrt(1.0f - x * x) - theta_sintheta;
	
	float3 u = cross(v1, v2);

	return theta_sintheta * dot(u, N);
}
float EdgeIntegralDiffuse(float3 v1, float3 v2, float3 N)
{
	float x = dot(v1, v2);
	float y = abs(x);
	
	float theta_sintheta = 1.5708f + (-0.879406f + 0.308609f * y) * y;

	if (x < 0.0f)
		theta_sintheta = PI * rsqrt(1.0f - x * x) - theta_sintheta;
	
	float3 u = cross(v1, v2);
	
	return theta_sintheta * dot(u, N);
}
float EdgeIntegral(float3 v1, float3 v2, float3 N)
{
	float x = dot(v1, v2);
	float y = abs(x);

	float a = 0.8543985f + (0.4965155f + 0.0145206f * y) * y;
	float b = 3.4175940f + (4.1616724f + y) * y;
	float v = a / b;
	
	float theta_sintheta = (x > 0.0f) ? v : (0.5f * rsqrt(max(1.0f - x * x, 1e-7)) - v);
	
	return dot(cross(v1, v2) * theta_sintheta, N);
}


// ------------------------------------------------------------------------------------------------------------------------
// ANALYTIC SOLUTIONS FOR LIGHT SHAPES
// ------------------------------------------------------------------------------------------------------------------------
// See GPU Zen source for line analytic integral
float Fpo(float d, float l)
{
	float d2 = d * d;
	float l2 = l * l;
	return l / (d * (d2 + l2)) + atan(1.0f / d) / d2;
}
float Fwt(float d, float l)
{
	float d2 = d * d;
	float l2 = l * l;
	return l2 / (d * (d2 + l2)); // how was this derived???
}
float I_diffuse_line(float3 p1, float3 p2)
{
	// tangent 
	float3 wt = normalize(p2 - p1);
	
	// clamping
	if (p1.z <= 0.0f && p2.z < 0.0f) return 0.0f;  // line entirely below horizon
	if (p1.z < 0.0f) p1 = (+p1*p2.z - p2*p1.z) / (+p2.z - p1.z);
	if (p2.z < 0.0f) p2 = (-p1*p2.z + p2*p1.z) / (-p2.z + p1.z);
	
	// parameterization
	float l1 = dot(p1, wt);
	float l2 = dot(p2, wt);
	
	// shading point orthonormal projection on line (axis)
	float3 po = p1 - l1 * wt;
	
	// distance to line
	float d = length(po);
	
	// integral
	float I = (Fpo(d, l2) - Fpo(d, l1)) * po.z +
	          (Fwt(d, l2) - Fwt(d, l1)) * wt.z;
	
	return I / PI;
}
// ------------------------------------------------------------------------------------------------------------------------
// LINEARLY TRANSFORMED COSINE EVALUATIONS
// ------------------------------------------------------------------------------------------------------------------------
float3 I_ltc_quad(float3 N, float3 V, float3 P, float3x3 Minv, float3 points[4])
{
	// construct tangent basis
	float3 T1 = normalize(V - N * dot(V, N));
	float3 T2 = cross(N, T1);

	// transform tangent basis by LTC matrix
	Minv = Minv * float3x3(T1, T2, N);

	// transform polygon into LTC transformed tangent space
	float3 L[4];
	L[0] = mul(Minv, (points[0] - P));
	L[1] = mul(Minv, (points[1] - P));
	L[2] = mul(Minv, (points[2] - P));
	L[3] = mul(Minv, (points[3] - P));
	
	// use tabulated horizon-clipped sphere
	// check if the shading point is behind the light
	float3 dir = points[0] - P; // LTC space
	float3 lightNormal = cross(points[1] - points[0], points[3] - points[0]);
	bool behind = (dot(dir, lightNormal) < 0.0);
	
	// shading space
	L[0] = normalize(L[0]);
	L[1] = normalize(L[1]);
	L[2] = normalize(L[2]);
	L[3] = normalize(L[3]);
	
	// integrate
	float3 vsum = 0.0f.xxx;
	vsum += EdgeIntegral(L[0], L[1], N);
	vsum += EdgeIntegral(L[1], L[2], N);
	vsum += EdgeIntegral(L[2], L[3], N);
	vsum += EdgeIntegral(L[3], L[0], N);

	// form factor of the polygon
	float F = length(vsum);
	
	float z = vsum.z / F;
	if(behind)
		z = -z;
	
	// TODO
	
	return vsum.xxx	;
}

float I_ltc_line(float3 p1, float3 p2, float3x3 Minv)
{
	// transform to diffuse configuration
	float3 p1o = mul(Minv, p1);
	float3 p2o = mul(Minv, p2);
	float I_diffuse = I_diffuse_line(p1o, p2o);
	
	// width factor
	float3 ortho = normalize(cross(p1, p2));
	
	float3 ortho_o = mul(ortho, inverse(transpose(Minv)));
	float w = 1.0f / length(ortho_o);
	
	return w * I_diffuse;
}