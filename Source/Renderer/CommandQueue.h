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

#pragma once

struct ID3D12CommandQueue;
class Device;

class CommandQueue
{
public:
	enum EType
	{
		GFX = 0,
		COMPUTE,
		COPY,

		NUM_COMMAND_QUEUE_TYPES
	};

public:
	void Create(Device* pDevice, EType type, const char* pName = nullptr);
	void Destroy();

	ID3D12CommandQueue* pQueue;
};