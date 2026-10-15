/*
 * RageTextureManager - Interface for loading and releasing textures.
 */

#ifndef RAGE_TEXTURE_MANAGER_H
#define RAGE_TEXTURE_MANAGER_H

#include "RageTexture.h"
#include "RageThreads.h"

#include <map>

struct RageTextureManagerPrefs
{
	int m_iTextureColorDepth;
	int m_iMovieColorDepth;
	bool m_bDelayedDelete;
	int m_iMaxTextureResolution;
	bool m_bMipMaps;
	
	RageTextureManagerPrefs()
	{
		m_bDelayedDelete = false;
		m_iMovieColorDepth = 16;
		m_iTextureColorDepth = 16;
		m_iMaxTextureResolution = 1024;
		m_bMipMaps = false;
	}
	RageTextureManagerPrefs( 
		int iTextureColorDepth,
		int iMovieColorDepth,
		bool bDelayedDelete,
		int iMaxTextureResolution,
		bool bMipMaps )
	{
		m_bDelayedDelete = bDelayedDelete;
		m_iMovieColorDepth = iMovieColorDepth;
		m_iTextureColorDepth = iTextureColorDepth;
		m_iMaxTextureResolution = iMaxTextureResolution;
		m_bMipMaps = bMipMaps;
	}

	bool operator!=( const RageTextureManagerPrefs& rhs )
	{
		return 
			m_iTextureColorDepth != rhs.m_iTextureColorDepth ||
			m_iMovieColorDepth != rhs.m_iMovieColorDepth ||
			m_bDelayedDelete != rhs.m_bDelayedDelete ||
			m_iMaxTextureResolution != rhs.m_iMaxTextureResolution ||
			m_bMipMaps != rhs.m_bMipMaps;
	}
};

class RageTextureManager
{
public:
	RageTextureManager();
	void Update( float fDeltaTime );
	~RageTextureManager();

	RageTexture* LoadTexture( RageTextureID ID );
	bool IsTextureRegistered( RageTextureID ID ) const;

	/* Decode a texture on a background thread, so that the main thread only
	 * has to upload it.  Once it's done, the texture is registered with no
	 * references, as VolatileTexture would leave it, and LoadTexture is quick.
	 * This is for banners, and not for movies.
	 *
	 * Returns a ticket for IsLoadingAsync and CancelLoadAsync, or 0 if the
	 * texture can't be loaded this way, in which case it has to be loaded the
	 * usual way.  Every non-zero ticket must be handed back to CancelLoadAsync,
	 * unless IsLoadingAsync has returned false for it. */
	int LoadTextureAsync( RageTextureID ID );

	/* True until the texture is registered, or the load is dropped (because the
	 * display or the texture preferences changed meanwhile; the caller should
	 * load it the usual way instead). */
	bool IsLoadingAsync( int iTicket ) const;

	/* Not interested in the texture any more: don't register it. */
	void CancelLoadAsync( int iTicket );
	void RegisterTexture( RageTextureID ID, RageTexture *p );
	void CacheTexture( RageTextureID ID );
	void VolatileTexture( RageTextureID ID );
	void PermanentTexture( RageTextureID ID );
	void UnloadTexture( RageTexture *t );
	void ReloadAll();

	bool SetPrefs( RageTextureManagerPrefs prefs );
	RageTextureManagerPrefs GetPrefs() { return m_Prefs; };

	RageTextureID::TexPolicy GetDefaultTexturePolicy() const { return m_TexturePolicy; }
	void SetDefaultTexturePolicy( RageTextureID::TexPolicy p ) { m_TexturePolicy = p; }

	// call this between Screens
	void DeleteCachedTextures()	{ GarbageCollect(screen_changed); }
	
	// call this on switch theme
	void DoDelayedDelete()	{ GarbageCollect(delayed_delete); }
	
	void InvalidateTextures();
	
	void AdjustTextureID(RageTextureID &ID) const;
	void DiagnosticOutput() const;

	void DisableOddDimensionWarning() { m_iNoWarnAboutOddDimensions++; }
	void EnableOddDimensionWarning() { m_iNoWarnAboutOddDimensions--; }
	bool GetOddDimensionWarning() const { return m_iNoWarnAboutOddDimensions == 0; }

protected:
	void DeleteTexture( RageTexture *t );
	enum GCType { screen_changed, delayed_delete };
	void GarbageCollect( GCType type );

	RageTextureManagerPrefs m_Prefs;

	RageTexture* LoadTextureInternal( RageTextureID ID );

	/* The loader thread: it takes jobs off m_AsyncJobs and decodes them.  The
	 * main thread picks up the finished ones in Update.  m_AsyncMutex guards
	 * everything in this block. */
	struct AsyncJob;
	vector<AsyncJob*> m_AsyncJobs;
	mutable RageMutex m_AsyncMutex;
	RageSemaphore m_AsyncSem;
	RageThread m_AsyncThread;
	volatile bool m_bAsyncQuit;
	int m_iAsyncNextTicket;
	int m_iAsyncGeneration;		// jobs made before the display changed are stale

	void StartAsyncThread();
	void AsyncThread();
	static int AsyncThread_Start( void *p ) { ((RageTextureManager *) p)->AsyncThread(); return 0; }
	void FinishAsyncJobs();

	std::map<RageTextureID, RageTexture*> m_mapPathToTexture;

	/* The textures in m_mapPathToTexture, in the same order, for Update to walk
	 * every frame; rebuilt when the map changes. */
	vector<RageTexture*> m_vTextures;
	bool m_bTexturesChanged;
	int m_iNoWarnAboutOddDimensions;
	RageTextureID::TexPolicy m_TexturePolicy;
};

extern RageTextureManager*	TEXTUREMAN;	// global and accessable from anywhere in our program

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
