/*
 * RageBitmapTexture: Holder for a static texture with metadata.
 */

#ifndef RAGEBITMAPTEXTURE_H
#define RAGEBITMAPTEXTURE_H

#include "RageTexture.h"
#include "RageDisplay.h"
struct SDL_Surface;
struct RageSurface;

class RageBitmapTexture : public RageTexture
{
public:
	/* Loading an image takes two steps: Decode reads and converts the file, and
	 * the constructor uploads the result.  Only the upload needs the display, so
	 * Decode can run on another thread, if it's handed what it needs to know
	 * about the display (made on the main thread) rather than asking DISPLAY. */
	struct DisplayInfo
	{
		int iMaxTextureSize;
		bool bSupports[RageDisplay::NUM_PIX_FORMATS];
		RageDisplay::PixelFormatDesc desc[RageDisplay::NUM_PIX_FORMATS];

		void Fetch();	// from DISPLAY
	};

	/* An image that's been decoded and is ready to upload. */
	struct Decoded
	{
		RageSurface *img;
		RageTextureID ID;	// as adjusted by the hints
		RageDisplay::PixelFormat pixfmt;
		int iSourceWidth, iSourceHeight;
		int iImageWidth, iImageHeight;
		int iTextureWidth, iTextureHeight;
		bool bOpaque;

		Decoded(): img(NULL) { }
		~Decoded();
	};
	static Decoded *Decode( const RageTextureID &ID, const DisplayInfo &info );

	RageBitmapTexture( RageTextureID name );
	/* Upload an image that was decoded earlier.  Takes ownership of pDecoded. */
	RageBitmapTexture( RageTextureID name, Decoded *pDecoded );
	virtual ~RageBitmapTexture();
	/* only called by RageTextureManager::InvalidateTextures */
	virtual void Invalidate() { m_uTexHandle = 0; /* don't Destroy() */}
	virtual void Reload();
	virtual unsigned GetTexHandle() const { return m_uTexHandle; };	// accessed by RageDisplay

private:
	void Create();	// called by constructor and Reload
	void Upload( Decoded *pDecoded );
	void Destroy();
	unsigned m_uTexHandle;	// treat as unsigned in OpenGL, ID3D8Texture* for D3D

	SDL_Surface *CreateImg(int &pixfmt);
};

#endif

/*
 * Copyright (c) 2001-2004 Chris Danford, Glenn Maynard
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

