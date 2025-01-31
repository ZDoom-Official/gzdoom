/*
** i_input.h
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
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

#ifndef __I_INPUT_H__
#define __I_INPUT_H__

#include "basics.h"

bool I_InitInput (void *hwnd);
void I_ShutdownInput ();

void I_GetEvent();

enum
{
	INPUT_DIJoy,
	INPUT_XInput,
	INPUT_RawPS2,
	NUM_JOYDEVICES
};


#ifdef _WIN32
#include "m_joy.h"

// Don't make these definitions available to the main body of the source code.


struct tagRAWINPUT;

class FInputDevice
{
public:
	virtual ~FInputDevice() = 0;
	virtual bool GetDevice() = 0;
	virtual void ProcessInput();
	virtual bool ProcessRawInput(tagRAWINPUT *raw, int code);
	virtual bool WndProcHook(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT *result);
};

class FMouse : public FInputDevice
{
public:
	FMouse();

	virtual void Grab() = 0;
	virtual void Ungrab() = 0;

protected:
	void WheelMoved(int axis, int wheelmove);
	void PostButtonEvent(int button, bool down);
	void ClearButtonState();

	int WheelMove[2];
	int ButtonState;	// bit mask of current button states (1=down, 0=up)
};

class FKeyboard : public FInputDevice
{
public:
	FKeyboard();
	~FKeyboard();

	void AllKeysUp();

protected:
	uint8_t KeyStates[256/8];

	int CheckKey(int keynum) const
	{
		return KeyStates[keynum >> 3] & (1 << (keynum & 7));
	}
	void SetKey(int keynum, bool down)
	{
		if (down)
		{
			KeyStates[keynum >> 3] |= 1 << (keynum & 7);
		}
		else
		{
			KeyStates[keynum >> 3] &= ~(1 << (keynum & 7));
		}
	}
	bool CheckAndSetKey(int keynum, INTBOOL down);
	void PostKeyEvent(int keynum, INTBOOL down, bool foreground);
};

class FJoystickCollection : public FInputDevice
{
public:
	virtual void AddAxes(float axes[NUM_KEYS]) = 0;
	virtual void GetDevices(TArray<IJoystickConfig *> &sticks) = 0;
	virtual IJoystickConfig *Rescan() = 0;
};

extern FJoystickCollection *JoyDevices[NUM_JOYDEVICES];

void I_StartupMouse();
void I_CheckNativeMouse(bool prefer_native, bool eh);
void I_StartupKeyboard();
void I_StartupXInput();
void I_StartupDirectInputJoystick();
void I_StartupRawPS2();
bool I_IsPS2Adapter(DWORD vidpid);

// USB HID usage page numbers
#define HID_GENERIC_DESKTOP_PAGE			0x01
#define HID_SIMULATION_CONTROLS_PAGE		0x02
#define HID_VR_CONTROLS_PAGE				0x03
#define HID_SPORT_CONTROLS_PAGE				0x04
#define HID_GAME_CONTROLS_PAGE				0x05
#define HID_GENERIC_DEVICE_CONTROLS_PAGE	0x06
#define HID_KEYBOARD_PAGE					0x07
#define HID_LED_PAGE						0x08
#define HID_BUTTON_PAGE						0x09
#define HID_ORDINAL_PAGE					0x0a
#define HID_TELEPHONY_DEVICE_PAGE			0x0b
#define HID_CONSUMER_PAGE					0x0c
#define HID_DIGITIZERS_PAGE					0x0d
#define HID_UNICODE_PAGE					0x10
#define HID_ALPHANUMERIC_DISPLAY_PAGE		0x14
#define HID_MEDICAL_INSTRUMENT_PAGE			0x40

// HID Generic Desktop Page usages
#define HID_GDP_UNDEFINED					0x00
#define HID_GDP_POINTER						0x01
#define HID_GDP_MOUSE						0x02
#define HID_GDP_JOYSTICK					0x04
#define HID_GDP_GAMEPAD						0x05
#define HID_GDP_KEYBOARD					0x06
#define HID_GDP_KEYPAD						0x07
#define HID_GDP_MULTIAXIS_CONTROLLER		0x08
#endif


#endif
