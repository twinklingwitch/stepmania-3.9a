#ifndef RAGE_SOUND_SNDIO
#define RAGE_SOUND_SNDIO

#include "RageSoundDriver_Generic_Software.h"
#include "RageThreads.h"

struct sio_hdl;

class RageSound_Sndio: public RageSound_Generic_Software
{
	sio_hdl *hdl;

	bool shutdown;
	int64_t last_cursor_pos;
	int64_t played_pos;
	int samplerate;
	int chunksize_frames;
	mutable RageMutex m_PosMutex;

	static int MixerThread_start(void *p);
	void MixerThread();
	RageThread MixingThread;

	static void OnMove( void *arg, int delta );

public:
	int GetSampleRate( int rate ) const { return samplerate; }

	/* virtuals: */
	int64_t GetPosition( const RageSoundBase *snd ) const;
	float GetPlayLatency() const;
	void SetupDecodingThread();

	RageSound_Sndio();
	~RageSound_Sndio();
};
#define USE_RAGE_SOUND_SNDIO

#endif
