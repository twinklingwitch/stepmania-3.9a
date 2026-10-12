#include "global.h"
#include "RageSoundDriver_SDL.h"

#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"

#include <unistd.h>

#if defined(WINDOWS)
#include <windows.h>
#endif

static const int channels = 2;
static const int bytes_per_frame = channels * sizeof(int16_t);

/* Ask SDL for a small device buffer.  This is only a hint; the backend may
 * round it or ignore it, so the driver logs what it actually got. */
static const int default_device_frames = 512;

/* The most we mix in one go.  A larger request is split up. */
static const int max_mix_frames = 1024;

/* If the position implied by a callback and our running estimate disagree by
 * more than this (in seconds), stop smoothing and jump to the new one (device
 * stall, resume from suspend, ...). */
static const double max_slew_seconds = 0.050;

/* How much of the disagreement we take up on each callback. */
static const double slew_gain = 0.1;

/*
 * The game mixes its own sound; SDL is only given the finished S16 stereo
 * stream.  Everything is done from the stream's get callback, which SDL calls
 * from its audio thread whenever the device needs more data, and we mix exactly
 * as much as it asks for.
 *
 * SDL 3 has no way to ask where the play cursor is, so GetPosition estimates
 * it: frames handed to SDL, minus what the stream still holds, minus one device
 * buffer.  That is only updated when the callback runs (once per device
 * buffer), so between callbacks the position is advanced with the monotonic
 * clock, and each callback pulls the estimate toward the new value a little at
 * a time so that scheduling jitter in the callback doesn't show up as jitter in
 * the music position.
 *
 * What this can't see is latency beyond SDL's own buffer: a PipeWire or
 * PulseAudio server, a USB or Bluetooth device, a dmix layer.  That time is not
 * visible to the game and ends up in the global offset.
 */

void SDLCALL RageSound_SDL::StreamCallback( void *pUserData, SDL_AudioStream *pStream, int iAdditional, int iTotal )
{
	RageSound_SDL *pThis = (RageSound_SDL *) pUserData;
	if( iAdditional > 0 )
		pThis->FillStream( iAdditional );
}

/* Called on SDL's audio thread, with m_pMutex unlocked. */
void RageSound_SDL::FillStream( int iBytes )
{
	const uint64_t iNowNS = SDL_GetTicksNS();

	/* What SDL is playing right now: everything we've handed over, less what
	 * hasn't left the stream yet, less the device's own buffer. */
	const int iQueuedFrames = SDL_GetAudioStreamQueued( m_pStream ) / bytes_per_frame;

	SDL_LockMutex( m_pMutex );
	const int64_t iMixed = m_iFramesMixed;
	const double fInstant = max( 0.0, double(iMixed - iQueuedFrames - m_iDeviceFrames) );

	if( !m_bHaveAnchor )
	{
		m_bHaveAnchor = true;
		m_iAnchorNS = iNowNS;
		m_fAnchorFrame = fInstant;
	}
	else
	{
		const double fPredicted = EstimatePosition( iNowNS );
		const double fError = fInstant - fPredicted;
		m_iAnchorNS = iNowNS;
		if( fabs(fError) > max_slew_seconds * m_iSampleRate )
			m_fAnchorFrame = fInstant;
		else
			m_fAnchorFrame = fPredicted + fError * slew_gain;
	}
	const int64_t iCurrentFrame = int64_t( m_fAnchorFrame );
	SDL_UnlockMutex( m_pMutex );

	int16_t buf[max_mix_frames * channels];
	int iFramesLeft = (iBytes + bytes_per_frame - 1) / bytes_per_frame;
	int64_t iFrameNo = iMixed;
	while( iFramesLeft > 0 )
	{
		const int iFrames = min( iFramesLeft, max_mix_frames );
		this->Mix( buf, iFrames, iFrameNo, iCurrentFrame );
		SDL_PutAudioStreamData( m_pStream, buf, iFrames * bytes_per_frame );

		iFrameNo += iFrames;
		iFramesLeft -= iFrames;
	}

	SDL_LockMutex( m_pMutex );
	m_iFramesMixed = iFrameNo;
	SDL_UnlockMutex( m_pMutex );
}

/* Call with m_pMutex held. */
double RageSound_SDL::EstimatePosition( uint64_t iNowNS ) const
{
	double fPos = m_fAnchorFrame;
	if( iNowNS > m_iAnchorNS )
		fPos += double(iNowNS - m_iAnchorNS) * 1e-9 * m_iSampleRate;

	/* We can't be hearing more than we've mixed, even if the device stalls. */
	return min( fPos, double(m_iFramesMixed) );
}

int64_t RageSound_SDL::GetPosition( const RageSoundBase *snd ) const
{
	const uint64_t iNowNS = SDL_GetTicksNS();

	SDL_LockMutex( m_pMutex );
	int64_t iPos = 0;
	if( m_bHaveAnchor )
		iPos = int64_t( EstimatePosition(iNowNS) );

	/* Never go backwards. */
	iPos = max( iPos, m_iLastReturned );
	m_iLastReturned = iPos;
	SDL_UnlockMutex( m_pMutex );

	return iPos;
}

float RageSound_SDL::GetPlayLatency() const
{
	/* The soonest a new sound can be heard is after the device buffer ahead of
	 * it has played. */
	return float(m_iDeviceFrames) / m_iSampleRate;
}

void RageSound_SDL::SetupDecodingThread()
{
#if defined(WINDOWS)
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL );
#else
	nice( -5 );
#endif
}

RageSound_SDL::RageSound_SDL()
{
	m_pStream = NULL;
	m_pMutex = NULL;
	m_iSampleRate = 44100;
	m_iDeviceFrames = 0;
	m_iFramesMixed = 0;
	m_bHaveAnchor = false;
	m_iAnchorNS = 0;
	m_fAnchorFrame = 0;
	m_iLastReturned = 0;

	if( !SDL_InitSubSystem(SDL_INIT_AUDIO) )
		RageException::ThrowNonfatal( "RageSound_SDL: SDL_INIT_AUDIO failed: %s", SDL_GetError() );

	m_pMutex = SDL_CreateMutex();
	if( m_pMutex == NULL )
	{
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		RageException::ThrowNonfatal( "RageSound_SDL: couldn't create a mutex: %s", SDL_GetError() );
	}

	int iFrames = default_device_frames;
	if( PREFSMAN->m_iSoundWriteAhead )
		iFrames = PREFSMAN->m_iSoundWriteAhead;
	SDL_SetHint( SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, ssprintf("%i", iFrames) );
	SDL_SetHint( SDL_HINT_AUDIO_DEVICE_STREAM_ROLE, "game" );

	SDL_AudioSpec spec;
	memset( &spec, 0, sizeof(spec) );
	spec.format = SDL_AUDIO_S16;
	spec.channels = channels;
	spec.freq = m_iSampleRate;

	m_pStream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, StreamCallback, this );
	if( m_pStream == NULL )
	{
		CString sError = SDL_GetError();
		SDL_DestroyMutex( m_pMutex );
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		RageException::ThrowNonfatal( "RageSound_SDL: couldn't open the audio device: %s", sError.c_str() );
	}

	SDL_AudioSpec devspec;
	int iDeviceFrames = 0;
	if( SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(m_pStream), &devspec, &iDeviceFrames) )
	{
		m_iDeviceFrames = iDeviceFrames;
		LOG->Info( "RageSound_SDL: audio driver %s, device %ihz %i channels, %i frames (%.1fms) per buffer (asked for %i), stream %ihz",
			SDL_GetCurrentAudioDriver(), devspec.freq, devspec.channels, iDeviceFrames,
			1000.0f * iDeviceFrames / devspec.freq, iFrames, m_iSampleRate );
	}
	else
	{
		/* Assume we got what we asked for. */
		m_iDeviceFrames = iFrames;
		LOG->Warn( "RageSound_SDL: couldn't get the device format: %s", SDL_GetError() );
	}

	StartDecodeThread();

	/* The device starts paused. */
	if( !SDL_ResumeAudioStreamDevice(m_pStream) )
	{
		CString sError = SDL_GetError();
		SDL_DestroyAudioStream( m_pStream );
		SDL_DestroyMutex( m_pMutex );
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		m_pStream = NULL;
		m_pMutex = NULL;
		RageException::ThrowNonfatal( "RageSound_SDL: couldn't start the audio device: %s", sError.c_str() );
	}
}

RageSound_SDL::~RageSound_SDL()
{
	/* This stops the callback, and closes the device since we opened it along
	 * with the stream.  It has to happen before the decoding thread goes away. */
	if( m_pStream != NULL )
	{
		LOG->Trace( "Shutting down SDL audio ..." );
		SDL_DestroyAudioStream( m_pStream );
		LOG->Trace( "SDL audio shut down." );
	}
	if( m_pMutex != NULL )
		SDL_DestroyMutex( m_pMutex );
	SDL_QuitSubSystem( SDL_INIT_AUDIO );
}
