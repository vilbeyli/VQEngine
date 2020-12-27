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


#ifndef _SHADING_MATH_H
#define _SHADING_MATH_H

// Method for Hammersley Sequence creation
#define USE_BIT_MANIPULATION 1
#define PI          3.14159265359f
#define TWO_PI      6.28318530718f
#define PI_OVER_TWO 1.5707963268f
#define PI_SQUARED  9.86960440109f
#define SQRT2       1.41421356237f

inline float3 UnpackNormals(Texture2D normalMap, SamplerState normalSampler, float2 uv, float3 worldNormal, float3 worldTangent)
{
	// uncompressed normal in tangent space
	float3 SampledNormal = normalMap.Sample(normalSampler, uv).xyz;
	SampledNormal = normalize(SampledNormal * 2.0f - 1.0f);

	const float3 T = normalize(worldTangent - dot(worldNormal, worldTangent) * worldNormal);
	const float3 N = normalize(worldNormal);
	const float3 B = normalize(cross(T, N));
	const float3x3 TBN = float3x3(T, B, N);
	return mul(SampledNormal, TBN);
}

inline float3 UnpackNormal(float3 SampledNormal, float3 worldNormal, float3 worldTangent)
{
	SampledNormal = SampledNormal * 2.0f - 1.0f;
	const float3 T = normalize(worldTangent - dot(worldNormal, worldTangent) * worldNormal);
	const float3 N = normalize(worldNormal);
	const float3 B = normalize(cross(T, N));
	const float3x3 TBN = float3x3(T, B, N);
	return mul(SampledNormal, TBN);
}

float3 SRGBToLinear(float3 c) { return pow(c, 2.2f); }

// additional sources: 
// - Converting to/from cubemaps: http://paulbourke.net/miscellaneous/cubemaps/
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

// the Hammersley Sequence,a random low-discrepancy sequence based on the Quasi-Monte Carlo method as carefully described by Holger Dammertz. 
// It is based on the Van Der Corpus sequence which mirrors a decimal binary representation around its decimal point.
// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html 
// https://www.scratchapixel.com/lessons/mathematics-physics-for-computer-graphics/monte-carlo-methods-in-practice/introduction-quasi-monte-carlo
#ifdef USE_BIT_MANIPULATION
float RadicalInverse_VdC(uint bits) //  Van Der Corpus
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
#else
// the non-bit-manipulation version of the above function
float VanDerCorpus(uint n, uint base)
{
    float invBase = 1.0 / float(base);
    float denom = 1.0;
    float result = 0.0;

    for (uint i = 0u; i < 32u; ++i)
    {
        if (n > 0u)
        {
            denom = n % 2.0f;
            result += denom * invBase;
            invBase = invBase / 2.0;
            n = uint(float(n) / 2.0);
        }
    }

    return result;
}
#endif

float2 Hammersley(uint i, uint count)
{
#ifdef USE_BIT_MANIPULATION
	return float2(float(i) / float(count), RadicalInverse_VdC(i));
#else
	// note: this crashes for some reason.
    return float2(float(i) / float(count), VanDerCorpus(uint(i), 2u));
#endif
}

inline float LinearDepth(in float zBufferSample, in float A, in float B)
{
	// src:
	// https://developer.nvidia.com/content/depth-precision-visualized
	// http://dev.theomader.com/linear-depth/
	// https://www.mvps.org/directx/articles/linear_z/linearz.htm
	// http://www.humus.name/index.php?ID=255
	return A / (zBufferSample - B);
}
inline float LinearDepth(in float zBufferSample, in matrix matProjInverse)
{
	return LinearDepth(zBufferSample, matProjInverse[2][3], matProjInverse[2][2]);
}

float3 ViewSpacePosition(in const float nonLinearDepth, const in float2 uv, const in matrix invProjection)
{
	// src: 
	// https://mynameismjp.wordpress.com/2009/03/10/reconstructing-position-from-depth/
	// http://www.derschmale.com/2014/01/26/reconstructing-positions-from-the-depth-buffer/

	const float x = uv.x * 2 - 1; // [-1, 1]
	const float y = (1.0f - uv.y) * 2 - 1; // [-1, 1]
	const float z = nonLinearDepth; // [ 0, 1]

	float4 projectedPosition = float4(x, y, z, 1);
	float4 viewSpacePosition = mul(invProjection, projectedPosition);
	return viewSpacePosition.xyz / viewSpacePosition.w;
}

#endif


//-------------------------------------------------------------------------------------------------------------------------------------------------
//
// SPHERICAL HARMONICS
//
//-------------------------------------------------------------------------------------------------------------------------------------------------
// Sources: 
// - https://patapom.com/blog/SHPortal/ (Link Broke)
//   - https://web.archive.org/web/20200508201042/http://www.patapom.com/blog/SHPortal/
// - https://cseweb.ucsd.edu/~ravir/papers/invlamb/josa.pdf
// - https://cseweb.ucsd.edu/~ravir/papers/envmap/envmap.pdf
// - https://github.com/sebh/HLSL-Spherical-Harmonics/blob/master/SphericalHarmonics.hlsl
// - https://web.archive.org/web/20200619094612/http://silviojemma.com/public/papers/lighting/spherical-harmonic-lighting.pdf
// - Robin Green's "gritty details"
//-------------------------------------------------------------------------------------------------------------------------------------------------

#if 0 
// Renormalisation constant for SH function
double K(int l, int m)
{
	double temp = ((2.0 * l + 1.0) * factorial(l - m)) / (4.0 * PI * factorial(l + m)); // Here, you can use a precomputed table for factorials
	return sqrt(temp);
}

 // Evaluate an Associated Legendre Polynomial P(l,m,x) at x
 // For more, see “Numerical Methods in C: The Art of Scientific Computing”, Cambridge University Press, 1992, pp 252-254 
double P(int l, int m, double x)
{
	double pmm = 1.0;
	if (m > 0)
	{
		double somx2 = sqrt((1.0 - x) * (1.0 + x));
		double fact = 1.0;
		for (int i = 1; i <= m; i++)
		{
			pmm *= (-fact) * somx2;
			fact += 2.0;
		}
	}
	if (l == m)
		return pmm;

	double pmmp1 = x * (2.0 * m + 1.0) * pmm;
	if (l == m + 1)
		return pmmp1;

	double pll = 0.0;
	for (int ll = m + 2; ll <= l; ++ll)
	{
		pll = ((2.0 * ll - 1.0) * x * pmmp1 - (ll + m - 1.0) * pmm) / (ll - m);
		pmm = pmmp1;
		pmmp1 = pll;
	}

	return pll;
}

 // Returns a point sample of a Spherical Harmonic basis function
 // l is the band, range [0..N]
 // m in the range [-l..l]
 // theta in the range [0..Pi]
 // phi in the range [0..2*Pi]
double SH(int l, int m, double theta, double phi)
{	
	     if (m == 0) return         K(l,  0) * P(l, m, cos(theta));
	else if (m >  0) return SQRT2 * K(l,  m) * cos( m * phi) * P(l,  m, cos(theta));
	else             return SQRT2 * K(l, -m) * sin(-m * phi) * P(l, -m, cos(theta));
}
float EncodeSHCoeff(int l, int m)
{
	const int STEPS_PHI = 200;
	const int STEPS_THETA = 100;
	const float dPhi = 2 * PI / STEPS_PHI;
	const float dTheta = PI / STEPS_THETA;
	float coeff = 0.0f;
	for (int i = 0; i < STEPS_PHI; i++)
	{
		float phi = i * dPhi;
		for (int j = 0; j < STEPS_THETA; j++)
		{
			float theta = (0.5f + j) * dTheta;
			float value = EstimateFunction(phi, theta);
			float SHvalue = EstimateSH(l, m, phi, theta);

			coeff += value * SHValue * sin(theta) * dPhi * dTheta;
		}
	}
	return coeff;
}
 // Performs the SH triple product r = a * b
 // From John Snyder (appendix A8)
 //
 void SHProduct( const in float4 a[9], const in float4 b[9], out float4 r[9] ) {
    float4  ta, tb, t;

    const float C0 = 0.282094792935999980;
    const float C1 = -0.126156626101000010;
    const float C2 = 0.218509686119999990;
    const float C3 = 0.252313259986999990;
    const float C4 = 0.180223751576000010;
    const float C5 = 0.156078347226000000;
    const float C6 = 0.090111875786499998;

    // [0,0]: 0,
    r[0] = C0*a[0]*b[0];

    // [1,1]: 0,6,8,
    ta = C0*a[0]+C1*a[6]-C2*a[8];
    tb = C0*b[0]+C1*b[6]-C2*b[8];
    r[1] = ta*b[1]+tb*a[1];
    t = a[1]*b[1];
    r[0] += C0*t;
    r[6] = C1*t;
    r[8] = -C2*t;

    // [1,2]: 5,
    ta = C2*a[5];
    tb = C2*b[5];
    r[1] += ta*b[2]+tb*a[2];
    r[2] = ta*b[1]+tb*a[1];
    t = a[1]*b[2]+a[2]*b[1];
    r[5] = C2*t;

    // [1,3]: 4,
    ta = C2*a[4];
    tb = C2*b[4];
    r[1] += ta*b[3]+tb*a[3];
    r[3] = ta*b[1]+tb*a[1];
    t = a[1]*b[3]+a[3]*b[1];
    r[4] = C2*t;

    // [2,2]: 0,6,
    ta = C0*a[0]+C3*a[6];
    tb = C0*b[0]+C3*b[6];
    r[2] += ta*b[2]+tb*a[2];
    t = a[2]*b[2];
    r[0] += C0*t;
    r[6] += C3*t;

    // [2,3]: 7,
    ta = C2*a[7];
    tb = C2*b[7];
    r[2] += ta*b[3]+tb*a[3];
    r[3] += ta*b[2]+tb*a[2];
    t = a[2]*b[3]+a[3]*b[2];
    r[7] = C2*t;

    // [3,3]: 0,6,8,
    ta = C0*a[0]+C1*a[6]+C2*a[8];
    tb = C0*b[0]+C1*b[6]+C2*b[8];
    r[3] += ta*b[3]+tb*a[3];
    t = a[3]*b[3];
    r[0] += C0*t;
    r[6] += C1*t;
    r[8] += C2*t;

    // [4,4]: 0,6,
    ta = C0*a[0]-C4*a[6];
    tb = C0*b[0]-C4*b[6];
    r[4] += ta*b[4]+tb*a[4];
    t = a[4]*b[4];
    r[0] += C0*t;
    r[6] -= C4*t;

    // [4,5]: 7,
    ta = C5*a[7];
    tb = C5*b[7];
    r[4] += ta*b[5]+tb*a[5];
    r[5] += ta*b[4]+tb*a[4];
    t = a[4]*b[5]+a[5]*b[4];
    r[7] += C5*t;

    // [5,5]: 0,6,8,
    ta = C0*a[0]+C6*a[6]-C5*a[8];
    tb = C0*b[0]+C6*b[6]-C5*b[8];
    r[5] += ta*b[5]+tb*a[5];
    t = a[5]*b[5];
    r[0] += C0*t;
    r[6] += C6*t;
    r[8] -= C5*t;

    // [6,6]: 0,6,
    ta = C0*a[0];
    tb = C0*b[0];
    r[6] += ta*b[6]+tb*a[6];
    t = a[6]*b[6];
    r[0] += C0*t;
    r[6] += C4*t;

    // [7,7]: 0,6,8,
    ta = C0*a[0]+C6*a[6]+C5*a[8];
    tb = C0*b[0]+C6*b[6]+C5*b[8];
    r[7] += ta*b[7]+tb*a[7];
    t = a[7]*b[7];
    r[0] += C0*t;
    r[6] += C6*t;
    r[8] += C5*t;

    // [8,8]: 0,6,
    ta = C0*a[0]-C4*a[6];
    tb = C0*b[0]-C4*b[6];
    r[8] += ta*b[8]+tb*a[8];
    t = a[8]*b[8];
    r[0] += C0*t;
    r[6] -= C4*t;
    // entry count=13
    // **multiplications count=120**
    // **addition count=74**
 }

// Evaluates the irradiance perceived in the provided direction
// Analytic method from http://www1.cs.columbia.edu/~ravir/papers/envmap/envmap.pdf eq. 13
//
float3 EvaluateSHIrradiance(float3 _Direction, float3 _SH[9])
{
	const float c1 = 0.42904276540489171563379376569857; // 4 * Â2.Y22 = 1/4 * sqrt(15.PI)
	const float c2 = 0.51166335397324424423977581244463; // 0.5 * Â1.Y10 = 1/2 * sqrt(PI/3)
	const float c3 = 0.24770795610037568833406429782001; // Â2.Y20 = 1/16 * sqrt(5.PI)
	const float c4 = 0.88622692545275801364908374167057; // Â0.Y00 = 1/2 * sqrt(PI)

	float x = _Direction.x;
	float y = _Direction.y;
	float z = _Direction.z;

	return max(0.0,
            (c1 * (x * x - y * y)) * _SH[8] // c1.L22.(x²-y²)
            + (c3 * (3.0 * z * z - 1)) * _SH[6] // c3.L20.(3.z² - 1)
            + c4 * _SH[0] // c4.L00 
            + 2.0 * c1 * (_SH[4] * x * y + _SH[7] * x * z + _SH[5] * y * z) // 2.c1.(L2-2.xy + L21.xz + L2-1.yz)
            + 2.0 * c2 * (_SH[3] * x + _SH[1] * y + _SH[2] * z)); // 2.c2.(L11.x + L1-1.y + L10.z)
}

// Evaluates the irradiance perceived in the provided direction, also accounting for Ambient Occlusion
// Details can be found at http://wiki.nuaj.net/index.php?title=SphericalHarmonicsPortal
// Here, _CosThetaAO = cos( PI/2 * AO ) and represents the cosine of the half-cone angle that drives the amount of light a surface is perceiving
//
float3 EvaluateSHIrradiance2(float3 _Direction, float _CosThetaAO, float3 _SH[9])
{
	float t2 = _CosThetaAO * _CosThetaAO;
	float t3 = t2 * _CosThetaAO;
	float t4 = t3 * _CosThetaAO;
	float ct2 = 1.0 - t2;

	float c0 = 0.88622692545275801364908374167057 * ct2; // 1/2 * sqrt(PI) * (1-t^2)
	float c1 = 1.02332670794648848847955162488930 * (1.0 - t3); // sqrt(PI/3) * (1-t^3)
	float c2 = 0.24770795610037568833406429782001 * (3.0 * (1.0 - t4) - 2.0 * ct2); // 1/16 * sqrt(5*PI) * [3(1-t^4) - 2(1-t^2)]
	const float sqrt3 = 1.7320508075688772935274463415059;

	float x = _Direction.x;
	float y = _Direction.y;
	float z = _Direction.z;

	return max(0.0, c0 * _SH[0] // c0.L00
            + c1 * (_SH[1] * y + _SH[2] * z + _SH[3] * x) // c1.(L1-1.y + L10.z + L11.x)
            + c2 * (_SH[6] * (3.0 * z * z - 1.0) // c2.L20.(3z²-1)
                + sqrt3 * (_SH[8] * (x * x - y * y) // sqrt(3).c2.L22.(x²-y²)
                    + 2.0 * (_SH[4] * x * y + _SH[5] * y * z + _SH[7] * z * x))) // 2sqrt(3).c2.(L2-2.xy + L2-1.yz + L21.zx)
        );
}
#endif
/*
 struct sample_t {
    float3  direction;
    float   theta, phi;
    float*  Ylm;
 };

 // Fills an N*N*2 array with uniformly distributed samples across the sphere using jittered stratification
 void PreComputeSamples( int sqrt_n_samples, int n_bands, sample_t samples[], float ) {
    int i = 0; // array index
    double oneoverN = 1.0 / sqrt_n_samples;
    for ( int a=0; a < sqrt_n_samples; a++ ) {
        for ( int b=0; b < sqrt_n_samples; b++ ) {
            // Generate unbiased distribution of spherical coords
            double x = (a + random()) * oneoverN;           // Do not reuse results
            double y = (b + random()) * oneoverN;           // Each sample must be random!
            double theta = 2.0 * acos( sqrt( 1.0 - x ) );   // Uniform sampling on theta
            double phi = 2.0 * PI * y;                      // Uniform sampling on phi

            // Convert spherical coords to unit vector
            samples[i].direction = float3( sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta) );
            samples[i].theta = theta;
            samples[i].phi = phi;

            // precompute all SH coefficients for this sample
            for ( int l=0; l < n_bands; ++l ) {
                for ( int m=-l; m<=l; ++m ) {
                    int index = l*(l+1)+m;
                    samples[i].Ylm[index] = EstimateSH( l, m, theta, phi );
                }
            }
            ++i;
        }
    }
 }
*/