#pragma once

#include <d3d11.h>
#include <Windows.h>

namespace bf2dev {

bool IsPrivateTestEnabled();
void InitializeMenu(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
void ShutdownMenu();
void ToggleMenu();
bool IsMenuOpen();
void RenderMenu();
LRESULT HandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled);

} // namespace bf2dev
