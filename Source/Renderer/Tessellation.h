//	VQE
//	Copyright(C) 2024  - Volkan Ilbeyli
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

#pragma once

enum ETessellationDomain
{
	TRIANGLE_PATCH = 0,
	QUAD_PATCH,
	ISOLINE_PATCH,

	NUM_TESSELLATION_DOMAINS
};
enum ETessellationOutputTopology
{
	TESSELLATION_OUTPUT_POINT = 0,
	TESSELLATION_OUTPUT_LINE,
	TESSELLATION_OUTPUT_TRIANGLE_CW,
	TESSELLATION_OUTPUT_TRIANGLE_CCW,

	NUM_TESSELLATION_OUTPUT_TOPOLOGY
};
enum ETessellationPartitioning
{
	INTEGER = 0,
	FRACTIONAL_EVEN,
	FRACTIONAL_ODD,
	POWER_OF_TWO,
};

struct FTessellationParameters
{
	bool bEnableTessellation = false;
	ETessellationDomain Domain = ETessellationDomain::TRIANGLE_PATCH;
	ETessellationOutputTopology OutputTopology = ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CW;
	ETessellationPartitioning Partitioning = ETessellationPartitioning::FRACTIONAL_ODD;

	inline void SetAllTessellationFactors(float TessellationFactor)
	{
		switch (Domain)
		{
		case TRIANGLE_PATCH: TriInner = TriOuter[0] = TriOuter[1] = TriOuter[2] = TessellationFactor; break;
		case QUAD_PATCH: QuadInner[0] = QuadInner[1] = QuadOuter[0] = QuadOuter[1] = QuadOuter[2] = QuadOuter[3] = TessellationFactor; break;
		case ISOLINE_PATCH: // TODO: line
			break;
		}
	}

	float TriOuter[3];
	float TriInner;
	float QuadInner[2];
	float QuadOuter[4];
	// TODO: line
	static constexpr float MAX_TESSELLATION_FACTOR = 64.0f;

	FTessellationParameters()
	{
		TriOuter[0] = TriOuter[1] = TriOuter[2] = TriInner = 1.0f;
		QuadOuter[0] = QuadOuter[1] = QuadOuter[2] = QuadOuter[3] = QuadInner[0] = QuadInner[1] = 1.0f;
	}
};
