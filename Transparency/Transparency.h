// WinBind - Transparency declarations
#pragma once
#include <windows.h>
#include <string>

int LoadTransparencyConfig();
int LoadTransparencyDelayMs();
void SetWindowTransparency(HWND hwnd, int percentage_opacity);
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
