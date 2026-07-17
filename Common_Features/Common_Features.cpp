#define UNICODE
#define _UNICODE

#include <windows.h>
#include <iostream>

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
                 HCURSOR cursorPath)
{
    SetSystemCursor(cursorNORMAL, OCR_NORMAL);
    SetSystemCursor(cursorIBEAM, OCR_IBEAM);
    SetSystemCursor(cursorWAIT, OCR_WAIT);
    SetSystemCursor(cursorCROSS, OCR_CROSS);
    SetSystemCursor(cursorUP, OCR_UP);
    SetSystemCursor(cursorSIZENWSE, OCR_SIZENWSE);
    SetSystemCursor(cursorSIZENESW, OCR_SIZENESW);
    SetSystemCursor(cursorSIZEWE, OCR_SIZEWE);
    SetSystemCursor(cursorSIZENS, OCR_SIZENS);
    SetSystemCursor(cursorSIZEALL, OCR_SIZEALL);
    SetSystemCursor(cursorNO, OCR_NO);
    SetSystemCursor(cursorHAND, OCR_HAND);
    SetSystemCursor(cursorAPPSTARTING, OCR_APPSTARTING);
}

void ChangeWallpaper(const wchar_t* wallpaper_path) 
{
    SystemParametersInfo(SPI_SETDESKWALLPAPER,
        0,
        (PVOID)wallpaper_path,
        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

int main() 
{
    const wchar_t* wallpaper_path= L"C:\\Users\\advay\\Downloads\\res\\windows-server-2025-3840x2160-15386.jpg";
    ChangeWallpaper(wallpaper_path);
}
