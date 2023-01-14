#ifndef OALSOUND_H
#define OALSOUND_H

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <unordered_map>

#include "i_sound.h"
#include "s_soundinternal.h"

#ifndef NO_OPENAL

#ifdef DYN_OPENAL
#define AL_NO_PROTOTYPES
#include "thirdparty/al.h"
#include "thirdparty/alc.h"
#else
#include "al.h"
#include "alc.h"
#endif

#include "alext.h"

#ifndef AL_SOFT_UHJ
#define AL_SOFT_UHJ
#define AL_FORMAT_UHJ2CHN8_SOFT                  0x19A2
#define AL_FORMAT_UHJ2CHN16_SOFT                 0x19A3
#define AL_FORMAT_UHJ2CHN_FLOAT32_SOFT           0x19A4
#define AL_FORMAT_UHJ3CHN8_SOFT                  0x19A5
#define AL_FORMAT_UHJ3CHN16_SOFT                 0x19A6
#define AL_FORMAT_UHJ3CHN_FLOAT32_SOFT           0x19A7
#define AL_FORMAT_UHJ4CHN8_SOFT                  0x19A8
#define AL_FORMAT_UHJ4CHN16_SOFT                 0x19A9
#define AL_FORMAT_UHJ4CHN_FLOAT32_SOFT           0x19AA
#define AL_STEREO_MODE_SOFT                      0x19B0
#define AL_NORMAL_SOFT                           0x0000
#define AL_SUPER_STEREO_SOFT                     0x0001
#define AL_SUPER_STEREO_WIDTH_SOFT               0x19B1
#endif


class OpenALSoundStream;

class OpenALSoundRenderer final : public SoundRenderer
{
public:
	OpenALSoundRenderer();
	virtual ~OpenALSoundRenderer();

	virtual void SetSfxVolume(float volume);
	virtual void SetMusicVolume(float volume);
	virtual void UpdateMusicParams();
	virtual SoundHandle LoadSound(uint8_t *sfxdata, int length);
	virtual SoundHandle LoadSoundRaw(uint8_t *sfxdata, int length, int frequency, int channels, int bits, int loopstart, int loopend = -1);
	virtual void UnloadSound(SoundHandle sfx);
	virtual unsigned int GetMSLength(SoundHandle sfx);
	virtual unsigned int GetSampleLength(SoundHandle sfx);
	virtual float GetOutputRate();

	// Streaming sounds.
	virtual SoundStream *CreateStream(SoundStreamCallback callback, int buffbytes, int flags, int samplerate, void *userdata);

	// Starts a sound.
	virtual FISoundChannel *StartSound(SoundHandle sfx, float vol, int pitch, int chanflags, FISoundChannel *reuse_chan, float startTime);
	virtual FISoundChannel *StartSound3D(SoundHandle sfx, SoundListener *listener, float vol, FRolloffInfo *rolloff, float distscale, int pitch, int priority, const FVector3 &pos, const FVector3 &vel, int channum, int chanflags, FISoundChannel *reuse_chan, float startTime);

	// Changes a channel's volume.
	virtual void ChannelVolume(FISoundChannel *chan, float volume);

	// Changes a channel's pitch.
	virtual void ChannelPitch(FISoundChannel *chan, float pitch);

	// Stops a sound channel.
	virtual void StopChannel(FISoundChannel *chan);

	// Returns position of sound on this channel, in samples.
	virtual unsigned int GetPosition(FISoundChannel *chan);

	// Synchronizes following sound startups.
	virtual void Sync(bool sync);

	// Pauses or resumes all sound effect channels.
	virtual void SetSfxPaused(bool paused, int slot);

	// Pauses or resumes *every* channel, including environmental reverb.
	virtual void SetInactive(SoundRenderer::EInactiveState inactive);

	// Updates the volume, separation, and pitch of a sound channel.
	virtual void UpdateSoundParams3D(SoundListener *listener, FISoundChannel *chan, bool areasound, const FVector3 &pos, const FVector3 &vel);

	virtual void UpdateListener(SoundListener *);
	virtual void UpdateSounds();

	virtual void MarkStartTime(FISoundChannel*, float startTime);
	virtual float GetAudibility(FISoundChannel*);


	virtual bool IsValid();
	virtual void PrintStatus();
	virtual void PrintDriversList();
	virtual FString GatherStats();

private:
    struct {
        bool EXT_EFX : 1;
        bool EXT_disconnect : 1;
        bool SOFT_HRTF : 1;
        bool SOFT_pause_device : 1;
        bool SOFT_output_limiter : 1;
    } ALC;
    struct {
        bool EXT_source_distance_model : 1;
        bool EXT_SOURCE_RADIUS : 1;
        bool SOFT_deferred_updates : 1;
        bool SOFT_loop_points : 1;
        bool SOFT_source_latency : 1;
        bool SOFT_source_resampler : 1;
        bool SOFT_source_spatialize : 1;
        bool SOFT_UHJ : 1;
    } AL;

	// EFX Extension function pointer variables. Loaded after context creation
	// if EFX is supported. These pointers may be context- or device-dependant,
	// thus can't be static
	// Effect objects
	LPALGENEFFECTS alGenEffects;
	LPALDELETEEFFECTS alDeleteEffects;
	LPALISEFFECT alIsEffect;
	LPALEFFECTI alEffecti;
	LPALEFFECTIV alEffectiv;
	LPALEFFECTF alEffectf;
	LPALEFFECTFV alEffectfv;
	LPALGETEFFECTI alGetEffecti;
	LPALGETEFFECTIV alGetEffectiv;
	LPALGETEFFECTF alGetEffectf;
	LPALGETEFFECTFV alGetEffectfv;
	// Filter objects
	LPALGENFILTERS alGenFilters;
	LPALDELETEFILTERS alDeleteFilters;
	LPALISFILTER alIsFilter;
	LPALFILTERI alFilteri;
	LPALFILTERIV alFilteriv;
	LPALFILTERF alFilterf;
	LPALFILTERFV alFilterfv;
	LPALGETFILTERI alGetFilteri;
	LPALGETFILTERIV alGetFilteriv;
	LPALGETFILTERF alGetFilterf;
	LPALGETFILTERFV alGetFilterfv;
	// Auxiliary slot objects
	LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
	LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
	LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
	LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
	LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
	LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
	LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
	LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
	LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
	LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
	LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;

    ALvoid (AL_APIENTRY*alDeferUpdatesSOFT)(void);
    ALvoid (AL_APIENTRY*alProcessUpdatesSOFT)(void);

    LPALGETSTRINGISOFT alGetStringiSOFT;

    LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT;

    void (ALC_APIENTRY*alcDevicePauseSOFT)(ALCdevice *device);
    void (ALC_APIENTRY*alcDeviceResumeSOFT)(ALCdevice *device);

    void BackgroundProc();
    void AddStream(OpenALSoundStream *stream);
    void RemoveStream(OpenALSoundStream *stream);

	void LoadReverb(const ReverbContainer *env);
	void PurgeStoppedSources();
	static FSoundChan *FindLowestChannel();

    std::thread StreamThread;
    std::mutex StreamLock;
    std::condition_variable StreamWake;
    std::atomic<bool> QuitThread;

	ALCdevice *Device;
	ALCcontext *Context;

	TArray<ALuint> Sources;

	ALfloat SfxVolume;
	ALfloat MusicVolume;

	int SFXPaused;
	TArray<ALuint> FreeSfx;
	TArray<ALuint> PausableSfx;
	TArray<ALuint> ReverbSfx;
	TArray<ALuint> SfxGroup;

	const ReverbContainer *PrevEnvironment;

    typedef TMap<uint16_t,ALuint> EffectMap;
    typedef TMapIterator<uint16_t,ALuint> EffectMapIter;
    ALuint EnvSlot;
    ALuint EnvFilters[2];
    EffectMap EnvEffects;

    bool WasInWater;

    TArray<OpenALSoundStream*> Streams;
    friend class OpenALSoundStream;

	ALCdevice *InitDevice();
};

#endif // NO_OPENAL

#endif
