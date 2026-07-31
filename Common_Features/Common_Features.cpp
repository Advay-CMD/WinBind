// For using L"" Wide Char
#define UNICODE
#define _UNICODE

// Include the library's
#include <windows.h>
#include "Common_Features.h"

// Change the Mouse
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
    // OH MY GOD! How MUCH!? I think I need to watch more video's on OOP
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

// Simple. Wallpaper
void ChangeWallpaper(const wchar_t* wallpaper_path) 
{
    SystemParametersInfo(SPI_SETDESKWALLPAPER,
        0,
        (PVOID)wallpaper_path,
        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

void ChangeKeyBoardSpeedResponse(int speed) 
{
    SystemParametersInfo(SPI_SETKEYBOARDSPEED,
    speed,
    0,
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    //Error handling later
}

void SetMouseSpeed(int speed) 
{
    // 1=slowest, 10=default, 20=fastest
    SystemParametersInfo(SPI_SETMOUSESPEED,
    speed,
    0,
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    //Error handling later
}

void EnableComboBoxAnimation(BOOL state) 
{
    SystemParametersInfo(SPI_SETCOMBOBOXANIMATION,
    0,
    (PVOID)(INT_PTR)state,
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    //Error handling later
}

void EnableGradientCaption(BOOL state) 
{
    SystemParametersInfo(SPI_SETGRADIENTCAPTIONS,
    0,
    (PVOID)(INT_PTR)state,
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    //Error handling later
}

void EnableListboxSmoothScrolling(BOOL state) 
{
    SystemParametersInfo(SPI_SETLISTBOXSMOOTHSCROLLING,
    0,
    (PVOID)(INT_PTR)state,
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    //Error handling later
}

void EnableMenuAnimation(BOOL state) 
{
    SystemParametersInfo(SPI_SETMENUANIMATION,
    0,
    (PVOID)(INT_PTR)state,
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    //Error handling later
}

void EnableSelectionFade(BOOL state) 
{
    SystemParametersInfo(SPI_SETSELECTIONFADE,
    0,
    (PVOID)(INT_PTR)state,
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    //Error handling later
}

void EnableTooltipAnimation(BOOL state, const char* type) 
{
    SystemParametersInfo(SPI_SETTOOLTIPANIMATION,
    0,
    (PVOID)(INT_PTR)state,
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    //Error handling later
    if(strcmp(type, "slide") == 0) 
    {
        SystemParametersInfo(SPI_SETTOOLTIPFADE,
        0,
        (PVOID)TRUE,
        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }
    else
    {
        SystemParametersInfo(SPI_SETTOOLTIPFADE,
        0,
        (PVOID)FALSE,
        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }
}

int main() 
{
    // Big Chunk
    const wchar_t* wallpaper_path= L"C:\\Users\\advay\\Downloads\\res\\windows-server-2025-3840x2160-15386.jpg";
    ChangeWallpaper(wallpaper_path);
    ChangeKeyBoardSpeedResponse(31);
    SetMouseSpeed(20);
    system("pause");
    SetMouseSpeed(10);
    EnableComboBoxAnimation(TRUE);
    EnableGradientCaption(TRUE);
    EnableListboxSmoothScrolling(TRUE);
    EnableMenuAnimation(TRUE);
    EnableSelectionFade(TRUE);
    EnableTooltipAnimation(TRUE, "fade");
}
