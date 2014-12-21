#ifndef EQHACK_H
#define EQHACK_H

// Sets whether any of the 'cheat' features are enabled.
//#define PROVER

#include "eqhack.h"

#define EQHACK_VERSION "EQWindow 1.0b4"

#define D3DHACK_DEBUG(s)    DebugOut( s )
//#define D3DHACK_DEBUG(s) OutputDebugString( s )
//#define D3DHACK_DEBUG(s) 

//#define DINPUTHACK_DEBUG(s)    DebugOut( s )
//#define DINPUTHACK_DEBUG(s) OutputDebugString( s )
#define DINPUTHACK_DEBUG(s) 

// Text buffer for sprintf
extern char Work[256];

void DebugOut( char* s );
HRESULT ReportResult( HRESULT hr );

void RegSetWP( char* Key, LPWINDOWPLACEMENT Value );
void RegGetWP( char* Key, LPWINDOWPLACEMENT Value );
void RegSetInt( char* Key, int Value );
int RegGetInt( char* Key );

// Interface between D3D and DInput.
void ToggleWindowTopmost();
void UpdateWindowText();
void SetDefaultSize();
void CheckKeyboard();
void AcquireInput( bool Acquire );

extern bool InputAcquired;
extern bool MacroRecording;
extern bool MacroPlayback;
extern bool MacroLooping;

extern SEQControl* Control;

#define D3DHACK_FORCEWINDOW
#define D3DHACK_SURFACEPROXY

#endif
