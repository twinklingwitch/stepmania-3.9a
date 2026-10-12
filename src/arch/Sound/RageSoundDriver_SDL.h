#ifndef RAGE_SOUND_SDL
#define RAGE_SOUND_SDL

#include "RageSoundDriver_Generic_Software.h"

#include <SDL3/SDL.h>

class RageSound_SDL: public RageSound_Generic_Software
{
	SDL_AudioStream *m_pStream;
	SDL_Mutex *m_pMutex;

	int m_iSampleRate;
	int m_iDeviceFrames;	/* frames SDL buffers in the device */

	/* Everything below is protected by m_pMutex. */
	int64_t m_iFramesMixed;		/* frames handed to SDL so far */
	bool m_bHaveAnchor;
	uint64_t m_iAnchorNS;		/* SDL_GetTicksNS at the last callback */
	double m_fAnchorFrame;		/* frame being heard at m_iAnchorNS */
	mutable int64_t m_iLastReturned;

	static void SDLCALL StreamCallback( void *pUserData, SDL_AudioStream *pStream, int iAdditional, int iTotal );
	void FillStream( int iBytes );
	double EstimatePosition( uint64_t iNowNS ) const;

public:
	/* virtuals: */
	int64_t GetPosition( const RageSoundBase *snd ) const;
	float GetPlayLatency() const;
	int GetSampleRate( int rate ) const { return m_iSampleRate; }
	void SetupDecodingThread();

	RageSound_SDL();
	~RageSound_SDL();
};
#define USE_RAGE_SOUND_SDL

#endif
