#include "global.h"
#include "ChartMSD.h"
#include "CalcRows.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "SongCacheIndex.h"
#include "Steps.h"
#include "WifeManager.h"
#include "song.h"

#include <map>
#include <math.h>
#include <set>
#include <string.h>

static const char *g_szSkillsetNames[MINACALC_NUM_SKILLSETS] =
{
	"Overall", "Stream", "Jumpstream", "Handstream", "Stamina", "JackSpeed", "Chordjack", "Technical"
};

/* The calculator keeps big buffers around, so share one.  The caches have a
 * lock of their own: the music wheel's thread (below) holds a calculator for
 * tens of milliseconds, and the main thread can't wait for it to look in them. */
static RageMutex g_CalcMutex( "ChartMSD" );
static minacalc_handle *g_pCalc = NULL;
static RageMutex g_CacheMutex( "ChartMSD cache" );

/* MSD tables by chart key. */
struct CachedMSD
{
	unsigned uRowsHash;
	ChartMSDTable table;
};
static std::map<CString, CachedMSD> g_Cache;

/* MSDs under the table's lowest rate, by chart key and rate in hundredths. */
struct CachedLowMSD
{
	unsigned uRowsHash;
	float fMSD[MINACALC_NUM_SKILLSETS];
};
static std::map<std::pair<CString,int>, CachedLowMSD> g_LowCache;

float ChartMSDTable::Get( float fRate, int iSkillset ) const
{
	ASSERT( iSkillset >= 0 && iSkillset < MINACALC_NUM_SKILLSETS );
	if( fRate > 2.F )
	{
		const float pDiff = fMSD[13][iSkillset];
		return pDiff + pDiff * ((fRate - 2.F) * .5F);
	}
	if( fRate < .7F )
	{
		const float pDiff = fMSD[0][iSkillset];
		return max( pDiff - pDiff * (.7F - fRate), 0.F );
	}

	const int idx = (int) (fRate * 10) - 7;
	const float prop = fmodf( fRate * 10.F, 1.F );
	if( prop == 0 )
		return fMSD[idx][iSkillset];

	const float pDiffL = fMSD[idx][iSkillset];
	const float pDiffH = fMSD[idx + 1][iSkillset];
	return prop * (pDiffH - pDiffL) + pDiffL;
}

int ChartMSD::GetCalcVersion()
{
	return minacalc_version();
}

const char *ChartMSD::SkillsetName( int iSkillset )
{
	ASSERT( iSkillset >= 0 && iSkillset < MINACALC_NUM_SKILLSETS );
	return g_szSkillsetNames[iSkillset];
}

bool ChartMSD::IsSupported( const Steps *pSteps )
{
	return pSteps != NULL && pSteps->m_StepsType == STEPS_TYPE_DANCE_SINGLE && !pSteps->IsAutogen();
}

unsigned ChartMSD::GetKeyCount( const Steps *pSteps )
{
	return 4;
}

bool ChartMSD::GetRows( const Song *pSong, const Steps *pSteps, vector<minacalc_row> &out )
{
	out.clear();
	if( pSong == NULL || !IsSupported(pSteps) )
		return false;
	const NoteData *pNoteData = pSteps->PeekNoteData();
	if( pNoteData == NULL )
		return false;
	CalcRowsBuild( *pNoteData, pSong->m_Timing, out );
	return true;
}

bool ChartMSD::Compute( const vector<minacalc_row> &rows, unsigned iKeyCount, ChartMSDTable &out )
{
	LockMut( g_CalcMutex );
	if( g_pCalc == NULL )
		g_pCalc = minacalc_create();
	if( g_pCalc == NULL )
		return false;
	return minacalc_msd( g_pCalc, rows.empty()? NULL: &rows[0], rows.size(), iKeyCount, &out.fMSD[0][0] ) == MINACALC_OK;
}

bool ChartMSD::ComputeSSR( const vector<minacalc_row> &rows, unsigned iKeyCount, float fRate, float fGoal, float out[MINACALC_NUM_SKILLSETS] )
{
	LockMut( g_CalcMutex );
	if( g_pCalc == NULL )
		g_pCalc = minacalc_create();
	if( g_pCalc == NULL )
		return false;
	return minacalc_ssr( g_pCalc, rows.empty()? NULL: &rows[0], rows.size(), iKeyCount, fRate, fGoal, out ) == MINACALC_OK;
}

/* FNV-1a over the rows' bits. */
unsigned ChartMSD::HashRows( const vector<minacalc_row> &rows )
{
	unsigned h = 2166136261u;
	for( unsigned i = 0; i < rows.size(); ++i )
	{
		unsigned char buf[8];
		memcpy( buf, &rows[i].notes, 4 );
		memcpy( buf + 4, &rows[i].time, 4 );
		for( int b = 0; b < 8; ++b )
		{
			h ^= buf[b];
			h *= 16777619u;
		}
	}
	return h;
}

bool ChartMSD::LookupMSD( const CString &sKey, unsigned uHash, ChartMSDTable &out )
{
	{
		LockMut( g_CacheMutex );
		std::map<CString, CachedMSD>::const_iterator it = g_Cache.find( sKey );
		if( it != g_Cache.end() && it->second.uRowsHash == uHash )
		{
			out = it->second.table;
			return true;
		}
	}

	const int iNumFloats = MINACALC_NUM_RATES * MINACALC_NUM_SKILLSETS;
	CachedMSD c;
	c.uRowsHash = uHash;
	if( SONGINDEX == NULL || !SONGINDEX->GetChartMSD(sKey, GetCalcVersion(), uHash, &c.table.fMSD[0][0], iNumFloats) )
		return false;

	LockMut( g_CacheMutex );
	g_Cache[sKey] = c;
	out = c.table;
	return true;
}

void ChartMSD::StoreMSD( const CString &sKey, unsigned uHash, const ChartMSDTable &table, bool bSongCache )
{
	CachedMSD c;
	c.uRowsHash = uHash;
	c.table = table;
	if( bSongCache && SONGINDEX != NULL )
		SONGINDEX->SetChartMSD( sKey, GetCalcVersion(), uHash, &c.table.fMSD[0][0], MINACALC_NUM_RATES * MINACALC_NUM_SKILLSETS );
	LockMut( g_CacheMutex );
	g_Cache[sKey] = c;
}

static bool LookupLowMSD( const std::pair<CString,int> &key, unsigned uHash, float out[MINACALC_NUM_SKILLSETS] )
{
	LockMut( g_CacheMutex );
	std::map<std::pair<CString,int>, CachedLowMSD>::const_iterator it = g_LowCache.find( key );
	if( it == g_LowCache.end() || it->second.uRowsHash != uHash )
		return false;
	memcpy( out, it->second.fMSD, sizeof(it->second.fMSD) );
	return true;
}

static void StoreLowMSD( const std::pair<CString,int> &key, const CachedLowMSD &c )
{
	LockMut( g_CacheMutex );
	/* Only a handful of low rates are ever asked for, but don't grow forever. */
	if( g_LowCache.size() > 2000 )
		g_LowCache.clear();
	g_LowCache[key] = c;
}

bool ChartMSD::GetMSD( const Song *pSong, const Steps *pSteps, ChartMSDTable &out )
{
	vector<minacalc_row> rows;
	if( !GetRows(pSong, pSteps, rows) )
		return false;
	const CString sKey = WifeManager::GetChartKey( pSong, pSteps );
	if( sKey.empty() )
		return false;
	const unsigned uHash = HashRows( rows );
	if( LookupMSD(sKey, uHash, out) )
		return true;

	CachedMSD c;
	c.uRowsHash = uHash;
	if( !Compute(rows, GetKeyCount(pSteps), c.table) )
	{
		LOG->Warn( "MinaCalc failed on %s", sKey.c_str() );
		return false;
	}
	if( SONGINDEX != NULL )
		SONGINDEX->SetChartMSD( sKey, GetCalcVersion(), uHash, &c.table.fMSD[0][0], MINACALC_NUM_RATES * MINACALC_NUM_SKILLSETS );

	LockMut( g_CacheMutex );
	g_Cache[sKey] = c;
	out = c.table;
	return true;
}

bool ChartMSD::GetMSDAtRate( const Song *pSong, const Steps *pSteps, float fRate, float out[MINACALC_NUM_SKILLSETS] )
{
	if( fRate >= 0.7f )
	{
		ChartMSDTable table;
		if( !GetMSD(pSong, pSteps, table) )
			return false;
		for( int s = 0; s < MINACALC_NUM_SKILLSETS; ++s )
			out[s] = table.Get( fRate, s );
		return true;
	}

	vector<minacalc_row> rows;
	if( fRate <= 0 || !GetRows(pSong, pSteps, rows) )
		return false;
	const CString sKey = WifeManager::GetChartKey( pSong, pSteps );
	if( sKey.empty() )
		return false;
	const unsigned uHash = HashRows( rows );
	const std::pair<CString,int> key( sKey, (int) floorf(fRate * 100 + 0.5f) );

	CachedLowMSD c;
	if( LookupLowMSD(key, uHash, c.fMSD) )
	{
		memcpy( out, c.fMSD, sizeof(c.fMSD) );
		return true;
	}

	c.uRowsHash = uHash;
	{
		LockMut( g_CalcMutex );
		if( g_pCalc == NULL )
			g_pCalc = minacalc_create();
		if( g_pCalc == NULL )
			return false;
		if( minacalc_msd_at_rate( g_pCalc, rows.empty()? NULL: &rows[0], rows.size(), GetKeyCount(pSteps), fRate, c.fMSD ) != MINACALC_OK )
		{
			LOG->Warn( "MinaCalc failed on %s at %.2fx", sKey.c_str(), fRate );
			return false;
		}
	}
	StoreLowMSD( key, c );
	memcpy( out, c.fMSD, sizeof(c.fMSD) );
	return true;
}

bool ChartMSD::GetSSR( const Song *pSong, const Steps *pSteps, float fRate, float fGoal, float out[MINACALC_NUM_SKILLSETS] )
{
	vector<minacalc_row> rows;
	if( !GetRows(pSong, pSteps, rows) )
		return false;
	return ComputeSSR( rows, GetKeyCount(pSteps), fRate, fGoal, out );
}

/* The MSD for the music wheel, worked out on a thread of its own: a chart that
 * isn't in the cache yet takes tens of milliseconds, which is a skipped frame
 * if the main thread waits for it.  The notes are read on the main thread (that
 * isn't thread safe), the calculator and the song cache run on the thread, and
 * the answer goes to the same caches GetMSDAtRate uses.  One job waits at most:
 * the wheel only cares about the chart it is on now. */
namespace
{
	struct MSDJob
	{
		CString sID;		// the key, rows hash and rate, to tell jobs apart
		CString sKey;
		unsigned uHash;
		unsigned iKeys;
		float fRate;		// 0 for the table of 0.7 and up, else the rate under it
		vector<minacalc_row> rows;
		MSDJob(): uHash(0), iKeys(4), fRate(0) { }
	};

	RageMutex g_JobMutex( "ChartMSD jobs" );
	RageSemaphore *g_pJobWake = NULL;
	RageThread g_JobThread;
	volatile bool g_bJobQuit = false;
	bool g_bJobStarted = false;

	/* All under g_JobMutex. */
	bool g_bHavePending = false;
	MSDJob g_Pending;
	CString g_sRunning;
	std::set<CString> g_Failed;
	const Steps *g_pAskedSteps = NULL;	// who the latest job was for, to answer without reading the notes again
	float g_fAskedRate = 0;

	bool JobBusy()
	{
		return g_bHavePending || !g_sRunning.empty();
	}

	/* Memory only. */
	bool PeekCache( const CString &sKey, unsigned uHash, float fRate, float out[MINACALC_NUM_SKILLSETS] )
	{
		if( fRate >= 0.7f )
		{
			LockMut( g_CacheMutex );
			std::map<CString, CachedMSD>::const_iterator it = g_Cache.find( sKey );
			if( it == g_Cache.end() || it->second.uRowsHash != uHash )
				return false;
			for( int s = 0; s < MINACALC_NUM_SKILLSETS; ++s )
				out[s] = it->second.table.Get( fRate, s );
			return true;
		}
		return LookupLowMSD( std::make_pair(sKey, (int) floorf(fRate * 100 + 0.5f)), uHash, out );
	}

	int JobMain( void * )
	{
		/* Not g_pCalc, which the main thread might want in the meantime. */
		minacalc_handle *pCalc = minacalc_create();
		for(;;)
		{
			g_pJobWake->Wait( false );
			if( g_bJobQuit )
				break;

			MSDJob job;
			{
				LockMut( g_JobMutex );
				if( !g_bHavePending )
					continue;
				job.sID.swap( g_Pending.sID );
				job.sKey.swap( g_Pending.sKey );
				job.uHash = g_Pending.uHash;
				job.iKeys = g_Pending.iKeys;
				job.fRate = g_Pending.fRate;
				job.rows.swap( g_Pending.rows );
				g_bHavePending = false;
				g_sRunning = job.sID;
			}

			bool bOK = false;
			const minacalc_row *pRows = job.rows.empty()? NULL: &job.rows[0];
			if( job.fRate == 0 )
			{
				ChartMSDTable table;
				if( ChartMSD::LookupMSD(job.sKey, job.uHash, table) )
					bOK = true;
				else if( pCalc != NULL && minacalc_msd(pCalc, pRows, job.rows.size(), job.iKeys, &table.fMSD[0][0]) == MINACALC_OK )
				{
					ChartMSD::StoreMSD( job.sKey, job.uHash, table, true );
					bOK = true;
				}
				else
					LOG->Warn( "MinaCalc failed on %s", job.sKey.c_str() );
			}
			else
			{
				CachedLowMSD c;
				c.uRowsHash = job.uHash;
				if( pCalc != NULL && minacalc_msd_at_rate(pCalc, pRows, job.rows.size(), job.iKeys, job.fRate, c.fMSD) == MINACALC_OK )
				{
					StoreLowMSD( std::make_pair(job.sKey, (int) floorf(job.fRate * 100 + 0.5f)), c );
					bOK = true;
				}
				else
					LOG->Warn( "MinaCalc failed on %s at %.2fx", job.sKey.c_str(), job.fRate );
			}

			/* The cache has the answer before anybody can see the job is over. */
			LockMut( g_JobMutex );
			g_sRunning = "";
			if( !bOK )
				g_Failed.insert( job.sID );
		}
		if( pCalc != NULL )
			minacalc_destroy( pCalc );
		return 0;
	}
}

int ChartMSD::PeekMSDAtRate( const Song *pSong, const Steps *pSteps, float fRate, float out[MINACALC_NUM_SKILLSETS] )
{
	if( g_bJobQuit || fRate <= 0 )
		return GetMSDAtRate( pSong, pSteps, fRate, out )? 1: 0;

	{
		LockMut( g_JobMutex );
		if( JobBusy() && pSteps == g_pAskedSteps && fRate == g_fAskedRate )
			return -1;
	}

	vector<minacalc_row> rows;
	if( !GetRows(pSong, pSteps, rows) )
		return 0;
	const CString sKey = WifeManager::GetChartKey( pSong, pSteps );
	if( sKey.empty() )
		return 0;
	const unsigned uHash = HashRows( rows );
	if( PeekCache(sKey, uHash, fRate, out) )
		return 1;

	const CString sID = ssprintf( "%s %u %i", sKey.c_str(), uHash, fRate >= 0.7f? 0: (int) floorf(fRate * 100 + 0.5f) );
	{
		LockMut( g_JobMutex );
		/* It may have finished since the look above. */
		if( PeekCache(sKey, uHash, fRate, out) )
			return 1;
		if( g_Failed.find(sID) != g_Failed.end() )
			return 0;
		g_pAskedSteps = pSteps;
		g_fAskedRate = fRate;
		if( sID == g_sRunning || (g_bHavePending && sID == g_Pending.sID) )
			return -1;

		g_Pending.sID = sID;
		g_Pending.sKey = sKey;
		g_Pending.uHash = uHash;
		g_Pending.iKeys = GetKeyCount( pSteps );
		g_Pending.fRate = fRate >= 0.7f? 0: fRate;
		g_Pending.rows.swap( rows );
		g_bHavePending = true;
		if( g_Failed.size() > 100 )
			g_Failed.clear();
	}

	if( !g_bJobStarted )
	{
		g_bJobStarted = true;
		g_pJobWake = new RageSemaphore( "ChartMSD" );
		g_JobThread.SetName( "ChartMSD" );
		g_JobThread.Create( JobMain, NULL );
	}
	g_pJobWake->Post();
	return -1;
}

void ChartMSD::Shutdown()
{
	if( !g_bJobStarted )
		return;
	g_bJobQuit = true;
	g_pJobWake->Post();
	g_JobThread.Wait();
}
