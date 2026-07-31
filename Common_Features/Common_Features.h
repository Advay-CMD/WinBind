// WinBind - Common_Features Header Script
#pragma once
// For using L"" Wide Char
#define UNICODE
#define _UNICODE

// Include the library's
#include <windows.h>

// Pain. Pure Pain.
#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif

#ifndef OCR_IBEAM
#define OCR_IBEAM 32513
#endif

#ifndef OCR_WAIT
#define OCR_WAIT 32514
#endif

#ifndef OCR_CROSS
#define OCR_CROSS 32515
#endif

#ifndef OCR_UP
#define OCR_UP 32516
#endif

#ifndef OCR_SIZENWSE
#define OCR_SIZENWSE 32642
#endif

#ifndef OCR_SIZENESW
#define OCR_SIZENESW 32643
#endif

#ifndef OCR_SIZEWE
#define OCR_SIZEWE 32644
#endif

#ifndef OCR_SIZENS
#define OCR_SIZENS 32645
#endif

#ifndef OCR_SIZEALL
#define OCR_SIZEALL 32646
#endif

#ifndef OCR_NO
#define OCR_NO 32648
#endif

#ifndef OCR_HAND
#define OCR_HAND 32649
#endif

#ifndef OCR_APPSTARTING
#define OCR_APPSTARTING 32650
#endif

// Definitions were soo long, I wouldn't expect this to be any shorter.
void ChangeMouse(HCURSOR cursorNORMAL,
                 HCURSOR cursorIBEAM,
                 HCURSOR cursorWAIT,
                 HCURSOR cursorCROSS,
                 HCURSOR cursorUP,
                 HCURSOR cursorSIZENWSE,
                 HCURSOR cursorSIZENESW,
                 HCURSOR cursorSIZEWE,
                 HCURSOR cursorSIZENS,
                 HCURSOR cursorSIZEALL,
                 HCURSOR cursorNO,
                 HCURSOR cursorHAND,
                 HCURSOR cursorAPPSTARTING,
                 HCURSOR cursorPath);

// I have a BIG doubt whether Enable* features will be used by people
void ChangeWallpaper(const wchar_t* wallpaper_path);
void ChangeKeyBoardSpeedResponse(int speed);
void SetMouseSpeed(int speed);
void EnableComboBoxAnimation(BOOL state);
void EnableGradientCaption(BOOL state);
void EnableListboxSmoothScrolling(BOOL state);
void EnableMenuAnimation(BOOL state);
void EnableSelectionFade(BOOL state);
void EnableTooltipAnimation(BOOL state, const char* type);
