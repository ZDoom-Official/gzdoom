/*
 ** i_joystick.cpp
 **
 **---------------------------------------------------------------------------
 ** Copyright 2012-2015 Alexey Lysiuk
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

#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOMessage.h>
#include <IOKit/hid/IOHIDLib.h>

#include "d_eventbase.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_joy.h"

#include "v_text.h"
#include "printf.h"
#include "keydef.h"


EXTERN_CVAR(Bool, joy_axespolling)


namespace
{

FString ToFString(const CFStringRef string)
{
	if (NULL == string)
	{
		return FString();
	}

	const CFIndex stringLength = CFStringGetLength(string);

	if (0 == stringLength)
	{
		return FString();
	}

	const size_t bufferSize = CFStringGetMaximumSizeForEncoding(stringLength, kCFStringEncodingUTF8) + 1;

	char buffer[bufferSize];
	memset(buffer, 0, bufferSize);

	CFStringGetCString(string, buffer, bufferSize, kCFStringEncodingUTF8);

	return FString(buffer);
}


// ---------------------------------------------------------------------------


class IOKitJoystick : public IJoystickConfig
{
public:
	explicit IOKitJoystick(io_object_t device);
	virtual ~IOKitJoystick();

	virtual FString GetName();
	virtual float GetSensitivity();
	virtual void SetSensitivity(float scale);

	virtual int GetNumAxes();
	virtual float GetAxisDeadZone(int axis);
	virtual const char* GetAxisName(int axis);
	virtual float GetAxisScale(int axis);

	virtual void SetAxisDeadZone(int axis, float deadZone);
	virtual void SetAxisScale(int axis, float scale);

	virtual bool IsSensitivityDefault();
	virtual bool IsAxisDeadZoneDefault(int axis);
	virtual bool IsAxisScaleDefault(int axis);

	virtual bool GetEnabled();
	virtual void SetEnabled(bool enabled);

	bool AllowsEnabledInBackground() { return false; }
	bool GetEnabledInBackground() { return false; }
	void SetEnabledInBackground(bool enabled) {}

	virtual void SetDefaultConfig();
	virtual FString GetIdentifier();

	void AddAxes(float axes[NUM_KEYS]) const;

	void Update();

	void UseAxesPolling(bool axesPolling);

	io_object_t* GetNotificationPtr();

private:
	IOHIDDeviceInterface** m_interface;
	IOHIDQueueInterface**  m_queue;

	FString m_name;
	FString m_identifier;

	float m_sensitivity;

	struct AnalogAxis
	{
		IOHIDElementCookie cookie;

		char name[64];

		float value;

		int32_t minValue;
		int32_t maxValue;

		float deadZone;
		float defaultDeadZone;
		float sensitivity;
		float defaultSensitivity;

		int keys[2];
		uint8_t buttonValue;

		AnalogAxis()
		{
			memset(this, 0, sizeof *this);
		}
	};

	TArray<AnalogAxis> m_axes;

	struct DigitalButton
	{
		IOHIDElementCookie cookie;
		int32_t            value;

		explicit DigitalButton(const IOHIDElementCookie cookie)
		: cookie(cookie)
		, value(0)
		{ }
	};

	TArray<DigitalButton> m_buttons;
	TArray<DigitalButton> m_POVs;

	bool m_enabled;
	bool m_useAxesPolling;

	io_object_t m_notification;


	static const float DEFAULT_DEADZONE;
	static const float DEFAULT_SENSITIVITY;

	void ProcessAxes();
	bool ProcessAxis  (const IOHIDEventStruct& event);
	bool ProcessButton(const IOHIDEventStruct& event);
	bool ProcessPOV   (const IOHIDEventStruct& event);

	void GatherDeviceInfo(io_object_t device, CFDictionaryRef properties);

	static void GatherElementsHandler(const void* value, void* parameter);
	void GatherCollectionElements(CFDictionaryRef properties);

	void AddAxis(CFDictionaryRef element);
	void AddButton(CFDictionaryRef element);
	void AddPOV(CFDictionaryRef element);

	void AddToQueue(IOHIDElementCookie cookie);
	void RemoveFromQueue(IOHIDElementCookie cookie);
};


const float IOKitJoystick::DEFAULT_DEADZONE    = 0.25f;
const float IOKitJoystick::DEFAULT_SENSITIVITY = 1.0f;


IOHIDDeviceInterface** CreateDeviceInterface(const io_object_t device)
{
	IOCFPlugInInterface** plugInInterface = NULL;
	SInt32 score = 0;

	const kern_return_t pluginResult = IOCreatePlugInInterfaceForService(device,
		kIOHIDDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugInInterface, &score);

	IOHIDDeviceInterface** interface = NULL;

	if (KERN_SUCCESS == pluginResult)
	{
		// Call a method of the intermediate plug-in to create the device interface

		const HRESULT queryResult = (*plugInInterface)->QueryInterface(plugInInterface,
			CFUUIDGetUUIDBytes(kIOHIDDeviceInterfaceID), reinterpret_cast<LPVOID*>(&interface));

		IODestroyPlugInInterface(plugInInterface); // [?] or maybe (*plugInInterface)->Release(plugInInterface);

		if (S_OK == queryResult)
		{
			const IOReturn openResult = (*interface)->open(interface, 0);

			if (kIOReturnSuccess != openResult)
			{
				(*interface)->Release(interface);

				Printf(TEXTCOLOR_RED "IOHIDDeviceInterface::open() failed with code 0x%08X\n", openResult);
				return NULL;
			}
		}
		else
		{
			Printf(TEXTCOLOR_RED "IOCFPlugInInterface::QueryInterface() failed with code 0x%08X\n",
				static_cast<int>(queryResult));
			return NULL;
		}
	}
	else
	{
		Printf(TEXTCOLOR_RED "IOCreatePlugInInterfaceForService() failed with code %i\n", pluginResult);
		return NULL;
	}

	return interface;
}

IOHIDQueueInterface** CreateDeviceQueue(IOHIDDeviceInterface** const interface)
{
	if (NULL == interface)
	{
		return NULL;
	}

	IOHIDQueueInterface** queue = (*interface)->allocQueue(interface);

	if (NULL == queue)
	{
		Printf(TEXTCOLOR_RED "IOHIDDeviceInterface::allocQueue() failed\n");
		return NULL;
	}

	static const uint32_t QUEUE_FLAGS = 0;
	static const uint32_t QUEUE_DEPTH = 0;

	const IOReturn queueResult = (*queue)->create(queue, QUEUE_FLAGS, QUEUE_DEPTH);

	if (kIOReturnSuccess != queueResult)
	{
		(*queue)->Release(queue);

		Printf(TEXTCOLOR_RED "IOHIDQueueInterface::create() failed with code 0x%08X\n", queueResult);
		return NULL;
	}

	return queue;
}


IOKitJoystick::IOKitJoystick(const io_object_t device)
: m_interface(CreateDeviceInterface(device))
, m_queue(CreateDeviceQueue(m_interface))
, m_sensitivity(DEFAULT_SENSITIVITY)
, m_enabled(true)
, m_useAxesPolling(true)
, m_notification(0)
{
	if (NULL == m_interface || NULL == m_queue)
	{
		return;
	}

	CFMutableDictionaryRef properties = NULL;
	const kern_return_t propertiesResult =
		IORegistryEntryCreateCFProperties(device, &properties, kCFAllocatorDefault, kNilOptions);

	if (KERN_SUCCESS != propertiesResult || NULL == properties)
	{
		Printf(TEXTCOLOR_RED "IORegistryEntryCreateCFProperties() failed with code %i\n", propertiesResult);
		return;
	}

	GatherDeviceInfo(device, properties);
	GatherCollectionElements(properties);

	CFRelease(properties);

	UseAxesPolling(joy_axespolling);

	(*m_queue)->start(m_queue);

	SetDefaultConfig();
}

IOKitJoystick::~IOKitJoystick()
{
	M_SaveJoystickConfig(this);

	if (0 != m_notification)
	{
		IOObjectRelease(m_notification);
	}

	if (NULL != m_queue)
	{
		(*m_queue)->stop(m_queue);
		(*m_queue)->dispose(m_queue);
		(*m_queue)->Release(m_queue);
	}

	if (NULL != m_interface)
	{
		(*m_interface)->close(m_interface);
		(*m_interface)->Release(m_interface);
	}
}


FString IOKitJoystick::GetName()
{
	return m_name;
}


float IOKitJoystick::GetSensitivity()
{
	return m_sensitivity;
}

void IOKitJoystick::SetSensitivity(float scale)
{
	m_sensitivity = scale;
}


int IOKitJoystick::GetNumAxes()
{
	return static_cast<int>(m_axes.Size());
}

#define IS_AXIS_VALID (static_cast<unsigned int>(axis) < m_axes.Size())

float IOKitJoystick::GetAxisDeadZone(int axis)
{
	return IS_AXIS_VALID ? m_axes[axis].deadZone : 0.0f;
}

const char* IOKitJoystick::GetAxisName(int axis)
{
	return IS_AXIS_VALID ? m_axes[axis].name : "Invalid";
}

float IOKitJoystick::GetAxisScale(int axis)
{
	return IS_AXIS_VALID ? m_axes[axis].sensitivity : 0.0f;
}

void IOKitJoystick::SetAxisDeadZone(int axis, float deadZone)
{
	if (IS_AXIS_VALID)
	{
		m_axes[axis].deadZone = clamp(deadZone, 0.0f, 1.0f);
	}
}

void IOKitJoystick::SetAxisScale(int axis, float scale)
{
	if (IS_AXIS_VALID)
	{
		m_axes[axis].sensitivity = scale;
	}
}


bool IOKitJoystick::IsSensitivityDefault()
{
	return DEFAULT_SENSITIVITY == m_sensitivity;
}

bool IOKitJoystick::IsAxisDeadZoneDefault(int axis)
{
	return IS_AXIS_VALID
		? (m_axes[axis].deadZone == m_axes[axis].defaultDeadZone)
		: true;
}

bool IOKitJoystick::IsAxisScaleDefault(int axis)
{
	return IS_AXIS_VALID
		? (m_axes[axis].sensitivity == m_axes[axis].defaultSensitivity)
		: true;
}



bool IOKitJoystick::GetEnabled()
{
	return m_enabled;
}
void IOKitJoystick::SetEnabled(bool enabled)
{
	m_enabled = enabled;
}

#undef IS_AXIS_VALID

void IOKitJoystick::SetDefaultConfig()
{
	m_sensitivity = DEFAULT_SENSITIVITY;

	const size_t axisCount = m_axes.Size();

	for (size_t i = 0; i < axisCount; ++i)
	{
		m_axes[i].deadZone    = DEFAULT_DEADZONE;
		m_axes[i].sensitivity = DEFAULT_SENSITIVITY;
	}

	// Preserve defaults for config saving.

	for (size_t i = 0; i < axisCount; ++i)
	{
		m_axes[i].defaultDeadZone    = m_axes[i].deadZone;
		m_axes[i].defaultSensitivity = m_axes[i].sensitivity;
	}
}


FString IOKitJoystick::GetIdentifier()
{
	return m_identifier;
}


void IOKitJoystick::AddAxes(float axes[NUM_KEYS]) const
{
	for (size_t i = 0, count = m_axes.Size(); i < count; ++i)
	{
		// Add to the game axis.
		float axis_value = m_axes[i].value;
		int axis_key = 0;

		if (axis_value > 0.0f)
		{
			axis_key = m_axes[i].keys[0];
		}
		else if (axis_value < 0.0f)
		{
			axis_key = m_axes[i].keys[1];
		}

		if (axis_key > 0 && axis_key < NUM_KEYS)
		{
			axes[axis_key] += fabs(axis_value);
		}
	}
}


void IOKitJoystick::UseAxesPolling(const bool axesPolling)
{
	m_useAxesPolling = axesPolling;

	for (size_t i = 0, count = m_axes.Size(); i < count; ++i)
	{
		AnalogAxis& axis = m_axes[i];

		if (m_useAxesPolling)
		{
			RemoveFromQueue(axis.cookie);
		}
		else
		{
			AddToQueue(axis.cookie);
		}
	}
}


void IOKitJoystick::Update()
{
	if (NULL == m_queue)
	{
		return;
	}

	IOHIDEventStruct event = { };
	AbsoluteTime  zeroTime = { };

	const IOReturn eventResult = (*m_queue)->getNextEvent(m_queue, &event, zeroTime, 0);

	if (kIOReturnSuccess == eventResult)
	{
		if (use_joystick && m_enabled)
		{
			ProcessAxis(event) || ProcessButton(event) || ProcessPOV(event);
		}
	}
	else if (kIOReturnUnderrun != eventResult)
	{
		Printf(TEXTCOLOR_RED "IOHIDQueueInterface::getNextEvent() failed with code 0x%08X\n", eventResult);
	}

	if(m_enabled) ProcessAxes();
}


void IOKitJoystick::ProcessAxes()
{
	if (NULL == m_interface || !m_useAxesPolling)
	{
		return;
	}

	for (size_t i = 0, count = m_axes.Size(); i < count; ++i)
	{
		AnalogAxis& axis = m_axes[i];
		uint8_t buttonstate = 0;

		if (i < NUM_JOYAXISBUTTONS)
		{
			axis.keys[0] = KEY_JOYAXIS1PLUS + (i * 2);
			axis.keys[1] = KEY_JOYAXIS1PLUS + (i * 2) + 1;
		}
		else
		{
			axis.keys[0] = axis.keys[1] = 0;
		}

		static const double scaledMin = -1;
		static const double scaledMax =  1;

		IOHIDEventStruct event;

		if (kIOReturnSuccess == (*m_interface)->getElementValue(m_interface, axis.cookie, &event))
		{
			const double scaledValue = scaledMin +
				(event.value - axis.minValue) * (scaledMax - scaledMin) / (axis.maxValue - axis.minValue);
			const double filteredValue = Joy_RemoveDeadZone(scaledValue, axis.deadZone, &buttonstate);

			axis.value = static_cast<float>(filteredValue * m_sensitivity * axis.sensitivity);
		}
		else
		{
			axis.value = 0.0f;
		}

		if (i < NUM_JOYAXISBUTTONS && (i > 2 || m_axes.Size() == 1))
		{
			Joy_GenerateButtonEvents(axis.buttonValue, buttonstate, 2, KEY_JOYAXIS1PLUS + i*2);
		}
		else if (i == 1)
		{
			// Since we sorted the axes, we know that the first two are definitely X and Y.
			// They are probably a single stick, so use angular position to determine buttons.
			buttonstate = Joy_XYAxesToButtons(m_axes[0].value, axis.value);
			Joy_GenerateButtonEvents(axis.buttonValue, buttonstate, 4, KEY_JOYAXIS1PLUS);
		}
		axis.buttonValue = buttonstate;
	}
}


bool IOKitJoystick::ProcessAxis(const IOHIDEventStruct& event)
{
	if (m_useAxesPolling)
	{
		return false;
	}

	for (size_t i = 0, count = m_axes.Size(); i < count; ++i)
	{
		if (event.elementCookie != m_axes[i].cookie)
		{
			continue;
		}

		AnalogAxis& axis = m_axes[i];
		uint8_t buttonstate = 0;

		if (i < NUM_JOYAXISBUTTONS)
		{
			axis.keys[0] = KEY_JOYAXIS1PLUS + (i * 2);
			axis.keys[1] = KEY_JOYAXIS1PLUS + (i * 2) + 1;
		}
		else
		{
			axis.keys[0] = axis.keys[1] = 0;
		}

		static const double scaledMin = -1;
		static const double scaledMax =  1;

		const double scaledValue = scaledMin +
			(event.value - axis.minValue) * (scaledMax - scaledMin) / (axis.maxValue - axis.minValue);
		const double filteredValue = Joy_RemoveDeadZone(scaledValue, axis.deadZone, &buttonstate);

		axis.value = static_cast<float>(filteredValue * m_sensitivity * axis.sensitivity);

		if (i < NUM_JOYAXISBUTTONS)
		{
			Joy_GenerateButtonEvents(axis.buttonValue, buttonstate, 2, KEY_JOYAXIS1PLUS + i*2);
		}
		axis.buttonValue = buttonstate;
		return true;
	}

	return false;
}

bool IOKitJoystick::ProcessButton(const IOHIDEventStruct& event)
{
	for (size_t i = 0, count = m_buttons.Size(); i < count; ++i)
	{
		if (event.elementCookie != m_buttons[i].cookie)
		{
			continue;
		}

		int32_t& current = m_buttons[i].value;
		const int32_t previous = current;
		current = event.value;

		Joy_GenerateButtonEvents(previous, current, 1, static_cast<int>(KEY_FIRSTJOYBUTTON + i));

		return true;
	}

	return false;
}

bool IOKitJoystick::ProcessPOV(const IOHIDEventStruct& event)
{
	for (size_t i = 0, count = m_POVs.Size(); i <count; ++i)
	{
		if (event.elementCookie != m_POVs[i].cookie)
		{
			continue;
		}

		int32_t& current = m_buttons[i].value;
		const int32_t previous = current;
		current = event.value;

		static const int POV_BUTTONS[] =
		{
			0x01, 0x03, 0x02, 0x06, 0x04, 0x0C, 0x08, 0x09, 0x00
		};

		Joy_GenerateButtonEvents(POV_BUTTONS[previous], POV_BUTTONS[current], 4, KEY_JOYPOV1_UP + i * 4);

		return true;
	}

	return false;
}


void IOKitJoystick::GatherDeviceInfo(const io_object_t device, const CFDictionaryRef properties)
{
	assert(NULL != properties);

	CFStringRef vendorRef = static_cast<CFStringRef>(
		CFDictionaryGetValue(properties, CFSTR(kIOHIDManufacturerKey)));
	CFStringRef productRef = static_cast<CFStringRef>(
		CFDictionaryGetValue(properties, CFSTR(kIOHIDProductKey)));
	CFNumberRef vendorIDRef = static_cast<CFNumberRef>(
		CFDictionaryGetValue(properties, CFSTR(kIOHIDVendorIDKey)));
	CFNumberRef productIDRef = static_cast<CFNumberRef>(
		CFDictionaryGetValue(properties, CFSTR(kIOHIDProductIDKey)));

	CFMutableDictionaryRef usbProperties = NULL;

	if (   NULL ==   vendorRef || NULL ==   productRef
		|| NULL == vendorIDRef || NULL == productIDRef)
	{
		// OS X is not mirroring all USB properties to HID page, so need to look at USB device page also
		// Step up two levels and get dictionary of USB properties

		io_registry_entry_t parent1;
		kern_return_t ioResult = IORegistryEntryGetParentEntry(device,  kIOServicePlane, &parent1);

		if (KERN_SUCCESS == ioResult)
		{
			io_registry_entry_t parent2;
			ioResult = IORegistryEntryGetParentEntry(device,  kIOServicePlane, &parent2);

			if (KERN_SUCCESS == ioResult)
			{
				ioResult = IORegistryEntryCreateCFProperties(parent2, &usbProperties, kCFAllocatorDefault, kNilOptions);

				if (KERN_SUCCESS != ioResult)
				{
					Printf(TEXTCOLOR_RED "IORegistryEntryCreateCFProperties() failed with code %i\n", ioResult);
				}

				IOObjectRelease(parent2);
			}
			else
			{
				Printf(TEXTCOLOR_RED "IORegistryEntryGetParentEntry(2) failed with code %i\n", ioResult);
			}

			IOObjectRelease(parent1);
		}
		else
		{
			Printf(TEXTCOLOR_RED "IORegistryEntryGetParentEntry(1) failed with code %i\n", ioResult);
		}
	}

	if (NULL != usbProperties)
	{
		if (NULL == vendorRef)
		{
			vendorRef = static_cast<CFStringRef>(
				CFDictionaryGetValue(usbProperties, CFSTR("USB Vendor Name")));
		}

		if (NULL == productRef)
		{
			productRef = static_cast<CFStringRef>(
				CFDictionaryGetValue(usbProperties, CFSTR("USB Product Name")));
		}

		if (NULL == vendorIDRef)
		{
			vendorIDRef = static_cast<CFNumberRef>(
				CFDictionaryGetValue(usbProperties, CFSTR("idVendor")));
		}

		if (NULL == productIDRef)
		{
			productIDRef = static_cast<CFNumberRef>(
				CFDictionaryGetValue(usbProperties, CFSTR("idProduct")));
		}
	}

	m_name += ToFString(vendorRef);
	m_name += " ";
	m_name += ToFString(productRef);

	int vendorID = 0, productID = 0;

	if (NULL != vendorIDRef)
	{
		CFNumberGetValue(vendorIDRef, kCFNumberIntType, &vendorID);
	}

	if (NULL != productIDRef)
	{
		CFNumberGetValue(productIDRef, kCFNumberIntType, &productID);
	}

	m_identifier.AppendFormat("VID_%04x_PID_%04x", vendorID, productID);

	if (NULL != usbProperties)
	{
		CFRelease(usbProperties);
	}
}


long GetElementValue(const CFDictionaryRef element, const CFStringRef key)
{
	const CFNumberRef number =
		static_cast<CFNumberRef>(CFDictionaryGetValue(element, key));
	long result = 0;

	if (NULL != number && CFGetTypeID(number) == CFNumberGetTypeID())
	{
		CFNumberGetValue(number, kCFNumberLongType, &result);
	}

	return result;
}

void IOKitJoystick::GatherElementsHandler(const void* value, void* parameter)
{
	assert(NULL != value);
	assert(NULL != parameter);

	const CFDictionaryRef element = static_cast<CFDictionaryRef>(value);
	IOKitJoystick* thisPtr = static_cast<IOKitJoystick*>(parameter);

	if (CFGetTypeID(element) != CFDictionaryGetTypeID())
	{
		Printf(TEXTCOLOR_RED "IOKitJoystick: Encountered wrong element type\n");
		return;
	}

	const long type = GetElementValue(element, CFSTR(kIOHIDElementTypeKey));

	if (kIOHIDElementTypeCollection == type)
	{
		thisPtr->GatherCollectionElements(element);
	}
	else if (0 != type)
	{
		const long usagePage = GetElementValue(element, CFSTR(kIOHIDElementUsagePageKey));

		if (kHIDPage_GenericDesktop == usagePage)
		{
			const long usage = GetElementValue(element, CFSTR(kIOHIDElementUsageKey));

			if (   kHIDUsage_GD_Slider == usage
				|| kHIDUsage_GD_X      == usage || kHIDUsage_GD_Y  == usage || kHIDUsage_GD_Z  == usage
				|| kHIDUsage_GD_Rx     == usage || kHIDUsage_GD_Ry == usage || kHIDUsage_GD_Rz == usage)
			{
				thisPtr->AddAxis(element);
			}
			else if (kHIDUsage_GD_Hatswitch == usage && thisPtr->m_POVs.Size() < 4)
			{
				thisPtr->AddPOV(element);
			}
		}
		else if (kHIDPage_Button == usagePage)
		{
			thisPtr->AddButton(element);
		}
	}
}

void IOKitJoystick::GatherCollectionElements(const CFDictionaryRef properties)
{
	const CFArrayRef topElement = static_cast<CFArrayRef>(
		CFDictionaryGetValue(properties, CFSTR(kIOHIDElementKey)));

	if (NULL == topElement || CFGetTypeID(topElement) != CFArrayGetTypeID())
	{
		Printf(TEXTCOLOR_RED "GatherCollectionElements: invalid properties dictionary\n");
		return;
	}

	const CFRange range = { 0, CFArrayGetCount(topElement) };

	CFArrayApplyFunction(topElement, range, GatherElementsHandler, this);
}


IOHIDElementCookie GetElementCookie(const CFDictionaryRef element)
{
	// Use C-style cast to avoid 32/64-bit IOHIDElementCookie type issue
	return (IOHIDElementCookie)GetElementValue(element, CFSTR(kIOHIDElementCookieKey));
}

void IOKitJoystick::AddAxis(const CFDictionaryRef element)
{
	AnalogAxis axis;

	axis.cookie   = GetElementCookie(element);
	axis.minValue = GetElementValue(element, CFSTR(kIOHIDElementMinKey));
	axis.maxValue = GetElementValue(element, CFSTR(kIOHIDElementMaxKey));

	const CFStringRef nameRef = static_cast<CFStringRef>(
		CFDictionaryGetValue(element, CFSTR(kIOHIDElementNameKey)));

	if (NULL != nameRef && CFStringGetTypeID() == CFGetTypeID(nameRef))
	{
		CFStringGetCString(nameRef, axis.name, sizeof(axis.name) - 1, kCFStringEncodingUTF8);
	}
	else
	{
		snprintf(axis.name, sizeof(axis.name), "Axis %i", m_axes.Size() + 1);
	}

	m_axes.Push(axis);
}

void IOKitJoystick::AddButton(CFDictionaryRef element)
{
	const DigitalButton button(GetElementCookie(element));

	m_buttons.Push(button);

	AddToQueue(button.cookie);
}

void IOKitJoystick::AddPOV(CFDictionaryRef element)
{
	const DigitalButton pov(GetElementCookie(element));

	m_POVs.Push(pov);

	AddToQueue(pov.cookie);
}


void IOKitJoystick::AddToQueue(const IOHIDElementCookie cookie)
{
	if (NULL == m_queue)
	{
		return;
	}

	if (!(*m_queue)->hasElement(m_queue, cookie))
	{
		(*m_queue)->addElement(m_queue, cookie, 0);
	}
}

void IOKitJoystick::RemoveFromQueue(const IOHIDElementCookie cookie)
{
	if (NULL == m_queue)
	{
		return;
	}

	if ((*m_queue)->hasElement(m_queue, cookie))
	{
		(*m_queue)->removeElement(m_queue, cookie);
	}
}


io_object_t* IOKitJoystick::GetNotificationPtr()
{
	return &m_notification;
}


// ---------------------------------------------------------------------------


class IOKitJoystickManager
{
public:
	IOKitJoystickManager();
	~IOKitJoystickManager();

	void GetJoysticks(TArray<IJoystickConfig*>& joysticks) const;

	void AddAxes(float axes[NUM_KEYS]) const;

	// Updates axes/buttons states
	void Update();

	void UseAxesPolling(bool axesPolling);

private:
	typedef TDeletingArray<IOKitJoystick*> JoystickList;
	JoystickList m_joysticks;

	static const size_t NOTIFICATION_PORT_COUNT = 2;

	IONotificationPortRef m_notificationPorts[NOTIFICATION_PORT_COUNT];
	io_iterator_t         m_notifications    [NOTIFICATION_PORT_COUNT];

	// Rebuilds device list
	void Rescan(int usagePage, int usage, size_t notificationPortIndex);
	void AddDevices(IONotificationPortRef notificationPort, const io_iterator_t iterator);

	static void OnDeviceAttached(void* refcon, io_iterator_t iterator);
	static void OnDeviceRemoved(void* refcon, io_service_t service,
		natural_t messageType, void* messageArgument);
};


IOKitJoystickManager* s_joystickManager;


IOKitJoystickManager::IOKitJoystickManager()
{
	memset(m_notifications, 0, sizeof m_notifications);

	for (size_t i = 0; i < NOTIFICATION_PORT_COUNT; ++i)
	{
		m_notificationPorts[i] = IONotificationPortCreate(kIOMasterPortDefault);

		if (NULL == m_notificationPorts[i])
		{
			Printf(TEXTCOLOR_RED "IONotificationPortCreate(%zu) failed\n", i);
			return;
		}

		CFRunLoopAddSource(CFRunLoopGetCurrent(),
			IONotificationPortGetRunLoopSource(m_notificationPorts[i]), kCFRunLoopDefaultMode);
	}

	Rescan(kHIDPage_GenericDesktop, kHIDUsage_GD_Joystick, 0);
	Rescan(kHIDPage_GenericDesktop, kHIDUsage_GD_GamePad,  1);
}

IOKitJoystickManager::~IOKitJoystickManager()
{
	for (size_t i = 0; i < NOTIFICATION_PORT_COUNT; ++i)
	{
		IONotificationPortRef& port = m_notificationPorts[i];

		if (NULL != port)
		{
			CFRunLoopRemoveSource(CFRunLoopGetCurrent(),
				IONotificationPortGetRunLoopSource(port), kCFRunLoopDefaultMode);

			IONotificationPortDestroy(port);
			port = NULL;
		}

		io_iterator_t& notification = m_notifications[i];

		if (0 != notification)
		{
			IOObjectRelease(notification);
			notification = 0;
		}
	}
}


void IOKitJoystickManager::GetJoysticks(TArray<IJoystickConfig*>& joysticks) const
{
	const size_t joystickCount = m_joysticks.Size();

	joysticks.Resize(joystickCount);

	for (size_t i = 0; i < joystickCount; ++i)
	{
		M_LoadJoystickConfig(m_joysticks[i]);

		joysticks[i] = m_joysticks[i];
	}
}

void IOKitJoystickManager::AddAxes(float axes[NUM_KEYS]) const
{
	for (size_t i = 0, count = m_joysticks.Size(); i < count; ++i)
	{
		m_joysticks[i]->AddAxes(axes);
	}
}


void IOKitJoystickManager::Update()
{
	for (size_t i = 0, count = m_joysticks.Size(); i < count; ++i)
	{
		m_joysticks[i]->Update();
	}
}


void IOKitJoystickManager::UseAxesPolling(const bool axesPolling)
{
	for (size_t i = 0, count = m_joysticks.Size(); i < count; ++i)
	{
		m_joysticks[i]->UseAxesPolling(axesPolling);
	}
}


void PostDeviceChangeEvent()
{
	event_t event = { EV_DeviceChange };
	D_PostEvent(&event);
}


void IOKitJoystickManager::Rescan(const int usagePage, const int usage, const size_t notificationPortIndex)
{
	CFMutableDictionaryRef deviceMatching = IOServiceMatching(kIOHIDDeviceKey);

	if (NULL == deviceMatching)
	{
		Printf(TEXTCOLOR_RED "IOServiceMatching() returned NULL\n");
		return;
	}

	const CFNumberRef usagePageRef =
		CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usagePage);
	CFDictionarySetValue(deviceMatching, CFSTR(kIOHIDPrimaryUsagePageKey), usagePageRef);

	const CFNumberRef usageRef =
		CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
	CFDictionarySetValue(deviceMatching, CFSTR(kIOHIDPrimaryUsageKey), usageRef);

	assert(notificationPortIndex < NOTIFICATION_PORT_COUNT);
	io_iterator_t* iteratorPtr = &m_notifications[notificationPortIndex];

	const IONotificationPortRef notificationPort = m_notificationPorts[notificationPortIndex];
	assert(NULL != notificationPort);

	const kern_return_t notificationResult = IOServiceAddMatchingNotification(notificationPort,
		kIOFirstMatchNotification, deviceMatching, OnDeviceAttached, notificationPort, iteratorPtr);

	// IOServiceAddMatchingNotification() consumes one reference of matching dictionary
	// Thus CFRelease(deviceMatching) is not needed

	CFRelease(usageRef);
	CFRelease(usagePageRef);

	if (KERN_SUCCESS != notificationResult)
	{
		Printf(TEXTCOLOR_RED "IOServiceAddMatchingNotification() failed with code %i\n", notificationResult);
	}

	AddDevices(notificationPort, *iteratorPtr);
}

void IOKitJoystickManager::AddDevices(const IONotificationPortRef notificationPort, const io_iterator_t iterator)
{
	while (io_object_t device = IOIteratorNext(iterator))
	{
		IOKitJoystick* joystick = new IOKitJoystick(device);
		m_joysticks.Push(joystick);

		const kern_return_t notificationResult = IOServiceAddInterestNotification(notificationPort,
			device, kIOGeneralInterest, OnDeviceRemoved, joystick, joystick->GetNotificationPtr());
		if (KERN_SUCCESS != notificationResult)
		{
			Printf(TEXTCOLOR_RED "IOServiceAddInterestNotification() failed with code %i\n", notificationResult);
		}

		IOObjectRelease(device);

		PostDeviceChangeEvent();
	}
}


void IOKitJoystickManager::OnDeviceAttached(void* const refcon, const io_iterator_t iterator)
{
	assert(NULL != refcon);
	const IONotificationPortRef notificationPort = static_cast<IONotificationPortRef>(refcon);

	assert(NULL != s_joystickManager);
	s_joystickManager->AddDevices(notificationPort, iterator);
}

void IOKitJoystickManager::OnDeviceRemoved(void* const refcon, io_service_t, const natural_t messageType, void*)
{
	if (messageType != kIOMessageServiceIsTerminated)
	{
		return;
	}

	assert(NULL != refcon);
	IOKitJoystick* const joystick = static_cast<IOKitJoystick*>(refcon);

	assert(NULL != s_joystickManager);
	JoystickList& joysticks = s_joystickManager->m_joysticks;

	for (unsigned int i = 0, count = joysticks.Size(); i < count; ++i)
	{
		if (joystick == joysticks[i])
		{
			joysticks.Delete(i);
			break;
		}
	}

	delete joystick;

	PostDeviceChangeEvent();
}

} // unnamed namespace


// ---------------------------------------------------------------------------


void I_ShutdownInput()
{
	delete s_joystickManager;
	s_joystickManager = NULL;
}

void I_GetJoysticks(TArray<IJoystickConfig*>& sticks)
{
	// Instances of IOKitJoystick depend on GameConfig object.
	// M_SaveDefaultsFinal() must be called after destruction of IOKitJoystickManager.
	// To ensure this, its initialization is moved here.
	// As M_LoadDefaults() was already called at this moment,
	// the order of atterm's functions will be correct

	if (NULL == s_joystickManager && !Args->CheckParm("-nojoy"))
	{
		s_joystickManager = new IOKitJoystickManager;
	}

	if (NULL != s_joystickManager)
	{
		s_joystickManager->GetJoysticks(sticks);
	}
}

void I_GetAxes(float axes[NUM_KEYS])
{
	for (size_t i = 0; i < NUM_KEYS; ++i)
	{
		axes[i] = 0.0f;
	}

	if (use_joystick && NULL != s_joystickManager)
	{
		s_joystickManager->AddAxes(axes);
	}
}

IJoystickConfig* I_UpdateDeviceList()
{
	// Does nothing, device list is always kept up-to-date

	return NULL;
}


// ---------------------------------------------------------------------------


void I_ProcessJoysticks()
{
	if (NULL != s_joystickManager)
	{
		s_joystickManager->Update();
	}
}


// ---------------------------------------------------------------------------


CUSTOM_CVAR(Bool, joy_axespolling, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (NULL != s_joystickManager)
	{
		s_joystickManager->UseAxesPolling(self);
	}
}
