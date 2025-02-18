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
#define DIRECTINPUT_VERSION 0x800
#include <windows.h>
#include <dinput.h>
#ifndef __GNUC__
#include <wbemidl.h>
#endif
#include <oleauto.h>
#include <malloc.h>

#include "i_input.h"
#include "d_eventbase.h"

#include "gameconfigfile.h"
#include "cmdlib.h"
#include "v_text.h"
#include "m_argv.h"
#include "keydef.h"
#include "printf.h"

#include "i_mainwindow.h"

#define SAFE_RELEASE(x)		{ if (x != NULL) { x->Release(); x = NULL; } }

// WBEMIDL BITS -- because w32api doesn't have this, either -----------------

#ifdef __GNUC__
struct IWbemClassObject : public IUnknown
{
public:
	virtual HRESULT __stdcall GetQualifierSet() = 0;
	virtual HRESULT __stdcall Get(LPCWSTR wszName, long lFlags, VARIANT *pVal, long *pType, long *plFlavor) = 0;
	virtual HRESULT __stdcall Put() = 0;
	virtual HRESULT __stdcall Delete() = 0;
	virtual HRESULT __stdcall GetNames() = 0;
	virtual HRESULT __stdcall BeginEnumeration() = 0;
	virtual HRESULT __stdcall Next() = 0;
	virtual HRESULT __stdcall EndEnumeration() = 0;
	virtual HRESULT __stdcall GetPropertyQualifierSet() = 0;
	virtual HRESULT __stdcall Clone() = 0;
	virtual HRESULT __stdcall GetObjectText() = 0;
	virtual HRESULT __stdcall SpawnDerivedClass() = 0;
	virtual HRESULT __stdcall SpawnInstance() = 0;
	virtual HRESULT __stdcall CompareTo() = 0;
	virtual HRESULT __stdcall GetPropertyOrigin() = 0;
	virtual HRESULT __stdcall InheritsFrom() = 0;
	virtual HRESULT __stdcall GetMethod() = 0;
	virtual HRESULT __stdcall PutMethod() = 0;
	virtual HRESULT __stdcall DeleteMethod() = 0;
	virtual HRESULT __stdcall BeginMethodEnumeration() = 0;
	virtual HRESULT __stdcall NextMethod() = 0;
	virtual HRESULT __stdcall EndMethodEnumeration() = 0;
	virtual HRESULT __stdcall GetMethodQualifierSet() = 0;
	virtual HRESULT __stdcall GetMethodOrigin() = 0;
};

struct IEnumWbemClassObject : public IUnknown
{
public:
	virtual HRESULT __stdcall Reset() = 0;
	virtual HRESULT __stdcall Next(long lTimeout, ULONG uCount,
		IWbemClassObject **apObjects, ULONG *puReturned) = 0;
	virtual HRESULT __stdcall NextAsync() = 0;
	virtual HRESULT __stdcall Clone() = 0;
	virtual HRESULT __stdcall Skip(long lTimeout, ULONG nCount) = 0;
};

struct IWbemServices : public IUnknown
{
public:
	virtual HRESULT __stdcall OpenNamespace() = 0;
	virtual HRESULT __stdcall CancelAsyncCall() = 0;
	virtual HRESULT __stdcall QueryObjectSink() = 0;
	virtual HRESULT __stdcall GetObject() = 0;
	virtual HRESULT __stdcall GetObjectAsync() = 0;
	virtual HRESULT __stdcall PutClass() = 0;
	virtual HRESULT __stdcall PutClassAsync() = 0;
	virtual HRESULT __stdcall DeleteClass() = 0;
	virtual HRESULT __stdcall DeleteClassAsync() = 0;
	virtual HRESULT __stdcall CreateClassEnum() = 0;
	virtual HRESULT __stdcall CreateClassEnumAsync() = 0;
	virtual HRESULT __stdcall PutInstance() = 0;
	virtual HRESULT __stdcall PutInstanceAsync() = 0;
	virtual HRESULT __stdcall DeleteInstance() = 0;
	virtual HRESULT __stdcall DeleteInstanceAsync() = 0;
	virtual HRESULT __stdcall CreateInstanceEnum(
		const BSTR strFilter, long lFlags, void *pCtx, IEnumWbemClassObject **ppEnum) = 0;
	virtual HRESULT __stdcall CreateInstanceEnumAsync() = 0;
	virtual HRESULT __stdcall ExecQuery() = 0;
	virtual HRESULT __stdcall ExecQueryAsync() = 0;
	virtual HRESULT __stdcall ExecNotificationQuery() = 0;
	virtual HRESULT __stdcall ExecNotificationQueryAsync() = 0;
	virtual HRESULT __stdcall ExecMethod() = 0;
	virtual HRESULT __stdcall ExecMethodAsync() = 0;
};

struct IWbemLocator : public IUnknown
{
public:
	virtual HRESULT __stdcall ConnectServer(
		const BSTR strNetworkResource,
		const BSTR strUser,
		const BSTR strPassword,
		const BSTR strLocale,
		long lSecurityFlags,
		const BSTR strAuthority,
		void *pCtx,
		IWbemServices **ppNamespace) = 0;
};
#endif

// MACROS ------------------------------------------------------------------

#define DEFAULT_DEADZONE			0.25f

// TYPES -------------------------------------------------------------------

class FDInputJoystick : public FInputDevice, IJoystickConfig
{
public:
	FDInputJoystick(const GUID *instance, FString &name);
	~FDInputJoystick();

	bool GetDevice();
	void ProcessInput();
	void AddAxes(float axes[NUM_KEYS]);

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

	bool AllowsEnabledInBackground() { return false; }
	bool GetEnabledInBackground() { return false; }
	void SetEnabledInBackground(bool enabled) {}

	void SetDefaultConfig();
	FString GetIdentifier();

protected:
	struct AxisInfo
	{
		FString Name;
		GUID Guid;
		DWORD Type;
		DWORD Ofs;
		LONG Min, Max;
		float Value;
		float DeadZone, DefaultDeadZone;
		float Multiplier, DefaultMultiplier;
		int Keys[2]; // 0: positive, 1: negative
		uint8_t ButtonValue;
	};
	struct ButtonInfo
	{
		FString Name;
		GUID Guid;
		DWORD Type;
		DWORD Ofs;
		uint8_t Value;
	};

	LPDIRECTINPUTDEVICE8 Device;
	GUID Instance;
	FString Name;
	bool Marked;
	float Multiplier;
	int Warmup;
	TArray<AxisInfo> Axes;
	TArray<ButtonInfo> Buttons;
	TArray<ButtonInfo> POVs;

	DIOBJECTDATAFORMAT *Objects;
	DIDATAFORMAT DataFormat;

	bool Enabled;

	static BOOL CALLBACK EnumObjectsCallback(LPCDIDEVICEOBJECTINSTANCE lpddoi, LPVOID pvRef);
	void OrderAxes();
	bool ReorderAxisPair(const GUID &x, const GUID &y, int pos);
	HRESULT SetDataFormat();

	friend class FDInputJoystickManager;
};

class FDInputJoystickManager : public FJoystickCollection
{
public:
	FDInputJoystickManager();
	~FDInputJoystickManager();

	bool GetDevice();
	void ProcessInput();
	void AddAxes(float axes[NUM_KEYS]);
	void GetDevices(TArray<IJoystickConfig *> &sticks);
	IJoystickConfig *Rescan();

protected:
	struct Enumerator
	{
		GUID Instance;
		FString Name;
	};
	struct EnumData
	{
		TArray<Enumerator> *All;
		bool GenericDevices;
	};
	TArray<FDInputJoystick *> Devices;

	FDInputJoystick *EnumDevices();

	static BOOL CALLBACK EnumCallback(LPCDIDEVICEINSTANCE lpddi, LPVOID pvRef);
	static int NameSort(const void *a, const void *b);
	static bool IsXInputDevice(const GUID *guid);
};

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern LPDIRECTINPUT8 g_pdi;

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CUSTOM_CVAR(Bool, joy_dinput, true, CVAR_GLOBALCONFIG|CVAR_ARCHIVE|CVAR_NOINITCALL)
{
	I_StartupDirectInputJoystick();
	event_t ev = { EV_DeviceChange };
	D_PostEvent(&ev);
}

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static const uint8_t POVButtons[9] = { 0x01, 0x03, 0x02, 0x06, 0x04, 0x0C, 0x08, 0x09, 0x00 };

// CODE --------------------------------------------------------------------

//===========================================================================
//
// FDInputJoystick - Constructor
//
//===========================================================================

FDInputJoystick::FDInputJoystick(const GUID *instance, FString &name)
{
	Device = NULL;
	DataFormat.rgodf = NULL;
	Instance = *instance;
	Name = name;
	Marked = false;
	Enabled = true;
}

//===========================================================================
//
// FDInputJoystick - Destructor
//
//===========================================================================

FDInputJoystick::~FDInputJoystick()
{
	unsigned int i;

	if (Device != NULL)
	{
		M_SaveJoystickConfig(this);
		Device->Release();
		Device = NULL;
	}
	if (DataFormat.rgodf != NULL)
	{
		delete[] DataFormat.rgodf;
	}
	// Send key ups before destroying this.
	if (Axes.Size() == 1)
	{
		Joy_GenerateButtonEvents(Axes[0].ButtonValue, 0, 2, KEY_JOYAXIS1PLUS);
	}
	else if (Axes.Size() > 1)
	{
		Joy_GenerateButtonEvents(Axes[1].ButtonValue, 0, 4, KEY_JOYAXIS1PLUS);
		for (i = 2; i < Axes.Size(); ++i)
		{
			Joy_GenerateButtonEvents(Axes[i].ButtonValue, 0, 2, KEY_JOYAXIS1PLUS + i*2);
		}
	}
	for (i = 0; i < Buttons.Size(); ++i)
	{
		if (Buttons[i].Value)
		{
			event_t ev = { EV_KeyUp };
			ev.data1 = KEY_FIRSTJOYBUTTON + i;
		}
	}
	for (i = 0; i < POVs.Size(); ++i)
	{
		Joy_GenerateButtonEvents(POVs[i].Value, 0, 4, KEY_JOYPOV1_UP + i*4);
	}
}

//===========================================================================
//
// FDInputJoystick :: GetDevice
//
//===========================================================================

bool FDInputJoystick::GetDevice()
{
	HRESULT hr;

	if (g_pdi == NULL)
	{
		return false;
	}
	hr = g_pdi->CreateDevice(Instance, &Device, NULL);
	if (FAILED(hr) || Device == NULL)
	{
		return false;
	}
	hr = Device->EnumObjects(EnumObjectsCallback, this, DIDFT_ABSAXIS | DIDFT_BUTTON | DIDFT_POV);
	OrderAxes();
	hr = SetDataFormat();
	if (FAILED(hr))
	{
		Printf(TEXTCOLOR_ORANGE "Setting data format for %s failed.\n", Name.GetChars());
		return false;
	}
	hr = Device->SetCooperativeLevel(mainwindow.GetHandle(), DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
	if (FAILED(hr))
	{
		Printf(TEXTCOLOR_ORANGE "Setting cooperative level for %s failed.\n", Name.GetChars());
		return false;
	}
	Device->Acquire();
	M_LoadJoystickConfig(this);
	Warmup = 4;
	return true;
}

//===========================================================================
//
// FDInputJoystick :: ProcessInput
//
// Send button events and record axes for later.
//
//===========================================================================

void FDInputJoystick::ProcessInput()
{
	HRESULT hr;
	uint8_t *state;
	unsigned i;
	event_t ev;

	if (Device == NULL)
	{
		return;
	}

	hr = Device->Poll();
	if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
	{
		hr = Device->Acquire();
	}
	if (FAILED(hr) || !Enabled)
	{
		return;
	}

	TArray<uint8_t> statearr(DataFormat.dwDataSize, true);
	state = statearr.data();
	hr = Device->GetDeviceState(DataFormat.dwDataSize, state);
	if (FAILED(hr))
		return;

	// Some controllers send false values when they are first
	// initialized, so give some time for them to get past that
	// before we pay any attention to their state.
	if (Warmup > 0)
	{
		Warmup--;
		return;
	}

	// Convert axis values to floating point and save them for a later call
	// to AddAxes(). Axes that are past their dead zone will also be translated
	// into button presses.
	for (i = 0; i < Axes.Size(); ++i)
	{
		AxisInfo *info = &Axes[i];
		LONG value = *(LONG *)(state + info->Ofs);
		double axisval;
		uint8_t buttonstate = 0;

		info->Keys[0] = KEY_JOYAXIS1PLUS + (i * 2);
		info->Keys[1] = KEY_JOYAXIS1PLUS + (i * 2) + 1;

		// Scale to [-1.0, 1.0]
		axisval = (value - info->Min) * 2.0 / (info->Max - info->Min) - 1.0;
		// Cancel out dead zone
		axisval = Joy_RemoveDeadZone(axisval, info->DeadZone, &buttonstate);
		info->Value = float(axisval);
		if (i < NUM_JOYAXISBUTTONS && (i > 2 || Axes.Size() == 1))
		{
			Joy_GenerateButtonEvents(info->ButtonValue, buttonstate, 2, KEY_JOYAXIS1PLUS + i*2);
		}
		else if (i == 1)
		{
			// Since we sorted the axes, we know that the first two are definitely X and Y.
			// They are probably a single stick, so use angular position to determine buttons.
			buttonstate = Joy_XYAxesToButtons(Axes[0].Value, axisval);
			Joy_GenerateButtonEvents(info->ButtonValue, buttonstate, 4, KEY_JOYAXIS1PLUS);
		}
		info->ButtonValue = buttonstate;
	}

	// Compare button states and generate events for buttons that have changed.
	memset(&ev, 0, sizeof(ev));
	for (i = 0; i < Buttons.Size(); ++i)
	{
		ButtonInfo *info = &Buttons[i];
		uint8_t newstate = *(uint8_t *)(state + info->Ofs) & 0x80;
		if (newstate != info->Value)
		{
			info->Value = newstate;
			ev.data1 = KEY_FIRSTJOYBUTTON + i;
			ev.type = (newstate != 0) ? EV_KeyDown : EV_KeyUp;
			D_PostEvent(&ev);
		}
	}

	// POV hats are treated as a set of four buttons, because if it's a
	// D-pad, that's exactly what it is.
	for (i = 0; i < POVs.Size(); ++i)
	{
		ButtonInfo *info = &POVs[i];
		DWORD povangle = *(DWORD *)(state + info->Ofs);
		int pov;

		// Smoosh POV angles down into octants. 8 is centered.
		pov = (LOWORD(povangle) == 0xFFFF) ? 8 : ((povangle + 2250) % 36000) / 4500;

		// Convert octant to one or two buttons needed to represent it.
		pov = POVButtons[pov];

		// Send events for POV "buttons" that have changed.
		Joy_GenerateButtonEvents(info->Value, pov, 4, KEY_JOYPOV1_UP + i*4);
		info->Value = pov;
	}
}

//===========================================================================
//
// FDInputJoystick :: AddAxes
//
// Add the values of each axis to the game axes.
//
//===========================================================================

void FDInputJoystick::AddAxes(float axes[NUM_KEYS])
{
	for (unsigned i = 0; i < Axes.Size(); ++i)
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

//===========================================================================
//
// FDInputJoystick :: EnumObjectsCallback							STATIC
//
// Finds all axes, buttons, and hats on the controller.
//
//===========================================================================

BOOL CALLBACK FDInputJoystick::EnumObjectsCallback(LPCDIDEVICEOBJECTINSTANCE lpddoi, LPVOID pvRef)
{
	FDInputJoystick *joy = (FDInputJoystick *)pvRef;

	if (lpddoi->guidType == GUID_Button)
	{
		ButtonInfo info;
		info.Name = lpddoi->tszName;
		info.Guid = lpddoi->guidType;
		info.Type = lpddoi->dwType;
		info.Ofs = 0;
		info.Value = 0;
		// We don't have the key labels necessary to support more than 128
		// joystick buttons. This is what DIJOYSTATE2 offers, so we
		// probably don't need to worry about any devices with more than
		// that.
		if (joy->Buttons.Size() < 128)
		{
			joy->Buttons.Push(info);
		}
	}
	else if (lpddoi->guidType == GUID_POV)
	{
		ButtonInfo info;
		info.Name = lpddoi->tszName;
		info.Guid = lpddoi->guidType;
		info.Type = lpddoi->dwType;
		info.Ofs = 0;
		info.Value = 0;
		// We don't have the key labels necessary to support more than 4
		// hats. I don't know any devices with more than 1, and the
		// standard DirectInput DIJOYSTATE does not support more than 4
		// hats either, so this is probably a non-issue.
		if (joy->POVs.Size() < 4)
		{
			joy->POVs.Push(info);
		}
	}
	else
	if (lpddoi->guidType == GUID_XAxis ||
		lpddoi->guidType == GUID_YAxis ||
		lpddoi->guidType == GUID_ZAxis ||
		lpddoi->guidType == GUID_RxAxis ||
		lpddoi->guidType == GUID_RyAxis ||
		lpddoi->guidType == GUID_RzAxis ||
		lpddoi->guidType == GUID_Slider)
	{
		DIPROPRANGE diprg;
		AxisInfo info;

		diprg.diph.dwSize = sizeof(DIPROPRANGE);
		diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
		diprg.diph.dwObj = lpddoi->dwType;
		diprg.diph.dwHow = DIPH_BYID;
		diprg.lMin = 0;
		diprg.lMax = 0;
		joy->Device->GetProperty(DIPROP_RANGE, &diprg.diph);

		info.Name = lpddoi->tszName;
		info.Guid = lpddoi->guidType;
		info.Type = lpddoi->dwType;
		info.Ofs = 0;
		info.Min = diprg.lMin;
		info.Max = diprg.lMax;
		info.Value = 0;
		info.ButtonValue = 0;
		joy->Axes.Push(info);
	}
	return DIENUM_CONTINUE;
}

//===========================================================================
//
// FDInputJoystick :: OrderAxes
//
// Try to put the axes in some sort of sane order. X and Y axes are pretty
// much standard. Unfortunately, the rest are entirely up to the
// manufacturers to decide how they want to assign them.
//
//===========================================================================

void FDInputJoystick::OrderAxes()
{
	// Make X,Y the first pair.
	if (!ReorderAxisPair(GUID_XAxis, GUID_YAxis, 0))
	{
		return;
	}
	// The second pair is either Rx,Ry or Rz,Z, depending on what we have
	if (!ReorderAxisPair(GUID_RxAxis, GUID_RyAxis, 2))
	{
		ReorderAxisPair(GUID_RzAxis, GUID_ZAxis, 2);
	}
}

bool FDInputJoystick::ReorderAxisPair(const GUID &xid, const GUID &yid, int pos)
{
	unsigned i;
	int x, y;

	// Find each axis.
	x = -1;
	y = -1;
	for (i = 0; i < Axes.Size(); ++i)
	{
		if (x < 0 && Axes[i].Guid == xid)
		{
			x = i;
		}
		else if (y < 0 && Axes[i].Guid == yid)
		{
			y = i;
		}
	}
	// If we don't have both X and Y axes, do nothing.
	if (x < 0 || y < 0)
	{
		return false;
	}
	if (x == pos + 1 && y == pos)
	{ // Xbox 360 Controllers return them in this order.
		std::swap(Axes[pos], Axes[pos + 1]);
	}
	else if (x != pos || y != pos + 1)
	{
		AxisInfo xinfo = Axes[x], yinfo = Axes[y];
		Axes.Delete(x);
		if (x < y)
		{
			y--;
		}
		Axes.Delete(y);
		Axes.Insert(pos, xinfo);
		Axes.Insert(pos + 1, yinfo);
	}
	return true;
}

//===========================================================================
//
// FDInputJoystick :: SetDataFormat
//
// Using the objects we previously enumerated, construct a data format
// structure for DirectInput to use for this device. We could use the
// predefined c_dfDIJoystick2, except that we would have no way of knowing
// which axis mapped to a certain point in the structure if there is more
// than one of a particular type of axis. The dwOfs member of
// DIDEVICEOBJECTINSTANCE is practically useless, because it describes the
// offset of the object in the device's native data format, and not
// the offset in something we can actually use.
//
//===========================================================================

HRESULT FDInputJoystick::SetDataFormat()
{
	DIOBJECTDATAFORMAT *objects;
	DWORD numobjs;
	DWORD nextofs;
	unsigned i;

	objects = new DIOBJECTDATAFORMAT[Axes.Size() + POVs.Size() + Buttons.Size()];
	numobjs = nextofs = 0;

	// Add all axes
	for (i = 0; i < Axes.Size(); ++i)
	{
		objects[i].pguid = &Axes[i].Guid;
		objects[i].dwOfs = Axes[i].Ofs = nextofs;
		objects[i].dwType = Axes[i].Type;
		objects[i].dwFlags = 0;
		nextofs += sizeof(LONG);
	}
	numobjs = i;
	// Add all POVs
	for (i = 0; i < POVs.Size(); ++i)
	{
		objects[numobjs + i].pguid = &POVs[i].Guid;
		objects[numobjs + i].dwOfs = POVs[i].Ofs = nextofs;
		objects[numobjs + i].dwType = POVs[i].Type;
		objects[numobjs + i].dwFlags = 0;
		nextofs += sizeof(DWORD);
	}
	numobjs += i;
	// Add all buttons
	for (i = 0; i < Buttons.Size(); ++i)
	{
		objects[numobjs + i].pguid = &Buttons[i].Guid;
		objects[numobjs + i].dwOfs = Buttons[i].Ofs = nextofs;
		objects[numobjs + i].dwType = Buttons[i].Type;
		objects[numobjs + i].dwFlags = 0;
		nextofs += sizeof(uint8_t);
	}
	numobjs += i;

	// Set format
	DataFormat.dwSize = sizeof(DIDATAFORMAT);
	DataFormat.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
	DataFormat.dwFlags = DIDF_ABSAXIS;
	DataFormat.dwDataSize = (nextofs + 3) & ~3;		// Round to the nearest multiple of 4.
	DataFormat.dwNumObjs = numobjs;
	DataFormat.rgodf = objects;
	return Device->SetDataFormat(&DataFormat);
}

//===========================================================================
//
// FDInputJoystick :: GetIdentifier
//
//===========================================================================

FString FDInputJoystick::GetIdentifier()
{
	char id[48];

	id[0] = 'D'; id[1] = 'I'; id[2] = ':';
	FormatGUID(id + 3, countof(id) - 3, Instance);
	return id;
}

//===========================================================================
//
// FDInputJoystick :: SetDefaultConfig
//
// Try for a reasonable default axis configuration.
//
//===========================================================================

void FDInputJoystick::SetDefaultConfig()
{
	unsigned i;

	Multiplier = 1;
	for (i = 0; i < Axes.Size(); ++i)
	{
		Axes[i].DeadZone = DEFAULT_DEADZONE;
		Axes[i].Multiplier = 1;
	}
	// Triggers on a 360 controller have a much smaller deadzone.
	if (Axes.Size() == 5 && Axes[4].Guid == GUID_ZAxis)
	{
		Axes[4].DeadZone = 30 / 256.f;
	}

	// Preserve defaults for config saving.
	for (i = 0; i < Axes.Size(); ++i)
	{
		Axes[i].DefaultDeadZone = Axes[i].DeadZone;
		Axes[i].DefaultMultiplier = Axes[i].Multiplier;
	}
}

//===========================================================================
//
// FDInputJoystick :: GetName
//
//===========================================================================

FString FDInputJoystick::GetName()
{
	return Name;
}

//===========================================================================
//
// FDInputJoystick :: GetSensitivity
//
//===========================================================================

float FDInputJoystick::GetSensitivity()
{
	return Multiplier;
}

//===========================================================================
//
// FDInputJoystick :: SetSensitivity
//
//===========================================================================

void FDInputJoystick::SetSensitivity(float scale)
{
	Multiplier = scale;
}

//===========================================================================
//
// FDInputJoystick :: IsSensitivityDefault
//
//===========================================================================

bool FDInputJoystick::IsSensitivityDefault()
{
	return Multiplier == 1;
}

//===========================================================================
//
// FDInputJoystick :: GetNumAxes
//
//===========================================================================

int FDInputJoystick::GetNumAxes()
{
	return (int)Axes.Size();
}

//===========================================================================
//
// FDInputJoystick :: GetAxisDeadZone
//
//===========================================================================

float FDInputJoystick::GetAxisDeadZone(int axis)
{
	if (unsigned(axis) >= Axes.Size())
	{
		return 0;
	}
	return Axes[axis].DeadZone;
}

//===========================================================================
//
// FDInputJoystick :: GetAxisName
//
//===========================================================================

const char *FDInputJoystick::GetAxisName(int axis)
{
	if (unsigned(axis) < Axes.Size())
	{
		return Axes[axis].Name.GetChars();
	}
	return "Invalid";
}

//===========================================================================
//
// FDInputJoystick :: GetAxisScale
//
//===========================================================================

float FDInputJoystick::GetAxisScale(int axis)
{
	if (unsigned(axis) >= Axes.Size())
	{
		return 0;
	}
	return Axes[axis].Multiplier;
}

//===========================================================================
//
// FDInputJoystick :: SetAxisDeadZone
//
//===========================================================================

void FDInputJoystick::SetAxisDeadZone(int axis, float deadzone)
{
	if (unsigned(axis) < Axes.Size())
	{
		Axes[axis].DeadZone = clamp(deadzone, 0.f, 1.f);
	}
}

//===========================================================================
//
// FDInputJoystick :: SetAxisScale
//
//===========================================================================

void FDInputJoystick::SetAxisScale(int axis, float scale)
{
	if (unsigned(axis) < Axes.Size())
	{
		Axes[axis].Multiplier = scale;
	}
}

//===========================================================================
//
// FDInputJoystick :: IsAxisDeadZoneDefault
//
//===========================================================================

bool FDInputJoystick::IsAxisDeadZoneDefault(int axis)
{
	if (unsigned(axis) < Axes.Size())
	{
		return Axes[axis].DeadZone == Axes[axis].DefaultDeadZone;
	}
	return true;
}

//===========================================================================
//
// FDInputJoystick :: IsAxisScaleDefault
//
//===========================================================================

bool FDInputJoystick::IsAxisScaleDefault(int axis)
{
	if (unsigned(axis) < Axes.Size())
	{
		return Axes[axis].Multiplier == Axes[axis].DefaultMultiplier;
	}
	return true;
}

//===========================================================================
//
// FDInputJoystick :: GetEnabled
//
//===========================================================================

bool FDInputJoystick::GetEnabled()
{
	return Enabled;
}

//===========================================================================
//
// FDInputJoystick :: SetEnabled
//
//===========================================================================

void FDInputJoystick::SetEnabled(bool enabled)
{
	Enabled = enabled;
}

//===========================================================================
//
// FDInputJoystickManager - Constructor
//
//===========================================================================

FDInputJoystickManager::FDInputJoystickManager()
{
}

//===========================================================================
//
// FDInputJoystickManager - Destructor
//
//===========================================================================

FDInputJoystickManager::~FDInputJoystickManager()
{
	for (unsigned i = 0; i < Devices.Size(); ++i)
	{
		delete Devices[i];
	}
}

//===========================================================================
//
// FDInputJoystickManager :: GetDevice
//
//===========================================================================

bool FDInputJoystickManager::GetDevice()
{
	if (g_pdi == NULL)
	{
		return false;
	}
	EnumDevices();
	return true;
}

//===========================================================================
//
// FDInputJoystickManager :: ProcessInput
//
// Process input for every attached device.
//
//===========================================================================

void FDInputJoystickManager::ProcessInput()
{
	for (unsigned i = 0; i < Devices.Size(); ++i)
	{
		if (Devices[i] != NULL)
		{
			Devices[i]->ProcessInput();
		}
	}
}

//===========================================================================
//
// FDInputJoystickManager :: AddAxes
//
// Adds the state of all attached device axes to the passed array.
//
//===========================================================================

void FDInputJoystickManager :: AddAxes(float axes[NUM_KEYS])
{
	for (unsigned i = 0; i < Devices.Size(); ++i)
	{
		Devices[i]->AddAxes(axes);
	}
}

//===========================================================================
//
// FDInputJoystickManager :: GetDevices
//
// Adds the IJoystick interfaces for each device we created to the sticks
// array.
//
//===========================================================================

void FDInputJoystickManager::GetDevices(TArray<IJoystickConfig *> &sticks)
{
	for (unsigned i = 0; i < Devices.Size(); ++i)
	{
		sticks.Push(Devices[i]);
	}
}

//===========================================================================
//
// FDInputJoystickManager :: EnumCallback							STATIC
//
// Adds each DirectInput game controller to a TArray.
//
//===========================================================================

BOOL CALLBACK FDInputJoystickManager::EnumCallback(LPCDIDEVICEINSTANCE lpddi, LPVOID pvRef)
{
	EnumData *data = (EnumData *)pvRef;

	// Special check for the Microsoft SideWinder Strategic Commander,
	// which for some odd reason reports itself as a generic device and
	// not a game controller.
	if (data->GenericDevices && lpddi->guidProduct.Data1 != MAKELONG(0x45e, 0x0033))
	{
		return DIENUM_CONTINUE;
	}

	// Do not add PS2 adapters if Raw PS2 Input was initialized.
	// Do not add XInput devices if XInput was initialized.
	if ((JoyDevices[INPUT_RawPS2] == NULL || !I_IsPS2Adapter(lpddi->guidProduct.Data1)) &&
		(JoyDevices[INPUT_XInput] == NULL || !IsXInputDevice(&lpddi->guidProduct)))
	{
		Enumerator thisone;

		thisone.Instance = lpddi->guidInstance;
		thisone.Name = lpddi->tszInstanceName;
		data->All->Push(thisone);
	}
	return DIENUM_CONTINUE;
}

//===========================================================================
//
// FDInputJoystickManager :: IsXInputDeviceFast						STATIC
//
// The theory of operation is the same as for IsXInputDeviceSlow, except that
// we use the Raw Input device list to find the device ID instead of WMI.
// This generally takes ~0.02 ms on my machine, though it occasionally spikes
// at over 1 ms. Either way, it is a huge order of magnitude faster than WMI.
// (around 10,000 times faster, in fact!)
//
//===========================================================================

bool FDInputJoystickManager::IsXInputDevice(const GUID *guid)
{
	UINT nDevices, numDevices;
	RAWINPUTDEVICELIST *devices;
	UINT i;
	bool isxinput = false;

	if (GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) != 0)
	{
		return false;
	}
	if ((devices = (RAWINPUTDEVICELIST *)malloc(sizeof(RAWINPUTDEVICELIST) * nDevices)) == NULL)
	{
		return false;
	}
	if ((numDevices = GetRawInputDeviceList(devices, &nDevices, sizeof(RAWINPUTDEVICELIST))) == (UINT)-1)
	{
		free(devices);
		return false;
	}
	for (i = 0; i < numDevices; ++i)
	{
		// I am making the assumption here that all possible XInput devices
		// will report themselves as generic HID devices and not as keyboards
		// or mice.
		if (devices[i].dwType == RIM_TYPEHID)
		{
			RID_DEVICE_INFO rdi;
			UINT cbSize;

			cbSize = rdi.cbSize = sizeof(rdi);
			if ((INT)GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICEINFO, &rdi, &cbSize) >= 0)
			{
				if(MAKELONG(rdi.hid.dwVendorId, rdi.hid.dwProductId) == (LONG)guid->Data1)
				{
					char name[256];
					UINT namelen = countof(name);
					UINT reslen;

					reslen = GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICENAME, name, &namelen);
					if (reslen != (UINT)-1)
					{
						isxinput = (strstr(name, "IG_") != NULL);
						break;
					}
				}
			}
		}
	}
	free(devices);
	return isxinput;
}

//===========================================================================
//
// FDInputJoystickManager :: NameSort								STATIC
//
//===========================================================================

int FDInputJoystickManager::NameSort(const void *a, const void *b)
{
	const Enumerator *ea = (const Enumerator *)a;
	const Enumerator *eb = (const Enumerator *)b;
	int lex = ea->Name.Compare(eb->Name);
	if (lex == 0)
	{
		return memcmp(&ea->Instance, &eb->Instance, sizeof(GUID));
	}
	return lex;
}

//===========================================================================
//
// FDInputJoystickManager :: EnumDevices
//
// Find out what DirectInput game controllers are on the system and create
// FDInputJoystick objects for them. May return a pointer to the first new
// device found.
//
//===========================================================================

FDInputJoystick *FDInputJoystickManager::EnumDevices()
{
	FDInputJoystick *newone = NULL;
	TArray<Enumerator> controllers;
	EnumData data;
	unsigned i, j, k;

	// Find all game controllers
	data.All = &controllers;
	data.GenericDevices = false;
	g_pdi->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumCallback, &data, DIEDFL_ALLDEVICES);
	// Again, just for the Strategic Commander
	data.GenericDevices = true;
	g_pdi->EnumDevices(DI8DEVCLASS_DEVICE, EnumCallback, &data, DIEDFL_ALLDEVICES);

	// Sort by name so that devices with duplicate names can have numbers appended.
	qsort(&controllers[0], controllers.Size(), sizeof(Enumerator), NameSort);
	for (i = 1; i < controllers.Size(); ++i)
	{
		// Does this device have the same name as the previous one? If so, how
		// many more have the same name?
		for (j = i; j < controllers.Size(); ++j)
		{
			if (controllers[j-1].Name.Compare(controllers[j].Name) != 0)
				break;
		}
		// j is one past the last duplicate name.
		if (j > i)
		{
			// Append numbers.
			for (k = i - 1; k < j; ++k)
			{
				controllers[k].Name.AppendFormat(" #%d", k - i + 2);
			}
		}
	}

	// Compare the new list of devices with the one we previously instantiated.
	// Every device we currently hold is marked 0. Then scan through the new
	// list and try to find it there, if it's found, it is marked 1. At the end
	// of this, devices marked 1 existed before and are left alone. Devices
	// marked 0 are no longer present and should be destroyed. If a device is
	// present in the new list that we have not yet instantiated, we 
	// instantiate it now.
	for (j = 0; j < Devices.Size(); ++j)
	{
		Devices[j]->Marked = false;
	}
	for (i = 0; i < controllers.Size(); ++i)
	{
		GUID *lookfor = &controllers[i].Instance;
		for (j = 0; j < Devices.Size(); ++j)
		{
			if (Devices[j]->Instance == *lookfor)
			{
				Devices[j]->Marked = true;
				break;
			}
		}
		if (j == Devices.Size())
		{ // Not found. Add it.
			FDInputJoystick *device = new FDInputJoystick(lookfor, controllers[i].Name);
			if (!device->GetDevice())
			{
				delete device;
			}
			else
			{
				device->Marked = true;
				Devices.Push(device);
				if (newone == NULL)
				{
					newone = device;
				}
			}
		}
	}
	// Remove detached devices and avoid holes in the list.
	for (i = j = 0; j < Devices.Size(); ++j)
	{
		if (!Devices[j]->Marked)
		{
			delete Devices[j];
		}
		else
		{
			if (i != j)
			{
				Devices[i] = Devices[j];
			}
			++i;
		}
	}
	Devices.Resize(i);
	return newone;
}

//===========================================================================
//
// FDInputJoystickManager :: Rescan
//
// Used by the joy_ps2raw and joy_xinput callbacks to rescan the
// DirectInput devices, since their presence or absence can affect the
// results of the scan.
//
//===========================================================================

IJoystickConfig *FDInputJoystickManager::Rescan()
{
	return EnumDevices();
}

//===========================================================================
//
// I_StartupDirectInputJoystick
//
//===========================================================================

void I_StartupDirectInputJoystick()
{
	if (!joy_dinput || !use_joystick || Args->CheckParm("-nojoy"))
	{
		if (JoyDevices[INPUT_DIJoy] != NULL)
		{
			delete JoyDevices[INPUT_DIJoy];
			JoyDevices[INPUT_DIJoy] = NULL;
			UpdateJoystickMenu(NULL);
		}
	}
	else
	{
		if (JoyDevices[INPUT_DIJoy] == NULL)
		{
			FJoystickCollection *joys = new FDInputJoystickManager;
			if (joys->GetDevice())
			{
				JoyDevices[INPUT_DIJoy] = joys;
			}
		}
	}
}
