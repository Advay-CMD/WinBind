#include <windows.h>
// Yes, I know, I know, this is OLD.
#include <mmsystem.h>
#include <string>

#pragma comment(lib, "winmm.lib")

int main()
{
    // Get File
    const char* file = "C:\\Users\\advay\\Music\\Isolation - NightHawk22.mp3";

    // Open
    std::string cmd = "open \"" + std::string(file) + "\" type mpegvideo alias music";

    // Get error
    MCIERROR err = mciSendStringA(cmd.c_str(), NULL, 0, NULL);

    // Logic for error handling
    if (err != 0)
    {
        char error[256];
        mciGetErrorStringA(err, error, sizeof(error));
        MessageBoxA(NULL, error, "Open Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Now get errors in playing music
    err = mciSendStringA("play music repeat", NULL, 0, NULL);

    if (err != 0)
    {
        char error[256];
        mciGetErrorStringA(err, error, sizeof(error));
        MessageBoxA(NULL, error, "Play Error", MB_OK | MB_ICONERROR);
        mciSendStringA("close music", NULL, 0, NULL);
        return 1;
    }

    mciSendStringA("stop music", NULL, 0, NULL);
    mciSendStringA("close music", NULL, 0, NULL);

    return 0;
}
