// d3dhack.cpp : Defines the entry point for the DLL application.
//
#pragma warning(disable:4800)

//#define INITGUID
#include <stdio.h>
#define CINTERFACE
#include "dataonly_ddraw.h"
#include "dataonly_d3d.h"
#include "eqhackint.h"
#include "apihijack.h"
#include "exceptionhandler.h"

HWND g_hWnd = NULL;
LPDIRECTDRAW2 g_pDD = 0;
LPDIRECTDRAWSURFACE g_pddsFrontBuffer = 0;
LPDIRECTDRAWSURFACE g_pddsBackBuffer = 0;
LPDIRECTDRAWSURFACE g_pddsZBuffer = 0;

// Used for that wonderful RestoreAllSurfaces call. Which doesn't even work.
LPDIRECTDRAW7 g_pDD7 = 0;

// Since the above ones get released immediately after these are queried,
// all calls go through these interfaces.
LPDIRECTDRAWSURFACE3 g_pddsFrontBuffer3 = 0;
LPDIRECTDRAWSURFACE3 g_pddsBackBuffer3 = 0;
LPDIRECTDRAWSURFACE3 g_pddsZBuffer3 = 0;

// Hooked Vtbls (since Direct3D protects its Vtbls).  Hope this doesn't cause problems.
IDirect3D2Vtbl HookedIDirect3D2Vtbl;

// Saved Vtbls.
IDirect3D2Vtbl OldIDirect3D2Vtbl;
IDirectDrawSurface3Vtbl OldIDirectDrawSurface3Vtbl;
IDirectDrawSurfaceVtbl OldIDirectDrawSurfaceVtbl;
IDirectDraw2Vtbl OldIDirectDraw2Vtbl;
IDirectDrawVtbl OldIDirectDrawVtbl;

bool WindowTopmost = false;
DWORD Timer = 0;

int ScreenWidth = 0, ScreenHeight = 0;  // Width and height of the EQ screen - 640x480/800x600/etc.

float FPS = 0.0;

void CreateWindowedBuffers()
{
    D3DHACK_DEBUG( "TRACE: CreateWindowedBuffers.\n" );

    // Create the primary surface
    DDSURFACEDESC ddsd;
    ZeroMemory( &ddsd, sizeof(ddsd) );
    ddsd.dwSize         = sizeof(ddsd);
    ddsd.dwWidth        = ScreenWidth;
    ddsd.dwHeight       = ScreenHeight;
    ddsd.dwFlags        = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    if( FAILED( OldIDirectDraw2Vtbl.CreateSurface( g_pDD, &ddsd, &g_pddsFrontBuffer, NULL ) ) )
    {
        D3DHACK_DEBUG( "ERROR: Can't create primary surface.\n" );
        return;
    }

    // Create a clipper object
    LPDIRECTDRAWCLIPPER pcClipper;
    if( FAILED( OldIDirectDraw2Vtbl.CreateClipper( g_pDD, 0, &pcClipper, NULL ) ) )
    {
        D3DHACK_DEBUG( "ERROR: Can't create clipper.\n" );
        return;
    }

    // Associate the clipper with the window
    pcClipper->lpVtbl->SetHWnd( pcClipper, 0, g_hWnd );
    g_pddsFrontBuffer->lpVtbl->SetClipper( g_pddsFrontBuffer, pcClipper );
    pcClipper->lpVtbl->Release( pcClipper );

    // Create a backbuffer
    ddsd.dwFlags        = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
    ddsd.dwWidth        = ScreenWidth;
    ddsd.dwHeight       = ScreenHeight;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;

    if( FAILED( OldIDirectDraw2Vtbl.CreateSurface( g_pDD, &ddsd, &g_pddsBackBuffer, NULL ) ) )
    {
        D3DHACK_DEBUG( "ERROR: Can't create back buffer.\n" );
        return;
    }

    // Get z-buffer dimensions from the render target
    ddsd.dwSize = sizeof(ddsd);
    g_pddsBackBuffer->lpVtbl->GetSurfaceDesc( g_pddsBackBuffer, &ddsd );

    // Setup the surface desc for the z-buffer.
    ddsd.dwFlags        = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS | DDSD_PIXELFORMAT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
    ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ZBUFFER;
    ddsd.ddpfPixelFormat.dwRGBBitCount = 16;

    // Create and attach a z-buffer
    if( FAILED( OldIDirectDraw2Vtbl.CreateSurface( g_pDD, &ddsd, &g_pddsZBuffer, NULL ) ) )
    {
        D3DHACK_DEBUG( "ERROR: Can't create zbuffer.\n" );
        return;
    }

    // We let EQ attached the ZBuffer itself, no need to do it here.
/*
    if( FAILED( g_pddsBackBuffer->AddAttachedSurface( g_pddsZBuffer ) ) )
    {
        D3DHACK_DEBUG( "ERROR: Can't attach zbuffer to back buffer.\n" );
        return;
    }
*/
}

void UpdateWindowText()
{
#ifdef PROVER
    sprintf( Work, "(%c%c%c%c) %0.2ffps - " EQHACK_VERSION " - eqwindow@wadeb.com - DO NOT DISTRIBUTE", 
        InputAcquired ? 'M' : 'm', 
        WindowTopmost ? 'T' : 't',
        MacroRecording ? 'R' : 'r',
        MacroPlayback ? 'P' : 'p',
        FPS );
#else
    sprintf( Work, "(%c%c) %0.2ffps - " EQHACK_VERSION " - eqwindow@wadeb.com - DO NOT DISTRIBUTE", 
        InputAcquired ? 'M' : 'm', 
        WindowTopmost ? 'T' : 't',
        FPS );
#endif
    SetWindowText( g_hWnd, Work );
}

void ToggleWindowTopmost()
{
    WindowTopmost = !WindowTopmost;
    SetWindowPos( g_hWnd, WindowTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
    RegSetInt( "Topmost", WindowTopmost );
    UpdateWindowText();
}

HRESULT FlipWindow( bool CanSleep )
{
//	D3DHACK_DEBUG( "TRACE: FlipWindow" );
    if ( !g_pddsFrontBuffer )
	{
		D3DHACK_DEBUG( " - g_pddsFrontBuffer is NULL, returning.\n" );
        return DD_OK;
	}
    if ( (DWORD)g_pddsFrontBuffer->lpVtbl == 0xdeadbeef )  // DirectX released object code.  Debug ver only??
	{
		D3DHACK_DEBUG( " - g_pddsFrontBuffer is a released interface, returning.\n" );
        return DD_OK;
	}

	D3DHACK_DEBUG( ".\n" );

    // Calculate FPS
    static float lastupdate = 0;
    static int frames = 0;

    float curtime=((float)timeGetTime())/1000.0f;
    if (curtime - lastupdate > 1.0)
    {
        FPS = frames ? frames / ( curtime - lastupdate ) : 0;
        lastupdate = curtime;
        frames = 0;
    }
    else
        frames++;

    RECT r;
    GetClientRect( g_hWnd, &r );
    ClientToScreen( g_hWnd, (POINT*)&r.left );
    ClientToScreen( g_hWnd, (POINT*)&r.right);
    HRESULT ret = g_pddsFrontBuffer->lpVtbl->Blt( g_pddsFrontBuffer, &r, g_pddsBackBuffer, NULL, DDBLT_WAIT, NULL );
    if ( ret == DDERR_SURFACELOST )
    {
        // This doesn't work yet, not sure why..
        D3DHACK_DEBUG( "TRACE:  D3DERR_SURFACELOST on Flip.\n" );
        g_pDD7->lpVtbl->RestoreAllSurfaces( g_pDD7 );
    }

    // Force the framerate down when the window isn't active.
    if ( CanSleep && !InputAcquired )
        Sleep( 200 );

    return ret;
}

// Custom window proc to keep EQ from bringing itself to the front automatically.
WNDPROC OldMainWindowProc = 0;

LRESULT CALLBACK MainWindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch (message) 
    {
    case WM_DESTROY:
        g_hWnd = 0;
//        // This doesn't work, so I disabled the Close button in the window style.
//        D3DHACK_DEBUG( "TRACE: Posted Quit Message.\n" );
//        PostQuitMessage( 0 );
        break;

    case WM_TIMER:
        UpdateWindowText();
        break;

    // Prevent background flicker when resizing.
    case WM_ERASEBKGND:
        break;

    case WM_LBUTTONDOWN:
        AcquireInput( true );
        break;

    case WM_ACTIVATE:
		D3DHACK_DEBUG( "TRACE: WM_ACTIVATE - " );
        if ( LOWORD( wParam ) == WA_ACTIVE )
		{
			D3DHACK_DEBUG( "Activate.\n" );
            AcquireInput( true );
		}
        else
		{
			D3DHACK_DEBUG( "Deactivate.\n" );
            AcquireInput( false );
		}
        // Fall through...

    // Redraw the window automatically when sized/activated.
    // Helps things behave nicely when zoning.
    case WM_PAINT:
    case WM_SIZE:
        {
            WINDOWPLACEMENT wp;
            GetWindowPlacement( g_hWnd, &wp );
            RegSetWP( "Window", &wp );

            if ( g_pddsFrontBuffer )
                FlipWindow( false );
            return DefWindowProc(hWnd, message, wParam, lParam);
        }

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Empty the message queue.  Makes the window still behave nicely even when EQ is loading.
// Called from IDirectDraw2::CreateSurface.
bool CheckMessages()
{
    MSG msg;

	D3DHACK_DEBUG( "TRACE: CheckMessages.\n" );

    // Main message loop:
    while ( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) ) 
    {
        if ( !GetMessage( &msg, NULL, 0, 0 ) )
            return false;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return true;
}

// Sets the window so that the EQ window has a 1:1 aspect ration, making the text look better.
void SetDefaultSize()
{
    RECT r = { 0, 0, ScreenWidth, ScreenHeight };
    AdjustWindowRect( &r, WS_OVERLAPPED, FALSE );
    SetWindowPos( g_hWnd, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_DRAWFRAME | SWP_NOZORDER | SWP_NOMOVE );
}

void SetupWindow( HWND hWnd )
{
    D3DHACK_DEBUG( "TRACE: SetupWindow.\n" );

//    CrashTestFunction( 3 );

    g_hWnd = hWnd;
//    Control->hWnd = hWnd;

    SetWindowLong( hWnd, GWL_WNDPROC, (long)MainWindowProc );
    SetWindowLong( hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE );
    SetClassLong( hWnd, GCL_HCURSOR, (long)LoadCursor( NULL, IDC_ARROW ) );
    SetClassLong( hWnd, GCL_STYLE, CS_HREDRAW | CS_VREDRAW | CS_NOCLOSE );
    SetClassLong( hWnd, GCL_MENUNAME, 0 );

    // Reset the ShowCursor count thing.
    while ( ShowCursor( TRUE ) <= 0 ) {}

    SetTimer( hWnd, 0, 1000, 0 );

    WINDOWPLACEMENT wp;
    RegGetWP( "Window", &wp );
    if ( wp.length )
    {
        wp.showCmd = SW_SHOW;
        SetWindowPlacement( hWnd, &wp );
    }
    else
    {
        RECT r = { 0, 0, 640, 480 };
        AdjustWindowRect( &r, WS_OVERLAPPED, FALSE );
        SetWindowPos( hWnd, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_DRAWFRAME );
    }

    WindowTopmost = RegGetInt( "Topmost" );
    SetWindowPos( hWnd, WindowTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_DRAWFRAME );

    UpdateWindowText();

	// Allow the debugger to connect.
//	if ( GetAsyncKeyState( VK_SHIFT ) )
//		MessageBox( NULL, "Violate me!!!", "Error", MB_ICONHAND | MB_OK );
}

// COM Interface hooks - Thank you COM for being hackable in so many different ways..

// IDirect3D2
HRESULT __stdcall IDirect3D2Hook_CreateDevice( LPDIRECT3D2 _this, REFCLSID riid, LPDIRECTDRAWSURFACE tgt, LPDIRECT3DDEVICE2* ppvObj )
{
    sprintf( Work, "TRACE: IDirect3D2::CreateDevice (tgt=0x%08x, obj=0x%08x)\n", (DWORD)tgt, (DWORD)ppvObj );
    D3DHACK_DEBUG( Work );

    return OldIDirect3D2Vtbl.CreateDevice( _this, riid, tgt, ppvObj );
}

// IDirectDrawSurface3
int FlipCount = 0;
HRESULT __stdcall IDirectDrawSurface3Hook_Flip( LPDIRECTDRAWSURFACE3 _this, LPDIRECTDRAWSURFACE3 a, DWORD b )
{
//    D3DHACK_DEBUG( "TRACE: IDirectDrawSurface3::Flip (BLT)\n" );
    if ( FlipCount++ == 0 ) return DD_OK;
	CheckMessages();
    return FlipWindow( true );
}

HRESULT __stdcall IDirectDrawSurface3Hook_GetAttachedSurface( LPDIRECTDRAWSURFACE3 _this, LPDDSCAPS a, LPDIRECTDRAWSURFACE3 FAR * b )
{
//    sprintf( Work, "TRACE: IDirectDrawSurface3::GetAttachedSurface (0x%08x)", (DWORD)b );
//    D3DHACK_DEBUG( Work );

    if ( a->dwCaps & DDSCAPS_BACKBUFFER )
    {
        // Just return the back buffer, previously created.
        D3DHACK_DEBUG( " - returning g_pddsBackBuffer.\n" );
        OldIDirectDrawSurfaceVtbl.QueryInterface( g_pddsBackBuffer, IID_IDirectDrawSurface3, (void**)b );
        return DD_OK;
    }
    else
    if ( a->dwCaps & DDSCAPS_ZBUFFER )
    {
        // Just return the Z buffer, previously created.
        D3DHACK_DEBUG( " - returning g_pddsZBuffer.\n" );
        OldIDirectDrawSurfaceVtbl.QueryInterface( g_pddsZBuffer, IID_IDirectDrawSurface3, (void**)b );
        return DD_OK;
    }
    else
        D3DHACK_DEBUG( "\n" );

    return OldIDirectDrawSurface3Vtbl.GetAttachedSurface( _this, a, b );
}

ULONG  __stdcall IDirectDrawSurface3Hook_Release( LPDIRECTDRAWSURFACE3 _this ) 
{ 
    sprintf( Work, "TRACE: IDirectDrawSurface3::Release (0x%08x)", (DWORD)_this );
    D3DHACK_DEBUG( Work );

    UINT ret = OldIDirectDrawSurface3Vtbl.Release( _this );

    if ( _this == g_pddsFrontBuffer3 )
    {
        if ( ret == 0 )
        {
            D3DHACK_DEBUG( " - g_pddsFrontBuffer released.\n" );
            g_pddsFrontBuffer = 0;
        }
        else
            D3DHACK_DEBUG( " - g_pddsFrontBuffer.\n" );
    }
    else
    if ( _this == g_pddsBackBuffer3 )
    {
        if ( ret == 0 )
        {
            D3DHACK_DEBUG( " - g_pddsBackBuffer released.\n" );
            g_pddsBackBuffer = 0;
        }
        else
            D3DHACK_DEBUG( " - g_pddsBackBuffer.\n" );
    }
    else
    if ( _this == g_pddsZBuffer3 )
    {
        if ( ret == 0 )
        {
            D3DHACK_DEBUG( " - g_pddsZBuffer released.\n" );
            g_pddsZBuffer = 0;
        }
        else
            D3DHACK_DEBUG( " - g_pddsZBuffer.\n" );
    }
    else
        D3DHACK_DEBUG( ".\n" );

    return ret;  
}

// IDirectDrawSurface
HRESULT __stdcall IDirectDrawSurfaceHook_QueryInterface( LPDIRECTDRAWSURFACE _this, REFIID riid, LPVOID * ppvObj ) 
{ 
//    sprintf( Work, "TRACE: IDirectDrawSurface::QueryInterface (0x%08x)", (DWORD)ppvObj );
//    D3DHACK_DEBUG( Work );

    if ( IsEqualGUID( riid, IID_IDirectDrawSurface3 ) )
    {
        D3DHACK_DEBUG( " - requested IDirectDrawSurface3 interface.\n" );
        HRESULT ret = OldIDirectDrawSurfaceVtbl.QueryInterface( _this, riid, ppvObj );

        static bool IDirectDrawSurface3Hooked = false;
        if ( !IDirectDrawSurface3Hooked )
        {
            D3DHACK_DEBUG( "TRACE:   Hooking IDirectDrawSurface3 interface.\n" );

            // Make a copy of the old Vtbl.
            memcpy( &OldIDirectDrawSurface3Vtbl, ( (LPDIRECTDRAWSURFACE2)*ppvObj )->lpVtbl, sizeof(IDirectDrawSurface3Vtbl) );

            // Hook functions.
            ( (LPDIRECTDRAWSURFACE3)*ppvObj )->lpVtbl->Flip = IDirectDrawSurface3Hook_Flip;
            ( (LPDIRECTDRAWSURFACE3)*ppvObj )->lpVtbl->GetAttachedSurface = IDirectDrawSurface3Hook_GetAttachedSurface;
            ( (LPDIRECTDRAWSURFACE3)*ppvObj )->lpVtbl->Release = IDirectDrawSurface3Hook_Release;

            IDirectDrawSurface3Hooked = true;
        }

        if ( _this == g_pddsFrontBuffer )
            g_pddsFrontBuffer3 = (LPDIRECTDRAWSURFACE3)*ppvObj;
        else
        if ( _this == g_pddsBackBuffer )
            g_pddsBackBuffer3 = (LPDIRECTDRAWSURFACE3)*ppvObj;
        else
        if ( _this == g_pddsZBuffer )
            g_pddsZBuffer3 = (LPDIRECTDRAWSURFACE3)*ppvObj;

        return ret;
    }
    else
    {
        D3DHACK_DEBUG( " - unsupported interface requested.\n" );
        return OldIDirectDrawSurfaceVtbl.QueryInterface( _this, riid, ppvObj );
    }
}

// IDirectDraw2
HRESULT __stdcall IDirectDraw2Hook_QueryInterface( LPDIRECTDRAW2 _this, REFIID riid, LPVOID * ppvObj) 
{ 
    sprintf( Work, "TRACE: IDirectDraw2::QueryInterface (0x%08x)", (DWORD)ppvObj );
    D3DHACK_DEBUG( Work );

    if ( IsEqualGUID( riid, IID_IDirect3D2 ) )
    {
        D3DHACK_DEBUG( " - requested IDirect3D2 interface.\n" );
        HRESULT ret = OldIDirectDraw2Vtbl.QueryInterface( _this, riid, ppvObj );

        static bool IDirect3D2Hooked = false;
        if ( !IDirect3D2Hooked )
        {
            D3DHACK_DEBUG( "TRACE:   Hooking IDirect3D2 interface.\n" );

            // Make a copy of the old Vtbl.
            memcpy( &OldIDirect3D2Vtbl, ( (LPDIRECT3D2)*ppvObj )->lpVtbl, sizeof(IDirect3D2Vtbl) );

            // Make it use a diff Vtbl.
            memcpy( &HookedIDirect3D2Vtbl, ( (LPDIRECT3D2)*ppvObj )->lpVtbl, sizeof(IDirect3D2Vtbl) );
            ( (LPDIRECT3D2)*ppvObj )->lpVtbl = &HookedIDirect3D2Vtbl;

            // Hook functions.
            ( (LPDIRECT3D2)*ppvObj )->lpVtbl->CreateDevice = IDirect3D2Hook_CreateDevice;

            IDirect3D2Hooked = true;
        }

        return ret;
    }
    else
    {
        D3DHACK_DEBUG( " - unsupported interface requested.\n" );
        return OldIDirectDraw2Vtbl.QueryInterface( _this, riid, ppvObj );
    }
}

HRESULT __stdcall IDirectDraw2Hook_CreateSurface( LPDIRECTDRAW2 _this, LPDDSURFACEDESC a, LPDIRECTDRAWSURFACE FAR * b, IUnknown FAR * c)
{
    // Trap creation of Primary surface, and create all buffers then.
    if ( a->dwFlags & DDSD_BACKBUFFERCOUNT )
    {
        sprintf( Work, "TRACE: IDirectDraw2::CreateSurface (0x%08x)", (DWORD)b );
        D3DHACK_DEBUG( Work );
        D3DHACK_DEBUG( " - returning g_pddsFrontBuffer.\n" );

        FlipCount = 0;
        CreateWindowedBuffers();

        *b = g_pddsFrontBuffer;

        static bool IDirectDrawSurfaceHooked = false;
        if ( !IDirectDrawSurfaceHooked )
        {
            D3DHACK_DEBUG( "TRACE:   Hooking IDirectDrawSurface interface.\n" );

            // Make a copy of the old Vtbl.
            memcpy( &OldIDirectDrawSurfaceVtbl, g_pddsFrontBuffer->lpVtbl, sizeof(IDirectDrawSurfaceVtbl) );

            // Hook functions.
            g_pddsFrontBuffer->lpVtbl->QueryInterface = IDirectDrawSurfaceHook_QueryInterface;

            IDirectDrawSurfaceHooked = true;
        }

        return DD_OK;
    }
	
	// Trap creation of ZBuffer surface, return the precreated object.
    if ( a->ddsCaps.dwCaps & DDSCAPS_ZBUFFER )
    {
        sprintf( Work, "TRACE: IDirectDraw2::CreateSurface (0x%08x)", (DWORD)b );
        D3DHACK_DEBUG( Work );
        D3DHACK_DEBUG( " - returning g_pddsZBuffer.\n" );
        *b = g_pddsZBuffer;
        return DD_OK;
    }

	D3DHACK_DEBUG( ".\n" );

    // These two keep the window behaving nicely during load time, since CreateSurface is called 
    // often for texture creation.
    CheckKeyboard();
    CheckMessages();

    return OldIDirectDraw2Vtbl.CreateSurface( _this, a, b, c );
}

HRESULT __stdcall IDirectDraw2Hook_SetCooperativeLevel( LPDIRECTDRAW2 _this, HWND a, DWORD b )
{
    D3DHACK_DEBUG( "TRACE: IDirectDraw2::SetCooperativeLevel\n" );

    SetupWindow( a );

    return OldIDirectDraw2Vtbl.SetCooperativeLevel( _this, a, DDSCL_NORMAL );
}

HRESULT __stdcall IDirectDraw2Hook_SetDisplayMode( LPDIRECTDRAW2 _this, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e )
{
    sprintf( Work, "TRACE: MyIDirectDraw2::SetDisplayMode (%d, %d, %d, %d, %x) restoring instead.\n", a, b, c, d, e );
    D3DHACK_DEBUG( Work );

    ScreenWidth = a;
    ScreenHeight = b;

    return OldIDirectDraw2Vtbl.RestoreDisplayMode( _this );
}

// IDirectDraw
HRESULT __stdcall IDirectDrawHook_QueryInterface( LPDIRECTDRAW _this, REFIID riid, LPVOID * ppvObj ) 
{ 
    sprintf( Work, "TRACE: IDirectDraw::QueryInterface (0x%08x)", (DWORD)ppvObj );
    D3DHACK_DEBUG( Work );

    if ( IsEqualGUID( riid, IID_IDirectDraw2 ) )
    {
        D3DHACK_DEBUG( " - requested IDirectDraw2 interface.\n" );
        HRESULT ret = OldIDirectDrawVtbl.QueryInterface( _this, riid, ppvObj );

        g_pDD = (LPDIRECTDRAW2)*ppvObj;

        g_pDD->lpVtbl->QueryInterface( g_pDD, IID_IDirectDraw7, (void**)&g_pDD7 );

        static bool IDirectDraw2Hooked = false;
        if ( !IDirectDraw2Hooked )
        {
            D3DHACK_DEBUG( "TRACE:   Hooking IDirectDraw2 interface.\n" );

            // Make a copy of the old Vtbl.
            memcpy( &OldIDirectDraw2Vtbl, ( (LPDIRECTDRAW2)*ppvObj )->lpVtbl, sizeof(IDirectDraw2Vtbl) );

            // Hook functions.
            ( (LPDIRECTDRAW2)*ppvObj )->lpVtbl->QueryInterface = IDirectDraw2Hook_QueryInterface;
            ( (LPDIRECTDRAW2)*ppvObj )->lpVtbl->CreateSurface = IDirectDraw2Hook_CreateSurface;
            ( (LPDIRECTDRAW2)*ppvObj )->lpVtbl->SetCooperativeLevel = IDirectDraw2Hook_SetCooperativeLevel;
            ( (LPDIRECTDRAW2)*ppvObj )->lpVtbl->SetDisplayMode = IDirectDraw2Hook_SetDisplayMode;

            IDirectDraw2Hooked = true;
        }

        return ret;
    }
    else
    {
        D3DHACK_DEBUG( " - unsupported interface requested.\n" );
        return OldIDirectDrawVtbl.QueryInterface( _this, riid, ppvObj );
    }
}

// Function types.
DECLARE_HANDLE(HMONITOR);
typedef BOOL (FAR PASCAL * LPDDENUMCALLBACKA)(GUID FAR *, LPSTR, LPSTR, LPVOID);
typedef BOOL (FAR PASCAL * LPDDENUMCALLBACKW)(GUID FAR *, LPWSTR, LPWSTR, LPVOID);

typedef BOOL (FAR PASCAL * LPDDENUMCALLBACKEXA)(GUID FAR *, LPSTR, LPSTR, LPVOID, HMONITOR);
typedef BOOL (FAR PASCAL * LPDDENUMCALLBACKEXW)(GUID FAR *, LPWSTR, LPWSTR, LPVOID, HMONITOR);

typedef HRESULT (WINAPI *DirectDrawEnumerateW_Type)( LPDDENUMCALLBACKW lpCallback, LPVOID lpContext );
typedef HRESULT (WINAPI *DirectDrawEnumerateA_Type)( LPDDENUMCALLBACKA lpCallback, LPVOID lpContext );
typedef HRESULT (WINAPI *DirectDrawEnumerateExW_Type)( LPDDENUMCALLBACKEXW lpCallback, LPVOID lpContext, DWORD dwFlags);
typedef HRESULT (WINAPI *DirectDrawEnumerateExA_Type)( LPDDENUMCALLBACKEXA lpCallback, LPVOID lpContext, DWORD dwFlags);
typedef HRESULT (WINAPI *DirectDrawCreate_Type)( GUID FAR *lpGUID, LPDIRECTDRAW FAR *lplpDD, IUnknown FAR *pUnkOuter );
typedef HRESULT (WINAPI *DirectDrawCreateEx_Type)( GUID FAR * lpGuid, LPVOID  *lplpDD, REFIID  iid,IUnknown FAR *pUnkOuter );
typedef HRESULT (WINAPI *DirectDrawCreateClipper_Type)( DWORD dwFlags, LPDIRECTDRAWCLIPPER FAR *lplpDDClipper, IUnknown FAR *pUnkOuter );

// Function prototypes.
HRESULT WINAPI DirectDrawEnumerateW( LPDDENUMCALLBACKW lpCallback, LPVOID lpContext );
HRESULT WINAPI DirectDrawEnumerateA( LPDDENUMCALLBACKA lpCallback, LPVOID lpContext );
HRESULT WINAPI DirectDrawEnumerateExW( LPDDENUMCALLBACKEXW lpCallback, LPVOID lpContext, DWORD dwFlags);
HRESULT WINAPI DirectDrawEnumerateExA( LPDDENUMCALLBACKEXA lpCallback, LPVOID lpContext, DWORD dwFlags);
HRESULT WINAPI DirectDrawCreate( GUID FAR *lpGUID, LPDIRECTDRAW FAR *lplpDD, IUnknown FAR *pUnkOuter );
HRESULT WINAPI DirectDrawCreateEx( GUID FAR * lpGuid, LPVOID  *lplpDD, REFIID  iid,IUnknown FAR *pUnkOuter );
HRESULT WINAPI DirectDrawCreateClipper( DWORD dwFlags, LPDIRECTDRAWCLIPPER FAR *lplpDDClipper, IUnknown FAR *pUnkOuter );

enum
{
    D3DFN_DirectDrawEnumerateW = 0,
    D3DFN_DirectDrawEnumerateA,
    D3DFN_DirectDrawEnumerateExW,
    D3DFN_DirectDrawEnumerateExA,
    D3DFN_DirectDrawCreate,
    D3DFN_DirectDrawCreateEx,
    D3DFN_DirectDrawCreateClipper,
};

// Hook structure.
SDLLHook D3DHook = 
{
    "DDRAW.DLL",
    false, 0,
    {
        { "DirectDrawEnumerateW", DirectDrawEnumerateW },
        { "DirectDrawEnumerateA", DirectDrawEnumerateA },
        { "DirectDrawEnumerateExW", DirectDrawEnumerateExW },
        { "DirectDrawEnumerateExA", DirectDrawEnumerateExA },
        { "DirectDrawCreate", DirectDrawCreate },
        { "DirectDrawCreateEx", DirectDrawCreateEx },
        { "DirectDrawCreateClipper", DirectDrawCreateClipper },
        { NULL, NULL }
    }
};

// Global hook functions.
HRESULT WINAPI DirectDrawEnumerateW( LPDDENUMCALLBACKW lpCallback, LPVOID lpContext )
{
    D3DHACK_DEBUG( "TRACE: DirectDrawEnumerateW\n" );

    DirectDrawEnumerateW_Type OldFn = 
        (DirectDrawEnumerateW_Type)D3DHook.Functions[D3DFN_DirectDrawEnumerateW].OrigFn;
    return OldFn( lpCallback, lpContext );
}

HRESULT WINAPI DirectDrawEnumerateA( LPDDENUMCALLBACKA lpCallback, LPVOID lpContext )
{
    D3DHACK_DEBUG( "TRACE: DirectDrawEnumerateA\n" );

    DirectDrawEnumerateA_Type OldFn = 
        (DirectDrawEnumerateA_Type)D3DHook.Functions[D3DFN_DirectDrawEnumerateA].OrigFn;
    return OldFn( lpCallback, lpContext );
}

HRESULT WINAPI DirectDrawEnumerateExW( LPDDENUMCALLBACKEXW lpCallback, LPVOID lpContext, DWORD dwFlags)
{
    D3DHACK_DEBUG( "TRACE: DirectDrawEnumerateExW\n" );

    DirectDrawEnumerateExW_Type OldFn = 
        (DirectDrawEnumerateExW_Type)D3DHook.Functions[D3DFN_DirectDrawEnumerateExW].OrigFn;
    return OldFn( lpCallback, lpContext, dwFlags );
}

HRESULT WINAPI DirectDrawEnumerateExA( LPDDENUMCALLBACKEXA lpCallback, LPVOID lpContext, DWORD dwFlags)
{
    D3DHACK_DEBUG( "TRACE: DirectDrawEnumerateExA\n" );

    DirectDrawEnumerateExA_Type OldFn = 
        (DirectDrawEnumerateExA_Type)D3DHook.Functions[D3DFN_DirectDrawEnumerateExA].OrigFn;
    return OldFn( lpCallback, lpContext, dwFlags );
}

HRESULT WINAPI DirectDrawCreate( GUID FAR *lpGuid, LPDIRECTDRAW FAR *lplpDD, IUnknown FAR *pUnkOuter )
{
    // Poor man's serial port...
//    MessageBeep( MB_ICONHAND );

    D3DHACK_DEBUG( "TRACE: DirectDrawCreate\n" );

    DirectDrawCreate_Type OldFn = 
        (DirectDrawCreate_Type)D3DHook.Functions[D3DFN_DirectDrawCreate].OrigFn;
    HRESULT ret = OldFn( lpGuid, lplpDD, pUnkOuter );

    // Don't hook the device polling process.
    static int CreateCount = 0;
    CreateCount++;

	if ( CreateCount == 1 )
	{
		// Allow the debugger to connect.
//		if ( GetAsyncKeyState( VK_SHIFT ) )
//			MessageBox( NULL, "Violate me!!!", "Error", MB_ICONHAND | MB_OK );

		// Make sure the display is in 16bpp.
		DDSURFACEDESC ddsd;
		ddsd.dwSize = sizeof(ddsd);

		LPDIRECTDRAW2 pDD2;
		IDirectDraw_QueryInterface( *lplpDD, IID_IDirectDraw2, (void**)&pDD2 );
		IDirectDraw2_GetDisplayMode( pDD2, &ddsd );
		
		if( ddsd.ddpfPixelFormat.dwRGBBitCount != 16 )
		{
			MessageBox( NULL, "EQWindow requires a 16bit color depth display mode.\n\nPlease change the setting in Display Properties.", "Error", MB_ICONHAND | MB_OK );
			return DDERR_INVALIDMODE;
		}

		IDirectDraw2_Release( pDD2 );
	}

    if ( CreateCount < 2 )
        return ret;

/*
    Other way to do it:

    // Make copies of the old Vtbl.
    memcpy( &OldIDirectDrawVtbl, ( *lplpDD )->lpVtbl, sizeof(IDirectDrawVtbl) );
    memcpy( &HookedIDirectDrawVtbl, ( *lplpDD )->lpVtbl, sizeof(IDirectDrawVtbl) );

    // Create the hooked vtbl.
    HookedIDirectDrawVtbl.QueryInterface = IDirectDrawHook_QueryInterface;

    // Reroute the Vtbl.
    ( *lplpDD )->lpVtbl = &HookedIDirectDrawVtbl;
*/

    static bool IDirectDrawHooked = false;
    if ( !IDirectDrawHooked )
    {
        D3DHACK_DEBUG( "TRACE:   Hooking IDirectDraw interface.\n" );

        // Make a copy of the old Vtbl.
        memcpy( &OldIDirectDrawVtbl, ( *lplpDD )->lpVtbl, sizeof(IDirectDrawVtbl) );

        // Hook functions.
        ( *lplpDD )->lpVtbl->QueryInterface = IDirectDrawHook_QueryInterface;

        IDirectDrawHooked = true;
    }

    return ret;
}

HRESULT WINAPI DirectDrawCreateEx( GUID FAR * lpGuid, LPVOID *lplpDD, REFIID iid, IUnknown FAR *pUnkOuter )
{
    D3DHACK_DEBUG( "TRACE: DirectDrawCreateEx\n" );

    DirectDrawCreateEx_Type OldFn = 
        (DirectDrawCreateEx_Type)D3DHook.Functions[D3DFN_DirectDrawCreateEx].OrigFn;
    return OldFn( lpGuid, lplpDD, iid, pUnkOuter );
}

HRESULT WINAPI DirectDrawCreateClipper( DWORD dwFlags, LPDIRECTDRAWCLIPPER FAR *lplpDDClipper, IUnknown FAR *pUnkOuter )
{
    D3DHACK_DEBUG( "TRACE: DirectDrawCreateClipper\n" );

    DirectDrawCreateClipper_Type OldFn = 
        (DirectDrawCreateClipper_Type)D3DHook.Functions[D3DFN_DirectDrawCreateClipper].OrigFn;
    return OldFn( dwFlags, lplpDDClipper, pUnkOuter );
}

