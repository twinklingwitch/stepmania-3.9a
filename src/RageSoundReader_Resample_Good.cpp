#include "global.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageSoundReader_Resample_Good.h"

#include <samplerate.h>

#include "RageTimer.h"

#define channels source->GetNumChannels()

RageSoundReader_Resample_Good::RageSoundReader_Resample_Good()
{
	source = NULL;
	resamp = NULL;
	samplerate = -1;
	BufSamples = 0;
	eof = false;
	HighQuality = false;
}

/* Call this if the input position is changed or reset. */
void RageSoundReader_Resample_Good::Reset()
{
	BufSamples = 0;
	eof = false;

	/* Flush the resampler. */
	if( resamp )
		src_reset( resamp );
}


/* Call this if the sample factor changes. */
void RageSoundReader_Resample_Good::ReopenResampler()
{
	if( resamp )
		src_delete( resamp );

	int error;
	resamp = src_new( HighQuality? SRC_SINC_BEST_QUALITY:SRC_SINC_MEDIUM_QUALITY, channels, &error );
	if( resamp == NULL )
		RageException::Throw( "Error opening resampler: %s", src_strerror(error) );
}

void RageSoundReader_Resample_Good::Open(SoundReader *source_)
{
	source = source_;
	ASSERT(source);

	samplerate = source->GetSampleRate();

	inbuf.resize( BUFSIZE * channels );
}


RageSoundReader_Resample_Good::~RageSoundReader_Resample_Good()
{
	if( resamp )
		src_delete( resamp );

	delete source;
}

double RageSoundReader_Resample_Good::GetFactor() const
{
	return double(samplerate) / source->GetSampleRate();
}

void RageSoundReader_Resample_Good::SetSampleRate(int hz)
{
	samplerate = hz;
	ReopenResampler();
}

int RageSoundReader_Resample_Good::GetLength() const
{
	return source->GetLength();
}

int RageSoundReader_Resample_Good::GetLength_Fast() const
{
	return source->GetLength_Fast();
}

int RageSoundReader_Resample_Good::SetPosition_Accurate(int ms)
{
	Reset();
	return source->SetPosition_Accurate(ms);
}

int RageSoundReader_Resample_Good::SetPosition_Fast(int ms)
{
	Reset();
	return source->SetPosition_Fast(ms);
}

bool RageSoundReader_Resample_Good::FillBuf()
{
	int samples_free = BUFSIZE-BufSamples;
	if( eof )
		return true;
	if( !samples_free )
		return true;

	const int bytes_per_frame = sizeof(int16_t)*channels;
	int16_t *tmpbuf = (int16_t *) alloca( BUFSIZE*bytes_per_frame );
	int cnt = source->Read( (char *) tmpbuf, samples_free * bytes_per_frame );

	if( cnt == -1 )
	{
		SetError(source->GetError());
		return false;
	}

	if( cnt < samples_free * bytes_per_frame )
		eof = true;

	cnt /= bytes_per_frame;

	float *in = &inbuf[0] + BufSamples*channels;
	for( int s = 0; s < cnt*(int) channels; ++s )
		in[s] = tmpbuf[s];
	BufSamples += cnt;
	return true;
}

int RageSoundReader_Resample_Good::Read(char *bufp, unsigned len)
{
	int16_t *buf = (int16_t *) bufp;
	len /= sizeof(int16_t); /* bytes -> samples */
	const double factor = GetFactor();
	ASSERT( resamp );

	float *outbuf = (float *) alloca( BUFSIZE*channels*sizeof(float) );

	int bytes_read = 0;
	while( 1 )
	{
		int samples_used = 0, samples_output = 0;
		const int frames_wanted = min( int(len/channels), (int) BUFSIZE );
		if( (BufSamples || eof) && frames_wanted )
		{
			SRC_DATA data;
			memset( &data, 0, sizeof(data) );
			data.data_in = &inbuf[0];
			data.input_frames = BufSamples;
			data.data_out = outbuf;
			data.output_frames = frames_wanted;
			data.src_ratio = factor;
			data.end_of_input = eof;

			int error = src_process( resamp, &data );
			if( error )
				RageException::Throw( "Unexpected resample_process error: %s", src_strerror(error) );

			samples_used = data.input_frames_used;
			samples_output = data.output_frames_gen;

			memmove( &inbuf[0], &inbuf[0] + samples_used*channels, sizeof(float) * (BufSamples-samples_used) * channels );

			for( int s = 0; s < samples_output*(int) channels; ++s )
				buf[s] = int16_t(clamp(outbuf[s], -32768, 32767));
		}

		BufSamples -= samples_used;

		if( !samples_output )
		{
			if( !len )
				return bytes_read; /* buffer full */
			if( eof )
				return bytes_read; /* EOF and we're completely flushed */
			if( !FillBuf() )
				return -1; /* source error */
		}

		len -= samples_output*channels;
		buf += samples_output*channels;
		bytes_read += samples_output*channels*sizeof(int16_t);
	}
}

SoundReader *RageSoundReader_Resample_Good::Copy() const
{
	SoundReader *new_source = source->Copy();
	RageSoundReader_Resample_Good *ret = new RageSoundReader_Resample_Good;

	ASSERT( resamp );
	int error;
	ret->resamp = src_clone( resamp, &error );
	if( ret->resamp == NULL )
		RageException::Throw( "Error copying resampler: %s", src_strerror(error) );
	ret->inbuf = inbuf;
	ret->source = new_source;
	ret->HighQuality = HighQuality;
	ret->samplerate = samplerate;
	ret->BufSamples = BufSamples;
	ret->eof = eof;

//	ret->Open(new_source);
//	ret->SetSampleRate(samplerate);
	return ret;
}

/*
 * Copyright (c) 2003 Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

