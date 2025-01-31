/*
** i_joystick.cpp
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
#include <SDL.h>

#include "basics.h"
#include "cmdlib.h"

#include "m_joy.h"
#include "keydef.h"

#define DEFAULT_DEADZONE 0.25f;

// Very small deadzone so that floating point magic doesn't happen
#define MIN_DEADZONE 0.000001f

class SDLInputJoystick: public IJoystickConfig
{
public:
	SDLInputJoystick(int DeviceIndex) : DeviceIndex(DeviceIndex), Multiplier(1.0f) , Enabled(true)
	{
		Device = SDL_JoystickOpen(DeviceIndex);
		if(Device != NULL)
		{
			NumAxes = SDL_JoystickNumAxes(Device);
			NumHats = SDL_JoystickNumHats(Device);

			SetDefaultConfig();
		}
	}
	~SDLInputJoystick()
	{
		if(Device != NULL)
			M_SaveJoystickConfig(this);
		SDL_JoystickClose(Device);
	}

	bool IsValid() const
	{
		return Device != NULL;
	}

	FString GetName()
	{
		return SDL_JoystickName(Device);
	}
	float GetSensitivity()
	{
		return Multiplier;
	}
	void SetSensitivity(float scale)
	{
		Multiplier = scale;
	}

	int GetNumAxes()
	{
		return NumAxes + NumHats*2;
	}
	float GetAxisDeadZone(int axis)
	{
		return Axes[axis].DeadZone;
	}
	const char *GetAxisName(int axis)
	{
		return Axes[axis].Name.GetChars();
	}
	float GetAxisScale(int axis)
	{
		return Axes[axis].Multiplier;
	}

	void SetAxisDeadZone(int axis, float zone)
	{
		Axes[axis].DeadZone = clamp(zone, MIN_DEADZONE, 1.f);
	}
	void SetAxisScale(int axis, float scale)
	{
		Axes[axis].Multiplier = scale;
	}

	// Used by the saver to not save properties that are at their defaults.
	bool IsSensitivityDefault()
	{
		return Multiplier == 1.0f;
	}
	bool IsAxisDeadZoneDefault(int axis)
	{
		return Axes[axis].DeadZone <= MIN_DEADZONE;
	}
	bool IsAxisScaleDefault(int axis)
	{
		return Axes[axis].Multiplier == 1.0f;
	}

	void SetDefaultConfig()
	{
		for(int i = 0;i < GetNumAxes();i++)
		{
			AxisInfo info;
			if(i < NumAxes)
				info.Name.Format("Axis %d", i+1);
			else
				info.Name.Format("Hat %d (%c)", (i-NumAxes)/2 + 1, (i-NumAxes)%2 == 0 ? 'x' : 'y');
			info.DeadZone = DEFAULT_DEADZONE;
			info.Multiplier = 1.0f;
			info.Value = 0.0;
			info.ButtonValue = 0;
			info.Keys[0] = info.Keys[1] = 0;
			Axes.Push(info);
		}
	}

	bool GetEnabled()
	{
		return Enabled;
	}
	
	void SetEnabled(bool enabled)
	{
		Enabled = enabled;
	}

	bool AllowsEnabledInBackground() { return false; }
	bool GetEnabledInBackground() { return false; }
	void SetEnabledInBackground(bool enabled) {}

	FString GetIdentifier()
	{
		char id[16];
		snprintf(id, countof(id), "JS:%d", DeviceIndex);
		return id;
	}

	void AddAxes(float axes[NUM_KEYS])
	{
		// Add to game axes.
		for (int i = 0; i < GetNumAxes(); ++i)
		{
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

	void ProcessInput()
	{
		uint8_t buttonstate;

		for (int i = 0; i < NumAxes; ++i)
		{
			buttonstate = 0;

			if (i < NUM_JOYAXISBUTTONS)
			{
				Axes[i].Keys[0] = KEY_JOYAXIS1PLUS + (i * 2);
				Axes[i].Keys[1] = KEY_JOYAXIS1PLUS + (i * 2) + 1;
			}
			else
			{
				Axes[i].Keys[0] = Axes[i].Keys[1] = 0;
			}

			Axes[i].Value = SDL_JoystickGetAxis(Device, i)/32767.0;
			Axes[i].Value = Joy_RemoveDeadZone(Axes[i].Value, Axes[i].DeadZone, &buttonstate);

			// Map button to axis
			// X and Y are handled differently so if we have 2 or more axes then we'll use that code instead.
			if (NumAxes == 1 || (i >= 2 && i < NUM_JOYAXISBUTTONS))
			{
				Joy_GenerateButtonEvents(Axes[i].ButtonValue, buttonstate, 2, KEY_JOYAXIS1PLUS + i*2);
				Axes[i].ButtonValue = buttonstate;
			}
		}

		if(NumAxes > 1)
		{
			buttonstate = Joy_XYAxesToButtons(Axes[0].Value, Axes[1].Value);
			Joy_GenerateButtonEvents(Axes[0].ButtonValue, buttonstate, 4, KEY_JOYAXIS1PLUS);
			Axes[0].ButtonValue = buttonstate;
		}

		// Map POV hats to buttons and axes.  Why axes?  Well apparently I have
		// a gamepad where the left control stick is a POV hat (instead of the
		// d-pad like you would expect, no that's pressure sensitive).  Also
		// KDE's joystick dialog maps them to axes as well.
		for (int i = 0; i < NumHats; ++i)
		{
			AxisInfo &x = Axes[NumAxes + i*2];
			AxisInfo &y = Axes[NumAxes + i*2 + 1];

			buttonstate = SDL_JoystickGetHat(Device, i);

			// If we're going to assume that we can pass SDL's value into
			// Joy_GenerateButtonEvents then we might as well assume the format here.
			if(buttonstate & 0x1) // Up
				y.Value = -1.0;
			else if(buttonstate & 0x4) // Down
				y.Value = 1.0;
			else
				y.Value = 0.0;
			if(buttonstate & 0x2) // Left
				x.Value = 1.0;
			else if(buttonstate & 0x8) // Right
				x.Value = -1.0;
			else
				x.Value = 0.0;

			if(i < 4)
			{
				Joy_GenerateButtonEvents(x.ButtonValue, buttonstate, 4, KEY_JOYPOV1_UP + i*4);
				x.ButtonValue = buttonstate;
			}
		}
	}

protected:
	struct AxisInfo
	{
		FString Name;
		float DeadZone;
		float Multiplier;
		int Keys[2];
		double Value;
		uint8_t ButtonValue;
	};

	int					DeviceIndex;
	SDL_Joystick		*Device;

	float				Multiplier;
	bool				Enabled;
	TArray<AxisInfo>	Axes;
	int					NumAxes;
	int					NumHats;

	friend class SDLInputJoystickManager;
};

class SDLInputJoystickManager
{
public:
	SDLInputJoystickManager()
	{
		for(int i = 0;i < SDL_NumJoysticks();i++)
		{
			SDLInputJoystick *device = new SDLInputJoystick(i);
			if(device->IsValid())
				Joysticks.Push(device);
			else
				delete device;
		}
	}
	~SDLInputJoystickManager()
	{
		for(unsigned int i = 0;i < Joysticks.Size();i++)
			delete Joysticks[i];
	}

	void AddAxes(float axes[NUM_KEYS])
	{
		for(unsigned int i = 0;i < Joysticks.Size();i++)
			Joysticks[i]->AddAxes(axes);
	}
	void GetDevices(TArray<IJoystickConfig *> &sticks)
	{
		for(unsigned int i = 0;i < Joysticks.Size();i++)
		{
			M_LoadJoystickConfig(Joysticks[i]);
			sticks.Push(Joysticks[i]);
		}
	}

	void ProcessInput() const
	{
		for(unsigned int i = 0;i < Joysticks.Size();++i)
			if(Joysticks[i]->Enabled) Joysticks[i]->ProcessInput();
	}
protected:
	TArray<SDLInputJoystick *> Joysticks;
};
static SDLInputJoystickManager *JoystickManager;

void I_StartupJoysticks()
{
#ifndef NO_SDL_JOYSTICK
	if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) >= 0)
		JoystickManager = new SDLInputJoystickManager();
#endif
}
void I_ShutdownInput()
{
	if(JoystickManager)
	{
		delete JoystickManager;
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	}
}

void I_GetJoysticks(TArray<IJoystickConfig *> &sticks)
{
	sticks.Clear();

	if (JoystickManager)
		JoystickManager->GetDevices(sticks);
}

void I_GetAxes(float axes[NUM_KEYS])
{
	for (int i = 0; i < NUM_KEYS; ++i)
	{
		axes[i] = 0;
	}
	if (use_joystick && JoystickManager)
	{
		JoystickManager->AddAxes(axes);
	}
}

void I_ProcessJoysticks()
{
	if (use_joystick && JoystickManager)
		JoystickManager->ProcessInput();
}

IJoystickConfig *I_UpdateDeviceList()
{
	return NULL;
}
