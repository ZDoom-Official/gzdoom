/*
** i_input.cpp
** Handles input from keyboard, mouse, and joystick
**
**---------------------------------------------------------------------------
** Copyright 1998-2009 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

// DI3 only supports up to 4 mouse buttons, and I want the joystick to
// be read using DirectInput instead of winmm.

#define WIN32_LEAN_AND_MEAN
#define __BYTEBOOL__
#ifndef __GNUC__
#define INITGUID
#endif
#define DIRECTINPUT_VERSION 0x800
#include <windows.h>
#include <dbt.h>
#include <dinput.h>
#include <malloc.h>

#ifdef _MSC_VER
#pragma warning(disable:4244)
#endif

#ifndef GET_RAWINPUT_CODE_WPARAM
#define GET_RAWINPUT_CODE_WPARAM(wParam)	((wParam) & 0xff)
#endif


#include "c_dispatch.h"
#include "m_argv.h"
#include "i_input.h"
#include "v_video.h"
#include "i_sound.h"
#include "d_gui.h"
#include "c_console.h"
#include "s_soundinternal.h"
#include "hardware.h"
#include "d_eventbase.h"
#include "v_text.h"
#include "version.h"
#include "engineerrors.h"
#include "i_system.h"
#include "i_interface.h"
#include "printf.h"
#include "c_buttons.h"
#include "cmdlib.h"
#include "i_mainwindow.h"

// Compensate for w32api's lack
#ifndef GET_XBUTTON_WPARAM
#define GET_XBUTTON_WPARAM(wParam) (HIWORD(wParam))
#endif


#ifdef _DEBUG
#define INGAME_PRIORITY_CLASS	NORMAL_PRIORITY_CLASS
#else
//#define INGAME_PRIORITY_CLASS	HIGH_PRIORITY_CLASS
#define INGAME_PRIORITY_CLASS	NORMAL_PRIORITY_CLASS
#endif

FJoystickCollection *JoyDevices[NUM_JOYDEVICES];


extern HINSTANCE g_hInst;

bool GUICapture;
extern FMouse *Mouse;
extern FKeyboard *Keyboard;
extern bool ToggleFullscreen;

bool VidResizing;

extern BOOL vidactive;

EXTERN_CVAR (String, language)
EXTERN_CVAR (Bool, lookstrafe)
EXTERN_CVAR (Bool, use_joystick)
EXTERN_CVAR (Bool, use_mouse)

static int WheelDelta;
extern bool CursorState;

void SetCursorState(bool visible);

extern BOOL paused;
static bool noidle = false;

LPDIRECTINPUT8			g_pdi;

extern bool AppActive;

int BlockMouseMove;

static bool EventHandlerResultForNativeMouse;


EXTERN_CVAR(Bool, i_pauseinbackground);


CVAR (Bool, k_allowfullscreentoggle, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

static void I_CheckGUICapture ()
{
	bool wantCapt = sysCallbacks.WantGuiCapture && sysCallbacks.WantGuiCapture();

	if (wantCapt != GUICapture)
	{
		GUICapture = wantCapt;
		if (wantCapt && Keyboard != NULL)
		{
			Keyboard->AllKeysUp();
		}
	}
}

void I_SetMouseCapture()
{
	SetCapture(mainwindow.GetHandle());
}

void I_ReleaseMouseCapture()
{
	ReleaseCapture();
}

bool GUIWndProcHook(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT *result)
{
	event_t ev = { EV_GUI_Event };

	*result = 0;

	switch (message)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYUP:
		if (message == WM_KEYUP || message == WM_SYSKEYUP)
		{
			ev.subtype = EV_GUI_KeyUp;
		}
		else
		{
			ev.subtype = (lParam & 0x40000000) ? EV_GUI_KeyRepeat : EV_GUI_KeyDown;
		}
		if (GetKeyState(VK_SHIFT) & 0x8000)		ev.data3 |= GKM_SHIFT;
		if (GetKeyState(VK_CONTROL) & 0x8000)	ev.data3 |= GKM_CTRL;
		if (GetKeyState(VK_MENU) & 0x8000)		ev.data3 |= GKM_ALT;
		if (wParam == VK_PROCESSKEY)
		{ // Use the scan code to determine the real virtual-key code.
		  // ImmGetVirtualKey() will supposedly do this, but it just returns
		  // VK_PROCESSKEY again.
			wParam = MapVirtualKey((lParam >> 16) & 255, 1);
		}
		if ( (ev.data1 = MapVirtualKey(wParam, 2)) )
		{
			D_PostEvent(&ev);
		}
		else
		{
			switch (wParam)
			{
			case VK_PRIOR:			ev.data1 = GK_PGUP;		break;
			case VK_NEXT:			ev.data1 = GK_PGDN;		break;
			case VK_END:			ev.data1 = GK_END;		break;
			case VK_HOME:			ev.data1 = GK_HOME;		break;
			case VK_LEFT:			ev.data1 = GK_LEFT;		break;
			case VK_RIGHT:			ev.data1 = GK_RIGHT;	break;
			case VK_UP:				ev.data1 = GK_UP;		break;
			case VK_DOWN:			ev.data1 = GK_DOWN;		break;
			case VK_DELETE:			ev.data1 = GK_DEL;		break;
			case VK_ESCAPE:			ev.data1 = GK_ESCAPE;	break;
			case VK_F1:				ev.data1 = GK_F1;		break;
			case VK_F2:				ev.data1 = GK_F2;		break;
			case VK_F3:				ev.data1 = GK_F3;		break;
			case VK_F4:				ev.data1 = GK_F4;		break;
			case VK_F5:				ev.data1 = GK_F5;		break;
			case VK_F6:				ev.data1 = GK_F6;		break;
			case VK_F7:				ev.data1 = GK_F7;		break;
			case VK_F8:				ev.data1 = GK_F8;		break;
			case VK_F9:				ev.data1 = GK_F9;		break;
			case VK_F10:			ev.data1 = GK_F10;		break;
			case VK_F11:			ev.data1 = GK_F11;		break;
			case VK_F12:			ev.data1 = GK_F12;		break;
			case VK_BROWSER_BACK:	ev.data1 = GK_BACK;		break;
			}
			if (ev.data1 != 0)
			{
				D_PostEvent(&ev);
			}
		}
		// Return false for key downs so that we can handle special hotkeys
		// in the main WndProc.
		return ev.subtype == EV_GUI_KeyUp;

	case WM_CHAR:
	case WM_SYSCHAR:
		if (wParam >= ' ')		// only send displayable characters
		{
			ev.subtype = EV_GUI_Char;
			ev.data1 = wParam;
			ev.data2 = (message == WM_SYSCHAR);
			D_PostEvent(&ev);
			return true;
		}
		break;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_MOUSEMOVE:
		if (message >= WM_LBUTTONDOWN && message <= WM_LBUTTONDBLCLK)
		{
			ev.subtype = message - WM_LBUTTONDOWN + EV_GUI_LButtonDown;
		}
		else if (message >= WM_RBUTTONDOWN && message <= WM_RBUTTONDBLCLK)
		{
			ev.subtype = message - WM_RBUTTONDOWN + EV_GUI_RButtonDown;
		}
		else if (message >= WM_MBUTTONDOWN && message <= WM_MBUTTONDBLCLK)
		{
			ev.subtype = message - WM_MBUTTONDOWN + EV_GUI_MButtonDown;
		}
		else if (message >= WM_XBUTTONDOWN && message <= WM_XBUTTONUP)
		{
			ev.subtype = message - WM_XBUTTONDOWN + EV_GUI_BackButtonDown;
			if (GET_XBUTTON_WPARAM(wParam) == 2)
			{
				ev.subtype += EV_GUI_FwdButtonDown - EV_GUI_BackButtonDown;
			}
			else if (GET_XBUTTON_WPARAM(wParam) != 1)
			{
				break;
			}
		}
		else if (message == WM_MOUSEMOVE)
		{
			ev.subtype = EV_GUI_MouseMove;
			if (BlockMouseMove > 0) return true;
		}

		ev.data1 = LOWORD(lParam);
		ev.data2 = HIWORD(lParam);
		if (screen != NULL)
		{
			screen->ScaleCoordsFromWindow(ev.data1, ev.data2);
		}

		if (wParam & MK_SHIFT)				ev.data3 |= GKM_SHIFT;
		if (wParam & MK_CONTROL)			ev.data3 |= GKM_CTRL;
		if (GetKeyState(VK_MENU) & 0x8000)	ev.data3 |= GKM_ALT;

		if (use_mouse) D_PostEvent(&ev);
		return true;

	// Note: If the mouse is grabbed, it sends the mouse wheel events itself.
	case WM_MOUSEWHEEL:
		if (!use_mouse)  return false;
		if (wParam & MK_SHIFT)				ev.data3 |= GKM_SHIFT;
		if (wParam & MK_CONTROL)			ev.data3 |= GKM_CTRL;
		if (GetKeyState(VK_MENU) & 0x8000)	ev.data3 |= GKM_ALT;
		WheelDelta += (SHORT)HIWORD(wParam);
		if (WheelDelta < 0)
		{
			ev.subtype = EV_GUI_WheelDown;
			while (WheelDelta <= -WHEEL_DELTA)
			{
				D_PostEvent(&ev);
				WheelDelta += WHEEL_DELTA;
			}
		}
		else
		{
			ev.subtype = EV_GUI_WheelUp;
			while (WheelDelta >= WHEEL_DELTA)
			{
				D_PostEvent(&ev);
				WheelDelta -= WHEEL_DELTA;
			}
		}
		return true;
	}
	return false;
}

bool CallHook(FInputDevice *device, HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT *result)
{
	if (device == NULL)
	{
		return false;
	}
	*result = 0;
	return device->WndProcHook(hWnd, message, wParam, lParam, result);
}

LRESULT CALLBACK WndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT result;

	if (message == WM_INPUT)
	{
		UINT size;

		if (!GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) &&
			size != 0)
		{
			TArray<uint8_t> array(size, true);
			uint8_t *buffer = array.data();
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) == size)
			{
				int code = GET_RAWINPUT_CODE_WPARAM(wParam);
				if (Keyboard == NULL || !Keyboard->ProcessRawInput((RAWINPUT *)buffer, code))
				{
					if (Mouse == NULL || !Mouse->ProcessRawInput((RAWINPUT *)buffer, code))
					{
						if (JoyDevices[INPUT_RawPS2] != NULL)
						{
							JoyDevices[INPUT_RawPS2]->ProcessRawInput((RAWINPUT *)buffer, code);
						}
					}
				}
			}
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	if (CallHook(Keyboard, hWnd, message, wParam, lParam, &result))
	{
		return result;
	}
	if (CallHook(Mouse, hWnd, message, wParam, lParam, &result))
	{
		return result;
	}
	for (int i = 0; i < NUM_JOYDEVICES; ++i)
	{
		if (CallHook(JoyDevices[i], hWnd, message, wParam, lParam, &result))
		{
			return result;
		}
	}
	if (GUICapture && GUIWndProcHook(hWnd, message, wParam, lParam, &result))
	{
		return result;
	}

	if (message == WM_LBUTTONDOWN && sysCallbacks.WantLeftButton() && sysCallbacks.WantLeftButton())
	{
		if (GUIWndProcHook(hWnd, message, wParam, lParam, &result))
		{
			return result;
		}
	}


	switch (message)
	{
	case WM_DESTROY:
		SetPriorityClass (GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
		PostQuitMessage (0);
		break;

	case WM_HOTKEY:
		break;

	case WM_PAINT:
		return DefWindowProc (hWnd, message, wParam, lParam);

	case WM_SETTINGCHANGE:
		// If regional settings were changed, reget preferred languages
		if (wParam == 0 && lParam != 0 && strcmp ((const char *)lParam, "intl") == 0)
		{
			language->Callback ();
		}
		return 0;

	case WM_KILLFOCUS:
		I_CheckNativeMouse (true, false);	// Make sure mouse gets released right away
		break;

	case WM_SETFOCUS:
		I_CheckNativeMouse (false, EventHandlerResultForNativeMouse);	// This cannot call the event handler. Doing it from here is unsafe.
		break;

	case WM_SETCURSOR:
		if (!CursorState)
		{
			SetCursorState(false); // turn off window cursor
			return TRUE;	// Prevent Windows from setting cursor to window class cursor
		}
		else
		{
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;

	case WM_SIZE:
		InvalidateRect (hWnd, NULL, FALSE);
		break;

	case WM_KEYDOWN:
		break;

	case WM_SYSKEYDOWN:
		// Pressing Alt+Enter can toggle between fullscreen and windowed.
		if (wParam == VK_RETURN && k_allowfullscreentoggle && !(lParam & 0x40000000))
		{
			ToggleFullscreen = !ToggleFullscreen;
		}
		// Pressing Alt+F4 quits the program.
		if (wParam == VK_F4 && !(lParam & 0x40000000))
		{
			PostQuitMessage(0);
		}
		break;

	case WM_SYSCOMMAND:
		// Prevent activation of the window menu with Alt+Space
		if ((wParam & 0xFFF0) != SC_KEYMENU)
		{
			return DefWindowProc (hWnd, message, wParam, lParam);
		}
		break;

	case WM_DISPLAYCHANGE:
	case WM_STYLECHANGED:
		return DefWindowProc(hWnd, message, wParam, lParam);

	case WM_GETMINMAXINFO:
		if (screen && !VidResizing)
		{
			LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
			if (screen->IsFullscreen())
			{
				RECT rect = { 0, 0, screen->GetWidth(), screen->GetHeight() };
				AdjustWindowRectEx(&rect, WS_VISIBLE | WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);
				mmi->ptMinTrackSize.x = rect.right - rect.left;
				mmi->ptMinTrackSize.y = rect.bottom - rect.top;
			}
			else
			{
				RECT rect = { 0, 0, VID_MIN_WIDTH, VID_MIN_HEIGHT };
				AdjustWindowRectEx(&rect, GetWindowLongW(hWnd, GWL_STYLE), FALSE, GetWindowLongW(hWnd, GWL_EXSTYLE));
				mmi->ptMinTrackSize.x = rect.right - rect.left;
				mmi->ptMinTrackSize.y = rect.bottom - rect.top;
			}
			return 0;
		}
		break;

	case WM_ACTIVATEAPP:
		AppActive = (wParam == TRUE);
		S_SetSoundPaused (wParam);
		break;

	case WM_ERASEBKGND:
		return DefWindowProc(hWnd, message, wParam, lParam);

	case WM_DEVICECHANGE:
		if (wParam == DBT_DEVNODES_CHANGED ||
			wParam == DBT_DEVICEARRIVAL ||
			wParam == DBT_CONFIGCHANGED)
		{
			event_t ev = { EV_DeviceChange };
			D_PostEvent(&ev);
		}
		return DefWindowProc (hWnd, message, wParam, lParam);

	default:
		return DefWindowProc (hWnd, message, wParam, lParam);
	}

	return 0;
}

bool I_InitInput (void *hwnd)
{
	HRESULT hr;

	Printf ("I_InitInput\n");

	noidle = !!Args->CheckParm ("-noidle");
	g_pdi = NULL;

	hr = DirectInput8Create(g_hInst, DIRECTINPUT_VERSION, IID_IDirectInput8, (void **)&g_pdi, NULL);
	if (FAILED(hr))
	{
		Printf(TEXTCOLOR_ORANGE "DirectInput8Create failed: %08lx\n", hr);
		g_pdi = NULL;	// Just to be sure DirectInput8Create didn't change it
	}

	Printf ("I_StartupMouse\n");
	I_StartupMouse();

	Printf ("I_StartupKeyboard\n");
	I_StartupKeyboard();

	Printf ("I_StartupXInput\n");
	I_StartupXInput();

	Printf ("I_StartupRawPS2\n");
	I_StartupRawPS2();

	Printf ("I_StartupDirectInputJoystick\n");
	I_StartupDirectInputJoystick();

	return TRUE;
}


// Free all input resources
void I_ShutdownInput ()
{
	if (Keyboard != NULL)
	{
		delete Keyboard;
		Keyboard = NULL;
	}
	if (Mouse != NULL)
	{
		delete Mouse;
		Mouse = NULL;
	}
	for (int i = 0; i < NUM_JOYDEVICES; ++i)
	{
		if (JoyDevices[i] != NULL)
		{
			delete JoyDevices[i];
			JoyDevices[i] = NULL;
		}
	}
	if (g_pdi)
	{
		g_pdi->Release ();
		g_pdi = NULL;
	}
}

void I_GetEvent ()
{
	MSG mess;

	// Briefly enter an alertable state so that if a secondary thread
	// crashed, we will execute the APC it sent now.
	SleepEx (0, TRUE);

	while (PeekMessage (&mess, NULL, 0, 0, PM_REMOVE))
	{
		if (mess.message == WM_QUIT)
			throw CExitEvent(mess.wParam);

		if (GUICapture)
		{
			TranslateMessage (&mess);
		}
		DispatchMessage (&mess);
	}

	if (Keyboard != NULL)
	{
		Keyboard->ProcessInput();
	}
	if (Mouse != NULL)
	{
		Mouse->ProcessInput();
	}
}

//
// I_StartTic
//
void I_StartTic ()
{
	BlockMouseMove--;
	buttonMap.ResetButtonTriggers ();
	I_CheckGUICapture ();
	EventHandlerResultForNativeMouse = sysCallbacks.WantNativeMouse && sysCallbacks.WantNativeMouse();
	I_CheckNativeMouse (false, EventHandlerResultForNativeMouse);
	I_GetEvent ();
}

//
// I_StartFrame
//
void I_StartFrame ()
{
	if (use_joystick)
	{
		for (int i = 0; i < NUM_JOYDEVICES; ++i)
		{
			if (JoyDevices[i] != NULL)
			{
				JoyDevices[i]->ProcessInput();
			}
		}
	}
}

void I_GetAxes(float axes[NUM_KEYS])
{
	int i;

	for (i = 0; i < NUM_KEYS; ++i)
	{
		axes[i] = 0.0f;
	}

	if (use_joystick)
	{
		for (i = 0; i < NUM_JOYDEVICES; ++i)
		{
			if (JoyDevices[i] != NULL)
			{
				JoyDevices[i]->AddAxes(axes);
			}
		}
	}
}

void I_GetJoysticks(TArray<IJoystickConfig *> &sticks)
{
	sticks.Clear();
	for (int i = 0; i < NUM_JOYDEVICES; ++i)
	{
		if (JoyDevices[i] != NULL)
		{
			JoyDevices[i]->GetDevices(sticks);
		}
	}
}

// If a new controller was added, returns a pointer to it.
IJoystickConfig *I_UpdateDeviceList()
{
	IJoystickConfig *newone = NULL;
	for (int i = 0; i < NUM_JOYDEVICES; ++i)
	{
		if (JoyDevices[i] != NULL)
		{
			IJoystickConfig *thisnewone = JoyDevices[i]->Rescan();
			if (newone == NULL)
			{
				newone = thisnewone;
			}
		}
	}
	return newone;
}

void I_PutInClipboard (const char *str)
{
	if (str == NULL || !OpenClipboard (mainwindow.GetHandle()))
		return;
	EmptyClipboard ();

	auto wstr = WideString(str);
	HGLOBAL cliphandle = GlobalAlloc (GMEM_DDESHARE, wstr.length() * 2 + 2);
	if (cliphandle != nullptr)
	{
		wchar_t *ptr = (wchar_t *)GlobalLock (cliphandle);
		if (ptr)
		{
			wcscpy(ptr, wstr.c_str());
			GlobalUnlock(cliphandle);
			SetClipboardData(CF_UNICODETEXT, cliphandle);
		}
	}
	CloseClipboard ();
}

FString I_GetFromClipboard (bool return_nothing)
{
	FString retstr;
	HGLOBAL cliphandle;
	wchar_t *clipstr;

	if (return_nothing || !IsClipboardFormatAvailable (CF_UNICODETEXT) || !OpenClipboard (mainwindow.GetHandle()))
		return retstr;

	cliphandle = GetClipboardData (CF_UNICODETEXT);
	if (cliphandle != nullptr)
	{
		clipstr = (wchar_t *)GlobalLock (cliphandle);
		if (clipstr != nullptr)
		{
			// Convert CR-LF pairs to just LF.
			retstr = clipstr;
			GlobalUnlock(clipstr);
			retstr.Substitute("\r\n", "\n");
		}
	}

	CloseClipboard ();
	return retstr;
}

//==========================================================================
//
// FInputDevice - Destructor
//
//==========================================================================

FInputDevice::~FInputDevice()
{
}

//==========================================================================
//
// FInputDevice :: ProcessInput
//
// Gives subclasses an opportunity to do input handling that doesn't involve
// window messages.
//
//==========================================================================

void FInputDevice::ProcessInput()
{
}

//==========================================================================
//
// FInputDevice :: ProcessRawInput
//
// Gives subclasses a chance to handle WM_INPUT messages. This is not part
// of WndProcHook so that we only need to fill the RAWINPUT buffer once
// per message and be sure it gets cleaned up properly.
//
//==========================================================================

bool FInputDevice::ProcessRawInput(RAWINPUT *raw, int code)
{
	return false;
}

//==========================================================================
//
// FInputDevice :: WndProcHook
//
// Gives subclasses a chance to intercept window messages. 
//
//==========================================================================

bool FInputDevice::WndProcHook(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT *result)
{
	return false;
}

