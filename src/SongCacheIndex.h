#ifndef SONG_CACHE_INDEX_H
#define SONG_CACHE_INDEX_H

#include "RageThreads.h"

#include <set>

struct sqlite3;
struct sqlite3_stmt;

/*
 * The song cache: a SQLite database (Cache/cache.db) holding, for each song directory,
 * the directory hash it was cached with and two versions of the cached SM text: the
 * whole thing, and one with the note data left out, which is what's read when loading
 * the song list.  If the database can't be opened, IsOpen() is false and songs are
 * simply loaded from their files.
 *
 * It may be used from any thread.
 */
class SongCacheIndex
{
public:
	/* With bOpen false there is no cache: nothing is read or written. */
	SongCacheIndex( bool bOpen = true );
	~SongCacheIndex();

	bool IsOpen() const { return m_pDB != NULL; }

	/* Get the directory hash a song was cached with, and its SM text without note data. */
	bool GetSongMeta( const CString &sSongDir, unsigned &uHashOut, CString &sMetaOut );

	/* Get the whole SM text of a song. */
	bool GetSongData( const CString &sSongDir, CString &sDataOut );

	bool SetSongData( const CString &sSongDir, unsigned uHash, const CString &sMeta, const CString &sData );

	/* MinaCalc's MSD table of a chart (iNumFloats floats), by chart key, for the
	 * calc version and rows (by a hash of them) it was worked out with.  These
	 * are in their own table, which is only created when the first is stored. */
	bool GetChartMSD( const CString &sChartKey, int iCalcVersion, unsigned uRowsHash, float *pOut, int iNumFloats );
	bool SetChartMSD( const CString &sChartKey, int iCalcVersion, unsigned uRowsHash, const float *pMSD, int iNumFloats );

	/* Group the SetSongData calls made while loading songs into transactions.  When the
	 * outermost batch ends, cached songs that weren't used in it are removed.  These nest. */
	void BeginBatch();
	void EndBatch();

private:
	bool OpenDatabase();
	void CloseDatabase();
	void RemoveDatabaseFiles();
	bool Exec( const char *szSQL );
	bool CheckVersion();
	void Error( const char *szWhat, int iRet );
	bool Prepare( sqlite3_stmt *&pStmt, const char *szSQL );
	bool HaveMSDTable( bool bCreate );
	bool BeginTransaction();
	void Commit();
	void RemoveUnusedSongs();

	CString m_sPath;
	sqlite3 *m_pDB;
	sqlite3_stmt *m_pGetMeta, *m_pGetData, *m_pSetData;
	sqlite3_stmt *m_pGetMSD, *m_pSetMSD;
	int m_iHaveMSDTable;	// -1 not looked yet, 0 no, 1 yes
	int m_iBatchDepth;
	int m_iPendingWrites;
	bool m_bInTransaction;
	bool m_bWriteFailed;
	bool m_bCorrupt;
	set<CString> m_Seen;	// songs used during the current batch
	RageMutex m_Mutex;
};

extern SongCacheIndex *SONGINDEX;	// global and accessable from anywhere in our program

#endif

/*
 * (c) 2002-2003 Glenn Maynard
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
