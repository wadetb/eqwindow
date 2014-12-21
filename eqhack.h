
// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the GLIDEHACK_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// GLIDEHACK_API functions as being imported from a DLL, wheras this DLL sees symbols
// defined with this macro as being exported.
#ifdef EQHACK_EXPORTS
#define EQHACK_API __declspec(dllexport)
#else
#define EQHACK_API __declspec(dllimport)
#endif

struct SEQControl
{
    HWND hWnd;      // Everquest's HWND.

    bool Wireframe;
    bool Fullbright;
    int Background;
    bool StartRec;
    bool StopRec;
    bool LoopRec;
    bool PlayRec;
};

EQHACK_API LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);

EQHACK_API void InstallHook();
EQHACK_API void RemoveHook();

