#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include "eqhackint.h"
#include "apihijack.h"
#include "exceptionhandler.h"

// Text buffer for sprintf
char Work[256];

void DebugOut( char* s )
{
    FILE* DebugFile = fopen( "eqwindow.log", "a" );
    if ( !DebugFile )
        return;
    fprintf( DebugFile, s );
    fclose( DebugFile );
}

HRESULT ReportResult( HRESULT hr )
{
    sprintf( Work, "Result: 0x%08x\n", hr );
    OutputDebugString( Work );
    return hr;
}

HKEY RegKey;              // Persistent registry key for saving data.

void RegSetWP( char* Key, LPWINDOWPLACEMENT Value )
{
    RegSetValueEx( RegKey, Key, 0, REG_BINARY, (LPBYTE)Value, sizeof(WINDOWPLACEMENT) );
}

void RegGetWP( char* Key, LPWINDOWPLACEMENT Value )
{
    DWORD Type, DataSize = sizeof(WINDOWPLACEMENT);
    if ( RegQueryValueEx( RegKey, Key, 0, &Type, (LPBYTE)Value, &DataSize ) != ERROR_SUCCESS )
        Value->length = 0;
}

void RegSetInt( char* Key, int Value )
{
    RegSetValueEx( RegKey, Key, 0, REG_DWORD, (LPBYTE)&Value, sizeof(int) );
}

int RegGetInt( char* Key )
{
    DWORD Value, Type, DataSize = sizeof(int);
    if ( RegQueryValueEx( RegKey, Key, 0, &Type, (LPBYTE)&Value, &DataSize ) == ERROR_SUCCESS )
        return Value;
    else
        return 0;
}


extern SDLLHook D3DHook;
extern SDLLHook DIHook;

#if 0

// Debug-style injection.
BOOL APIENTRY DllMain( HINSTANCE hModule, DWORD fdwReason, LPVOID lpReserved )
{
    if ( fdwReason == DLL_PROCESS_ATTACH )  // When initializing....
    {
        // Get access to our registry key.
        HKEY SoftwareKey;
        RegCreateKeyEx( HKEY_LOCAL_MACHINE, "Software", 0, NULL, 0, KEY_CREATE_SUB_KEY, NULL, &SoftwareKey, NULL );
        RegCreateKeyEx( SoftwareKey, "EverQuest", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &RegKey, NULL );

        RegCloseKey( SoftwareKey );

        // We don't need thread notifications for what we're doing.  Thus, get
        // rid of them, thereby eliminating some of the overhead of this DLL
        DisableThreadLibraryCalls( hModule );

        HookAPICalls( &D3DHook );
        HookAPICalls( &DIHook );
    }
    return TRUE;
}

__declspec(dllexport) void InstallHook( HINSTANCE hDLL )
{
}

__declspec(dllexport) void RemoveHook( HINSTANCE hDLL )
{
}

#else

HINSTANCE hDLL;

// CBT Hook-style injection.
BOOL APIENTRY DllMain( HINSTANCE hModule, DWORD fdwReason, LPVOID lpReserved )
{
    if ( fdwReason == DLL_PROCESS_ATTACH )  // When initializing....
    {
        hDLL = hModule;

        // We don't need thread notifications for what we're doing.  Thus, get
        // rid of them, thereby eliminating some of the overhead of this DLL
        DisableThreadLibraryCalls( hModule );

        // Only hook the APIs if this is the Everquest proess.
        GetModuleFileName( GetModuleHandle( NULL ), Work, sizeof(Work) );
        PathStripPath( Work );

        if (    stricmp( Work, "eqgame.exe" ) == 0 
            ||  stricmp( Work, "tutorial.exe" ) == 0 )
        {
            D3DHACK_DEBUG( "--- " EQHACK_VERSION " - eqwindow@wadeb.com ---\n" );

            // Trap EQ related crashes and write them to an output log to assist in debugging.
            SetUnhandledExceptionFilter( RecordExceptionInfo );
            D3DHACK_DEBUG( "Exception handler installed.\n" );

            // Get access to our registry key.
            HKEY SoftwareKey;
            RegCreateKeyEx( HKEY_LOCAL_MACHINE, "Software", 0, NULL, 0, KEY_CREATE_SUB_KEY, NULL, &SoftwareKey, NULL );
            RegCreateKeyEx( SoftwareKey, "EverQuest", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &RegKey, NULL );

            RegCloseKey( SoftwareKey );

            // Gain access to the memory mapped file that contains our control struct.
            HANDLE hFileMapping = CreateFileMapping( INVALID_HANDLE_VALUE, 0, PAGE_READWRITE | SEC_COMMIT, 0, sizeof(SEQControl), "EQHACK_CTRL" );
            if ( !hFileMapping )
                return FALSE;

            Control = (SEQControl*)MapViewOfFile( hFileMapping, FILE_MAP_WRITE, 0, 0, sizeof(SEQControl) );
            if ( !Control )
                return FALSE;

            HookAPICalls( &D3DHook );
            HookAPICalls( &DIHook );
        }
    }

    return TRUE;
}

// Other shared data.
SEQControl* Control;

// you must define as SHARED in .def
#pragma data_seg (".HookSection")		
// one instance for all processes
HHOOK hHook = NULL;	
#pragma data_seg ()

EQHACK_API LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) 
{
    return CallNextHookEx( hHook, nCode, wParam, lParam); 
}

EQHACK_API void InstallHook()
{
    hHook = SetWindowsHookEx( WH_CBT, HookProc, hDLL, 0 ); 
}

EQHACK_API void RemoveHook()
{
    UnhookWindowsHookEx( hHook );
}

#endif