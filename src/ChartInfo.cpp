#include "global.h"
#include "ChartInfo.h"
#if !defined(WITHOUT_EXTRAS)
#include "ChartMSD.h"
#endif
#include "GameState.h"
#include "HighScore.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "Steps.h"
#if !defined(WITHOUT_EXTRAS)
#include "Wife.h"
#include "WifeManager.h"
#endif
#include "song.h"

#include <math.h>

CString ChartInfo::RateString( float fRate )
{
	/* Two decimals, without a second one that is 0. */
	CString s = ssprintf( "%.2f", fRate );
	if( s.size() > 3 && s[s.size()-1] == '0' )
		s.erase( s.size()-1 );
	return s + "x";
}

#if !defined(WITHOUT_EXTRAS)
CString ChartInfo::PercentString( float fFraction )
{
	return ssprintf( "%.2f%%", Wife3Truncate(fFraction) );
}
#endif

CString ChartInfo::JudgeName( float fScale )
{
	static const float scales[] = { 1.50f,1.33f,1.16f,1.00f,0.84f,0.66f,0.50f,0.33f,0.20f };
	for( unsigned i=0; i<ARRAYSIZE(scales); i++ )
		if( fabsf(scales[i] - fScale) < 0.005f )
			return i == ARRAYSIZE(scales)-1? CString("Justice"): ssprintf( "J%i", i+1 );
	return ssprintf( "x%.2f", fScale );
}

#if !defined(WITHOUT_EXTRAS)
Grade ChartInfo::WifeToGrade( float fWife )
{
	if( fWife >= 0.99955f )	return GRADE_TIER_1;
	if( fWife >= 0.997f )	return GRADE_TIER_2;
	if( fWife >= 0.93f )	return GRADE_TIER_3;
	if( fWife >= 0.80f )	return GRADE_TIER_4;
	if( fWife >= 0.70f )	return GRADE_TIER_5;
	if( fWife >= 0.60f )	return GRADE_TIER_6;
	return GRADE_TIER_7;
}
#endif

static CString MinutesSeconds( float fSeconds )
{
	const int iSeconds = (int) floorf( max(fSeconds, 0.0f) + 0.5f );
	return ssprintf( "%i:%02i", iSeconds/60, iSeconds%60 );
}

CString ChartInfo::LengthString( const Song *pSong, float fRate )
{
	if( pSong == NULL || fRate <= 0 )
		return "";
	return "Length " + MinutesSeconds( pSong->GetElapsedTimeFromBeat(pSong->m_fLastBeat) / fRate );
}

CString ChartInfo::BPMString( const Song *pSong, float fRate )
{
	if( pSong == NULL )
		return "";
	float fMin, fMax;
	switch( pSong->m_DisplayBPMType )
	{
	case Song::DISPLAY_RANDOM:
		return "BPM ???";
	case Song::DISPLAY_SPECIFIED:
		fMin = pSong->m_fSpecifiedBPMMin;
		fMax = pSong->m_fSpecifiedBPMMax;
		break;
	default:
		pSong->m_Timing.GetActualBPM( fMin, fMax );
		break;
	}
	const int iMin = (int) floorf( fMin * fRate + 0.5f );
	const int iMax = (int) floorf( fMax * fRate + 0.5f );
	if( iMin == iMax )
		return ssprintf( "BPM %i", iMin );
	return ssprintf( "BPM %i-%i", iMin, iMax );
}

CString ChartInfo::NPSString( const Song *pSong, const Steps *pSteps, float fRate )
{
	if( pSong == NULL || pSteps == NULL )
		return "";
	const float fNotes = pSteps->GetRadarValues()[RADAR_NUM_TAPS_AND_HOLDS];
	const float fSeconds = (pSong->GetElapsedTimeFromBeat(pSong->m_fLastBeat) - pSong->GetElapsedTimeFromBeat(pSong->m_fFirstBeat)) / fRate;
	if( fNotes <= 0 || fSeconds <= 0 )
		return "";
	return ssprintf( "NPS %.2f", fNotes / fSeconds );
}

CString ChartInfo::NotesString( const Steps *pSteps )
{
	if( pSteps == NULL )
		return "";
	const RadarValues &rv = pSteps->GetRadarValues();
	return ssprintf( "Notes %i  Jumps %i  Hands %i  Holds %i  Mines %i",
		(int) rv[RADAR_NUM_TAPS_AND_HOLDS], (int) rv[RADAR_NUM_JUMPS], (int) rv[RADAR_NUM_HANDS],
		(int) rv[RADAR_NUM_HOLDS], (int) rv[RADAR_NUM_MINES] );
}

#if !defined(WITHOUT_EXTRAS)
CString ChartInfo::MSDString( const float fMSD[MINACALC_NUM_SKILLSETS] )
{
	return ssprintf( "MSD %.2f", fMSD[MINACALC_OVERALL] );
}

CString ChartInfo::SkillsetsString( const float fMSD[MINACALC_NUM_SKILLSETS] )
{
	/* The two highest, leaving Overall out. */
	int iFirst = -1, iSecond = -1;
	for( int s=0; s<MINACALC_NUM_SKILLSETS; s++ )
	{
		if( s == MINACALC_OVERALL )
			continue;
		if( iFirst == -1 || fMSD[s] > fMSD[iFirst] )
		{
			iSecond = iFirst;
			iFirst = s;
		}
		else if( iSecond == -1 || fMSD[s] > fMSD[iSecond] )
			iSecond = s;
	}
	if( iFirst == -1 )
		return "";
	CString s = ssprintf( "%s %.2f", ChartMSD::SkillsetName(iFirst), fMSD[iFirst] );
	if( iSecond != -1 )
		s += ssprintf( "  %s %.2f", ChartMSD::SkillsetName(iSecond), fMSD[iSecond] );
	return s;
}
#endif

static const Profile *GetProfile( PlayerNumber pn )
{
	if( PROFILEMAN->IsUsingProfile(pn) )
		return PROFILEMAN->GetProfile( pn );
	return PROFILEMAN->GetMachineProfile();
}

CString ChartInfo::BestScoreString( PlayerNumber pn, const Song *pSong, const Steps *pSteps, float fRate, bool bWife )
{
	if( pSong == NULL || pSteps == NULL )
		return "";

	if( !bWife )
	{
		const HighScore &hs = GetProfile(pn)->GetStepsHighScoreList( pSong, pSteps ).GetTopScore();
		if( hs.grade == GRADE_NO_DATA )
			return "No score yet";
		return ssprintf( "Best %s %.2f%%", GradeToThemedString(hs.grade).c_str(), hs.fPercentDP*100 );
	}

#if defined(WITHOUT_EXTRAS)
	return "";
#else
	const CString sKey = WIFE->GetChartKey( pSong, pSteps );
	vector<WifeScoreEntry> v;
	WIFE->GetLeaderboard( pn, sKey, fRate, false, v );
	if( !v.empty() )
		return "Best " + PercentString( v[0].fWife ) + " " + GradeToThemedString( WifeToGrade(v[0].fWife) );

	WIFE->GetLeaderboard( pn, sKey, -1, false, v );
	if( v.empty() )
		return "No score yet";
	return "Best " + PercentString( v[0].fWife ) + " " + GradeToThemedString( WifeToGrade(v[0].fWife) ) + " at " + RateString( v[0].fRate );
#endif
}
