/*
**
**
**---------------------------------------------------------------------------
** Copyright 2005-2016 Randy Heit
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

// HEADER FILES ------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <limits.h>

#include "i_input.h"
#include "d_eventbase.h"

#include "gameconfigfile.h"
#include "m_argv.h"
#include "cmdlib.h"
#include "keydef.h"

// MACROS ------------------------------------------------------------------

// This macro is defined by newer versions of xinput.h. In case we are
// compiling with an older version, define it here.
#ifndef XUSER_MAX_COUNT
#define XUSER_MAX_COUNT                 4
#endif

// MinGW
#ifndef XINPUT_DLL
#define XINPUT_DLL_A  "xinput1_3.dll"
#define XINPUT_DLL_W L"xinput1_3.dll"
#ifdef UNICODE
    #define XINPUT_DLL XINPUT_DLL_W
#else
    #define XINPUT_DLL XINPUT_DLL_A
#endif
#endif

extern bool AppActive;

// TYPES -------------------------------------------------------------------

typedef DWORD (WINAPI *XInputGetStateType)(DWORD index, XINPUT_STATE *state);
typedef DWORD (WINAPI *XInputSetStateType)(DWORD index, XINPUT_STATE *state);
typedef DWORD (WINAPI *XInputGetCapabilitiesType)(DWORD index, DWORD flags, XINPUT_CAPABILITIES *caps);
typedef void  (WINAPI *XInputEnableType)(BOOL enable);

class FXInputController : public IJoystickConfig
{
public:
	FXInputController(int index);
	~FXInputController();

	void ProcessInput();
	void AddAxes(float axes[NUM_KEYS]);
	bool IsConnected() { return Connected; }

	// IJoystickConfig interface
	FString GetName();
	float GetSensitivity();
	virtual void SetSensitivity(float scale);

	int GetNumAxes();
	float GetAxisDeadZone(int axis);
	const char *GetAxisName(int axis);
	float GetAxisScale(int axis);

	void SetAxisDeadZone(int axis, float deadzone);
	void SetAxisScale(int axis, float scale);

	bool IsSensitivityDefault();
	bool IsAxisDeadZoneDefault(int axis);
	bool IsAxisScaleDefault(int axis);

	bool GetEnabled();
	void SetEnabled(bool enabled);

	bool AllowsEnabledInBackground() { return true; }
	bool GetEnabledInBackground() { return EnabledInBackground; }
	void SetEnabledInBackground(bool enabled) { EnabledInBackground = enabled; }

	void SetDefaultConfig();
	FString GetIdentifier();

protected:
	struct AxisInfo
	{
		float Value;
		float DeadZone;
		float Multiplier;
		int Keys[2];
		uint8_t ButtonValue;
	};
	struct DefaultAxisConfig
	{
		float DeadZone;
		float Multiplier;
	};
	enum
	{
		AXIS_ThumbLX,
		AXIS_ThumbLY,
		AXIS_ThumbRX,
		AXIS_ThumbRY,
		AXIS_LeftTrigger,
		AXIS_RightTrigger,
		NUM_AXES
	};

	int Index;
	float Multiplier;
	AxisInfo Axes[NUM_AXES];
	static DefaultAxisConfig DefaultAxes[NUM_AXES];
	DWORD LastPacketNumber;
	int LastButtons;
	bool Connected;
	bool Enabled;
	bool EnabledInBackground;

	void Attached();
	void Detached();

	static void ProcessThumbstick(int value1, AxisInfo *axis1, int value2, AxisInfo *axis2, int base);
	static void ProcessTrigger(int value, AxisInfo *axis, int base);
};

class FXInputManager : public FJoystickCollection
{
public:
	FXInputManager();
	~FXInputManager();

	bool GetDevice();
	void ProcessInput();
	bool WndProcHook(HWND hWnd, uint32_t message, WPARAM wParam, LPARAM lParam, LRESULT *result);
	void AddAxes(float axes[NUM_KEYS]);
	void GetDevices(TArray<IJoystickConfig *> &sticks);
	IJoystickConfig *Rescan();

protected:
	HMODULE XInputDLL;
	FXInputController *Devices[XUSER_MAX_COUNT];
};

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CUSTOM_CVAR(Bool, joy_xinput, true, CVAR_GLOBALCONFIG|CVAR_ARCHIVE|CVAR_NOINITCALL)
{
	I_StartupXInput();
	event_t ev = { EV_DeviceChange };
	D_PostEvent(&ev);
}

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static XInputGetStateType			InputGetState;
static XInputSetStateType			InputSetState;
static XInputGetCapabilitiesType	InputGetCapabilities;
static XInputEnableType				InputEnable;

static const char *AxisNames[] =
{
	"Left Thumb X Axis",
	"Left Thumb Y Axis",
	"Right Thumb X Axis",
	"Right Thumb Y Axis",
	"Left Trigger",
	"Right Trigger"
};

FXInputController::DefaultAxisConfig FXInputController::DefaultAxes[NUM_AXES] =
{
	// Dead zone, multiplier
	{ XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32768.f, 1 },		// ThumbLX
	{ XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32768.f, 1 },		// ThumbLY
	{ XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32768.f, 1 },		// ThumbRX
	{ XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32768.f, 0.75 },	// ThumbRY
	{ XINPUT_GAMEPAD_TRIGGER_THRESHOLD / 256.f, 0 },			// LeftTrigger
	{ XINPUT_GAMEPAD_TRIGGER_THRESHOLD / 256.f, 0 }				// RightTrigger
};

// CODE --------------------------------------------------------------------

//==========================================================================
//
// FXInputController - Constructor
//
//==========================================================================

FXInputController::FXInputController(int index)
{
	Index = index;
	Connected = false;
	Enabled = true;
	M_LoadJoystickConfig(this);
}

//==========================================================================
//
// FXInputController - Destructor
//
//==========================================================================

FXInputController::~FXInputController()
{
	// Send button up events before destroying this.
	ProcessThumbstick(0, &Axes[AXIS_ThumbLX], 0, &Axes[AXIS_ThumbLY], KEY_PAD_LTHUMB_RIGHT);
	ProcessThumbstick(0, &Axes[AXIS_ThumbRX], 0, &Axes[AXIS_ThumbRY], KEY_PAD_RTHUMB_RIGHT);
	ProcessTrigger(0, &Axes[AXIS_LeftTrigger], KEY_PAD_LTRIGGER);
	ProcessTrigger(0, &Axes[AXIS_RightTrigger], KEY_PAD_RTRIGGER);
	Joy_GenerateButtonEvents(LastButtons, 0, 16, KEY_PAD_DPAD_UP);
	M_SaveJoystickConfig(this);
}

//==========================================================================
//
// FXInputController :: ProcessInput
//
//==========================================================================

void FXInputController::ProcessInput()
{
	DWORD res;
	XINPUT_STATE state;

	res = InputGetState(Index, &state);
	if (res == ERROR_DEVICE_NOT_CONNECTED)
	{
		if (Connected)
		{
			Detached();
		}
		return;
	}
	if (res != ERROR_SUCCESS)
	{
		return;
	}
	if (!Connected)
	{
		Attached();
	}
	if (state.dwPacketNumber == LastPacketNumber || !Enabled)
	{ // Nothing has changed since last time.
		return;
	}

	// There is a hole in the wButtons bitmask where two buttons could fit.
	// As per the XInput documentation, "bits that are set but not defined ... are reserved,
	// and their state is undefined," so we clear them to make sure they're not set.
	// Our keymapping uses these two slots for the triggers as buttons.
	state.Gamepad.wButtons &= 0xF3FF;

	// Convert axes to floating point and cancel out deadzones.
	// XInput's Y axes are reversed compared to DirectInput.
	ProcessThumbstick(state.Gamepad.sThumbLX, &Axes[AXIS_ThumbLX],
					 -state.Gamepad.sThumbLY, &Axes[AXIS_ThumbLY], KEY_PAD_LTHUMB_RIGHT);
	ProcessThumbstick(state.Gamepad.sThumbRX, &Axes[AXIS_ThumbRX],
					 -state.Gamepad.sThumbRY, &Axes[AXIS_ThumbRY], KEY_PAD_RTHUMB_RIGHT);
	ProcessTrigger(state.Gamepad.bLeftTrigger, &Axes[AXIS_LeftTrigger], KEY_PAD_LTRIGGER);
	ProcessTrigger(state.Gamepad.bRightTrigger, &Axes[AXIS_RightTrigger], KEY_PAD_RTRIGGER);

	// Generate events for buttons that have changed.
	Joy_GenerateButtonEvents(LastButtons, state.Gamepad.wButtons, 16, KEY_PAD_DPAD_UP);

	LastPacketNumber = state.dwPacketNumber;
	LastButtons = state.Gamepad.wButtons;
}

//==========================================================================
//
// FXInputController :: ProcessThumbstick							STATIC
//
// Converts both axes of a thumb stick to floating point, cancels out the
// deadzone, and generates button up/down events for them.
//
//==========================================================================

void FXInputController::ProcessThumbstick(int value1, AxisInfo *axis1,
	int value2, AxisInfo *axis2, int base)
{
	uint8_t buttonstate;
	double axisval1, axisval2;

	axis1->Keys[0] = base;
	axis1->Keys[1] = base + 1;
	axis2->Keys[0] = base + 2;
	axis2->Keys[1] = base + 3;

	axisval1 = (value1 - SHRT_MIN) * 2.0 / 65536 - 1.0;
	axisval2 = (value2 - SHRT_MIN) * 2.0 / 65536 - 1.0;
	axisval1 = Joy_RemoveDeadZone(axisval1, axis1->DeadZone, NULL);
	axisval2 = Joy_RemoveDeadZone(axisval2, axis2->DeadZone, NULL);
	axis1->Value = float(axisval1);
	axis2->Value = float(axisval2);

	// We store all four buttons in the first axis and ignore the second.
	buttonstate = Joy_XYAxesToButtons(axisval1, axisval2);
	Joy_GenerateButtonEvents(axis1->ButtonValue, buttonstate, 4, base);
	axis1->ButtonValue = buttonstate;
}

//==========================================================================
//
// FXInputController :: ProcessTrigger								STATIC
//
// Much like ProcessThumbstick, except triggers only go in the positive
// direction and have less precision.
//
//==========================================================================

void FXInputController::ProcessTrigger(int value, AxisInfo *axis, int base)
{
	uint8_t buttonstate;
	double axisval;

	axis->Keys[0] = base;
	axis->Keys[1] = 0;

	axisval = Joy_RemoveDeadZone(value / 256.0, axis->DeadZone, &buttonstate);
	Joy_GenerateButtonEvents(axis->ButtonValue, buttonstate, 1, base);
	axis->ButtonValue = buttonstate;
	axis->Value = float(axisval);
}

//==========================================================================
//
// FXInputController :: Attached
//
// This controller was just attached. Set all buttons and axes to 0.
//
//==========================================================================

void FXInputController::Attached()
{
	int i;

	Connected = true;
	LastPacketNumber = ~0;
	LastButtons = 0;
	for (i = 0; i < NUM_AXES; ++i)
	{
		Axes[i].Value = 0;
		Axes[i].ButtonValue = 0;
	}
	UpdateJoystickMenu(this);
}

//==========================================================================
//
// FXInputController :: Detached
//
// This controller was just detached. Send button ups for buttons that
// were pressed the last time we got input from it.
//
//==========================================================================

void FXInputController::Detached()
{
	int i;

	Connected = false;
	for (i = 0; i < 4; i += 2)
	{
		ProcessThumbstick(0, &Axes[i], 0, &Axes[i+1], KEY_PAD_LTHUMB_RIGHT + i*2);
	}
	for (i = 0; i < 2; ++i)
	{
		ProcessTrigger(0, &Axes[4+i], KEY_PAD_LTRIGGER + i);
	}
	Joy_GenerateButtonEvents(LastButtons, 0, 16, KEY_PAD_DPAD_UP);
	LastButtons = 0;
	UpdateJoystickMenu(NULL);
}

//==========================================================================
//
// FXInputController :: AddAxes
//
// Add the values of each axis to the game axes.
//
//==========================================================================

void FXInputController::AddAxes(float axes[NUM_KEYS])
{
	for (int i = 0; i < NUM_AXES; ++i)
	{
		// Add to the game axis.
		float axis_value = float(Axes[i].Value * Multiplier * Axes[i].Multiplier);
		int axis_key = 0;

		if (axis_value > 0.0f)
		{
			axis_key = Axes[i].Keys[0];
		}
		else if (axis_value < 0.0f)
		{
			axis_key = Axes[i].Keys[1];
		}

		if (axis_key > 0 && axis_key < NUM_KEYS)
		{
			axes[axis_key] += fabs(axis_value);
		}
	}
}

//==========================================================================
//
// FXInputController :: SetDefaultConfig
//
//==========================================================================

void FXInputController::SetDefaultConfig()
{
	Multiplier = 1;
	for (int i = 0; i < NUM_AXES; ++i)
	{
		Axes[i].DeadZone = DefaultAxes[i].DeadZone;
		Axes[i].Multiplier = DefaultAxes[i].Multiplier;
	}
}

//==========================================================================
//
// FXInputController :: GetIdentifier
//
//==========================================================================

FString FXInputController::GetIdentifier()
{
	return FStringf("XI:%d", Index);
}

//==========================================================================
//
// FXInputController :: GetName
//
//==========================================================================

FString FXInputController::GetName()
{
	FString res;
	res.Format("XInput Controller #%d", Index + 1);
	return res;
}

//==========================================================================
//
// FXInputController :: GetSensitivity
//
//==========================================================================

float FXInputController::GetSensitivity()
{
	return Multiplier;
}

//==========================================================================
//
// FXInputController :: SetSensitivity
//
//==========================================================================

void FXInputController::SetSensitivity(float scale)
{
	Multiplier = scale;
}

//==========================================================================
//
// FXInputController :: IsSensitivityDefault
//
//==========================================================================

bool FXInputController::IsSensitivityDefault()
{
	return Multiplier == 1;
}

//==========================================================================
//
// FXInputController :: GetNumAxes
//
//==========================================================================

int FXInputController::GetNumAxes()
{
	return NUM_AXES;
}

//==========================================================================
//
// FXInputController :: GetAxisDeadZone
//
//==========================================================================

float FXInputController::GetAxisDeadZone(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].DeadZone;
	}
	return 0;
}

//==========================================================================
//
// FXInputController :: GetAxisName
//
//==========================================================================

const char *FXInputController::GetAxisName(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return AxisNames[axis];
	}
	return "Invalid";
}

//==========================================================================
//
// FXInputController :: GetAxisScale
//
//==========================================================================

float FXInputController::GetAxisScale(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].Multiplier;
	}
	return 0;
}

//==========================================================================
//
// FXInputController :: SetAxisDeadZone
//
//==========================================================================

void FXInputController::SetAxisDeadZone(int axis, float deadzone)
{
	if (unsigned(axis) < NUM_AXES)
	{
		Axes[axis].DeadZone = clamp(deadzone, 0.f, 1.f);
	}
}

//==========================================================================
//
// FXInputController :: SetAxisScale
//
//==========================================================================

void FXInputController::SetAxisScale(int axis, float scale)
{
	if (unsigned(axis) < NUM_AXES)
	{
		Axes[axis].Multiplier = scale;
	}
}

//===========================================================================
//
// FXInputController :: IsAxisDeadZoneDefault
//
//===========================================================================

bool FXInputController::IsAxisDeadZoneDefault(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].DeadZone == DefaultAxes[axis].DeadZone;
	}
	return true;
}

//===========================================================================
//
// FXInputController :: IsAxisScaleDefault
//
//===========================================================================

bool FXInputController::IsAxisScaleDefault(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].Multiplier == DefaultAxes[axis].Multiplier;
	}
	return true;
}

//===========================================================================
//
// FXInputController :: GetEnabled
//
//===========================================================================

bool FXInputController::GetEnabled()
{
	return Enabled;
}

//===========================================================================
//
// FXInputController :: SetEnabled
//
//===========================================================================

void FXInputController::SetEnabled(bool enabled)
{
	Enabled = enabled;
}

//==========================================================================
//
// FXInputManager - Constructor
//
//==========================================================================

FXInputManager::FXInputManager()
{
	XInputDLL = LoadLibrary(XINPUT_DLL);
	if (XInputDLL != NULL)
	{
		InputGetState = (XInputGetStateType)GetProcAddress(XInputDLL, "XInputGetState");
		InputSetState = (XInputSetStateType)GetProcAddress(XInputDLL, "XInputSetState");
		InputGetCapabilities = (XInputGetCapabilitiesType)GetProcAddress(XInputDLL, "XInputGetCapabilities");
		InputEnable = (XInputEnableType)GetProcAddress(XInputDLL, "XInputEnable");
		// Treat XInputEnable() function as optional
		// It is not available in xinput9_1_0.dll which is XINPUT_DLL in modern SDKs
		// See https://msdn.microsoft.com/en-us/library/windows/desktop/hh405051(v=vs.85).aspx
		if (InputGetState == NULL || InputSetState == NULL || InputGetCapabilities == NULL)
		{
			FreeLibrary(XInputDLL);
			XInputDLL = NULL;
		}
	}
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		Devices[i] = (XInputDLL != NULL) ? new FXInputController(i) : NULL;
	}
}

//==========================================================================
//
// FXInputManager - Destructor
//
//==========================================================================

FXInputManager::~FXInputManager()
{
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		if (Devices[i] != NULL)
		{
			delete Devices[i];
		}
	}
	if (XInputDLL != NULL)
	{
		FreeLibrary(XInputDLL);
	}
}

//==========================================================================
//
// FXInputManager :: GetDevice
//
//==========================================================================

bool FXInputManager::GetDevice()
{
	return (XInputDLL != NULL);
}

//==========================================================================
//
// FXInputManager :: ProcessInput
//
// Process input for every attached device.
//
//==========================================================================

void FXInputManager::ProcessInput()
{
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		if(AppActive || Devices[i]->GetEnabledInBackground())
		{
			Devices[i]->ProcessInput();
		}
	}
}

//===========================================================================
//
// FXInputManager :: AddAxes
//
// Adds the state of all attached device axes to the passed array.
//
//===========================================================================

void FXInputManager::AddAxes(float axes[NUM_KEYS])
{
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		if (Devices[i]->IsConnected())
		{
			Devices[i]->AddAxes(axes);
		}
	}
}

//===========================================================================
//
// FXInputManager :: GetJoysticks
//
// Adds the IJoystick interfaces for each device we created to the sticks
// array, if they are detected as connected.
//
//===========================================================================

void FXInputManager::GetDevices(TArray<IJoystickConfig *> &sticks)
{
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		if (Devices[i]->IsConnected())
		{
			sticks.Push(Devices[i]);
		}
	}
}

//===========================================================================
//
// FXInputManager :: WndProcHook
//
// Enable and disable XInput as our window is (de)activated.
//
//===========================================================================

bool FXInputManager::WndProcHook(HWND hWnd, uint32_t message, WPARAM wParam, LPARAM lParam, LRESULT *result)
{
	if (nullptr != InputEnable && message == WM_ACTIVATE)
	{
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			InputEnable(FALSE);
		}
		else
		{
			InputEnable(TRUE);
		}
	}
	return false;
}

//===========================================================================
//
// FXInputManager :: Rescan
//
//===========================================================================

IJoystickConfig *FXInputManager::Rescan()
{
	return NULL;
}

//===========================================================================
//
// I_StartupXInput
//
//===========================================================================

void I_StartupXInput()
{
	if (!joy_xinput || !use_joystick || Args->CheckParm("-nojoy"))
	{
		if (JoyDevices[INPUT_XInput] != NULL)
		{
			delete JoyDevices[INPUT_XInput];
			JoyDevices[INPUT_XInput] = NULL;
			UpdateJoystickMenu(NULL);
		}
	}
	else
	{
		if (JoyDevices[INPUT_XInput] == NULL)
		{
			FJoystickCollection *joys = new FXInputManager;
			if (joys->GetDevice())
			{
				JoyDevices[INPUT_XInput] = joys;
			}
			else
			{
				delete joys;
			}
		}
	}
}

