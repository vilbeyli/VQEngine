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

#include "Platform.h"
#include "Window.h"

#include <memory>

class VQEngine : public IWindowOwner
{
public:

public:
	bool Initialize(const FStartupParameters& Params);
	void Exit();

	// Window event callbacks for the main Window
	void OnWindowCreate() override;
	void OnWindowResize(HWND hWnd) override;
	void OnWindowMinimize() override;
	void OnWindowFocus() override;
	void OnWindowKeyDown(WPARAM wParam) override;
	void OnWindowClose() override;
	
	void OnUpdate_MainThread();

private:

	std::unique_ptr<Window> WinMain;
	std::unique_ptr<Window> WinDebug;
};