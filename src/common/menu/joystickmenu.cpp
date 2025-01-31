/*
** joystickmenu.cpp
** The joystick configuration menus
**
**---------------------------------------------------------------------------
** Copyright 2010 Christoph Oelckers
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

#include "menu.h"
#include "m_joy.h"
#include "vm.h"

static TArray<IJoystickConfig *> Joysticks;

DEFINE_ACTION_FUNCTION(IJoystickConfig, GetSensitivity)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	ACTION_RETURN_FLOAT(self->GetSensitivity());
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, SetSensitivity)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_FLOAT(sens);
	self->SetSensitivity((float)sens);
	return 0;
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, GetAxisScale)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_INT(axis);
	ACTION_RETURN_FLOAT(self->GetAxisScale(axis));
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, SetAxisScale)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_INT(axis);
	PARAM_FLOAT(sens);
	self->SetAxisScale(axis, (float)sens);
	return 0;
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, GetAxisDeadZone)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_INT(axis);
	ACTION_RETURN_FLOAT(self->GetAxisDeadZone(axis));
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, SetAxisDeadZone)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_INT(axis);
	PARAM_FLOAT(dz);
	self->SetAxisDeadZone(axis, (float)dz);
	return 0;
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, GetAxisMap)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_INT(axis);
	ACTION_RETURN_INT(0);
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, SetAxisMap)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_INT(axis);
	PARAM_INT(map);
	//self->SetAxisMap(axis, (EJoyAxis)map);
	return 0;
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, GetName)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	ACTION_RETURN_STRING(self->GetName());
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, GetAxisName)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_INT(axis);
	ACTION_RETURN_STRING(self->GetAxisName(axis));
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, GetNumAxes)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	ACTION_RETURN_INT(self->GetNumAxes());
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, GetEnabled)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	ACTION_RETURN_BOOL(self->GetEnabled());
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, SetEnabled)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_BOOL(enabled);
	self->SetEnabled(enabled);
	return 0;
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, AllowsEnabledInBackground)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	ACTION_RETURN_BOOL(self->AllowsEnabledInBackground());
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, GetEnabledInBackground)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	ACTION_RETURN_BOOL(self->GetEnabledInBackground());
}

DEFINE_ACTION_FUNCTION(IJoystickConfig, SetEnabledInBackground)
{
	PARAM_SELF_STRUCT_PROLOGUE(IJoystickConfig);
	PARAM_BOOL(enabled);
	self->SetEnabledInBackground(enabled);
	return 0;
}


void UpdateJoystickMenu(IJoystickConfig *selected)
{
	DMenuDescriptor **desc = MenuDescriptors.CheckKey(NAME_JoystickOptions);
	DMenuDescriptor **ddesc = MenuDescriptors.CheckKey("JoystickOptionsDefaults");
	if (ddesc == nullptr) return;	// without any data the menu cannot be set up and must remain empty.
	if (desc != NULL && (*desc)->IsKindOf(RUNTIME_CLASS(DOptionMenuDescriptor)))
	{
		DOptionMenuDescriptor *opt = (DOptionMenuDescriptor *)*desc;
		DOptionMenuDescriptor *dopt = (DOptionMenuDescriptor *)*ddesc;
		if (dopt == nullptr) return;
		DMenuItemBase *it;

		int i;
		int itemnum = -1;

		I_GetJoysticks(Joysticks);
		if ((unsigned)itemnum >= Joysticks.Size())
		{
			itemnum = Joysticks.Size() - 1;
		}
		if (selected != NULL)
		{
			for (i = 0; (unsigned)i < Joysticks.Size(); ++i)
			{
				if (Joysticks[i] == selected)
				{
					itemnum = i;
					break;
				}
			}
		}
		opt->mItems = dopt->mItems;

		it = opt->GetItem("ConfigureMessage");
		if (it != nullptr) it->SetValue(0, !!Joysticks.Size());
		it = opt->GetItem("ConnectMessage1");
		if (it != nullptr) it->SetValue(0, !use_joystick);
		it = opt->GetItem("ConnectMessage2");
		if (it != nullptr) it->SetValue(0, !use_joystick);

		for (int ii = 0; ii < (int)Joysticks.Size(); ++ii)
		{
			it = CreateOptionMenuItemJoyConfigMenu(Joysticks[ii]->GetName().GetChars(), Joysticks[ii]);
			GC::WriteBarrier(opt, it);
			opt->mItems.Push(it);
			if (ii == itemnum) opt->mSelectedItem = opt->mItems.Size();
		}
		if (opt->mSelectedItem >= (int)opt->mItems.Size())
		{
			opt->mSelectedItem = opt->mItems.Size() - 1;
		}
		//opt->CalcIndent();

		// If the joystick config menu is open, close it if the device it's open for is gone.
		if (CurrentMenu != nullptr && (CurrentMenu->IsKindOf("JoystickConfigMenu")))
		{
			auto p = CurrentMenu->PointerVar<IJoystickConfig>("mJoy");
			if (p != nullptr)
			{
				unsigned ii;
				for (ii = 0; ii < Joysticks.Size(); ++ii)
				{
					if (Joysticks[ii] == p)
					{
						break;
					}
				}
				if (ii == Joysticks.Size())
				{
					CurrentMenu->Close();
				}
			}
		}
	}
}

