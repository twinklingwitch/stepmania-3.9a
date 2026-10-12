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
#include <string.h>

static const char *g_szSkillsetNames[MINACALC_NUM_SKILLSETS] =
{
	"Overall", "Stream", "Jumpstream", "Handstream", "Stamina", "JackSpeed", "Chordjack", "Technical"
};

/* The calculator keeps big buffers around, so share one. */
static RageMutex g_CalcMutex( "ChartMSD" );
static minacalc_handle *g_pCalc = NULL;

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
		LockMut( g_CalcMutex );
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

	LockMut( g_CalcMutex );
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
	LockMut( g_CalcMutex );
	g_Cache[sKey] = c;
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

	LockMut( g_CalcMutex );
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

	LockMut( g_CalcMutex );
	std::map<std::pair<CString,int>, CachedLowMSD>::const_iterator it = g_LowCache.find( key );
	if( it != g_LowCache.end() && it->second.uRowsHash == uHash )
	{
		memcpy( out, it->second.fMSD, sizeof(it->second.fMSD) );
		return true;
	}

	if( g_pCalc == NULL )
		g_pCalc = minacalc_create();
	if( g_pCalc == NULL )
		return false;
	CachedLowMSD c;
	c.uRowsHash = uHash;
	if( minacalc_msd_at_rate( g_pCalc, rows.empty()? NULL: &rows[0], rows.size(), GetKeyCount(pSteps), fRate, c.fMSD ) != MINACALC_OK )
	{
		LOG->Warn( "MinaCalc failed on %s at %.2fx", sKey.c_str(), fRate );
		return false;
	}
	/* Only a handful of low rates are ever asked for, but don't grow forever. */
	if( g_LowCache.size() > 2000 )
		g_LowCache.clear();
	g_LowCache[key] = c;
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
