/*
** i_sound.cpp
** Stubs for sound interfaces.
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

#include <stdio.h>
#include <stdlib.h>

#include "oalsound.h"

#include "i_module.h"
#include "cmdlib.h"

#include "c_dispatch.h"
#include "i_music.h"
#include "m_argv.h"
#include "v_text.h"
#include "c_cvars.h"
#include "stats.h"
#include <zmusic.h>


EXTERN_CVAR (Float, snd_sfxvolume)
EXTERN_CVAR(Float, snd_musicvolume)
CUSTOM_CVAR(Int, snd_samplerate, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self != 0 && self != 8000 && self != 11025 && self != 22050 && self != 32000 && self != 44100 && self != 48000)
	{
		self = 0;
		return;
	}
}
CVAR(Int, snd_buffersize, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, snd_hrtf, -1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

#if !defined(NO_OPENAL)	
#define DEF_BACKEND "openal"
#else
#define DEF_BACKEND "null"
#endif

CVAR(String, snd_backend, DEF_BACKEND, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

SoundRenderer *GSnd;
bool nosound;
bool nosfx;

void I_CloseSound ();


//
// SFX API
//

//==========================================================================
//
// CVAR snd_mastervolume
//
// Maximum volume of all audio
//==========================================================================

CUSTOM_CVAR(Float, snd_mastervolume, 1.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < 0.f)
		self = 0.f;
	else if (self > 1.f)
		self = 1.f;

	ChangeMusicSetting(zmusic_snd_mastervolume, nullptr, self);
	snd_sfxvolume->Callback();
	snd_musicvolume->Callback();
}

//==========================================================================
//
// CVAR snd_sfxvolume
//
// Maximum volume of a sound effect.
//==========================================================================

CUSTOM_CVAR (Float, snd_sfxvolume, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOINITCALL)
{
	if (self < 0.f)
		self = 0.f;
	else if (self > 1.f)
		self = 1.f;
	else if (GSnd != NULL)
	{
		GSnd->SetSfxVolume (self * snd_mastervolume);
	}
}

class MIDIStreamer;

class NullSoundRenderer : public SoundRenderer
{
public:
	virtual bool IsNull() { return true; }
	void SetSfxVolume (float volume)
	{
	}
	void SetMusicVolume (float volume)
	{
	}
	SoundHandle LoadSound(uint8_t *sfxdata, int length, int def_loop_start, int def_loop_end)
	{
		SoundHandle retval = { NULL };
		return retval;
	}
	SoundHandle LoadSoundRaw(uint8_t *sfxdata, int length, int frequency, int channels, int bits, int loopstart, int loopend)
	{
		SoundHandle retval = { NULL };
        return retval;
	}
	void UnloadSound (SoundHandle sfx)
	{
	}
	unsigned int GetMSLength(SoundHandle sfx)
	{
		// Return something that isn't 0. This is only used by some
		// ambient sounds to specify a default minimum period.
		return 250;
	}
	unsigned int GetSampleLength(SoundHandle sfx)
	{
		return 0;
	}
	float GetOutputRate()
	{
		return 11025;	// Lies!
	}
	void StopChannel(FISoundChannel *chan)
	{
	}
	void ChannelVolume(FISoundChannel *, float)
	{
	}
	void ChannelPitch(FISoundChannel *, float)
	{
	}

	// Streaming sounds.
	SoundStream *CreateStream (SoundStreamCallback callback, int buffbytes, SampleType stype, ChannelConfig chans, int samplerate, void *userdata)
	{
		return NULL;
	}

	// Starts a sound.
	FISoundChannel *StartSound (SoundHandle sfx, float vol, float pitch, int chanflags, FISoundChannel *reuse_chan, float startTime)
	{
		return NULL;
	}
	FISoundChannel *StartSound3D (SoundHandle sfx, SoundListener *listener, float vol, FRolloffInfo *rolloff, float distscale, float pitch, int priority, const FVector3 &pos, const FVector3 &vel, int channum, int chanflags, FISoundChannel *reuse_chan, float startTime)
	{
		return NULL;
	}

	// Marks a channel's start time without actually playing it.
	void MarkStartTime (FISoundChannel *chan, float startTime)
	{
	}

	// Returns position of sound on this channel, in samples.
	unsigned int GetPosition(FISoundChannel *chan)
	{
		return 0;
	}

	// Gets a channel's audibility (real volume).
	float GetAudibility(FISoundChannel *chan)
	{
		return 0;
	}

	// Synchronizes following sound startups.
	void Sync (bool sync)
	{
	}

	// Pauses or resumes all sound effect channels.
	void SetSfxPaused (bool paused, int slot)
	{
	}

	// Pauses or resumes *every* channel, including environmental reverb.
	void SetInactive(SoundRenderer::EInactiveState inactive)
	{
	}

	// Updates the volume, separation, and pitch of a sound channel.
	void UpdateSoundParams3D (SoundListener *listener, FISoundChannel *chan, bool areasound, const FVector3 &pos, const FVector3 &vel)
	{
	}

	void UpdateListener (SoundListener *)
	{
	}
	void UpdateSounds ()
	{
	}

	bool IsValid ()
	{
		return true;
	}
	void PrintStatus ()
	{
		Printf("Null sound module active.\n");
	}
	void PrintDriversList ()
	{
		Printf("Null sound module uses no drivers.\n");
	}
	FString GatherStats ()
	{
		return "Null sound module has no stats.";
	}
};

void I_InitSound ()
{
	FModule_SetProgDir(progdir.GetChars());
	/* Get command line options: */
	nosound = !!Args->CheckParm ("-nosound");
	nosfx = !!Args->CheckParm ("-nosfx");

	GSnd = NULL;
	if (nosound)
	{
		GSnd = new NullSoundRenderer;
		return;
	}

	// Keep it simple: let everything except "null" init the sound.
	if (stricmp(snd_backend, "null") == 0)
	{
		GSnd = new NullSoundRenderer;
	}
	else
	{
		#ifndef NO_OPENAL
			if (IsOpenALPresent())
			{
				GSnd = new OpenALSoundRenderer;
			}
		#endif
	}
	if (!GSnd || !GSnd->IsValid ())
	{
		I_CloseSound();
		GSnd = new NullSoundRenderer;
		Printf (TEXTCOLOR_RED"Sound init failed. Using nosound.\n");
	}
	snd_sfxvolume->Callback ();
}


void I_CloseSound ()
{
	// Free all loaded samples. Beware that the sound engine may already have been deleted.
	if (soundEngine) soundEngine->UnloadAllSounds();

	delete GSnd;
	GSnd = NULL;
}

const char *GetSampleTypeName(SampleType type)
{
    switch(type)
    {
        case SampleType_UInt8: return "Unsigned 8-bit";
        case SampleType_Int16: return "Signed 16-bit";
        case SampleType_Float32: return "32-bit float";
        default: break;
    }
    return "(invalid sample type)";
}

const char *GetChannelConfigName(ChannelConfig chan)
{
    switch(chan)
    {
        case ChannelConfig_Mono: return "Mono";
        case ChannelConfig_Stereo: return "Stereo";
    }
    return "(invalid channel config)";
}

SoundRenderer::SoundRenderer ()
{
}

SoundRenderer::~SoundRenderer ()
{
}

FString SoundRenderer::GatherStats ()
{
	return "No stats for this sound renderer.";
}

void SoundRenderer::DrawWaveDebug(int mode)
{
}

FString SoundStream::GetStats()
{
	return "No stream stats available.";
}

//==========================================================================
//
// SoundRenderer :: LoadSoundVoc
//
//==========================================================================

SoundHandle SoundRenderer::LoadSoundVoc(uint8_t *sfxdata, int length)
{
	uint8_t * data = NULL;
	int len, frequency, channels, bits, loopstart, loopend;
	len = frequency = channels = bits = 0;
	loopstart = loopend = -1;
	do if (length > 26)
	{
		// First pass to parse data and validate the file
		if (strncmp ((const char *)sfxdata, "Creative Voice File", 19))
			break;
		int i = 26, blocktype = 0, blocksize = 0, codec = -1;
		bool noextra = true, okay = true;
		while (i < length)
		{
			// Read block header
			blocktype = sfxdata[i];
			if (blocktype == 0)
				break;
			blocksize = sfxdata[i+1] + (sfxdata[i+2]<<8) + (sfxdata[i+3]<<16);
			i += 4;
			if (i + blocksize > length)
			{
				//okay = false;
				break;
			}

			// Read block data
			switch (blocktype)
			{
			case 1: // Sound data
				if (/*noextra &*/ (codec == -1 || codec == sfxdata[i + 1])) // NAM contains a VOC where a valid data block follows an extra block.
				{
					frequency = 1000000 / (256 - sfxdata[i]);
					channels = 1;
					codec = sfxdata[i + 1];
					if (codec == 0)
						bits = 8;
					else if (codec == 4)
						bits = 16;
					else okay = false;
					len += blocksize - 2;
				}
				else okay = false;
				break;
			case 2: // Sound data continuation
				if (codec == -1)
					okay = false;
				len += blocksize;
				break;
			case 3: // Silence
				if (frequency == 1000000/(256 - sfxdata[i+2]))
				{
					int silength = 1 + sfxdata[i] + (sfxdata[i+1]<<8);
					if (codec == 0) // 8-bit unsigned PCM
						len += silength;
					else if (codec == 4) // 16-bit signed PCM
						len += silength<<1;
					else okay = false;
				} else okay = false;
				break;
			case 4: // Mark (ignored)
			case 5: // Text (ignored)
				break;
			case 6: // Repeat start
				loopstart = len;
				break;
			case 7: // Repeat end
				loopend = len;
				if (loopend < loopstart)
					okay = false;
				break;
			case 8: // Extra info
				noextra = false;
				if (codec == -1)
				{
					codec = sfxdata[i+2];
					channels = 1+sfxdata[i+3];
					frequency = 256000000/(channels * (65536 - (sfxdata[i]+(sfxdata[i+1]<<8))));
				} else okay = false;
				break;
			case 9: // Sound data in new format
				if (codec == -1)
				{
					frequency = sfxdata[i] + (sfxdata[i+1]<<8) + (sfxdata[i+2]<<16) + (sfxdata[i+3]<<24);
					bits = sfxdata[i+4];
					channels = sfxdata[i+5];
					codec = sfxdata[i+6] + (sfxdata[i+7]<<8);
					if (codec == 0)
						bits = 8;
					else if (codec == 4)
						bits = 16;
					else okay = false;
					len += blocksize - 12;
				} else okay = false;
				break;
			default: // Unknown block type
				okay = false;
				DPrintf (DMSG_ERROR, "Unknown VOC block type %i\n", blocktype);
				break;
			}
			// Move to next block
			i += blocksize;
		}

		// Second pass to write the data
		if (okay && len > 0)
		{
			data = new uint8_t[len];
			i = 26;
			int j = 0;
			while (i < length)
			{
				// Read block header again
				blocktype = sfxdata[i];
				if (blocktype == 0) break;
				blocksize = sfxdata[i+1] + (sfxdata[i+2]<<8) + (sfxdata[i+3]<<16);
				i += 4;
				switch (blocktype)
				{
				case 1: memcpy(data+j, sfxdata+i+2,  blocksize-2 ); j += blocksize-2;	break;
				case 2: memcpy(data+j, sfxdata+i,    blocksize   ); j += blocksize;		break;
				case 9: memcpy(data+j, sfxdata+i+12, blocksize-12); j += blocksize-12;	break;
				case 3:
					{
						int silength = 1 + sfxdata[i] + (sfxdata[i+1]<<8);
						if (bits == 8)
						{
							memset(data+j, 128, silength);
							j += silength;
						}
						else if (bits == -16)
						{
							memset(data+j, 0, silength<<1);
							j += silength<<1;
						}
					}
					break;
				default: break;
				}
				i += blocksize;
			}
		}

	} while (false);
	SoundHandle retval = LoadSoundRaw(data, len, frequency, channels, bits, loopstart, loopend);
	if (data) delete[] data;
	return retval;
}

