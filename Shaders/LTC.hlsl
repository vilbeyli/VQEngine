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

// white paper: https://drive.google.com/file/d/0BzvWIdpUpRx_d09ndGVjNVJzZjA/view?resourcekey=0-21tmiqk55JIZU8UoeJatXQ
// siggraph   : https://advances.realtimerendering.com/s2016/s2016_ltc_rnd.pdf
// slides     : https://sgvr.kaist.ac.kr/~sungeui/ICG_F18/Students/Real-Time%20Polygonal-Light%20Shading%20with%20Linearly%20Transformed%20Cosines.pdf
// author page: https://eheitzresearch.wordpress.com/415-2/


// Reference Edge Integral
//
// - acos() can cause ringing artefacts in high-intensity lighting & smooth receiver cases
// 
// float3 IntegrateEdge(float3 v1, float3 v2, float3 N) 
// {
// 	float theta = acos(dot(v1, v2));
// 	float3 u = normalize(cross(v1, v2)); // causes major artefacts
// 	return theta * dot(u, N);
// }
//
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


// Heitz et al., linearly transformed cosines
float3 LTC_Evaluate_Rectangular(float3 N, float3 V, float3 P, float3x3 Minv, float3 points[4])
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
	
	return vsum.xxx	;
}