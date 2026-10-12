#include "global.h"

#include "SongCacheIndex.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageFileManager.h"
#include "song.h"

#include <sqlite3.h>
#include <cstdio>
#include <cstring>

/*
 * A quick explanation of song cache hashes: each cached song is stored under its
 * song path, along with GetHashForDirectory(m_sSongDir), the hash of the song
 * directory, which changes on each modification and is used to determine if a song
 * has changed.
 *
 * We can load songs from cache given only their path; we don't have to actually look
 * in the directory (to find out the directory hash) in order to find the cache entry.
 */
#define CACHE_DIR "Cache/"
#define CACHE_DB "cache.db"

/* Increment this if the layout of the tables below changes. */
const int DB_SCHEMA_VERSION = 1;

/* While loading, commit every so often, so quitting in the middle of a long first
 * load doesn't throw everything away. */
const int WRITES_PER_COMMIT = 64;

SongCacheIndex *SONGINDEX;

static void EmptyDir( CString dir, const CString &sKeepPrefix = "" )
{
	ASSERT(dir[dir.size()-1] == '/');

	CStringArray asCacheFileNames;
	GetDirListing( dir, asCacheFileNames );
	for( unsigned i=0; i<asCacheFileNames.size(); i++ )
	{
		if( sKeepPrefix.size() && asCacheFileNames[i].Left(sKeepPrefix.size()).CompareNoCase(sKeepPrefix) == 0 )
			continue;
		if( !IsADirectory(dir + asCacheFileNames[i]) )
			FILEMAN->Remove( dir + asCacheFileNames[i] );
	}
}

static bool IsCorrupt( int iRet )
{
	iRet &= 0xFF;	/* strip extended result codes */
	return iRet == SQLITE_CORRUPT || iRet == SQLITE_NOTADB;
}

SongCacheIndex::SongCacheIndex( bool bOpen ):
	m_Mutex("SongCacheIndex")
{
	m_pDB = NULL;
	m_pGetMeta = m_pGetData = m_pSetData = NULL;
	m_pGetMSD = m_pSetMSD = NULL;
	m_iHaveMSDTable = -1;
	m_iBatchDepth = 0;
	m_iPendingWrites = 0;
	m_bInTransaction = false;
	m_bWriteFailed = false;
	m_bCorrupt = false;

	if( !bOpen )
		return;

	/* Before the song cache was a database, it was Cache/index.cache and a file for each
	 * song in Cache/Songs.  Those are of no use now. */
	if( DoesFileExist( CACHE_DIR "index.cache" ) )
	{
		LOG->Info( "Deleting old song cache files" );
		FILEMAN->Remove( CACHE_DIR "index.cache" );
		EmptyDir( CACHE_DIR "Songs/" );
		FILEMAN->Remove( CACHE_DIR "Songs" );
	}

	if( !FILEMAN->GetRealWritablePath( CACHE_DIR CACHE_DB, m_sPath ) )
	{
		LOG->Warn( "No writable location for \"%s\"; the song cache is disabled", CACHE_DIR CACHE_DB );
		return;
	}

	for( int iTry = 0; iTry < 2; ++iTry )
	{
		if( OpenDatabase() )
		{
			LOG->Info( "Song cache: %s", m_sPath.c_str() );
			return;
		}

		/* If the database is unreadable, throw it away and start over. */
		const bool bCorrupt = m_bCorrupt;
		CloseDatabase();
		if( !bCorrupt || iTry == 1 )
			break;

		LOG->Warn( "The song cache \"%s\" is corrupt; recreating it", m_sPath.c_str() );
		RemoveDatabaseFiles();
	}

	LOG->Warn( "Couldn't open the song cache \"%s\"; it is disabled", m_sPath.c_str() );
}

SongCacheIndex::~SongCacheIndex()
{
	CloseDatabase();

	/* If we ran into damage while the game was running, start over next time. */
	if( m_bCorrupt )
		RemoveDatabaseFiles();
}

void SongCacheIndex::RemoveDatabaseFiles()
{
	remove( m_sPath.c_str() );
	remove( (m_sPath + "-wal").c_str() );
	remove( (m_sPath + "-shm").c_str() );
	remove( (m_sPath + "-journal").c_str() );
	m_bCorrupt = false;
}

void SongCacheIndex::Error( const char *szWhat, int iRet )
{
	LOG->Warn( "Song cache: %s failed: %s", szWhat, m_pDB? sqlite3_errmsg(m_pDB): sqlite3_errstr(iRet) );

	/* Don't close the database here: songs that are already loaded still need it to
	 * get their steps.  Just remember to replace it. */
	if( IsCorrupt(iRet) )
		m_bCorrupt = true;
}

bool SongCacheIndex::Exec( const char *szSQL )
{
	const int iRet = sqlite3_exec( m_pDB, szSQL, NULL, NULL, NULL );
	if( iRet != SQLITE_OK )
	{
		Error( szSQL, iRet );
		return false;
	}
	return true;
}

bool SongCacheIndex::Prepare( sqlite3_stmt *&pStmt, const char *szSQL )
{
	if( pStmt != NULL )
		return true;
	const int iRet = sqlite3_prepare_v2( m_pDB, szSQL, -1, &pStmt, NULL );
	if( iRet != SQLITE_OK )
	{
		pStmt = NULL;
		Error( szSQL, iRet );
		return false;
	}
	return true;
}

bool SongCacheIndex::OpenDatabase()
{
	int iRet = sqlite3_open_v2( m_sPath, &m_pDB, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX, NULL );
	if( iRet != SQLITE_OK )
	{
		LOG->Warn( "sqlite3_open(%s) failed: %s", m_sPath.c_str(), m_pDB? sqlite3_errmsg(m_pDB): sqlite3_errstr(iRet) );
		return false;
	}

	sqlite3_busy_timeout( m_pDB, 5000 );

	/* With the write-ahead log, a commit doesn't have to wait for the disk, and readers
	 * and writers don't block each other. */
	if( !Exec( "PRAGMA page_size=8192" ) ||
		!Exec( "PRAGMA journal_mode=WAL" ) ||
		!Exec( "PRAGMA synchronous=NORMAL" ) )
		return false;

	return CheckVersion();
}

/* Make sure the database is in the format we want, and was written for this version of
 * the cache; if not, empty it. */
bool SongCacheIndex::CheckVersion()
{
	int iSchemaVersion = -1;
	int iCacheVersion = -1;
	sqlite3_stmt *pStmt = NULL;
	int iRet = sqlite3_prepare_v2( m_pDB, "PRAGMA user_version", -1, &pStmt, NULL );
	if( iRet == SQLITE_OK )
	{
		if( sqlite3_step(pStmt) == SQLITE_ROW )
			iSchemaVersion = sqlite3_column_int( pStmt, 0 );
		iRet = sqlite3_finalize( pStmt );
	}
	if( iRet != SQLITE_OK )
	{
		Error( "reading the schema version", iRet );
		return false;
	}

	if( iSchemaVersion == DB_SCHEMA_VERSION )
	{
		/* If this can't be read, treat it as out of date and start over. */
		iRet = sqlite3_prepare_v2( m_pDB, "SELECT value FROM meta WHERE key='CacheVersion'", -1, &pStmt, NULL );
		if( iRet == SQLITE_OK )
		{
			if( sqlite3_step(pStmt) == SQLITE_ROW )
				iCacheVersion = sqlite3_column_int( pStmt, 0 );
			iRet = sqlite3_finalize( pStmt );
		}
		if( IsCorrupt(iRet) )
		{
			Error( "reading the cache version", iRet );
			return false;
		}

		if( iCacheVersion == FILE_CACHE_VERSION )
			return true; /* OK */
	}

	LOG->Trace( "Cache format is out of date.  Deleting all cache files." );
	EmptyDir( CACHE_DIR, CACHE_DB );
	EmptyDir( CACHE_DIR "Banners/" );

	/* Start with fresh tables, whatever was here before. */
	return Exec( "BEGIN" ) &&
		Exec( "DROP TABLE IF EXISTS songs" ) &&
		Exec( "DROP TABLE IF EXISTS meta" ) &&
		Exec( "DROP TABLE IF EXISTS msd" ) &&
		Exec( "CREATE TABLE meta( key TEXT PRIMARY KEY, value TEXT )" ) &&
		Exec( "CREATE TABLE songs( dir TEXT PRIMARY KEY, hash INTEGER NOT NULL, meta BLOB NOT NULL, data BLOB NOT NULL )" ) &&
		Exec( ssprintf( "REPLACE INTO meta VALUES( 'CacheVersion', '%i' )", FILE_CACHE_VERSION ) ) &&
		Exec( ssprintf( "PRAGMA user_version=%i", DB_SCHEMA_VERSION ) ) &&
		Exec( "COMMIT" );
}

void SongCacheIndex::CloseDatabase()
{
	if( m_pDB == NULL )
		return;

	if( m_bInTransaction )
		Commit();

	sqlite3_finalize( m_pGetMeta );
	sqlite3_finalize( m_pGetData );
	sqlite3_finalize( m_pSetData );
	sqlite3_finalize( m_pGetMSD );
	sqlite3_finalize( m_pSetMSD );
	m_pGetMeta = m_pGetData = m_pSetData = NULL;
	m_pGetMSD = m_pSetMSD = NULL;
	m_iHaveMSDTable = -1;

	sqlite3_close( m_pDB );
	m_pDB = NULL;
	m_iBatchDepth = 0;
}

/* Writes made while loading are grouped into transactions, since committing each
 * song separately would be slow. */
bool SongCacheIndex::BeginTransaction()
{
	if( m_bInTransaction )
		return true;
	if( m_bWriteFailed )
		return false;

	/* Take the write lock now.  If someone else has it, we don't want to find out
	 * halfway through. */
	if( !Exec( "BEGIN IMMEDIATE" ) )
	{
		m_bWriteFailed = true;
		return false;
	}
	m_bInTransaction = true;
	m_iPendingWrites = 0;
	return true;
}

void SongCacheIndex::Commit()
{
	if( !m_bInTransaction )
		return;

	/* If this fails, leave the transaction open.  The songs written so far are still
	 * visible to us, and rolling back could make them read as stale data. */
	if( !Exec( "COMMIT" ) )
	{
		m_bWriteFailed = true;
		return;
	}
	m_bInTransaction = false;
	m_iPendingWrites = 0;
}

void SongCacheIndex::BeginBatch()
{
	LockMut( m_Mutex );

	if( m_pDB == NULL )
		return;
	if( m_iBatchDepth++ == 0 )
		m_Seen.clear();
}

void SongCacheIndex::EndBatch()
{
	LockMut( m_Mutex );

	if( m_pDB == NULL || m_iBatchDepth == 0 )
		return;
	if( --m_iBatchDepth > 0 )
		return;

	RemoveUnusedSongs();
	Commit();
	m_Seen.clear();
}

/* Songs that were deleted or moved aren't touched by a load; drop them, so the
 * database doesn't grow forever. */
void SongCacheIndex::RemoveUnusedSongs()
{
	/* If nothing was loaded, the songs may just be unavailable at the moment. */
	if( m_Seen.empty() || m_bWriteFailed )
		return;

	vector<CString> asUnused;
	sqlite3_stmt *pList = NULL;
	int iRet = sqlite3_prepare_v2( m_pDB, "SELECT dir FROM songs", -1, &pList, NULL );
	if( iRet == SQLITE_OK )
	{
		while( sqlite3_step(pList) == SQLITE_ROW )
		{
			const CString sDir = (const char *) sqlite3_column_text( pList, 0 );
			if( m_Seen.find(sDir) == m_Seen.end() )
				asUnused.push_back( sDir );
		}
		iRet = sqlite3_finalize( pList );
	}
	if( iRet != SQLITE_OK )
	{
		Error( "listing songs", iRet );
		return;
	}

	if( asUnused.empty() || !BeginTransaction() )
		return;

	sqlite3_stmt *pDelete = NULL;
	if( !Prepare( pDelete, "DELETE FROM songs WHERE dir=?" ) )
		return;
	for( unsigned i = 0; i < asUnused.size(); ++i )
	{
		sqlite3_bind_text( pDelete, 1, asUnused[i], (int) asUnused[i].size(), SQLITE_STATIC );
		sqlite3_step( pDelete );
		iRet = sqlite3_reset( pDelete );
		if( iRet != SQLITE_OK )
		{
			Error( "removing a song", iRet );
			break;
		}
	}
	sqlite3_finalize( pDelete );
}

bool SongCacheIndex::GetSongMeta( const CString &sSongDir, unsigned &uHashOut, CString &sMetaOut )
{
	LockMut( m_Mutex );

	if( m_pDB == NULL || !Prepare( m_pGetMeta, "SELECT hash, meta FROM songs WHERE dir=?" ) )
		return false;

	bool bFound = false;
	sqlite3_bind_text( m_pGetMeta, 1, sSongDir, (int) sSongDir.size(), SQLITE_STATIC );
	int iRet = sqlite3_step( m_pGetMeta );
	if( iRet == SQLITE_ROW )
	{
		uHashOut = (unsigned) sqlite3_column_int64( m_pGetMeta, 0 );
		sMetaOut.assign( (const char *) sqlite3_column_blob( m_pGetMeta, 1 ), sqlite3_column_bytes( m_pGetMeta, 1 ) );
		bFound = true;
	}
	iRet = sqlite3_reset( m_pGetMeta );
	if( iRet != SQLITE_OK && iRet != SQLITE_DONE )
	{
		Error( "reading a song", iRet );
		return false;
	}

	if( bFound && m_iBatchDepth )
		m_Seen.insert( sSongDir );
	return bFound;
}

bool SongCacheIndex::GetSongData( const CString &sSongDir, CString &sDataOut )
{
	LockMut( m_Mutex );

	if( m_pDB == NULL || !Prepare( m_pGetData, "SELECT data FROM songs WHERE dir=?" ) )
		return false;

	bool bFound = false;
	sqlite3_bind_text( m_pGetData, 1, sSongDir, (int) sSongDir.size(), SQLITE_STATIC );
	int iRet = sqlite3_step( m_pGetData );
	if( iRet == SQLITE_ROW )
	{
		sDataOut.assign( (const char *) sqlite3_column_blob( m_pGetData, 0 ), sqlite3_column_bytes( m_pGetData, 0 ) );
		bFound = true;
	}
	iRet = sqlite3_reset( m_pGetData );
	if( iRet != SQLITE_OK && iRet != SQLITE_DONE )
	{
		Error( "reading a song", iRet );
		return false;
	}

	return bFound;
}

bool SongCacheIndex::SetSongData( const CString &sSongDir, unsigned uHash, const CString &sMeta, const CString &sData )
{
	LockMut( m_Mutex );

	if( m_pDB == NULL || m_bWriteFailed )
		return false;
	if( m_iBatchDepth && !BeginTransaction() )
		return false;
	if( !Prepare( m_pSetData, "REPLACE INTO songs VALUES( ?, ?, ?, ? )" ) )
		return false;

	if( uHash == 0 )
		++uHash; /* no 0 hash values */

	sqlite3_bind_text( m_pSetData, 1, sSongDir, (int) sSongDir.size(), SQLITE_STATIC );
	sqlite3_bind_int64( m_pSetData, 2, uHash );
	sqlite3_bind_blob( m_pSetData, 3, sMeta.data(), (int) sMeta.size(), SQLITE_STATIC );
	sqlite3_bind_blob( m_pSetData, 4, sData.data(), (int) sData.size(), SQLITE_STATIC );
	sqlite3_step( m_pSetData );
	const int iRet = sqlite3_reset( m_pSetData );
	if( iRet != SQLITE_OK )
	{
		Error( "writing a song", iRet );

		/* If we can't write, don't hang around waiting for every other song to fail
		 * the same way. */
		m_bWriteFailed = true;
		return false;
	}

	if( m_iBatchDepth )
	{
		m_Seen.insert( sSongDir );
		if( ++m_iPendingWrites >= WRITES_PER_COMMIT )
			Commit();
	}
	return true;
}

/* The msd table is only there once something has been stored in it, so a cache
 * that never had an MSD in it stays exactly as it was. */
bool SongCacheIndex::HaveMSDTable( bool bCreate )
{
	if( m_iHaveMSDTable == -1 )
	{
		sqlite3_stmt *pStmt = NULL;
		int iRet = sqlite3_prepare_v2( m_pDB, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='msd'", -1, &pStmt, NULL );
		if( iRet == SQLITE_OK )
		{
			m_iHaveMSDTable = sqlite3_step(pStmt) == SQLITE_ROW? 1: 0;
			iRet = sqlite3_finalize( pStmt );
		}
		if( iRet != SQLITE_OK )
		{
			Error( "looking for the msd table", iRet );
			m_iHaveMSDTable = -1;
			return false;
		}
	}
	if( m_iHaveMSDTable == 1 || !bCreate )
		return m_iHaveMSDTable == 1;

	if( !Exec( "CREATE TABLE IF NOT EXISTS msd( chartkey TEXT PRIMARY KEY, calcversion INTEGER NOT NULL, rowshash INTEGER NOT NULL, data BLOB NOT NULL )" ) )
		return false;
	m_iHaveMSDTable = 1;
	return true;
}

bool SongCacheIndex::GetChartMSD( const CString &sChartKey, int iCalcVersion, unsigned uRowsHash, float *pOut, int iNumFloats )
{
	LockMut( m_Mutex );

	if( m_pDB == NULL || !HaveMSDTable(false) )
		return false;
	if( !Prepare( m_pGetMSD, "SELECT calcversion, rowshash, data FROM msd WHERE chartkey=?" ) )
		return false;

	bool bFound = false;
	sqlite3_bind_text( m_pGetMSD, 1, sChartKey, (int) sChartKey.size(), SQLITE_STATIC );
	int iRet = sqlite3_step( m_pGetMSD );
	if( iRet == SQLITE_ROW &&
		sqlite3_column_int( m_pGetMSD, 0 ) == iCalcVersion &&
		(unsigned) sqlite3_column_int64( m_pGetMSD, 1 ) == uRowsHash &&
		sqlite3_column_bytes( m_pGetMSD, 2 ) == iNumFloats * (int) sizeof(float) )
	{
		memcpy( pOut, sqlite3_column_blob( m_pGetMSD, 2 ), iNumFloats * sizeof(float) );
		bFound = true;
	}
	iRet = sqlite3_reset( m_pGetMSD );
	if( iRet != SQLITE_OK && iRet != SQLITE_DONE )
	{
		Error( "reading an msd", iRet );
		return false;
	}
	return bFound;
}

bool SongCacheIndex::SetChartMSD( const CString &sChartKey, int iCalcVersion, unsigned uRowsHash, const float *pMSD, int iNumFloats )
{
	LockMut( m_Mutex );

	if( m_pDB == NULL || m_bWriteFailed )
		return false;
	if( m_iBatchDepth && !BeginTransaction() )
		return false;
	if( !HaveMSDTable(true) || !Prepare( m_pSetMSD, "REPLACE INTO msd VALUES( ?, ?, ?, ? )" ) )
		return false;

	sqlite3_bind_text( m_pSetMSD, 1, sChartKey, (int) sChartKey.size(), SQLITE_STATIC );
	sqlite3_bind_int( m_pSetMSD, 2, iCalcVersion );
	sqlite3_bind_int64( m_pSetMSD, 3, uRowsHash );
	sqlite3_bind_blob( m_pSetMSD, 4, pMSD, iNumFloats * (int) sizeof(float), SQLITE_STATIC );
	sqlite3_step( m_pSetMSD );
	const int iRet = sqlite3_reset( m_pSetMSD );
	if( iRet != SQLITE_OK )
	{
		Error( "writing an msd", iRet );
		return false;
	}
	return true;
}

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
