// dinputhack.cpp : Defines the entry point for the DLL application.
//

#include "dinputhack.h"
//#define INITGUID
#include "dataonly_dinput.h"
#include <mmsystem.h>
#include <stdio.h>
#include "eqhackint.h"
#include "apihijack.h"

typedef HRESULT (WINAPI *DirectInputCreateA_Type)(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA *ppDI, LPUNKNOWN punkOuter);

bool AutoLightSignalSet = false;
HANDLE AutoLightSignal = 0;

bool NoFogSignalSet = false;
HANDLE NoFogSignal = 0;

bool WireFrameSignalSet = false;
HANDLE WireFrameSignal = 0;

bool MacroRecording = false;
bool MacroPlayback = false;
bool MacroLooping = false;

bool AutoKeyEnabled = 0;
int LastKey;

enum EDeviceType
{
    DT_KEYBOARD,
    DT_MOUSE,
    DT_OTHER,
};

struct SMacroRecord
{
    EDeviceType Type;
    int Sequence;
    DIDEVICEOBJECTDATA Obj;
};

const int MAX_MACRO_RECORDS = 32768;
SMacroRecord MacroRecords[MAX_MACRO_RECORDS];
int NMacroRecords = 0; // number of records in MacroRecords
int CMacroRecord = 0;  // current macro reccord
int CMacroSequence = 0;  // sequence number (number of polls since starting macro recording/playback)

class MyIDirectInputDeviceA;
MyIDirectInputDeviceA* KeyboardDevice = 0;
MyIDirectInputDeviceA* MouseDevice = 0;

bool InputAcquired = false;
bool FirstAcquire = true;

void SetScrollLock( BOOL bState )
{ 
    BYTE KeyState[256];
    
    GetKeyboardState((LPBYTE)&KeyState);
    if( ( bState && !( KeyState[VK_SCROLL] & 1 ) ) ||
        ( !bState && ( KeyState[VK_SCROLL] & 1 ) ) )
    {
        // Simulate a key press
        keybd_event( 
            VK_NUMLOCK,
            0x45,
            KEYEVENTF_EXTENDEDKEY | 0,
            0 );
        
        // Simulate a key release
        keybd_event( 
            VK_NUMLOCK,
            0x45,
            KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP,
            0);
    }
}

void StartMacroRecording()
{
    if ( MacroPlayback )
        return;

    NMacroRecords = 0;
    MacroRecording = true;
    CMacroSequence = 0;

//    SetScrollLock( true );
}

void StopMacroRecording()
{
    MacroRecording = false;

//    SetScrollLock( false );
}

void StartMacroPlayback( bool Looping )
{
    if ( MacroRecording || NMacroRecords == 0 )
        return;

    CMacroRecord = 0;
    MacroPlayback = true;
    CMacroSequence = 0;

    MacroLooping = Looping;
}

void StopMacroPlayback()
{
    MacroPlayback = false;
}

bool InitDInputState()
{
    AutoLightSignal = CreateEvent( NULL, TRUE, FALSE, "WTBGlideAutoLightEnabled" );
    NoFogSignal = CreateEvent( NULL, TRUE, FALSE, "WTBGlideNoFogEnabled" );
    WireFrameSignal = CreateEvent( NULL, TRUE, FALSE, "WTBGlideWireFrameEnabled" );

    return true;
}

// IDirectInputDeviceA Proxy
class MyIDirectInputDeviceA : public IDirectInputDeviceA
{
public:
    virtual HRESULT __stdcall QueryInterface(REFIID riid, LPVOID * ppvObj)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::QueryInterface\n" );
        return pDID->QueryInterface(riid, ppvObj);
    }

    virtual ULONG __stdcall AddRef()
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::AddRef\n" );
        return pDID->AddRef();
    }

    virtual ULONG __stdcall Release()
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::Release\n" );
        ULONG ret = pDID->Release();
        if ( !ret )
        {
            D3DHACK_DEBUG( "Input device released.\n" );
            if ( Type == DT_MOUSE )
                MouseDevice = 0;
            else
            if ( Type == DT_KEYBOARD )
                KeyboardDevice = 0;
        }
        return ret;
    }

    virtual HRESULT __stdcall GetCapabilities(LPDIDEVCAPS a)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::GetCapabilities\n" );
        return pDID->GetCapabilities(a);
    }

    virtual HRESULT __stdcall EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA a,LPVOID b, DWORD c)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::EnumObjects\n" );
        return pDID->EnumObjects(a,b,c);
    }

    virtual HRESULT __stdcall GetProperty(REFGUID a,LPDIPROPHEADER b)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::GetProperty\n" );
        return pDID->GetProperty(a,b);
    }

    virtual HRESULT __stdcall SetProperty(REFGUID a, LPCDIPROPHEADER b)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::SetProperty\n" );
        return pDID->SetProperty(a,b);
    }

    virtual HRESULT __stdcall Acquire()
    {
//        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::Acquire\n" );
        if ( Type == DT_MOUSE )
        {
            if ( FirstAcquire )
            {
                FirstAcquire = false;
                InputAcquired = true;
                UpdateWindowText();
                return pDID->Acquire();
            }
            else
            if ( InputAcquired )
                return pDID->Acquire();
            else
                return DI_OK;
        }

        return pDID->Acquire();
    }

    virtual HRESULT __stdcall Unacquire()
    {
//        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::Unacquire\n" );
        if ( Type == DT_MOUSE )
        {
            if ( !InputAcquired )
                return pDID->Unacquire();
            else
                return DI_OK;
        }

        return pDID->Unacquire();
    }

    virtual HRESULT __stdcall GetDeviceState(DWORD cbData,LPVOID lpvData)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::GetDeviceState\n" );

        if ( !InputAcquired )
            return DI_OK;

        HRESULT ret = pDID->GetDeviceState( cbData, lpvData );
/*
        // GetDeviceState not currently supported for recording, special keys or autokey.
        if ( Type == DT_KEYBOARD )
        {
            BYTE* Keys = (BYTE*)lpvData;
            if ( timeGetTime() - LastTime > 1000 )
            {
                Keys[DIK_NUMPAD4] = 0x80;
                LastTime = timeGetTime();
            }
        }
*/

        return ret;
    }

    virtual HRESULT __stdcall GetDeviceData( 
        DWORD cbObjectData, 
        LPDIDEVICEOBJECTDATA rgdod, 
        LPDWORD pdwInOut, 
        DWORD dwFlags )
    {
//        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::GetDeviceData\n" );
        if ( !InputAcquired && Type == DT_MOUSE )
        {
            *pdwInOut = 0;
            return DI_OK;
        }

        DWORD Elements = *pdwInOut;
        static int LastTime = 0;

        HRESULT ret = pDID->GetDeviceData(cbObjectData,rgdod,pdwInOut,dwFlags);

        if ( Type == DT_KEYBOARD )
        {
            static bool CtrlState = 0;
            static bool AltState = 0;

            // Test for special keys.
            for ( DWORD i = 0; i < *pdwInOut; i++ )
            {
                LPDIDEVICEOBJECTDATA Obj = &rgdod[i];

                // Shift States
                // KeyWasShift prevents Alt and Control from occasionally being missed in normal use.
                bool KeyWasShift = false; 
                if ( Obj->dwOfs == DIK_LCONTROL || Obj->dwOfs == DIK_RCONTROL )
                {
                    CtrlState = Obj->dwData == 0x80;
                    KeyWasShift = true;
                }
                else
                if ( Obj->dwOfs == DIK_LMENU || Obj->dwOfs == DIK_RMENU )
                {
                    AltState = Obj->dwData == 0x80;
                    KeyWasShift = true;
                }

                // Test for special keypresses.
                bool KeyTaken = false;

				// Block EQ from seeing Alt-Tab.
				if ( !KeyWasShift && AltState && Obj->dwData == 0x80 )
				{
					if ( Obj->dwOfs == DIK_TAB )
						KeyTaken = true;
				}

                if ( !KeyWasShift && CtrlState && AltState && Obj->dwData == 0x80 )
                {
					KeyTaken = true;
                    switch( Obj->dwOfs )
                    {
#ifdef PROVER
                    // Auto key (sense heading)
                    case DIK_A:
                        AutoKeyEnabled = !AutoKeyEnabled;
                        break;
#endif

                    case DIK_T:
                        ToggleWindowTopmost();
                        break;

                    // Set the default window size.
                    case DIK_D:
                        SetDefaultSize();
                        break;

                    // Toggle exclusive access to the mouse.
                    case DIK_Z:
                        AcquireInput( false );
                        break;

#ifdef PROVER
                    // Glide DLL control.  Need to port to D3D, dammit.
                    case DIK_L:
                        // Signal the glide library to toggle lighting.
                        if ( AutoLightSignalSet )
                        {
                            ResetEvent( AutoLightSignal );
                            AutoLightSignalSet = false;
                        }
                        else
                        {
                            SetEvent( AutoLightSignal );
                            AutoLightSignalSet = true;
                        }
                        break;

                    case DIK_F:
                        // Signal the glide library to toggle fogging.
                        if ( NoFogSignalSet )
                        {
                            ResetEvent( NoFogSignal );
                            NoFogSignalSet = false;
                        }
                        else
                        {
                            SetEvent( NoFogSignal );
                            NoFogSignalSet = true;
                        }
                        break;

                    case DIK_W:
                        // Signal the glide library to toggle wire frame.
                        if ( WireFrameSignalSet )
                        {
                            ResetEvent( WireFrameSignal );
                            WireFrameSignalSet = false;
                        }
                        else
                        {
                            SetEvent( WireFrameSignal );
                            WireFrameSignalSet = true;
                        }
                        break;

                    // Macro recording
                    case DIK_R:
                        if ( MacroRecording )
                            StopMacroRecording();
                        else
                            StartMacroRecording();
                        break;

                    case DIK_P:
                        if ( MacroPlayback )
                            StopMacroPlayback();
                        else
                            StartMacroPlayback( false );
                        break;

                    case DIK_O:
                        if ( MacroPlayback )
                            StopMacroPlayback();
                        else
                            StartMacroPlayback( true );
#endif

                    default:
                        KeyTaken = false;
                    }
                }                      

                // If we intercepted the key, remove it from the buffer.
                if ( KeyTaken )
                {
                    for ( DWORD j = i; j < *pdwInOut - 1; j++ )
                        rgdod[j] = rgdod[j + 1];
                    (*pdwInOut)--;
                }
            }

            // Auto key logic, press the 4 key every second.
            if ( AutoKeyEnabled && ( LastTime == 0 || timeGetTime() - LastTime > 1000 ) )
            {
                // Store a 4 keypress in the buffer.  Avoids overflowing.
                if ( Elements > *pdwInOut && rgdod )
                {
                    LPDIDEVICEOBJECTDATA Obj = &rgdod[*pdwInOut];
                    Obj->dwOfs = LastKey;
                    
                    // Pressed and released on a cycle.
                    Obj->dwData = 0x80;

                    Obj->dwTimeStamp = 0; 
                    Obj->dwSequence = 0; 
                    (*pdwInOut)++;
                }
                LastTime = timeGetTime();
            }
        }

		if ( !AutoKeyEnabled && *pdwInOut )
			LastKey = rgdod[*pdwInOut - 1].dwOfs;

        // Macro recording
        if ( MacroRecording )
        {
            for ( DWORD i = 0; i < *pdwInOut; i++ )
            {
                MacroRecords[NMacroRecords].Type = Type;
                MacroRecords[NMacroRecords].Sequence = CMacroSequence;
                MacroRecords[NMacroRecords].Obj = rgdod[i];

//                    sprintf( Work, "Recorded character: %d\n", MacroRecords[NMacroRecords].Obj.dwOfs );
//                    OutputDebugString( Work );

                NMacroRecords++;
                if ( NMacroRecords >= MAX_MACRO_RECORDS )
                {
                    StopMacroRecording();
                    break;
                }
            }
            CMacroSequence++;
        }

        if ( MacroPlayback )
        {
            // Only playback for the correct type of device.  All the records in one sequence will be of the same type.
            if ( Type == MacroRecords[CMacroRecord].Type )
            {
                while ( MacroRecords[CMacroRecord].Sequence == CMacroSequence )
                {
                    rgdod[(*pdwInOut)++] = MacroRecords[CMacroRecord].Obj;

//                        sprintf( Work, "Playback character: %d\n", MacroRecords[CMacroRecord].Obj.dwOfs );
//                        OutputDebugString( Work );

                    CMacroRecord++;
                    if ( CMacroRecord >= NMacroRecords )
                    {
                        StopMacroPlayback();
                        if ( MacroLooping )
                            StartMacroPlayback( true );
                        break;
                    }
                }
            }
            CMacroSequence++;
        }

        return ret;
    }

    virtual HRESULT __stdcall SetDataFormat(LPCDIDATAFORMAT a)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::SetDataFormat\n" );
        return pDID->SetDataFormat(a);
    }

    virtual HRESULT __stdcall SetEventNotification(HANDLE a)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::SetEventNotification\n" );
        return pDID->SetEventNotification(a);
    }

    virtual HRESULT __stdcall SetCooperativeLevel(HWND a,DWORD b)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::SetCooperativeLevel\n" );

        if ( Type == DT_KEYBOARD )
            return pDID->SetCooperativeLevel( a, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE );
        else
        if ( Type == DT_MOUSE )
            return pDID->SetCooperativeLevel( a, DISCL_FOREGROUND | DISCL_EXCLUSIVE );

        return pDID->SetCooperativeLevel(a,b);
    }

    virtual HRESULT __stdcall GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA a,DWORD b,DWORD c)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::GetObjectInfo\n" );
        return pDID->GetObjectInfo(a,b,c);
    }

    virtual HRESULT __stdcall GetDeviceInfo(LPDIDEVICEINSTANCEA a)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::GetDeviceInfo\n" );
        return pDID->GetDeviceInfo(a);
    }

    virtual HRESULT __stdcall RunControlPanel(HWND a,DWORD b)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::RunControlPanel\n" );
        return pDID->RunControlPanel(a,b);
    }

    virtual HRESULT __stdcall Initialize(HINSTANCE a,DWORD b,REFGUID c)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::Initialize\n" );
        return pDID->Initialize(a,b,c);
    }

    EDeviceType Type;

    LPDIRECTINPUTDEVICEA pDID;
};


// IDirectInputA Proxy
class MyIDirectInputA : public IDirectInputA
{
public:
    virtual HRESULT __stdcall QueryInterface(REFIID riid, LPVOID * ppvObj) 
    { 
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::QueryInterface\n" );
        return pDI->QueryInterface( riid, ppvObj );
    }

    virtual ULONG __stdcall AddRef() 
    { 
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::AddRef\n" );
        return pDI->AddRef(); 
    }
    
    virtual ULONG __stdcall Release() 
    { 
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::Release\n" );
        return pDI->Release(); 
    }

    virtual HRESULT __stdcall CreateDevice( REFGUID riid, LPDIRECTINPUTDEVICEA *ppDID, LPUNKNOWN lpUnk)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::CreateDevice\n" );

        MyIDirectInputDeviceA* Proxy = new MyIDirectInputDeviceA;

        HRESULT ret = pDI->CreateDevice( riid, &Proxy->pDID, lpUnk );

        *ppDID = Proxy;

        // Check for keyboard or mouse
        if ( IsEqualGUID( riid, GUID_SysKeyboard ) )
        {
            DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::Keyboard Device Created.\n" );
            Proxy->Type = DT_KEYBOARD;
            KeyboardDevice = Proxy;
        }
        else
        if ( IsEqualGUID( riid, GUID_SysMouse ) )
        {
            DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::Mouse Device Created.\n" );
            Proxy->Type = DT_MOUSE;
            MouseDevice = Proxy;
        }
        else
        {
            DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::Other Device Created.\n" );
            Proxy->Type = DT_OTHER;
        }

       
        return ret;
    }

    virtual HRESULT __stdcall EnumDevices( DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags )
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::EnumDevices\n" );
        return pDI->EnumDevices( dwDevType, lpCallback, pvRef, dwFlags );
    }

    virtual HRESULT __stdcall GetDeviceStatus(REFGUID riid)
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::GetDeviceStatus\n" );
        return pDI->GetDeviceStatus( riid );
    }

    virtual HRESULT __stdcall RunControlPanel( HWND hWndOwner, DWORD dwFlags )
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::RunControlPanel\n" );
        return pDI->RunControlPanel( hWndOwner, dwFlags );
    }

    virtual HRESULT __stdcall Initialize( HINSTANCE hInst, DWORD dwVersion )
    {
        DINPUTHACK_DEBUG( "TRACE: MyIDirectInputA::Initialize\n" );
        return pDI->Initialize( hInst, dwVersion );
    }

    LPDIRECTINPUTA pDI;
};

void AcquireInput( bool Acquire )
{
    if ( !MouseDevice )
        return;

    if ( Acquire )
    {
        OutputDebugString( "TRACE: Input Acquired.\n" );
        InputAcquired = true;
        MouseDevice->Acquire();
    }
    else
    {
        OutputDebugString( "TRACE: Input Unacquired.\n" );
        InputAcquired = false;
        MouseDevice->Unacquire();
        while ( ShowCursor( TRUE ) <= 0 ) {}
    }
    InputAcquired = Acquire;
    UpdateWindowText();
}

// Called regularly during loading to check for special keyboard messages.
void CheckKeyboard()
{                        
    DIDEVICEOBJECTDATA obj[256];
    DWORD count = 256;

    if ( KeyboardDevice )
    {
        KeyboardDevice->Acquire();
        KeyboardDevice->GetDeviceData( sizeof( DIDEVICEOBJECTDATA ), obj, &count, 0 );
    }
}

// Returns a 128 byte array of key states.
void WINAPI DIHack_GetKeyState( int Size, char* Keys )
{
    if ( KeyboardDevice )
    {
        HRESULT ret = KeyboardDevice->GetDeviceState( Size, Keys );
        if ( ret != DI_OK )
        {
            sprintf( Work, "GetDeviceState Error: %d\n", ret );
            DINPUTHACK_DEBUG( Work );
        }
    }
}

HRESULT WINAPI DirectInputCreateA(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA *ppDI, LPUNKNOWN punkOuter);

enum
{
    DIFN_DirectInputCreateA = 0
};

// Hook structure.
SDLLHook DIHook = 
{
    "DINPUT.DLL",
    false, 0,
    {
        { "DirectInputCreateA", DirectInputCreateA },
        { NULL, NULL }
    }
};

HRESULT WINAPI DirectInputCreateA(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA *ppDI, LPUNKNOWN punkOuter)
{
    DINPUTHACK_DEBUG( "TRACE: MyIDirectInputDeviceA::TRACE: MyIDirectInputDeviceA::DirectInputCreateA\n" );

    MyIDirectInputA* Proxy = new MyIDirectInputA;

    DirectInputCreateA_Type OldFn = 
        (DirectInputCreateA_Type)DIHook.Functions[DIFN_DirectInputCreateA].OrigFn;
    HRESULT ret = OldFn( hinst, dwVersion, &Proxy->pDI, punkOuter );

    *ppDI = Proxy;

    return ret;
}

