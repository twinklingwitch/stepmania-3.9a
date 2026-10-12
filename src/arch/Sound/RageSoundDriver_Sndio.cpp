#include "global.h"
#include "RageSoundDriver_Sndio.h"

#include "RageLog.h"
#include "RageUtil.h"

#include <sndio.h>
#include <unistd.h>

const int channels = 2;
const int round_frames = 256;
const int buffer_frames = round_frames * 4;

int RageSound_Sndio::MixerThread_start(void *p)
{
	((RageSound_Sndio *) p)->MixerThread();
	return 0;
}

void RageSound_Sndio::MixerThread()
{
	/* We want to set a higher priority, but Unix only lets root renice
	 * < 0, which is silly.  Give it a try, anyway. */
	nice( -10 );

	vector<int16_t> buf( chunksize_frames * channels );
	while( !shutdown )
	{
		int64_t cursor;
		{
			LockMut( m_PosMutex );
			cursor = played_pos;
		}

		this->Mix( &buf[0], chunksize_frames, last_cursor_pos, cursor );

		const size_t size = buf.size() * sizeof(int16_t);
		if( sio_write(hdl, &buf[0], size) != size )
			RageException::Throw( "RageSound_Sndio: sio_write failed" );

		last_cursor_pos += chunksize_frames;
	}
}

void RageSound_Sndio::SetupDecodingThread()
{
	nice( -5 );
}

void RageSound_Sndio::OnMove( void *arg, int delta )
{
	RageSound_Sndio *pThis = (RageSound_Sndio *) arg;

	LockMut( pThis->m_PosMutex );
	pThis->played_pos += delta;
}

int64_t RageSound_Sndio::GetPosition( const RageSoundBase *snd ) const
{
	LockMut( m_PosMutex );
	return played_pos;
}

RageSound_Sndio::RageSound_Sndio():
	m_PosMutex( "SndioPosition" )
{
	hdl = NULL;
	shutdown = false;
	last_cursor_pos = 0;
	played_pos = 0;

	hdl = sio_open( SIO_DEVANY, SIO_PLAY, 0 );
	if( hdl == NULL )
		RageException::ThrowNonfatal( "RageSound_Sndio: sio_open failed" );

	sio_par par;
	sio_initpar( &par );
	par.bits = 16;
	par.sig = 1;
	par.le = SIO_LE_NATIVE;
	par.pchan = channels;
	par.rate = 44100;
	par.round = round_frames;
	par.appbufsz = buffer_frames;

	if( !sio_setpar(hdl, &par) || !sio_getpar(hdl, &par) )
	{
		sio_close( hdl );
		RageException::ThrowNonfatal( "RageSound_Sndio: couldn't set parameters" );
	}

	if( par.bits != 16 || par.sig != 1 || par.le != SIO_LE_NATIVE || par.pchan != channels )
	{
		sio_close( hdl );
		RageException::ThrowNonfatal( "RageSound_Sndio: unsupported format (%i bits, %i channels)", par.bits, par.pchan );
	}

	samplerate = par.rate;
	chunksize_frames = par.round;
	LOG->Trace( "RageSound_Sndio: sample rate %i, round %i, buffer %i", samplerate, par.round, par.appbufsz );

	sio_onmove( hdl, OnMove, this );
	if( !sio_start(hdl) )
	{
		sio_close( hdl );
		RageException::ThrowNonfatal( "RageSound_Sndio: sio_start failed" );
	}

	StartDecodeThread();

	MixingThread.SetName( "RageSound_Sndio" );
	MixingThread.Create( MixerThread_start, this );
}

RageSound_Sndio::~RageSound_Sndio()
{
	if( MixingThread.IsCreated() )
	{
		/* Signal the mixing thread to quit. */
		shutdown = true;
		LOG->Trace("Shutting down mixer thread ...");
		MixingThread.Wait();
		LOG->Trace("Mixer thread shut down.");
	}

	if( hdl != NULL )
	{
		sio_stop( hdl );
		sio_close( hdl );
	}
}

float RageSound_Sndio::GetPlayLatency() const
{
	/* GetPosition is where the speaker is, not what we've written, so this only
	 * says how soon to ask for a sound.  A buffer's worth is covered by the
	 * slack callers add. */
	return 0;
}
