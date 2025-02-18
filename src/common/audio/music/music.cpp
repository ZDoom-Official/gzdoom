/*
**
** music.cpp
**
** music engine
**
** Copyright 1999-2016 Randy Heit
** Copyright 2002-2016 Christoph Oelckers
**
**---------------------------------------------------------------------------
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
#include <stdexcept>
#include <memory>

#include "i_sound.h"
#include "i_music.h"
#include "printf.h"
#include "s_playlist.h"
#include "c_dispatch.h"
#include "filesystem.h"
#include "cmdlib.h"
#include "s_music.h"
#include "filereadermusicinterface.h"
#include <zmusic.h>
#include "md5.h"
#include "gain_analysis.h"
#include "i_specialpaths.h"
#include "configfile.h"
#include "c_cvars.h"
#include "md5.h"


// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

extern int nomusic;
extern float S_GetMusicVolume (const char *music);

static void S_ActivatePlayList(bool goBack);

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static bool		MusicPaused;		// whether music is paused
MusPlayingInfo mus_playing;	// music currently being played
static FPlayList PlayList;
float	relative_volume = 1.f;
float	saved_relative_volume = 1.0f;	// this could be used to implement an ACS FadeMusic function
MusicVolumeMap MusicVolumes;
MidiDeviceMap MidiDevices;
TMap<int, int> ModPlayers;

static int DefaultFindMusic(const char* fn)
{
	return -1;
}

MusicCallbacks mus_cb = { nullptr, DefaultFindMusic };


// PUBLIC DATA DEFINITIONS -------------------------------------------------
EXTERN_CVAR(Bool, mus_enabled)
EXTERN_CVAR(Float, snd_musicvolume)
EXTERN_CVAR(Int, snd_mididevice)
EXTERN_CVAR(Float, mod_dumb_mastervolume)
EXTERN_CVAR(Float, fluid_gain)


CVAR(Bool, mus_calcgain, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) // changing this will only take effect for the next song.
CVAR(Bool, mus_usereplaygain, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) // changing this will only take effect for the next song.
CVAR(Int, mod_preferred_player, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)// toggle between libXMP and Dumb. Unlike other sound CVARs this is not directly mapped to ZMusic's config.

// CODE --------------------------------------------------------------------

//==========================================================================
//
// OpenMusic
//
// opens a FileReader for the music - used as a callback to keep
// implementation details out of the core player.
//
//==========================================================================

static FileReader OpenMusic(const char* musicname)
{
	FileReader reader;
	if (!FileExists(musicname))
	{
		int lumpnum;
		lumpnum = mus_cb.FindMusic(musicname);
		if (lumpnum == -1) lumpnum = fileSystem.CheckNumForName(musicname, FileSys::ns_music);
		if (lumpnum == -1)
		{
			Printf("Music \"%s\" not found\n", musicname);
		}
		else if (fileSystem.FileLength(lumpnum) != 0)
		{
			reader = fileSystem.ReopenFileReader(lumpnum);
		}
	}
	else
	{
		// Load an external file.
		reader.OpenFile(musicname);
	}
	return reader;
}

bool MusicExists(const char* music_name)
{
	if (music_name == nullptr)
		return false;

	if (FileExists(music_name))
		return true;
	else
	{
		int lumpnum;
		lumpnum = mus_cb.FindMusic(music_name);
		if (lumpnum == -1) lumpnum = fileSystem.CheckNumForName(music_name, FileSys::ns_music);
		if (lumpnum != -1 && fileSystem.FileLength(lumpnum) != 0)
			return true;
	}
	return false;
}

void S_SetMusicCallbacks(MusicCallbacks* cb)
{
	mus_cb = *cb;
	if (mus_cb.FindMusic == nullptr) mus_cb.FindMusic = DefaultFindMusic;	// without this we are dead in the water.
}

int MusicEnabled() // int return is for scripting
{
	return mus_enabled && !nomusic;
} 

//==========================================================================
//
// 
//
// Create a sound system stream for the currently playing song 
//==========================================================================

static std::unique_ptr<SoundStream> musicStream;
static TArray<SoundStream*> customStreams;

SoundStream *S_CreateCustomStream(size_t size, int samplerate, int numchannels, MusicCustomStreamType sampletype, StreamCallback cb, void *userdata)
{
	ChannelConfig chans;
	if(numchannels == 1)
		chans = ChannelConfig_Mono;
	else if(numchannels == 2)
		chans = ChannelConfig_Stereo;
	else
		return nullptr;
	const SampleType stype{(sampletype == MusicSamplesFloat) ? SampleType_Float32 : SampleType_Int16};

	auto stream = GSnd->CreateStream(cb, int(size), stype, chans, samplerate, userdata);
	if (stream)
	{
		stream->Play(true, 1);
		customStreams.Push(stream);
	}
	return stream;
}

void S_StopCustomStream(SoundStream *stream)
{
	if (stream)
	{
		stream->Stop();
		auto f = customStreams.Find(stream);
		if (f < customStreams.Size()) customStreams.Delete(f);
		delete stream;
	}
}

void S_PauseAllCustomStreams(bool on)
{
	static bool paused = false;

	if (paused == on) return;
	paused = on;
	for (auto s : customStreams)
	{
		s->SetPaused(on);
	}
}

static TArray<int16_t> convert;
static bool FillStream(SoundStream* stream, void* buff, int len, void* userdata)
{
	bool written;
	if (mus_playing.isfloat)
	{
		written = ZMusic_FillStream(mus_playing.handle, buff, len);
		if (mus_playing.musicVolume != 1.f)
		{
			float* fbuf = (float*)buff;
			for (int i = 0; i < len / 4; i++)
			{
				fbuf[i] *= mus_playing.musicVolume;
			}
		}
	}
	else
	{
		// To apply replay gain we need floating point streaming data, so 16 bit input needs to be converted here.
		convert.Resize(len / 2);
		written = ZMusic_FillStream(mus_playing.handle, convert.Data(), len/2);
		float* fbuf = (float*)buff;
		for (int i = 0; i < len / 4; i++)
		{
			fbuf[i] = convert[i] * mus_playing.musicVolume * (1.f/32768.f);
		}
	}

	if (!written)
	{
		memset((char*)buff, 0, len);
		return false;
	}
	return true;
}


void S_CreateStream()
{
	if (!mus_playing.handle) return;
	SoundStreamInfoEx fmt;
	ZMusic_GetStreamInfoEx(mus_playing.handle, &fmt);
	mus_playing.isfloat = fmt.mSampleType == SampleType_Float32;
	if (!mus_playing.isfloat) fmt.mBufferSize *= 2;
	if (fmt.mBufferSize > 0) // if buffer size is 0 the library will play the song itself (e.g. Windows system synth.)
	{
		// always create a floating point streaming buffer so we can apply replay gain without risk of integer overflows.
		musicStream.reset(GSnd->CreateStream(FillStream, fmt.mBufferSize, SampleType_Float32, fmt.mChannelConfig, fmt.mSampleRate, nullptr));
		if (musicStream) musicStream->Play(true, 1);
	}
}


void S_PauseStream(bool paused)
{
	if (musicStream) musicStream->SetPaused(paused);
}

void S_StopStream()
{
	if (musicStream)
	{
		musicStream->Stop();
		musicStream.reset();
	}
}


//==========================================================================
//
// starts playing this song
//
//==========================================================================

static bool S_StartMusicPlaying(ZMusic_MusicStream song, bool loop, float rel_vol, int subsong)
{
	if (rel_vol > 0.f && !mus_usereplaygain)
	{
		float factor = relative_volume / saved_relative_volume;
		saved_relative_volume = rel_vol;
		I_SetRelativeVolume(saved_relative_volume * factor);
	}
	ZMusic_Stop(song);
	// make sure the volume modifiers update properly in case replay gain settings have changed.
	fluid_gain->Callback();
	mod_dumb_mastervolume->Callback();
	if (!ZMusic_Start(song, subsong, loop))
	{
		return false;
	}

	// Notify the sound system of the changed relative volume
	snd_musicvolume->Callback();
	return true;
}


//==========================================================================
//
// S_PauseMusic
//
// Stop music, during game PAUSE.
//==========================================================================

void S_PauseMusic ()
{
	if (mus_playing.handle && !MusicPaused)
	{
		ZMusic_Pause(mus_playing.handle);
		S_PauseStream(true);
		MusicPaused = true;
	}
}

//==========================================================================
//
// S_ResumeMusic
//
// Resume music, after game PAUSE.
//==========================================================================

void S_ResumeMusic ()
{
	if (mus_playing.handle && MusicPaused)
	{
		ZMusic_Resume(mus_playing.handle);
		S_PauseStream(false);
		MusicPaused = false;
	}
}

//==========================================================================
//
// S_UpdateSound
//
//==========================================================================

void S_UpdateMusic ()
{
	if (mus_playing.handle != nullptr)
	{
		ZMusic_Update(mus_playing.handle);

		// [RH] Update music and/or playlist. IsPlaying() must be called
		// to attempt to reconnect to broken net streams and to advance the
		// playlist when the current song finishes.
		if (!ZMusic_IsPlaying(mus_playing.handle))
		{
			if (PlayList.GetNumSongs())
			{
				PlayList.Advance();
				S_ActivatePlayList(false);
			}
			else
			{
				S_StopMusic(true);
			}
		}
	}
}

//==========================================================================
//
// Resets the music player if music playback was paused.
//
//==========================================================================

void S_ResetMusic ()
{
	// stop the old music if it has been paused.
	// This ensures that the new music is started from the beginning
	// if it's the same as the last one and it has been paused.
	if (MusicPaused) S_StopMusic(true);

	// start new music for the level
	MusicPaused = false;
}


//==========================================================================
//
// S_ActivatePlayList
//
// Plays the next song in the playlist. If no songs in the playlist can be
// played, then it is deleted.
//==========================================================================

void S_ActivatePlayList (bool goBack)
{
	int startpos, pos;

	startpos = pos = PlayList.GetPosition ();
	S_StopMusic (true);
	while (!S_ChangeMusic (PlayList.GetSong (pos), 0, false, true))
	{
		pos = goBack ? PlayList.Backup () : PlayList.Advance ();
		if (pos == startpos)
		{
			PlayList.Clear();
			Printf ("Cannot play anything in the playlist.\n");
			return;
		}
	}
}

//==========================================================================
//
// S_StartMusic
//
// Starts some music with the given name.
//==========================================================================

bool S_StartMusic (const char *m_id)
{
	return S_ChangeMusic (m_id, 0, false);
}

//==========================================================================
//
// S_ChangeMusic
//
// initiates playback of a song
//
//==========================================================================
static TMap<FString, float> gainMap;

EXTERN_CVAR(String, fluid_patchset)
EXTERN_CVAR(String, timidity_config)
EXTERN_CVAR(String, midi_config)
EXTERN_CVAR(String, wildmidi_config)
EXTERN_CVAR(String, adl_custom_bank)
EXTERN_CVAR(Int, adl_bank)
EXTERN_CVAR(Bool, adl_use_custom_bank)
EXTERN_CVAR(String, opn_custom_bank)
EXTERN_CVAR(Bool, opn_use_custom_bank)
EXTERN_CVAR(Int, opl_core)

static FString ReplayGainHash(ZMusicCustomReader* reader, int flength, int playertype, const char* _playparam)
{
	std::string playparam = _playparam;

	TArray<uint8_t> buffer(50000, true);	// for performance reasons only hash the start of the file. If we wanted to do this to large waveform songs it'd cause noticable lag.
	uint8_t digest[16];
	char digestout[33];
	auto length = reader->read(reader, buffer.data(), 50000);
	reader->seek(reader, 0, SEEK_SET);
	MD5Context md5;
	md5.Init();
	md5.Update(buffer.data(), (int)length);
	md5.Final(digest);

	for (size_t j = 0; j < sizeof(digest); ++j)
	{
		snprintf(digestout + (j * 2), 3, "%02X", digest[j]);
	}
	digestout[32] = 0;

	auto type = ZMusic_IdentifyMIDIType((uint32_t*)buffer.data(), 32);
	if (type == MIDI_NOTMIDI) return FStringf("%d:%s", flength, digestout);

	// get the default for MIDI synth
	if (playertype == -1)
	{
		switch (snd_mididevice)
		{
		case -1:		playertype = MDEV_FLUIDSYNTH; break;
		case -2:		playertype = MDEV_TIMIDITY; break;
		case -3:		playertype = MDEV_OPL; break;
		case -4:		playertype = MDEV_GUS; break;
		case -5:		playertype = MDEV_FLUIDSYNTH; break;
		case -6:		playertype = MDEV_WILDMIDI; break;
		case -7:		playertype = MDEV_ADL; break;
		case -8:		playertype = MDEV_OPN; break;
		default:		return "";
		}
	}
	else if (playertype == MDEV_SNDSYS) return "";

	// get the default for used sound font.
	if (playparam.empty())
	{
		switch (playertype)
		{
		case MDEV_FLUIDSYNTH:		playparam = fluid_patchset; break;
		case MDEV_TIMIDITY:			playparam = timidity_config; break;
		case MDEV_GUS:				playparam = midi_config; break;
		case MDEV_WILDMIDI:			playparam = wildmidi_config; break;
		case MDEV_ADL:				playparam = adl_use_custom_bank ? *adl_custom_bank : std::to_string(adl_bank); break;
		case MDEV_OPN:				playparam = opn_use_custom_bank ? *opn_custom_bank : ""; break;
		case MDEV_OPL:				playparam = std::to_string(opl_core); break;

		}
	}
	return FStringf("%d:%s:%d:%s", flength, digestout, playertype, playparam.c_str()).MakeUpper();
}

static void SaveGains()
{
	auto path = M_GetAppDataPath(true);
	path << "/replaygain.ini";
	FConfigFile gains(path.GetChars());
	TMap<FString, float>::Iterator it(gainMap);
	TMap<FString, float>::Pair* pair;

	if (gains.SetSection("Gains", true))
	{
		while (it.NextPair(pair))
		{
			gains.SetValueForKey(pair->Key.GetChars(), std::to_string(pair->Value).c_str());
		}
	}
	gains.WriteConfigFile();
}

static void ReadGains()
{
	static bool done = false;
	if (done) return;
	done = true;
	auto path = M_GetAppDataPath(true);
	path << "/replaygain.ini";
	FConfigFile gains(path.GetChars());
	if (gains.SetSection("Gains"))
	{
		const char* key;
		const char* value;

		while (gains.NextInSection(key, value))
		{
			gainMap.Insert(key, (float)strtod(value, nullptr));
		}
	}
}

CCMD(setreplaygain)
{
	// sets replay gain for current song to a fixed value
	if (!mus_playing.handle || mus_playing.hash.IsEmpty())
	{
		Printf("setreplaygain needs some music playing\n");
		return;
	}
	if (argv.argc() < 2)
	{
		Printf("Usage: setreplaygain {dB}\n");
		Printf("Current replay gain is %f dB\n", AmplitudeTodB(mus_playing.musicVolume));
		return;
	}
	float dB = (float)strtod(argv[1], nullptr);
	if (dB > 10) dB = 10; // don't blast the speakers. Values above 2 or 3 are very rare.
	gainMap.Insert(mus_playing.hash, dB);
	SaveGains();
	mus_playing.musicVolume = (float)dBToAmplitude(dB);
}

static void CheckReplayGain(const char *musicname, EMidiDevice playertype, const char *playparam)
{
	mus_playing.musicVolume = 1;
	fluid_gain->Callback();
	mod_dumb_mastervolume->Callback();
	if (!mus_usereplaygain) return;

	FileReader reader = OpenMusic(musicname);
	if (!reader.isOpen()) return;
	int flength = (int)reader.GetLength();
	auto mreader = GetMusicReader(reader);	// this passes the file reader to the newly created wrapper.

	ReadGains();
	auto hash = ReplayGainHash(mreader, flength, playertype, playparam);
	if (hash.IsEmpty()) return; // got nothing to measure.
	mus_playing.hash = hash;
	auto entry = gainMap.CheckKey(hash);
	if (entry)
	{
		mus_playing.musicVolume = dBToAmplitude(*entry);
		return;
	}
	if (!mus_calcgain) return;

	auto handle = ZMusic_OpenSong(mreader, playertype, playparam);
	if (handle == nullptr) return; // not a music file

	if (!ZMusic_Start(handle, 0, false))
	{
		ZMusic_Close(handle);
		return; // unable to open
	}

	SoundStreamInfoEx fmt;
	ZMusic_GetStreamInfoEx(handle, &fmt);
	if (fmt.mBufferSize == 0)
	{
		ZMusic_Close(handle);
		return; // external player.
	}

	TArray<uint8_t> readbuffer(fmt.mBufferSize, true);
	TArray<float> lbuffer;
	TArray<float> rbuffer;
	while (ZMusic_FillStream(handle, readbuffer.Data(), fmt.mBufferSize))
	{
		unsigned index;
		// 4 cases, all with different preparation needs.
		if (fmt.mSampleType == SampleType_Int16 && fmt.mChannelConfig == ChannelConfig_Stereo) // 16 bit stereo
		{
			int16_t* sbuf = (int16_t*)readbuffer.Data();
			int numsamples = fmt.mBufferSize / 4;
			index = lbuffer.Reserve(numsamples);
			rbuffer.Reserve(numsamples);

			for (int i = 0; i < numsamples; i++)
			{
				lbuffer[index + i] = sbuf[i * 2];
				rbuffer[index + i] = sbuf[i * 2 + 1];
			}
		}
		else if (fmt.mSampleType == SampleType_Int16 && fmt.mChannelConfig == ChannelConfig_Mono) // 16 bit mono
		{
			int16_t* sbuf = (int16_t*)readbuffer.Data();
			int numsamples = fmt.mBufferSize / 2;
			index = lbuffer.Reserve(numsamples);

			for (int i = 0; i < numsamples; i++)
			{
				lbuffer[index + i] = sbuf[i];
			}
		}
		else if (fmt.mSampleType == SampleType_Float32 && fmt.mChannelConfig == ChannelConfig_Mono) // float mono
		{
			float* sbuf = (float*)readbuffer.Data();
			int numsamples = fmt.mBufferSize / 4;
			index = lbuffer.Reserve(numsamples);
			for (int i = 0; i < numsamples; i++)
			{
				lbuffer[index + i] = sbuf[i] * 32768.f;
			}
		}
		else if (fmt.mSampleType == SampleType_Float32 && fmt.mChannelConfig == ChannelConfig_Stereo) // float stereo
		{
			float* sbuf = (float*)readbuffer.Data();
			int numsamples = fmt.mBufferSize / 8;
			auto addr = lbuffer.Reserve(numsamples);
			rbuffer.Reserve(numsamples);

			for (int i = 0; i < numsamples; i++)
			{
				lbuffer[addr + i] = sbuf[i * 2] * 32768.f;
				rbuffer[addr + i] = sbuf[i * 2 + 1] * 32768.f;
			}
		}
		float accTime = lbuffer.Size() / (float)fmt.mSampleRate;
		if (accTime > 8 * 60) break; // do at most 8 minutes, if the song forces a loop.
	}
	ZMusic_Close(handle);

	auto analyzer = std::make_unique<GainAnalyzer>();
	int result = analyzer->InitGainAnalysis(fmt.mSampleRate);
	if (result == GAIN_ANALYSIS_OK)
	{
		result = analyzer->AnalyzeSamples(lbuffer.Data(), rbuffer.Size() == 0 ? nullptr : rbuffer.Data(), lbuffer.Size(), rbuffer.Size() == 0 ? 1 : 2);
		if (result == GAIN_ANALYSIS_OK)
		{
			auto gain = analyzer->GetTitleGain();
			Printf("Calculated replay gain for %s (%s) at %f dB\n", musicname, hash.GetChars(), gain);

			gainMap.Insert(hash, gain);
			mus_playing.musicVolume = dBToAmplitude(gain);
			SaveGains();
		}
	}
}

bool S_ChangeMusic(const char* musicname, int order, bool looping, bool force)
{
	if (!MusicEnabled()) return false;	// skip the entire procedure if music is globally disabled.

	if (!force && PlayList.GetNumSongs())
	{ // Don't change if a playlist is active
		return true; // do not report an error here.
	}
	// Do game specific lookup.
	FString musicname_;
	if (mus_cb.LookupFileName)
	{
		musicname_ = mus_cb.LookupFileName(musicname, order);
		musicname = musicname_.GetChars();
	}

	if (musicname == nullptr || musicname[0] == 0)
	{
		// Don't choke if the map doesn't have a song attached
		S_StopMusic (true);
		mus_playing.name = "";
		mus_playing.LastSong = "";
		return true;
	}

	if (!mus_playing.name.IsEmpty() &&
		mus_playing.handle != nullptr &&
		mus_playing.name.CompareNoCase(musicname) == 0 &&
		ZMusic_IsLooping(mus_playing.handle) == zmusic_bool(looping))
	{
		if (order != mus_playing.baseorder)
		{
			if (ZMusic_SetSubsong(mus_playing.handle, order))
			{
				mus_playing.baseorder = order;
			}
		}
		else if (!ZMusic_IsPlaying(mus_playing.handle))
		{
			if (!ZMusic_Start(mus_playing.handle, order, looping))
			{
				Printf("Unable to start %s: %s\n", mus_playing.name.GetChars(), ZMusic_GetLastError());
			}
			S_CreateStream();

		}
		return true;
	}

	ZMusic_MusicStream handle = nullptr;

	// Strip off any leading file:// component.
	if (strncmp(musicname, "file://", 7) == 0)
	{
		musicname += 7;
	}

	// opening the music must be done by the game because it's different depending on the game's file system use.
	FileReader reader = OpenMusic(musicname);
	if (!reader.isOpen()) return false;
	auto m = reader.Read();
	reader.Seek(0, FileReader::SeekSet);

	// shutdown old music
	S_StopMusic(true);

	// Just record it if volume is 0 or music was disabled
	if (snd_musicvolume <= 0 || !mus_enabled)
	{
		mus_playing.loop = looping;
		mus_playing.name = musicname;
		mus_playing.baseorder = order;
		mus_playing.LastSong = musicname;
		return true;
	}

	// load & register it
	if (handle != nullptr)
	{
		mus_playing.handle = handle;
	}
	else
	{
		int lumpnum = mus_cb.FindMusic(musicname);
		MidiDeviceSetting* devp = MidiDevices.CheckKey(lumpnum);
		int* mplay = ModPlayers.CheckKey(lumpnum);

		auto volp = MusicVolumes.CheckKey(lumpnum);
		if (volp)
		{
			mus_playing.musicVolume = *volp;

		}
		else
		{
			CheckReplayGain(musicname, devp ? (EMidiDevice)devp->device : MDEV_DEFAULT, devp ? devp->args.GetChars() : "");
		}
		auto mreader = GetMusicReader(reader);	// this passes the file reader to the newly created wrapper.
		int mod_player = mplay? *mplay : *mod_preferred_player;
		int scratch;

		// This config var is only effective when opening a music stream so there's no need for active synchronization. Setting it here is sufficient.
		// Ideally this should have been a parameter to ZMusic_OpenSong, but that would have necessitated an API break.
		ChangeMusicSettingInt(zmusic_mod_preferredplayer, mus_playing.handle, mod_player, &scratch);
		mus_playing.handle = ZMusic_OpenSong(mreader, devp ? (EMidiDevice)devp->device : MDEV_DEFAULT, devp ? devp->args.GetChars() : "");
		if (mus_playing.handle == nullptr)
		{
			Printf("Unable to load %s: %s\n", mus_playing.name.GetChars(), ZMusic_GetLastError());
		}
	}

	mus_playing.loop = looping;
	mus_playing.name = musicname;
	mus_playing.baseorder = 0;
	mus_playing.LastSong = "";

	if (mus_playing.handle != 0)
	{ // play it
		if (!S_StartMusicPlaying(mus_playing.handle, looping, 1.f, order))
		{
			Printf("Unable to start %s: %s\n", mus_playing.name.GetChars(), ZMusic_GetLastError());
			return false;
		}

		S_CreateStream();
		mus_playing.baseorder = order;
		return true;
	}
	return false;
}

//==========================================================================
//
// S_RestartMusic
//
//==========================================================================

void S_RestartMusic ()
{
	if (snd_musicvolume <= 0) return;
	if (!mus_playing.LastSong.IsEmpty() && mus_enabled)
	{
		FString song = mus_playing.LastSong;
		mus_playing.LastSong = "";
		S_ChangeMusic (song.GetChars(), mus_playing.baseorder, mus_playing.loop, true);
	}
	else
	{
		S_StopMusic(true);
	}
}

//==========================================================================
//
// S_MIDIDeviceChanged
//
//==========================================================================


void S_MIDIDeviceChanged(int newdev)
{
	auto song = mus_playing.handle;
	if (song != nullptr && ZMusic_IsMIDI(song) && ZMusic_IsPlaying(song))
	{
		// Reload the song to change the device
		auto mi = mus_playing;
		S_StopMusic(true);
		S_ChangeMusic(mi.name.GetChars(), mi.baseorder, mi.loop);
	}
}

//==========================================================================
//
// S_GetMusic
//
//==========================================================================

int S_GetMusic (const char **name)
{
	int order;

	if (mus_playing.name.IsNotEmpty())
	{
		*name = mus_playing.name.GetChars();
		order = mus_playing.baseorder;
	}
	else
	{
		*name = nullptr;
		order = 0;
	}
	return order;
}

//==========================================================================
//
// S_StopMusic
//
//==========================================================================

void S_StopMusic (bool force)
{
	try
	{
		// [RH] Don't stop if a playlist is active.
		if ((force || PlayList.GetNumSongs() == 0) && !mus_playing.name.IsEmpty())
		{
			if (mus_playing.handle != nullptr)
			{
				S_ResumeMusic();
				S_StopStream();
				ZMusic_Stop(mus_playing.handle);
				auto h = mus_playing.handle;
				mus_playing.handle = nullptr;
				ZMusic_Close(h);
			}
			mus_playing.LastSong = std::move(mus_playing.name);
		}
	}
	catch (const std::runtime_error& )
	{
		//Printf("Unable to stop %s: %s\n", mus_playing.name.GetChars(), err.what());
		if (mus_playing.handle != nullptr)
		{
			auto h = mus_playing.handle;
			mus_playing.handle = nullptr;
			ZMusic_Close(h);
		}
		mus_playing.name = "";
	}
}

//==========================================================================
//
// CCMD changemus
//
//==========================================================================

CCMD (changemus)
{
	if (MusicEnabled())
	{
		if (argv.argc() > 1)
		{
			PlayList.Clear();
			S_ChangeMusic (argv[1], argv.argc() > 2 ? atoi (argv[2]) : 0);
		}
		else
		{
			const char *currentmus = mus_playing.name.GetChars();
			if(currentmus != nullptr && *currentmus != 0)
			{
				Printf ("currently playing %s\n", currentmus);
			}
			else
			{
				Printf ("no music playing\n");
			}
		}
	}
	else
	{
		Printf("Music is disabled\n");
	}
}

//==========================================================================
//
// CCMD stopmus
//
//==========================================================================

CCMD (stopmus)
{
	PlayList.Clear();
	S_StopMusic (false);
	mus_playing.LastSong = "";	// forget the last played song so that it won't get restarted if some volume changes occur
}

//==========================================================================
//
// CCMD playlist
//
//==========================================================================

UNSAFE_CCMD (playlist)
{
	int argc = argv.argc();

	if (argc < 2 || argc > 3)
	{
		Printf ("playlist <playlist.m3u> [<position>|shuffle]\n");
	}
	else
	{
		if (!PlayList.ChangeList(argv[1]))
		{
			Printf("Could not open " TEXTCOLOR_BOLD "%s" TEXTCOLOR_NORMAL ": %s\n", argv[1], strerror(errno));
			return;
		}
		if (PlayList.GetNumSongs () > 0)
		{
			if (argc == 3)
			{
				if (stricmp (argv[2], "shuffle") == 0)
				{
					PlayList.Shuffle ();
				}
				else
				{
					PlayList.SetPosition (atoi (argv[2]));
				}
			}
			S_ActivatePlayList (false);
		}
	}
}

//==========================================================================
//
// CCMD playlistpos
//
//==========================================================================

static bool CheckForPlaylist ()
{
	if (PlayList.GetNumSongs() == 0)
	{
		Printf ("No playlist is playing.\n");
		return false;
	}
	return true;
}

CCMD (playlistpos)
{
	if (CheckForPlaylist() && argv.argc() > 1)
	{
		PlayList.SetPosition (atoi (argv[1]) - 1);
		S_ActivatePlayList (false);
	}
}

//==========================================================================
//
// CCMD playlistnext
//
//==========================================================================

CCMD (playlistnext)
{
	if (CheckForPlaylist())
	{
		PlayList.Advance ();
		S_ActivatePlayList (false);
	}
}

//==========================================================================
//
// CCMD playlistprev
//
//==========================================================================

CCMD (playlistprev)
{
	if (CheckForPlaylist())
	{
		PlayList.Backup ();
		S_ActivatePlayList (true);
	}
}

//==========================================================================
//
// CCMD playliststatus
//
//==========================================================================

CCMD (playliststatus)
{
	if (CheckForPlaylist ())
	{
		Printf ("Song %d of %d:\n%s\n",
			PlayList.GetPosition () + 1,
			PlayList.GetNumSongs (),
			PlayList.GetSong (PlayList.GetPosition ()));
	}
}

//==========================================================================
//
// 
//
//==========================================================================

CCMD(currentmusic)
{
	if (mus_playing.name.IsNotEmpty())
	{
		Printf("Currently playing music '%s'\n", mus_playing.name.GetChars());
	}
	else
	{
		Printf("Currently no music playing\n");
	}
}
