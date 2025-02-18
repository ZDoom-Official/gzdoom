/*
** oalsound.cpp
** System interface for sound; uses OpenAL
**
**---------------------------------------------------------------------------
** Copyright 2008-2010 Chris Robinson
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

#include <functional>
#include <chrono>

#include "c_cvars.h"

#include "oalsound.h"
#include "c_dispatch.h"
#include "v_text.h"
#include "i_module.h"
#include "cmdlib.h"
#include "m_fixed.h"


const char *GetSampleTypeName(SampleType type);
const char *GetChannelConfigName(ChannelConfig chan);

FModule OpenALModule{"OpenAL"};

#include "oalload.h"

CUSTOM_CVAR(Int, snd_channels, 128, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)	// number of channels available
{
	if (self < 64) self = 64;
}
CVAR(Bool, snd_waterreverb, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) 
CVAR (String, snd_aldevice, "Default", CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR (Bool, snd_efx, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR (String, snd_alresampler, "Default", CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

#ifdef _WIN32
#define OPENALLIB "openal32.dll"
#elif defined(__OpenBSD__)
#define OPENALLIB "libopenal.so"
#else
#define OPENALLIB "libopenal.so.1"
#endif

#ifdef __APPLE__
// User's library (like OpenAL Soft installed manually or via Homebrew) has precedence
// over Apple's OpenAL framework which lacks several important features
#define OPENALLIB1 "libopenal.1.dylib"
#define OPENALLIB2 "OpenAL.framework/OpenAL"
#else // !__APPLE__
#define OPENALLIB1 NicePath("$PROGDIR/" OPENALLIB).GetChars()
#define OPENALLIB2 OPENALLIB
#endif

bool IsOpenALPresent()
{
#ifdef NO_OPENAL
	return false;
#elif !defined DYN_OPENAL
	return true;
#else
	static bool cached_result = false;
	static bool done = false;

	if (!done)
	{
		done = true;
		cached_result = OpenALModule.Load({OPENALLIB1, OPENALLIB2});
	}
	return cached_result;
#endif
}




ReverbContainer *ForcedEnvironment;


#ifndef NO_OPENAL


EXTERN_CVAR (Int, snd_channels)
EXTERN_CVAR (Int, snd_samplerate)
EXTERN_CVAR (Bool, snd_waterreverb)
EXTERN_CVAR (Int, snd_hrtf)


#define MAKE_PTRID(x)  ((void*)(uintptr_t)(x))
#define GET_PTRID(x)  ((uint32_t)(uintptr_t)(x))


static constexpr uint8_t SampleTypeSize(SampleType stype)
{
    switch(stype)
    {
    case SampleType_UInt8: return sizeof(uint8_t);
    case SampleType_Int16: return sizeof(int16_t);
    case SampleType_Float32: return sizeof(float);
    }
    return 0;
}

static constexpr uint8_t ChannelCount(ChannelConfig chans)
{
    switch(chans)
    {
    case ChannelConfig_Mono: return 1;
    case ChannelConfig_Stereo: return 2;
    }
    return 0;
}

static constexpr ALenum GetFormat(SampleType stype, ChannelConfig chans)
{
	if(stype == SampleType_UInt8)
	{
		if(chans == ChannelConfig_Mono) return AL_FORMAT_MONO8;
		if(chans == ChannelConfig_Stereo) return AL_FORMAT_STEREO8;
	}
	if(stype == SampleType_Int16)
	{
		if(chans == ChannelConfig_Mono) return AL_FORMAT_MONO16;
		if(chans == ChannelConfig_Stereo) return AL_FORMAT_STEREO16;
	}
	if(stype == SampleType_Float32)
	{
		if(chans == ChannelConfig_Mono) return AL_FORMAT_MONO_FLOAT32;
		if(chans == ChannelConfig_Stereo) return AL_FORMAT_STEREO_FLOAT32;
	}
	return AL_NONE;
}

static ALenum checkALError(const char *fn, unsigned int ln)
{
	ALenum err = alGetError();
	if(err != AL_NO_ERROR)
	{
		if(strchr(fn, '/'))
			fn = strrchr(fn, '/')+1;
		else if(strchr(fn, '\\'))
			fn = strrchr(fn, '\\')+1;
		Printf(">>>>>>>>>>>> Received AL error %s (%#x), %s:%u\n", alGetString(err), err, fn, ln);
	}
	return err;
}
#define getALError() checkALError(__FILE__, __LINE__)

static ALCenum checkALCError(ALCdevice *device, const char *fn, unsigned int ln)
{
	ALCenum err = alcGetError(device);
	if(err != ALC_NO_ERROR)
	{
		if(strchr(fn, '/'))
			fn = strrchr(fn, '/')+1;
		else if(strchr(fn, '\\'))
			fn = strrchr(fn, '\\')+1;
		Printf(">>>>>>>>>>>> Received ALC error %s (%#x), %s:%u\n", alcGetString(device, err), err, fn, ln);
	}
	return err;
}
#define getALCError(d) checkALCError((d), __FILE__, __LINE__)


// Fallback methods for when AL_SOFT_deferred_updates isn't available. In most
// cases these don't actually do anything, except on some Creative drivers
// where they act as appropriate fallbacks.
static ALvoid AL_APIENTRY _wrap_DeferUpdatesSOFT(void)
{
	alcSuspendContext(alcGetCurrentContext());
}

static ALvoid AL_APIENTRY _wrap_ProcessUpdatesSOFT(void)
{
	alcProcessContext(alcGetCurrentContext());
}


class OpenALSoundStream : public SoundStream
{
	OpenALSoundRenderer *Renderer;

	SoundStreamCallback Callback;
	void *UserData;

	TArray<ALubyte> Data;

	ALsizei SampleRate;
	ALenum Format;
	ALsizei FrameSize;

	static const int BufferCount = 4;
	ALuint Buffers[BufferCount];
	ALuint Source;

	std::atomic<bool> Playing;
	//bool Looping;
	ALfloat Volume;
	uint64_t Offset = 0;
	std::mutex Mutex;

	bool SetupSource()
	{
		/* Get a source, killing the farthest, lowest-priority sound if needed */
		if(Renderer->FreeSfx.Size() == 0)
		{
			FSoundChan *lowest = Renderer->FindLowestChannel();
			if(lowest) Renderer->StopChannel(lowest);

			if(Renderer->FreeSfx.Size() == 0)
				return false;
		}
		Renderer->FreeSfx.Pop(Source);

		/* Set the default properties for localized playback */
		alSource3f(Source, AL_DIRECTION, 0.f, 0.f, 0.f);
		alSource3f(Source, AL_VELOCITY, 0.f, 0.f, 0.f);
		alSource3f(Source, AL_POSITION, 0.f, 0.f, 0.f);
		alSourcef(Source, AL_MAX_GAIN, 1.f);
		alSourcef(Source, AL_GAIN, 1.f);
		alSourcef(Source, AL_PITCH, 1.f);
		alSourcef(Source, AL_DOPPLER_FACTOR, 0.f);
		alSourcef(Source, AL_ROLLOFF_FACTOR, 0.f);
		alSourcef(Source, AL_SEC_OFFSET, 0.f);
		alSourcei(Source, AL_SOURCE_RELATIVE, AL_TRUE);
		alSourcei(Source, AL_LOOPING, AL_FALSE);
		if(Renderer->EnvSlot)
		{
			alSourcef(Source, AL_ROOM_ROLLOFF_FACTOR, 0.f);
			alSourcef(Source, AL_AIR_ABSORPTION_FACTOR, 0.f);
			alSourcei(Source, AL_DIRECT_FILTER, AL_FILTER_NULL);
			alSource3i(Source, AL_AUXILIARY_SEND_FILTER, 0, 0, AL_FILTER_NULL);
		}
		if(Renderer->AL.EXT_SOURCE_RADIUS)
			alSourcef(Source, AL_SOURCE_RADIUS, 0.f);
		if(Renderer->AL.SOFT_source_spatialize)
			alSourcei(Source, AL_SOURCE_SPATIALIZE_SOFT, AL_AUTO_SOFT);

		alGenBuffers(BufferCount, Buffers);
		return (getALError() == AL_NO_ERROR);
	}

public:
	OpenALSoundStream(OpenALSoundRenderer *renderer)
	  : Renderer(renderer), Source(0), Playing(false), Volume(1.0f)
	{
		memset(Buffers, 0, sizeof(Buffers));
		Renderer->AddStream(this);
	}

	virtual ~OpenALSoundStream()
	{
		Renderer->RemoveStream(this);

		if(Source)
		{
			alSourceRewind(Source);
			alSourcei(Source, AL_BUFFER, 0);

			Renderer->FreeSfx.Push(Source);
			Source = 0;
		}

		if(Buffers[0])
		{
			alDeleteBuffers(BufferCount, &Buffers[0]);
			memset(Buffers, 0, sizeof(Buffers));
		}
		getALError();
	}


	virtual bool Play(bool loop, float vol)
	{
		SetVolume(vol);

		if(Playing.load())
			return true;

		/* Clear the buffer queue, then fill and queue each buffer */
		Offset = 0;
		alSourcei(Source, AL_BUFFER, 0);
		for(int i = 0;i < BufferCount;i++)
		{
			if(!Callback(this, &Data[0], Data.Size(), UserData))
			{
				if(i == 0)
					return false;
				break;
			}

			alBufferData(Buffers[i], Format, &Data[0], Data.Size(), SampleRate);
			alSourceQueueBuffers(Source, 1, &Buffers[i]);
		}
		if(getALError() != AL_NO_ERROR)
			return false;

		alSourcePlay(Source);
		if(getALError() != AL_NO_ERROR)
			return false;

		Playing.store(true);
		return true;
	}

	virtual void Stop()
	{
		if(!Playing.load())
			return;

		std::unique_lock<std::mutex> lock(Renderer->StreamLock);
		alSourceStop(Source);
		alSourcei(Source, AL_BUFFER, 0);
		getALError();

		Offset = 0;
		Playing.store(false);
	}

	virtual void SetVolume(float vol)
	{
		Volume = vol;
		UpdateVolume();
	}

	void UpdateVolume()
	{
		alSourcef(Source, AL_GAIN, Renderer->MusicVolume*Volume);
		getALError();
	}

	virtual bool SetPaused(bool pause)
	{
		if(pause)
			alSourcePause(Source);
		else
			alSourcePlay(Source);
		return (getALError()==AL_NO_ERROR);
	}

	virtual bool IsEnded()
	{
		return !Playing.load();
	}

	Position GetPlayPosition() override
	{
		using namespace std::chrono;

		std::lock_guard _{Mutex};
		ALint64SOFT offset[2]{};
		ALint state{}, queued{};
		alGetSourcei(Source, AL_BUFFERS_QUEUED, &queued);
		if(Renderer->AL.SOFT_source_latency)
		{
			// AL_SAMPLE_OFFSET_LATENCY_SOFT fills offset[0] with the source sample
			// offset in 32.32 fixed-point (which we chop off the sub-sample position
			// since it's not crucial), and offset[1] with the playback latency in
			// nanoseconds (how many nanoseconds until the sample point in offset[0]
			// reaches the DAC).
			Renderer->alGetSourcei64vSOFT(Source, AL_SAMPLE_OFFSET_LATENCY_SOFT, offset);
			offset[0] >>= 32;
		}
		else
		{
			// Without AL_SOFT_source_latency, we can only get the sample offset, no
			// latency info.
			ALint ioffset{};
			alGetSourcei(Source, AL_SAMPLE_OFFSET, &ioffset);
			offset[0] = ioffset;
			offset[1] = 0;
		}
		alGetSourcei(Source, AL_SOURCE_STATE, &state);
		// If the source is stopped, there was an underrun, so the play position is
		// the end of the queue.
		if(state == AL_STOPPED)
			return Position{Offset + queued*(Data.Size()/FrameSize), nanoseconds{offset[1]}};
		// The offset is otherwise valid as long as the source has been started.
		if(state != AL_INITIAL)
			return Position{Offset + offset[0], nanoseconds{offset[1]}};
		return Position{0, nanoseconds{0}};
	}

	virtual FString GetStats()
		{
		FString stats;
		ALfloat volume;
		ALint offset;
		ALint processed;
		ALint queued;
		ALint state;
		ALenum err;

		std::unique_lock<std::mutex> lock(Renderer->StreamLock);
		alGetSourcef(Source, AL_GAIN, &volume);
		alGetSourcei(Source, AL_SAMPLE_OFFSET, &offset);
		alGetSourcei(Source, AL_BUFFERS_PROCESSED, &processed);
		alGetSourcei(Source, AL_BUFFERS_QUEUED, &queued);
		alGetSourcei(Source, AL_SOURCE_STATE, &state);
		if((err=alGetError()) != AL_NO_ERROR)
		{
			lock.unlock();
			stats = "Error getting stats: ";
			stats += alGetString(err);
			return stats;
		}

		lock.unlock();

		stats = (state == AL_INITIAL) ? "Buffering" : (state == AL_STOPPED) ? "Underrun" :
				(state == AL_PLAYING || state == AL_PAUSED) ? "Ready" : "Unknown state";

		if(state == AL_PAUSED)
			stats += ", paused";
		if(state == AL_PLAYING)
			stats += ", playing";
		stats.AppendFormat(", %uHz", SampleRate);
		if(!Playing)
			stats += " XX";
		return stats;
	}

	bool Process()
	{
		if(!Playing.load())
			return false;

		ALint state, processed;
		alGetSourcei(Source, AL_SOURCE_STATE, &state);
		alGetSourcei(Source, AL_BUFFERS_PROCESSED, &processed);
		if(getALError() != AL_NO_ERROR)
		{
			Playing.store(false);
			return false;
		}

		// For each processed buffer in the queue...
		while(processed > 0)
		{
			ALuint bufid;

			// Unqueue the oldest buffer, fill it with more data, and queue it
			// on the end
			{
				std::lock_guard _{Mutex};
				alSourceUnqueueBuffers(Source, 1, &bufid);
				Offset += Data.Size() / FrameSize;
				processed--;
			}

			if(Callback(this, &Data[0], Data.Size(), UserData))
			{
				alBufferData(bufid, Format, &Data[0], Data.Size(), SampleRate);
				alSourceQueueBuffers(Source, 1, &bufid);
			}
		}

		// If the source is not playing or paused, and there are buffers queued,
		// then there was an underrun. Restart the source.
		bool ok = (getALError()==AL_NO_ERROR);
		if(ok && state != AL_PLAYING && state != AL_PAUSED)
		{
			ALint queued = 0;
			alGetSourcei(Source, AL_BUFFERS_QUEUED, &queued);

			ok = (getALError() == AL_NO_ERROR) && (queued > 0);
			if(ok)
			{
				alSourcePlay(Source);
				ok = (getALError()==AL_NO_ERROR);
			}
		}

		Playing.store(ok);
		return ok;
	}

	bool Init(SoundStreamCallback callback, int buffbytes, SampleType stype, ChannelConfig chans, int samplerate, void *userdata)
	{
		if(!SetupSource())
			return false;

		Callback = callback;
		UserData = userdata;
		SampleRate = samplerate;

		Format = GetFormat(stype, chans);
		if(Format == AL_NONE)
		{
			Printf("Unhandled audio format: %s, %s\n", GetChannelConfigName(chans),
				GetSampleTypeName(stype));
			return false;
		}

		FrameSize = SampleTypeSize(stype) * ChannelCount(chans);

		buffbytes += FrameSize-1;
		buffbytes -= buffbytes%FrameSize;
		Data.Resize(buffbytes);

		return true;
	}

};


#define AREA_SOUND_RADIUS  (32.f)

#define PITCH_MULT (0.7937005f) /* Approx. 4 semitones lower; what Nash suggested */

static float GetRolloff(const FRolloffInfo *rolloff, float distance)
{
	return soundEngine->GetRolloff(rolloff, distance);
}

ALCdevice *OpenALSoundRenderer::InitDevice()
{
	ALCdevice *device = NULL;
	if (IsOpenALPresent())
	{
		if(strcmp(snd_aldevice, "Default") != 0)
		{
			device = alcOpenDevice(*snd_aldevice);
			if(!device)
				Printf(TEXTCOLOR_BLUE" Failed to open device " TEXTCOLOR_BOLD"%s" TEXTCOLOR_BLUE". Trying default.\n", *snd_aldevice);
		}

		if(!device)
		{
			device = alcOpenDevice(NULL);
			if(!device)
			{
				Printf(TEXTCOLOR_RED" Could not open audio device\n");
			}
		}
	}
	else
	{
		Printf(TEXTCOLOR_ORANGE"Failed to load openal32.dll\n");
	}
	return device;
}


template<typename T>
static void LoadALFunc(const char *name, T *x)
{
	*x = reinterpret_cast<T>(alGetProcAddress(name));
}

template<typename T>
static void LoadALCFunc(ALCdevice *device, const char *name, T *x)
{
	*x = reinterpret_cast<T>(alcGetProcAddress(device, name));
}

#define LOAD_FUNC(x)  (LoadALFunc(#x, &x))
#define LOAD_DEV_FUNC(d, x)  (LoadALCFunc(d, #x, &x))
OpenALSoundRenderer::OpenALSoundRenderer()
	: QuitThread(false), Device(NULL), Context(NULL), SFXPaused(0), PrevEnvironment(NULL), EnvSlot(0)
{
	EnvFilters[0] = EnvFilters[1] = 0;

	Printf("I_InitSound: Initializing OpenAL\n");

	Device = InitDevice();
	if (Device == NULL) return;

	ALC.EXT_EFX = !!alcIsExtensionPresent(Device, "ALC_EXT_EFX");
	ALC.EXT_disconnect = !!alcIsExtensionPresent(Device, "ALC_EXT_disconnect");
	ALC.SOFT_HRTF = !!alcIsExtensionPresent(Device, "ALC_SOFT_HRTF");
	ALC.SOFT_pause_device = !!alcIsExtensionPresent(Device, "ALC_SOFT_pause_device");
	ALC.SOFT_output_limiter = !!alcIsExtensionPresent(Device, "ALC_SOFT_output_limiter");

	const ALCchar *current = NULL;
	if(alcIsExtensionPresent(Device, "ALC_ENUMERATE_ALL_EXT"))
		current = alcGetString(Device, ALC_ALL_DEVICES_SPECIFIER);
	if(alcGetError(Device) != ALC_NO_ERROR || !current)
		current = alcGetString(Device, ALC_DEVICE_SPECIFIER);
	Printf("  Opened device " TEXTCOLOR_ORANGE"%s\n", current);

	ALCint major=0, minor=0;
	alcGetIntegerv(Device, ALC_MAJOR_VERSION, 1, &major);
	alcGetIntegerv(Device, ALC_MINOR_VERSION, 1, &minor);
	DPrintf(DMSG_SPAMMY, "  ALC Version: " TEXTCOLOR_BLUE"%d.%d\n", major, minor);
	DPrintf(DMSG_SPAMMY, "  ALC Extensions: " TEXTCOLOR_ORANGE"%s\n", alcGetString(Device, ALC_EXTENSIONS));

	TArray<ALCint> attribs;
	if(*snd_samplerate > 0)
	{
		attribs.Push(ALC_FREQUENCY);
		attribs.Push(*snd_samplerate);
	}
	// Make sure one source is capable of stereo output with the rest doing
	// mono, without running out of voices
	attribs.Push(ALC_MONO_SOURCES);
	attribs.Push(max<ALCint>(snd_channels, 2) - 1);
	attribs.Push(ALC_STEREO_SOURCES);
	attribs.Push(1);
	if(ALC.SOFT_HRTF)
	{
		attribs.Push(ALC_HRTF_SOFT);
		if(*snd_hrtf == 0)
			attribs.Push(ALC_FALSE);
		else if(*snd_hrtf > 0)
			attribs.Push(ALC_TRUE);
		else
			attribs.Push(ALC_DONT_CARE_SOFT);
	}
	if(ALC.SOFT_output_limiter)
	{
		attribs.Push(ALC_OUTPUT_LIMITER_SOFT);
		attribs.Push(ALC_TRUE);
	}
	// Other attribs..?
	attribs.Push(0);

	Context = alcCreateContext(Device, &attribs[0]);
	if(!Context || alcMakeContextCurrent(Context) == ALC_FALSE)
	{
		Printf(TEXTCOLOR_RED"  Failed to setup context: %s\n", alcGetString(Device, alcGetError(Device)));
		if(Context)
			alcDestroyContext(Context);
		Context = NULL;
		alcCloseDevice(Device);
		Device = NULL;
		return;
	}
	attribs.Clear();

	const ALchar *const version = alGetString(AL_VERSION);

	if (strstr(version, "ALSOFT") == nullptr)
	{
		Printf(TEXTCOLOR_RED "  You are using an unsupported OpenAL implementation\n"
			"  Install OpenAL Soft library for a better experience\n");
	}

	DPrintf(DMSG_SPAMMY, "  Vendor: " TEXTCOLOR_ORANGE"%s\n", alGetString(AL_VENDOR));
	DPrintf(DMSG_SPAMMY, "  Renderer: " TEXTCOLOR_ORANGE"%s\n", alGetString(AL_RENDERER));
	DPrintf(DMSG_SPAMMY, "  Version: " TEXTCOLOR_ORANGE"%s\n", version);
	DPrintf(DMSG_SPAMMY, "  Extensions: " TEXTCOLOR_ORANGE"%s\n", alGetString(AL_EXTENSIONS));

	AL.EXT_source_distance_model = !!alIsExtensionPresent("AL_EXT_source_distance_model");
	AL.EXT_SOURCE_RADIUS = !!alIsExtensionPresent("AL_EXT_SOURCE_RADIUS");
	AL.SOFT_deferred_updates = !!alIsExtensionPresent("AL_SOFT_deferred_updates");
	AL.SOFT_loop_points = !!alIsExtensionPresent("AL_SOFT_loop_points");
	AL.SOFT_source_latency = !!alIsExtensionPresent("AL_SOFT_source_latency");
	AL.SOFT_source_resampler = !!alIsExtensionPresent("AL_SOFT_source_resampler");
	AL.SOFT_source_spatialize = !!alIsExtensionPresent("AL_SOFT_source_spatialize");

	// Speed of sound is in units per second. Presuming we want to simulate a
	// typical speed of sound of 343.3 meters per second, multiply it by the
	// units per meter scale (1), and set the meters per unit to the scale's
	// reciprocal. It's important to set these correctly for both doppler
	// effects and reverb.
	alSpeedOfSound(343.3f);
	if(ALC.EXT_EFX)
		alListenerf(AL_METERS_PER_UNIT, 1.0f);

	alDistanceModel(AL_INVERSE_DISTANCE);
	if(AL.EXT_source_distance_model)
		alEnable(AL_SOURCE_DISTANCE_MODEL);

	if(AL.SOFT_deferred_updates)
	{
		LOAD_FUNC(alDeferUpdatesSOFT);
		LOAD_FUNC(alProcessUpdatesSOFT);
	}
	else
	{
		alDeferUpdatesSOFT = _wrap_DeferUpdatesSOFT;
		alProcessUpdatesSOFT = _wrap_ProcessUpdatesSOFT;
	}

	if(AL.SOFT_source_latency)
		LOAD_FUNC(alGetSourcei64vSOFT);
	if(AL.SOFT_source_resampler)
		LOAD_FUNC(alGetStringiSOFT);

	if(ALC.SOFT_pause_device)
	{
		LOAD_DEV_FUNC(Device, alcDevicePauseSOFT);
		LOAD_DEV_FUNC(Device, alcDeviceResumeSOFT);
	}

	ALenum err = getALError();
	if(err != AL_NO_ERROR)
	{
		alcMakeContextCurrent(NULL);
		alcDestroyContext(Context);
		Context = NULL;
		alcCloseDevice(Device);
		Device = NULL;
		return;
	}

	ALCint numMono=0, numStereo=0;
	alcGetIntegerv(Device, ALC_MONO_SOURCES, 1, &numMono);
	alcGetIntegerv(Device, ALC_STEREO_SOURCES, 1, &numStereo);

	// OpenAL specification doesn't require alcGetIntegerv() to return
	// meaningful values for ALC_MONO_SOURCES and ALC_MONO_SOURCES.
	// At least Apple's OpenAL implementation returns zeroes,
	// although it can generate reasonable number of sources.

	const int numChannels = max<int>(snd_channels, 2);
	int numSources = numMono + numStereo;

	if (0 == numSources)
	{
		numSources = numChannels;
	}

	Sources.Resize(min<int>(numChannels, numSources));
	for(unsigned i = 0;i < Sources.Size();i++)
	{
		alGenSources(1, &Sources[i]);
		if(getALError() != AL_NO_ERROR)
		{
			Sources.Resize(i);
			Sources.ShrinkToFit();
			break;
		}
	}
	if(Sources.Size() == 0)
	{
		Printf(TEXTCOLOR_RED" Error: could not generate any sound sources!\n");
		alcMakeContextCurrent(NULL);
		alcDestroyContext(Context);
		Context = NULL;
		alcCloseDevice(Device);
		Device = NULL;
		return;
	}
	FreeSfx = Sources;
	DPrintf(DMSG_NOTIFY, "  Allocated " TEXTCOLOR_BLUE"%u" TEXTCOLOR_NORMAL" sources\n", Sources.Size());

	WasInWater = false;
	if(*snd_efx && ALC.EXT_EFX)
	{
		// EFX function pointers
		LOAD_FUNC(alGenEffects);
		LOAD_FUNC(alDeleteEffects);
		LOAD_FUNC(alIsEffect);
		LOAD_FUNC(alEffecti);
		LOAD_FUNC(alEffectiv);
		LOAD_FUNC(alEffectf);
		LOAD_FUNC(alEffectfv);
		LOAD_FUNC(alGetEffecti);
		LOAD_FUNC(alGetEffectiv);
		LOAD_FUNC(alGetEffectf);
		LOAD_FUNC(alGetEffectfv);

		LOAD_FUNC(alGenFilters);
		LOAD_FUNC(alDeleteFilters);
		LOAD_FUNC(alIsFilter);
		LOAD_FUNC(alFilteri);
		LOAD_FUNC(alFilteriv);
		LOAD_FUNC(alFilterf);
		LOAD_FUNC(alFilterfv);
		LOAD_FUNC(alGetFilteri);
		LOAD_FUNC(alGetFilteriv);
		LOAD_FUNC(alGetFilterf);
		LOAD_FUNC(alGetFilterfv);

		LOAD_FUNC(alGenAuxiliaryEffectSlots);
		LOAD_FUNC(alDeleteAuxiliaryEffectSlots);
		LOAD_FUNC(alIsAuxiliaryEffectSlot);
		LOAD_FUNC(alAuxiliaryEffectSloti);
		LOAD_FUNC(alAuxiliaryEffectSlotiv);
		LOAD_FUNC(alAuxiliaryEffectSlotf);
		LOAD_FUNC(alAuxiliaryEffectSlotfv);
		LOAD_FUNC(alGetAuxiliaryEffectSloti);
		LOAD_FUNC(alGetAuxiliaryEffectSlotiv);
		LOAD_FUNC(alGetAuxiliaryEffectSlotf);
		LOAD_FUNC(alGetAuxiliaryEffectSlotfv);
		if(getALError() == AL_NO_ERROR)
		{
			ALuint envReverb;
			alGenEffects(1, &envReverb);
			if(getALError() == AL_NO_ERROR)
			{
				alEffecti(envReverb, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
				if(alGetError() == AL_NO_ERROR)
					DPrintf(DMSG_SPAMMY, "  EAX Reverb found\n");
				alEffecti(envReverb, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
				if(alGetError() == AL_NO_ERROR)
					DPrintf(DMSG_SPAMMY, "  Standard Reverb found\n");

				alDeleteEffects(1, &envReverb);
				getALError();
			}

			alGenAuxiliaryEffectSlots(1, &EnvSlot);
			alGenFilters(2, EnvFilters);
			if(getALError() == AL_NO_ERROR)
			{
				alFilteri(EnvFilters[0], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
				alFilteri(EnvFilters[1], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
				if(getALError() == AL_NO_ERROR)
					DPrintf(DMSG_SPAMMY, "  Lowpass found\n");
				else
				{
					alDeleteFilters(2, EnvFilters);
					EnvFilters[0] = EnvFilters[1] = 0;
					alDeleteAuxiliaryEffectSlots(1, &EnvSlot);
					EnvSlot = 0;
					getALError();
				}
			}
			else
			{
				alDeleteFilters(2, EnvFilters);
				alDeleteAuxiliaryEffectSlots(1, &EnvSlot);
				EnvFilters[0] = EnvFilters[1] = 0;
				EnvSlot = 0;
				getALError();
			}
		}
	}

	if(EnvSlot)
		Printf("  EFX enabled\n");

	if(AL.SOFT_source_resampler && strcmp(*snd_alresampler, "Default") != 0)
	{
		const ALint num_resamplers = alGetInteger(AL_NUM_RESAMPLERS_SOFT);
		ALint ridx = alGetInteger(AL_DEFAULT_RESAMPLER_SOFT);
		ALint i;

		for(i = 0;i < num_resamplers;i++)
		{
			if(strcmp(alGetStringiSOFT(AL_RESAMPLER_NAME_SOFT, i), *snd_alresampler) == 0)
			{
				ridx = i;
				break;
			}
		}
		if(i == num_resamplers)
			Printf(TEXTCOLOR_RED" Failed to find resampler " TEXTCOLOR_ORANGE"%s\n", *snd_alresampler);
		else for(ALint src : Sources)
			alSourcei(src, AL_SOURCE_RESAMPLER_SOFT, ridx);
	}
}
#undef LOAD_DEV_FUNC
#undef LOAD_FUNC

OpenALSoundRenderer::~OpenALSoundRenderer()
{
	if(!Device)
		return;

	if(StreamThread.joinable())
	{
		std::unique_lock<std::mutex> lock(StreamLock);
		QuitThread.store(true);
		lock.unlock();
		StreamWake.notify_all();
		StreamThread.join();
	}

	while(Streams.Size() > 0)
		delete Streams[0];

	alDeleteSources(Sources.Size(), &Sources[0]);
	Sources.Clear();
	FreeSfx.Clear();
	SfxGroup.Clear();
	PausableSfx.Clear();
	ReverbSfx.Clear();

	if(EnvEffects.CountUsed() > 0)
	{
		EffectMapIter iter(EnvEffects);
		EffectMap::Pair *pair;
		while(iter.NextPair(pair))
			alDeleteEffects(1, &(pair->Value));
	}
	EnvEffects.Clear();

	if(EnvSlot)
	{
		alDeleteAuxiliaryEffectSlots(1, &EnvSlot);
		alDeleteFilters(2, EnvFilters);
	}
	EnvSlot = 0;
	EnvFilters[0] = EnvFilters[1] = 0;

	alcMakeContextCurrent(NULL);
	alcDestroyContext(Context);
	Context = NULL;
	alcCloseDevice(Device);
	Device = NULL;
}

void OpenALSoundRenderer::BackgroundProc()
{
	std::unique_lock<std::mutex> lock(StreamLock);
	while(!QuitThread.load())
	{
		if(Streams.Size() == 0)
		{
			// If there's nothing to play, wait indefinitely.
			StreamWake.wait(lock);
		}
		else
		{
			// Else, process all active streams and sleep for 100ms
			for(size_t i = 0;i < Streams.Size();i++)
				Streams[i]->Process();
			StreamWake.wait_for(lock, std::chrono::milliseconds(100));
		}
	}
}

void OpenALSoundRenderer::AddStream(OpenALSoundStream *stream)
{
	std::unique_lock<std::mutex> lock(StreamLock);
	Streams.Push(stream);
	lock.unlock();
	// There's a stream to play, make sure the background thread is aware
	StreamWake.notify_all();
}

void OpenALSoundRenderer::RemoveStream(OpenALSoundStream *stream)
{
	std::unique_lock<std::mutex> lock(StreamLock);
	unsigned int idx = Streams.Find(stream);
	if(idx < Streams.Size())
		Streams.Delete(idx);
}

void OpenALSoundRenderer::SetSfxVolume(float volume)
{
	SfxVolume = volume;

	if (!soundEngine) return;
	FSoundChan *schan = soundEngine->GetChannels();
	while(schan)
	{
		if(schan->SysChannel != NULL)
		{
			ALuint source = GET_PTRID(schan->SysChannel);
			volume = SfxVolume;

			alDeferUpdatesSOFT();
			alSourcef(source, AL_MAX_GAIN, volume);
			alSourcef(source, AL_GAIN, volume * schan->Volume);
		}
		schan = schan->NextChan;
	}

	alProcessUpdatesSOFT();

	getALError();
}

void OpenALSoundRenderer::SetMusicVolume(float volume)
{
	MusicVolume = volume;
	for(uint32_t i = 0;i < Streams.Size();++i)
		Streams[i]->UpdateVolume();
}

unsigned int OpenALSoundRenderer::GetMSLength(SoundHandle sfx)
{
	if(sfx.data)
	{
		ALuint buffer = GET_PTRID(sfx.data);
		if(alIsBuffer(buffer))
		{
			ALint bits, channels, freq, size;
			alGetBufferi(buffer, AL_BITS, &bits);
			alGetBufferi(buffer, AL_CHANNELS, &channels);
			alGetBufferi(buffer, AL_FREQUENCY, &freq);
			alGetBufferi(buffer, AL_SIZE, &size);
			if(getALError() == AL_NO_ERROR)
				return (unsigned int)(size / (channels*bits/8) * 1000. / freq);
		}
	}
	return 0;
}

unsigned int OpenALSoundRenderer::GetSampleLength(SoundHandle sfx)
{
	if(sfx.data)
	{
		ALuint buffer = GET_PTRID(sfx.data);
		ALint bits, channels, size;
		alGetBufferi(buffer, AL_BITS, &bits);
		alGetBufferi(buffer, AL_CHANNELS, &channels);
		alGetBufferi(buffer, AL_SIZE, &size);
		if(getALError() == AL_NO_ERROR)
			return (ALsizei)(size / (channels * bits / 8));
	}
	return 0;
}

float OpenALSoundRenderer::GetOutputRate()
{
	ALCint rate = 44100; // Default, just in case
	alcGetIntegerv(Device, ALC_FREQUENCY, 1, &rate);
	return (float)rate;
}


SoundHandle OpenALSoundRenderer::LoadSoundRaw(uint8_t *sfxdata, int length, int frequency, int channels, int bits, int loopstart, int loopend)
{
	SoundHandle retval = { NULL };

	if(length == 0) return retval;

	if(bits == -8)
	{
		// Simple signed->unsigned conversion
		for(int i = 0;i < length;i++)
			sfxdata[i] ^= 0x80;
		bits = -bits;
	}

	ALenum format = AL_NONE;
	if(bits == 16)
	{
		if(channels == 1) format = AL_FORMAT_MONO16;
		if(channels == 2) format = AL_FORMAT_STEREO16;
	}
	else if(bits == 8)
	{
		if(channels == 1) format = AL_FORMAT_MONO8;
		if(channels == 2) format = AL_FORMAT_STEREO8;
	}

	if(format == AL_NONE || frequency <= 0)
	{
		Printf("Unhandled format: %d bit, %d channel, %d hz\n", bits, channels, frequency);
		return retval;
	}
	length -= length%(channels*bits/8);

	ALenum err;
	ALuint buffer = 0;
	alGenBuffers(1, &buffer);
	alBufferData(buffer, format, sfxdata, length, frequency);
	if((err=getALError()) != AL_NO_ERROR)
	{
		Printf("Failed to buffer data: %s\n", alGetString(err));
		alDeleteBuffers(1, &buffer);
		getALError();
		return retval;
	}

	if((loopstart > 0 || loopend > 0) && AL.SOFT_loop_points)
	{
		if(loopstart < 0)
			loopstart = 0;
		if(loopend < loopstart)
			loopend = length / (channels*bits/8);

		ALint loops[2] = { loopstart, loopend };
		DPrintf(DMSG_NOTIFY, "Setting loop points %d -> %d\n", loops[0], loops[1]);
		alBufferiv(buffer, AL_LOOP_POINTS_SOFT, loops);
		getALError();
	}
	else if(loopstart > 0 || loopend > 0)
	{
		static bool warned = false;
		if(!warned)
			Printf(DMSG_WARNING, "Loop points not supported!\n");
		warned = true;
	}

	retval.data = MAKE_PTRID(buffer);
	return retval;
}

SoundHandle OpenALSoundRenderer::LoadSound(uint8_t *sfxdata, int length, int def_loop_start, int def_loop_end)
{
	SoundHandle retval = { NULL };
	uint32_t loop_start = 0, loop_end = ~0u;
	zmusic_bool startass = false, endass = false;

	if (def_loop_start < 0)
	{
		FindLoopTags(sfxdata, length, &loop_start, &startass, &loop_end, &endass);
	}
	else
	{
		loop_start = def_loop_start;
		loop_end = def_loop_end;
		startass = endass = true;
	}
	auto decoder = CreateDecoder(sfxdata, length, true);
	if (!decoder)
		return retval;

	ChannelConfig chans;
	SampleType type;
	int srate;
	SoundDecoder_GetInfo(decoder, &srate, &chans, &type);

	ALenum format = GetFormat(type, chans);
	if (format == AL_NONE)
	{
		SoundDecoder_Close(decoder);
		Printf("Unsupported audio format: %s, %s\n", GetChannelConfigName(chans),
			GetSampleTypeName(type));
		return retval;
	}
	int samplesize = SampleTypeSize(type) * ChannelCount(chans);

	TArray<uint8_t> data;
	unsigned total = 0;
	unsigned got;

	data.resize(total + 32768);
	while ((got = (unsigned)SoundDecoder_Read(decoder, (char*)&data[total], data.size() - total)) > 0)
	{
		total += got;
		data.resize(total * 2);
	}
	data.resize(total);
	if (total == 0)
	{
		return retval;
	}
	SoundDecoder_Close(decoder);

	ALenum err;
	ALuint buffer = 0;
	alGenBuffers(1, &buffer);
	alBufferData(buffer, format, &data[0], (ALsizei)data.size(), srate);
	if((err=getALError()) != AL_NO_ERROR)
	{
		Printf("Failed to buffer data: %s\n", alGetString(err));
		alDeleteBuffers(1, &buffer);
		getALError();
		return retval;
	}

	if (!startass) loop_start = Scale(loop_start, srate, 1000);
	if (!endass && loop_end != ~0u) loop_end = Scale(loop_end, srate, 1000);
	const uint32_t samples = (uint32_t)data.size() / samplesize;
	if (loop_start > samples) loop_start = 0;
	if (loop_end > samples) loop_end = samples;

	if ((loop_start > 0 || loop_end > 0) && loop_end > loop_start && AL.SOFT_loop_points)
	{
		ALint loops[2] = { static_cast<ALint>(loop_start), static_cast<ALint>(loop_end) };
		DPrintf(DMSG_NOTIFY, "Setting loop points %d -> %d\n", loops[0], loops[1]);
		alBufferiv(buffer, AL_LOOP_POINTS_SOFT, loops);
		// no console messages here, please!
	}

	retval.data = MAKE_PTRID(buffer);
	return retval;
}

void OpenALSoundRenderer::UnloadSound(SoundHandle sfx)
{
	if(!sfx.data)
		return;

	ALuint buffer = GET_PTRID(sfx.data);
	FSoundChan *schan = soundEngine->GetChannels();
	while(schan)
	{
		if(schan->SysChannel)
		{
			ALint bufID = 0;
			alGetSourcei(GET_PTRID(schan->SysChannel), AL_BUFFER, &bufID);
			if((ALuint)bufID == buffer)
			{
				FSoundChan *next = schan->NextChan;
				StopChannel(schan);
				schan = next;
				continue;
			}
		}
		schan = schan->NextChan;
	}

	alDeleteBuffers(1, &buffer);
	getALError();
}


SoundStream *OpenALSoundRenderer::CreateStream(SoundStreamCallback callback, int buffbytes, SampleType stype, ChannelConfig chans, int samplerate, void *userdata)
{
	if(StreamThread.get_id() == std::thread::id())
		StreamThread = std::thread(std::mem_fn(&OpenALSoundRenderer::BackgroundProc), this);
	OpenALSoundStream *stream = new OpenALSoundStream(this);
	if (!stream->Init(callback, buffbytes, stype, chans, samplerate, userdata))
	{
		delete stream;
		return NULL;
	}
	return stream;
}

FISoundChannel *OpenALSoundRenderer::StartSound(SoundHandle sfx, float vol, float pitch, int chanflags, FISoundChannel *reuse_chan, float startTime)
{
	if(FreeSfx.Size() == 0)
	{
		FSoundChan *lowest = FindLowestChannel();
		if(lowest) StopChannel(lowest);

		if(FreeSfx.Size() == 0)
			return NULL;
	}

	ALuint buffer = GET_PTRID(sfx.data);
	ALuint source = FreeSfx.Last();
	alSource3f(source, AL_POSITION, 0.f, 0.f, 0.f);
	alSource3f(source, AL_VELOCITY, 0.f, 0.f, 0.f);
	alSource3f(source, AL_DIRECTION, 0.f, 0.f, 0.f);
	alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);

	alSourcei(source, AL_LOOPING, (chanflags&SNDF_LOOP) ? AL_TRUE : AL_FALSE);

	alSourcef(source, AL_REFERENCE_DISTANCE, 1.f);
	alSourcef(source, AL_MAX_DISTANCE, 1000.f);
	alSourcef(source, AL_DOPPLER_FACTOR, 0.f);
	alSourcef(source, AL_ROLLOFF_FACTOR, 0.f);
	alSourcef(source, AL_MAX_GAIN, SfxVolume);
	alSourcef(source, AL_GAIN, SfxVolume*vol);
	if(AL.EXT_SOURCE_RADIUS)
		alSourcef(source, AL_SOURCE_RADIUS, 0.f);
	if(AL.SOFT_source_spatialize)
		alSourcei(source, AL_SOURCE_SPATIALIZE_SOFT, AL_AUTO_SOFT);

	if(EnvSlot)
	{
		if(!(chanflags&SNDF_NOREVERB))
		{
			alSourcei(source, AL_DIRECT_FILTER, EnvFilters[0]);
			alSource3i(source, AL_AUXILIARY_SEND_FILTER, EnvSlot, 0, EnvFilters[1]);
		}
		else
		{
			alSourcei(source, AL_DIRECT_FILTER, AL_FILTER_NULL);
			alSource3i(source, AL_AUXILIARY_SEND_FILTER, 0, 0, AL_FILTER_NULL);
		}
		alSourcef(source, AL_ROOM_ROLLOFF_FACTOR, 0.f);
	}
	if(WasInWater && !(chanflags&SNDF_NOREVERB))
		alSourcef(source, AL_PITCH, pitch * PITCH_MULT);
	else
		alSourcef(source, AL_PITCH, pitch);

	if(!reuse_chan || reuse_chan->StartTime == 0)
	{
		float st = (chanflags&SNDF_LOOP) ? fmod(startTime, (float)GetMSLength(sfx) / 1000.f) : clamp<float>(startTime, 0.f, (float)GetMSLength(sfx) / 1000.f);
		alSourcef(source, AL_SEC_OFFSET, st);
	}
	else
	{
		if((chanflags&SNDF_ABSTIME))
			alSourcei(source, AL_SAMPLE_OFFSET, ALint(reuse_chan->StartTime));
		else
		{
			float offset = std::chrono::duration_cast<std::chrono::duration<float>>(
				std::chrono::steady_clock::now().time_since_epoch() -
				std::chrono::steady_clock::time_point::duration(reuse_chan->StartTime)
			).count();
			if(offset > 0.f) alSourcef(source, AL_SEC_OFFSET, offset);
		}
	}
	if(getALError() != AL_NO_ERROR)
		return NULL;

	alSourcei(source, AL_BUFFER, buffer);
	if((chanflags&SNDF_NOPAUSE) || !SFXPaused)
		alSourcePlay(source);
	if(getALError() != AL_NO_ERROR)
	{
		alSourcei(source, AL_BUFFER, 0);
		getALError();
		return NULL;
	}

	if(!(chanflags&SNDF_NOREVERB))
		ReverbSfx.Push(source);
	if(!(chanflags&SNDF_NOPAUSE))
		PausableSfx.Push(source);
	SfxGroup.Push(source);
	FreeSfx.Pop();

	FISoundChannel *chan = reuse_chan;
	if(!chan) chan = soundEngine->GetChannel(MAKE_PTRID(source));
	else chan->SysChannel = MAKE_PTRID(source);

	chan->Rolloff.RolloffType = ROLLOFF_Log;
	chan->Rolloff.RolloffFactor = 0.f;
	chan->Rolloff.MinDistance = 1.f;
	chan->DistanceSqr = 0.f;
	chan->ManualRolloff = false;

	return chan;
}

FISoundChannel *OpenALSoundRenderer::StartSound3D(SoundHandle sfx, SoundListener *listener, float vol,
	FRolloffInfo *rolloff, float distscale, float pitch, int priority, const FVector3 &pos, const FVector3 &vel,
	int channum, int chanflags, FISoundChannel *reuse_chan, float startTime)
{
	float dist_sqr = (float)(pos - listener->position).LengthSquared();

	if(FreeSfx.Size() == 0)
	{
		FSoundChan *lowest = FindLowestChannel();
		if(lowest)
		{
			if(lowest->Priority < priority || (lowest->Priority == priority &&
			                                   lowest->DistanceSqr > dist_sqr))
				StopChannel(lowest);
		}
		if(FreeSfx.Size() == 0)
			return NULL;
	}

	bool manualRolloff = true;
	ALuint buffer = GET_PTRID(sfx.data);
	ALuint source = FreeSfx.Last();
	if(rolloff->RolloffType == ROLLOFF_Log)
	{
		if(AL.EXT_source_distance_model)
			alSourcei(source, AL_DISTANCE_MODEL, AL_INVERSE_DISTANCE);
		alSourcef(source, AL_REFERENCE_DISTANCE, rolloff->MinDistance/distscale);
		alSourcef(source, AL_MAX_DISTANCE, std::numeric_limits<float>::max());
		alSourcef(source, AL_ROLLOFF_FACTOR, rolloff->RolloffFactor);
		manualRolloff = false;
	}
	else if(rolloff->RolloffType == ROLLOFF_Linear && AL.EXT_source_distance_model)
	{
		alSourcei(source, AL_DISTANCE_MODEL, AL_LINEAR_DISTANCE);
		alSourcef(source, AL_REFERENCE_DISTANCE, rolloff->MinDistance/distscale);
		alSourcef(source, AL_MAX_DISTANCE, rolloff->MaxDistance/distscale);
		alSourcef(source, AL_ROLLOFF_FACTOR, 1.f);
		manualRolloff = false;
	}
	if(manualRolloff)
	{
		// How manual rolloff works:
		//
		// If a sound is using Custom or Doom style rolloff, or Linear style
		// when AL_EXT_source_distance_model is not supported, we have to play
		// around a bit to get appropriate distance attenation. What we do is
		// calculate the attenuation that should be applied, then given an
		// Inverse Distance rolloff model with OpenAL, calculate the reference
		// distance that will achieve that much attenuation with the current
		// distance. The Inverse Distance calculation is:
		//
		// Gain = MinDist / (MinDist + RolloffFactor*(Distance - MinDist))
		//
		// Simplifying for RolloffFactor=1, it can be broken down by:
		//
		// Gain = MinDist / (MinDist + (Distance - MinDist))
		// Gain = MinDist / Distance
		// Gain * Distance = MinDist
		//
		// The source's reference distance is then set according to the desired
		// gain and effective distance from the listener, and OpenAL takes care
		// of the rest.
		if(AL.EXT_source_distance_model)
			alSourcei(source, AL_DISTANCE_MODEL, AL_INVERSE_DISTANCE);

		float dist = sqrtf(dist_sqr);
		float gain = GetRolloff(rolloff, dist * distscale);
		// Don't let the ref distance go to 0, or else distance attenuation is
		// lost with the inverse distance model.
		alSourcef(source, AL_REFERENCE_DISTANCE, max<float>(gain*dist, 0.0004f));
		alSourcef(source, AL_MAX_DISTANCE, std::numeric_limits<float>::max());
		alSourcef(source, AL_ROLLOFF_FACTOR, 1.f);
	}

	if(dist_sqr < (0.0004f*0.0004f))
	{
		// Head relative
		alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(source, AL_POSITION, 0.f, 0.f, 0.f);
	}
	else
	{
		alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
		alSource3f(source, AL_POSITION, pos[0], pos[1], -pos[2]);
	}
	alSource3f(source, AL_VELOCITY, vel[0], vel[1], -vel[2]);
	alSource3f(source, AL_DIRECTION, 0.f, 0.f, 0.f);
	alSourcef(source, AL_DOPPLER_FACTOR, 0.f);
	if(AL.EXT_SOURCE_RADIUS)
		alSourcef(source, AL_SOURCE_RADIUS, (chanflags&SNDF_AREA) ? AREA_SOUND_RADIUS : 0.f);

	alSourcei(source, AL_LOOPING, (chanflags&SNDF_LOOP) ? AL_TRUE : AL_FALSE);

	alSourcef(source, AL_MAX_GAIN, SfxVolume);
	alSourcef(source, AL_GAIN, SfxVolume*vol);
	if(AL.SOFT_source_spatialize)
		alSourcei(source, AL_SOURCE_SPATIALIZE_SOFT, AL_TRUE);

	if(EnvSlot)
	{
		if(!(chanflags&SNDF_NOREVERB))
		{
			alSourcei(source, AL_DIRECT_FILTER, EnvFilters[0]);
			alSource3i(source, AL_AUXILIARY_SEND_FILTER, EnvSlot, 0, EnvFilters[1]);
		}
		else
		{
			alSourcei(source, AL_DIRECT_FILTER, AL_FILTER_NULL);
			alSource3i(source, AL_AUXILIARY_SEND_FILTER, 0, 0, AL_FILTER_NULL);
		}
		alSourcef(source, AL_ROOM_ROLLOFF_FACTOR, 0.f);
	}
	if(WasInWater && !(chanflags&SNDF_NOREVERB))
		alSourcef(source, AL_PITCH, pitch * PITCH_MULT);
	else
		alSourcef(source, AL_PITCH, pitch);

	if(!reuse_chan || reuse_chan->StartTime == 0)
	{
		float sfxlength = (float)GetMSLength(sfx) / 1000.f;
		float st = (chanflags & SNDF_LOOP)
				? (sfxlength > 0 ? fmod(startTime, sfxlength) : 0)
				: clamp<float>(startTime, 0.f, sfxlength);
		alSourcef(source, AL_SEC_OFFSET, st);
	}
	else
	{
		if((chanflags&SNDF_ABSTIME))
			alSourcei(source, AL_SAMPLE_OFFSET, ALint(reuse_chan->StartTime));
		else
		{
			float offset = std::chrono::duration_cast<std::chrono::duration<float>>(
				std::chrono::steady_clock::now().time_since_epoch() -
				std::chrono::steady_clock::time_point::duration(reuse_chan->StartTime)
			).count();
			if(offset > 0.f) alSourcef(source, AL_SEC_OFFSET, offset);
		}
	}
	if(getALError() != AL_NO_ERROR)
		return NULL;

	alSourcei(source, AL_BUFFER, buffer);
	if((chanflags&SNDF_NOPAUSE) || !SFXPaused)
		alSourcePlay(source);
	if(getALError() != AL_NO_ERROR)
	{
		alSourcei(source, AL_BUFFER, 0);
		getALError();
		return NULL;
	}

	if(!(chanflags&SNDF_NOREVERB))
		ReverbSfx.Push(source);
	if(!(chanflags&SNDF_NOPAUSE))
		PausableSfx.Push(source);
	SfxGroup.Push(source);
	FreeSfx.Pop();

	FISoundChannel *chan = reuse_chan;
	if(!chan) chan = soundEngine->GetChannel(MAKE_PTRID(source));
	else chan->SysChannel = MAKE_PTRID(source);

	chan->Rolloff = *rolloff;
	chan->DistanceSqr = dist_sqr;
	chan->ManualRolloff = manualRolloff;

	return chan;
}

void OpenALSoundRenderer::ChannelVolume(FISoundChannel *chan, float volume)
{
	if(chan == NULL || chan->SysChannel == NULL)
		return;

	alDeferUpdatesSOFT();

	ALuint source = GET_PTRID(chan->SysChannel);
	alSourcef(source, AL_GAIN, SfxVolume * volume);
}

void OpenALSoundRenderer::ChannelPitch(FISoundChannel *chan, float pitch)
{
	if (chan == NULL || chan->SysChannel == NULL)
		return;

	alDeferUpdatesSOFT();

	ALuint source = GET_PTRID(chan->SysChannel);
	if (WasInWater && !(chan->ChanFlags & CHANF_UI))
		alSourcef(source, AL_PITCH, max(pitch, 0.0001f)*PITCH_MULT);
	else
		alSourcef(source, AL_PITCH, max(pitch, 0.0001f));
}

void OpenALSoundRenderer::StopChannel(FISoundChannel *chan)
{
	if(chan == NULL || chan->SysChannel == NULL)
		return;

	ALuint source = GET_PTRID(chan->SysChannel);
	// Release first, so it can be properly marked as evicted if it's being killed
	soundEngine->ChannelEnded(chan);

	alSourceRewind(source);
	alSourcei(source, AL_BUFFER, 0);
	getALError();

	uint32_t i;
	if((i=PausableSfx.Find(source)) < PausableSfx.Size())
		PausableSfx.Delete(i);
	if((i=ReverbSfx.Find(source)) < ReverbSfx.Size())
		ReverbSfx.Delete(i);
	if((i=SfxGroup.Find(source)) < SfxGroup.Size())
		SfxGroup.Delete(i);

	if (!(chan->ChanFlags & CHANF_EVICTED))
		soundEngine->SoundDone(chan);

	FreeSfx.Push(source);
}


unsigned int OpenALSoundRenderer::GetPosition(FISoundChannel *chan)
{
	if(chan == NULL || chan->SysChannel == NULL)
		return 0;

	ALint pos;
	alGetSourcei(GET_PTRID(chan->SysChannel), AL_SAMPLE_OFFSET, &pos);
	if(getALError() == AL_NO_ERROR)
		return pos;
	return 0;
}


void OpenALSoundRenderer::SetSfxPaused(bool paused, int slot)
{
	int oldslots = SFXPaused;

	if(paused)
	{
		SFXPaused |= 1 << slot;
		if(oldslots == 0 && PausableSfx.Size() > 0)
		{
			alSourcePausev(PausableSfx.Size(), &PausableSfx[0]);
			getALError();
			PurgeStoppedSources();
		}
	}
	else
	{
		SFXPaused &= ~(1 << slot);
		if(SFXPaused == 0 && oldslots != 0 && PausableSfx.Size() > 0)
		{
			alSourcePlayv(PausableSfx.Size(), &PausableSfx[0]);
			getALError();
		}
	}
}

void OpenALSoundRenderer::SetInactive(SoundRenderer::EInactiveState state)
{
	switch(state)
	{
		case SoundRenderer::INACTIVE_Active:
			alListenerf(AL_GAIN, 1.0f);
			if(ALC.SOFT_pause_device)
			{
				alcDeviceResumeSOFT(Device);
				getALCError(Device);
			}
			break;

		case SoundRenderer::INACTIVE_Complete:
			if(ALC.SOFT_pause_device)
			{
				alcDevicePauseSOFT(Device);
				getALCError(Device);
			}
			/* fall-through */
		case SoundRenderer::INACTIVE_Mute:
			alListenerf(AL_GAIN, 0.0f);
			break;
	}
}

void OpenALSoundRenderer::Sync(bool sync)
{
	if(sync)
	{
		if(SfxGroup.Size() > 0)
		{
			alSourcePausev(SfxGroup.Size(), &SfxGroup[0]);
			getALError();
			PurgeStoppedSources();
		}
	}
	else
	{
		// Might already be something to handle this; basically, get a vector
		// of all values in SfxGroup that are not also in PausableSfx (when
		// SFXPaused is non-0).
		TArray<ALuint> toplay = SfxGroup;
		if(SFXPaused)
		{
			uint32_t i = 0;
			while(i < toplay.Size())
			{
				uint32_t p = PausableSfx.Find(toplay[i]);
				if(p < PausableSfx.Size())
					toplay.Delete(i);
				else
					i++;
			}
		}
		if(toplay.Size() > 0)
		{
			alSourcePlayv(toplay.Size(), &toplay[0]);
			getALError();
		}
	}
}

void OpenALSoundRenderer::UpdateSoundParams3D(SoundListener *listener, FISoundChannel *chan, bool areasound, const FVector3 &pos, const FVector3 &vel)
{
	if(chan == NULL || chan->SysChannel == NULL)
		return;

	float dist_sqr = (float)(pos - listener->position).LengthSquared();
	chan->DistanceSqr = dist_sqr;

	alDeferUpdatesSOFT();
	ALuint source = GET_PTRID(chan->SysChannel);

	if(dist_sqr < (0.0004f*0.0004f))
	{
		alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(source, AL_POSITION, 0.f, 0.f, 0.f);
	}
	else
	{
		if(chan->ManualRolloff)
		{
			float dist = sqrtf(dist_sqr);
			float gain = GetRolloff(&chan->Rolloff, dist * chan->DistanceScale);
			alSourcef(source, AL_REFERENCE_DISTANCE, max<float>(gain*dist, 0.0004f));
		}

		alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
		alSource3f(source, AL_POSITION, pos[0], pos[1], -pos[2]);
	}
	alSource3f(source, AL_VELOCITY, vel[0], vel[1], -vel[2]);
	getALError();
}

void OpenALSoundRenderer::UpdateListener(SoundListener *listener)
{
	if(!listener->valid)
		return;

	alDeferUpdatesSOFT();

	float angle = listener->angle;
	ALfloat orient[6];
	// forward
	orient[0] = cosf(angle);
	orient[1] = 0.f;
	orient[2] = -sinf(angle);
	// up
	orient[3] = 0.f;
	orient[4] = 1.f;
	orient[5] = 0.f;

	alListenerfv(AL_ORIENTATION, orient);
	alListener3f(AL_POSITION, listener->position.X,
	                          listener->position.Y,
	                         -listener->position.Z);
	alListener3f(AL_VELOCITY, listener->velocity.X,
	                          listener->velocity.Y,
	                         -listener->velocity.Z);
	getALError();

	const ReverbContainer *env = ForcedEnvironment;
	if(!env)
	{
		env = listener->Environment;
		if(!env)
			env = DefaultEnvironments[0];
	}
	if(env != PrevEnvironment || env->Modified)
	{
		PrevEnvironment = env;
		DPrintf(DMSG_NOTIFY, "Reverb Environment %s\n", env->Name);

		if(EnvSlot != 0)
			LoadReverb(env);

		const_cast<ReverbContainer*>(env)->Modified = false;
	}

	// NOTE: Moving into and out of water will undo pitch variations on sounds.
	if(listener->underwater || env->SoftwareWater)
	{
		if(!WasInWater)
		{
			WasInWater = true;

			if(EnvSlot != 0 && *snd_waterreverb)
			{
				// Find the "Underwater" reverb environment
				env = S_FindEnvironment(0x1600);
				LoadReverb(env ? env : DefaultEnvironments[0]);

				alFilterf(EnvFilters[0], AL_LOWPASS_GAIN, 1.f);
				alFilterf(EnvFilters[0], AL_LOWPASS_GAINHF, 0.125f);
				alFilterf(EnvFilters[1], AL_LOWPASS_GAIN, 1.f);
				alFilterf(EnvFilters[1], AL_LOWPASS_GAINHF, 1.f);

				// Apply the updated filters on the sources
				FSoundChan *schan = soundEngine->GetChannels();
				while (schan)
				{
					ALuint source = GET_PTRID(schan->SysChannel);
					if (source && !(schan->ChanFlags & CHANF_UI))
					{
						alSourcei(source, AL_DIRECT_FILTER, EnvFilters[0]);
						alSource3i(source, AL_AUXILIARY_SEND_FILTER, EnvSlot, 0, EnvFilters[1]);
					}
					schan = schan->NextChan;
				}
			}

			FSoundChan *schan = soundEngine->GetChannels();
			while (schan)
			{
				ALuint source = GET_PTRID(schan->SysChannel);
				if (source && !(schan->ChanFlags & CHANF_UI))
					alSourcef(source, AL_PITCH, schan->Pitch * PITCH_MULT);
				schan = schan->NextChan;
			}
			getALError();
		}
	}
	else if(WasInWater)
	{

		WasInWater = false;

		if(EnvSlot != 0)
		{
			LoadReverb(env);

			alFilterf(EnvFilters[0], AL_LOWPASS_GAIN, 1.f);
			alFilterf(EnvFilters[0], AL_LOWPASS_GAINHF, 1.f);
			alFilterf(EnvFilters[1], AL_LOWPASS_GAIN, 1.f);
			alFilterf(EnvFilters[1], AL_LOWPASS_GAINHF, 1.f);

			FSoundChan *schan = soundEngine->GetChannels();
			while (schan)
			{
				ALuint source = GET_PTRID(schan->SysChannel);
				if (source && !(schan->ChanFlags & CHANF_UI))
				{
					alSourcei(source, AL_DIRECT_FILTER, EnvFilters[0]);
					alSource3i(source, AL_AUXILIARY_SEND_FILTER, EnvSlot, 0, EnvFilters[1]);
				}
				schan = schan->NextChan;
			}
		}

		FSoundChan *schan = soundEngine->GetChannels();
		while (schan)
		{
			ALuint source = GET_PTRID(schan->SysChannel);
			if (source && !(schan->ChanFlags & CHANF_UI))
				alSourcef(source, AL_PITCH, schan->Pitch);
			schan = schan->NextChan;
		}
		getALError();
	}
}

void OpenALSoundRenderer::UpdateSounds()
{
	alProcessUpdatesSOFT();

	if(ALC.EXT_disconnect)
	{
		ALCint connected = ALC_TRUE;
		alcGetIntegerv(Device, ALC_CONNECTED, 1, &connected);
		if(connected == ALC_FALSE)
		{
			Printf("Sound device disconnected; restarting...\n");
			S_SoundReset();
			return;
		}
	}

	PurgeStoppedSources();
}

bool OpenALSoundRenderer::IsValid()
{
	return Device != NULL;
}

void OpenALSoundRenderer::MarkStartTime(FISoundChannel *chan, float startTime)
{
	// FIXME: Get current time (preferably from the audio clock, but the system
	// time will have to do)
	using namespace std::chrono;
	auto startTimeDuration = duration<double>(startTime);
	auto diff = steady_clock::now().time_since_epoch() - startTimeDuration;
	chan->StartTime = static_cast<uint64_t>(duration_cast<nanoseconds>(diff).count());
}

float OpenALSoundRenderer::GetAudibility(FISoundChannel *chan)
{
	if(chan == NULL || chan->SysChannel == NULL)
		return 0.f;

	ALuint source = GET_PTRID(chan->SysChannel);
	ALfloat volume = 0.f;

	alGetSourcef(source, AL_GAIN, &volume);
	getALError();

	volume *= GetRolloff(&chan->Rolloff, sqrtf(chan->DistanceSqr) * chan->DistanceScale);
	return volume;
}


void OpenALSoundRenderer::PrintStatus()
{
	Printf("Output device: " TEXTCOLOR_ORANGE"%s\n", alcGetString(Device, ALC_DEVICE_SPECIFIER));
	getALCError(Device);

	ALCint frequency, major, minor, mono, stereo;
	alcGetIntegerv(Device, ALC_FREQUENCY, 1, &frequency);
	alcGetIntegerv(Device, ALC_MAJOR_VERSION, 1, &major);
	alcGetIntegerv(Device, ALC_MINOR_VERSION, 1, &minor);
	alcGetIntegerv(Device, ALC_MONO_SOURCES, 1, &mono);
	alcGetIntegerv(Device, ALC_STEREO_SOURCES, 1, &stereo);
	if(getALCError(Device) == AL_NO_ERROR)
	{
		Printf("Device sample rate: " TEXTCOLOR_BLUE"%d" TEXTCOLOR_NORMAL"hz\n", frequency);
		Printf("ALC Version: " TEXTCOLOR_BLUE"%d.%d\n", major, minor);
		Printf("ALC Extensions: " TEXTCOLOR_ORANGE"%s\n", alcGetString(Device, ALC_EXTENSIONS));
		Printf("Available sources: " TEXTCOLOR_BLUE"%d" TEXTCOLOR_NORMAL" (" TEXTCOLOR_BLUE"%d" TEXTCOLOR_NORMAL" mono, " TEXTCOLOR_BLUE"%d" TEXTCOLOR_NORMAL" stereo)\n", mono+stereo, mono, stereo);
	}
	if(!alcIsExtensionPresent(Device, "ALC_EXT_EFX"))
		Printf("EFX not found\n");
	else
	{
		ALCint sends;
		alcGetIntegerv(Device, ALC_EFX_MAJOR_VERSION, 1, &major);
		alcGetIntegerv(Device, ALC_EFX_MINOR_VERSION, 1, &minor);
		alcGetIntegerv(Device, ALC_MAX_AUXILIARY_SENDS, 1, &sends);
		if(getALCError(Device) == AL_NO_ERROR)
		{
			Printf("EFX Version: " TEXTCOLOR_BLUE"%d.%d\n", major, minor);
			Printf("Auxiliary sends: " TEXTCOLOR_BLUE"%d\n", sends);
		}
	}
	Printf("Vendor: " TEXTCOLOR_ORANGE"%s\n", alGetString(AL_VENDOR));
	Printf("Renderer: " TEXTCOLOR_ORANGE"%s\n", alGetString(AL_RENDERER));
	Printf("Version: " TEXTCOLOR_ORANGE"%s\n", alGetString(AL_VERSION));
	Printf("Extensions: " TEXTCOLOR_ORANGE"%s\n", alGetString(AL_EXTENSIONS));
	getALError();
}

FString OpenALSoundRenderer::GatherStats()
{
	FString out;

	ALCint refresh = 1;
	alcGetIntegerv(Device, ALC_REFRESH, 1, &refresh);
	getALCError(Device);

	uint32_t total = Sources.Size();
	uint32_t used = SfxGroup.Size()+Streams.Size();
	uint32_t unused = FreeSfx.Size();

	out.Format("%u sources (" TEXTCOLOR_YELLOW"%u" TEXTCOLOR_NORMAL" active, " TEXTCOLOR_YELLOW"%u" TEXTCOLOR_NORMAL" free), Update interval: " TEXTCOLOR_YELLOW"%.1f" TEXTCOLOR_NORMAL"ms",
			   total, used, unused, 1000.f/static_cast<float>(refresh));
	return out;
}

void OpenALSoundRenderer::PrintDriversList()
{
	const ALCchar *drivers = (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT") ?
							  alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER) :
							  alcGetString(NULL, ALC_DEVICE_SPECIFIER));
	if(drivers == NULL)
	{
		Printf(TEXTCOLOR_YELLOW"Failed to retrieve device list: %s\n", alcGetString(NULL, alcGetError(NULL)));
		return;
	}

	const ALCchar *current = NULL;
	if(alcIsExtensionPresent(Device, "ALC_ENUMERATE_ALL_EXT"))
		current = alcGetString(Device, ALC_ALL_DEVICES_SPECIFIER);
	if(alcGetError(Device) != ALC_NO_ERROR || !current)
		current = alcGetString(Device, ALC_DEVICE_SPECIFIER);
	if(current == NULL)
	{
		Printf(TEXTCOLOR_YELLOW"Failed to retrieve device name: %s\n", alcGetString(Device, alcGetError(Device)));
		return;
	}

	Printf("%c%s%2d. %s\n", ' ', ((strcmp(snd_aldevice, "Default") == 0) ? TEXTCOLOR_BOLD : ""), 0,
		   "Default");
	for(int i = 1;*drivers;i++)
	{
		Printf("%c%s%2d. %s\n", ((strcmp(current, drivers)==0) ? '*' : ' '),
			   ((strcmp(*snd_aldevice, drivers)==0) ? TEXTCOLOR_BOLD : ""), i,
			   drivers);
		drivers += strlen(drivers)+1;
	}
}

void OpenALSoundRenderer::PurgeStoppedSources()
{
	// Release channels that are stopped
	for(uint32_t i = 0;i < SfxGroup.Size();++i)
	{
		ALuint src = SfxGroup[i];
		ALint state = AL_INITIAL;
		alGetSourcei(src, AL_SOURCE_STATE, &state);
		if(state == AL_INITIAL || state == AL_PLAYING || state == AL_PAUSED)
			continue;

		FSoundChan *schan = soundEngine->GetChannels();
		while(schan)
		{
			if(schan->SysChannel != NULL && src == GET_PTRID(schan->SysChannel))
			{
				StopChannel(schan);
				break;
			}
			schan = schan->NextChan;
		}
	}
	getALError();
}

void OpenALSoundRenderer::LoadReverb(const ReverbContainer *env)
{
	ALuint *envReverb = EnvEffects.CheckKey(env->ID);
	bool doLoad = (env->Modified || !envReverb);

	if(!envReverb)
	{
		bool ok = false;

		envReverb = &EnvEffects.Insert(env->ID, 0);
		alGenEffects(1, envReverb);
		if(getALError() == AL_NO_ERROR)
		{
			alEffecti(*envReverb, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
			ok = (alGetError() == AL_NO_ERROR);
			if(!ok)
			{
				alEffecti(*envReverb, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
				ok = (alGetError() == AL_NO_ERROR);
			}
			if(!ok)
			{
				alEffecti(*envReverb, AL_EFFECT_TYPE, AL_EFFECT_NULL);
				ok = (alGetError() == AL_NO_ERROR);
			}
			if(!ok)
			{
				alDeleteEffects(1, envReverb);
				getALError();
			}
		}
		if(!ok)
		{
			*envReverb = 0;
			doLoad = false;
		}
	}

	if(doLoad)
	{
		const REVERB_PROPERTIES &props = env->Properties;
		ALint type = AL_EFFECT_NULL;

		alGetEffecti(*envReverb, AL_EFFECT_TYPE, &type);
#define mB2Gain(x) ((float)pow(10., (x)/2000.))
		if(type == AL_EFFECT_EAXREVERB)
		{
			ALfloat reflectpan[3] = { props.ReflectionsPan0,
			                          props.ReflectionsPan1,
			                          props.ReflectionsPan2 };
			ALfloat latepan[3] = { props.ReverbPan0, props.ReverbPan1,
			                       props.ReverbPan2 };
#undef SETPARAM
#define SETPARAM(e,t,v) alEffectf((e), AL_EAXREVERB_##t, clamp((v), AL_EAXREVERB_MIN_##t, AL_EAXREVERB_MAX_##t))
			SETPARAM(*envReverb, DIFFUSION, props.EnvDiffusion);
			SETPARAM(*envReverb, DENSITY, powf(props.EnvSize, 3.0f) * 0.0625f);
			SETPARAM(*envReverb, GAIN, mB2Gain(props.Room));
			SETPARAM(*envReverb, GAINHF, mB2Gain(props.RoomHF));
			SETPARAM(*envReverb, GAINLF, mB2Gain(props.RoomLF));
			SETPARAM(*envReverb, DECAY_TIME, props.DecayTime);
			SETPARAM(*envReverb, DECAY_HFRATIO, props.DecayHFRatio);
			SETPARAM(*envReverb, DECAY_LFRATIO, props.DecayLFRatio);
			SETPARAM(*envReverb, REFLECTIONS_GAIN, mB2Gain(props.Reflections));
			SETPARAM(*envReverb, REFLECTIONS_DELAY, props.ReflectionsDelay);
			alEffectfv(*envReverb, AL_EAXREVERB_REFLECTIONS_PAN, reflectpan);
			SETPARAM(*envReverb, LATE_REVERB_GAIN, mB2Gain(props.Reverb));
			SETPARAM(*envReverb, LATE_REVERB_DELAY, props.ReverbDelay);
			alEffectfv(*envReverb, AL_EAXREVERB_LATE_REVERB_PAN, latepan);
			SETPARAM(*envReverb, ECHO_TIME, props.EchoTime);
			SETPARAM(*envReverb, ECHO_DEPTH, props.EchoDepth);
			SETPARAM(*envReverb, MODULATION_TIME, props.ModulationTime);
			SETPARAM(*envReverb, MODULATION_DEPTH, props.ModulationDepth);
			SETPARAM(*envReverb, AIR_ABSORPTION_GAINHF, mB2Gain(props.AirAbsorptionHF));
			SETPARAM(*envReverb, HFREFERENCE, props.HFReference);
			SETPARAM(*envReverb, LFREFERENCE, props.LFReference);
			SETPARAM(*envReverb, ROOM_ROLLOFF_FACTOR, props.RoomRolloffFactor);
			alEffecti(*envReverb, AL_EAXREVERB_DECAY_HFLIMIT,
					  (props.Flags&REVERB_FLAGS_DECAYHFLIMIT)?AL_TRUE:AL_FALSE);
#undef SETPARAM
		}
		else if(type == AL_EFFECT_REVERB)
		{
#define SETPARAM(e,t,v) alEffectf((e), AL_REVERB_##t, clamp((v), AL_REVERB_MIN_##t, AL_REVERB_MAX_##t))
			SETPARAM(*envReverb, DIFFUSION, props.EnvDiffusion);
			SETPARAM(*envReverb, DENSITY, powf(props.EnvSize, 3.0f) * 0.0625f);
			SETPARAM(*envReverb, GAIN, mB2Gain(props.Room));
			SETPARAM(*envReverb, GAINHF, mB2Gain(props.RoomHF));
			SETPARAM(*envReverb, DECAY_TIME, props.DecayTime);
			SETPARAM(*envReverb, DECAY_HFRATIO, props.DecayHFRatio);
			SETPARAM(*envReverb, REFLECTIONS_GAIN, mB2Gain(props.Reflections));
			SETPARAM(*envReverb, REFLECTIONS_DELAY, props.ReflectionsDelay);
			SETPARAM(*envReverb, LATE_REVERB_GAIN, mB2Gain(props.Reverb));
			SETPARAM(*envReverb, LATE_REVERB_DELAY, props.ReverbDelay);
			SETPARAM(*envReverb, AIR_ABSORPTION_GAINHF, mB2Gain(props.AirAbsorptionHF));
			SETPARAM(*envReverb, ROOM_ROLLOFF_FACTOR, props.RoomRolloffFactor);
			alEffecti(*envReverb, AL_REVERB_DECAY_HFLIMIT,
					  (props.Flags&REVERB_FLAGS_DECAYHFLIMIT)?AL_TRUE:AL_FALSE);
#undef SETPARAM
		}
#undef mB2Gain
	}

	alAuxiliaryEffectSloti(EnvSlot, AL_EFFECTSLOT_EFFECT, *envReverb);
	getALError();
}

FSoundChan *OpenALSoundRenderer::FindLowestChannel()
{
	FSoundChan *schan = soundEngine->GetChannels();
	FSoundChan *lowest = NULL;
	while(schan)
	{
		if(schan->SysChannel != NULL)
		{
			if(!lowest || schan->Priority < lowest->Priority ||
			   (schan->Priority == lowest->Priority &&
				schan->DistanceSqr > lowest->DistanceSqr))
				lowest = schan;
		}
		schan = schan->NextChan;
	}
	return lowest;
}

#endif // NO_OPENAL

#include "menu.h"

void I_BuildALDeviceList(FOptionValues* opt)
{
	opt->mValues.Resize(1);
	opt->mValues[0].TextValue = "Default";
	opt->mValues[0].Text = "Default";

#ifndef NO_OPENAL
	if (IsOpenALPresent())
	{
		const ALCchar* names = (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT") ?
			alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER) :
			alcGetString(NULL, ALC_DEVICE_SPECIFIER));
		if (!names)
			Printf("Failed to get device list: %s\n", alcGetString(NULL, alcGetError(NULL)));
		else while (*names)
		{
			unsigned int i = opt->mValues.Reserve(1);
			opt->mValues[i].TextValue = names;
			opt->mValues[i].Text = names;

			names += strlen(names) + 1;
		}
	}
#endif
}

void I_BuildALResamplersList(FOptionValues* opt)
{
	opt->mValues.Resize(1);
	opt->mValues[0].TextValue = "Default";
	opt->mValues[0].Text = "Default";

#ifndef NO_OPENAL
	if (!IsOpenALPresent())
		return;
	if (!alcGetCurrentContext() || !alIsExtensionPresent("AL_SOFT_source_resampler"))
		return;

	LPALGETSTRINGISOFT alGetStringiSOFT = reinterpret_cast<LPALGETSTRINGISOFT>(alGetProcAddress("alGetStringiSOFT"));
	ALint num_resamplers = alGetInteger(AL_NUM_RESAMPLERS_SOFT);

	unsigned int idx = opt->mValues.Reserve(num_resamplers);
	for (ALint i = 0; i < num_resamplers; ++i)
	{
		const ALchar* name = alGetStringiSOFT(AL_RESAMPLER_NAME_SOFT, i);
		opt->mValues[idx].TextValue = name;
		opt->mValues[idx].Text = name;
		++idx;
	}
#endif
}

