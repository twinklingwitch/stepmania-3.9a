#include "global.h"
#include "WifeScores.h"
#include "XmlFile.h"
#include "RageUtil.h"
#include "RageLog.h"

#include <algorithm>
#include <math.h>
#include <set>

static const char *SCORES_FILE = "WifeScores.xml";
static const char *REPLAY_DIR = "Replays/";

CString WifeScores::GetScoresFile( const CString &sProfileDir )
{
	return sProfileDir + SCORES_FILE;
}

CString WifeScores::GetReplayDir( const CString &sProfileDir )
{
	return sProfileDir + REPLAY_DIR;
}

XNode *WifeScoreEntry::CreateNode() const
{
	XNode *pNode = new XNode;
	pNode->name = "Score";
	pNode->AppendChild( "ScoreKey", sScoreKey );
	pNode->AppendChild( "ChartKey", sChartKey );
	pNode->AppendChild( "Wife", fWife );
	pNode->AppendChild( "Rate", fRate );
	pNode->AppendChild( "Judge", fJudgeScale );
	pNode->AppendChild( "Name", sName );
	pNode->AppendChild( "Grade", sGrade );
	pNode->AppendChild( "Song", sSong );
	pNode->AppendChild( "DateTime", dateTime );
	return pNode;
}

void WifeScoreEntry::LoadFromNode( const XNode *pNode )
{
	pNode->GetChildValue( "ScoreKey", sScoreKey );
	pNode->GetChildValue( "ChartKey", sChartKey );
	pNode->GetChildValue( "Wife", fWife );
	pNode->GetChildValue( "Rate", fRate );
	pNode->GetChildValue( "Judge", fJudgeScale );
	pNode->GetChildValue( "Name", sName );
	pNode->GetChildValue( "Grade", sGrade );
	pNode->GetChildValue( "Song", sSong );
	pNode->GetChildValue( "DateTime", dateTime );
}

void WifeScores::Load( const CString &sProfileDir, vector<WifeScoreEntry> &out )
{
	const CString sFile = GetScoresFile( sProfileDir );
	if( !IsAFile(sFile) )
		return;

	XNode xml;
	if( !xml.LoadFromFile(sFile) )
	{
		LOG->Warn( "Couldn't read \"%s\"", sFile.c_str() );
		return;
	}
	if( xml.name != "WifeScores" )
	{
		LOG->Warn( "\"%s\" isn't a Wife scores file", sFile.c_str() );
		return;
	}

	for( XNodes::const_iterator child = xml.childs.begin(); child != xml.childs.end(); ++child )
	{
		if( (*child)->name != "Score" )
			continue;
		WifeScoreEntry e;
		e.LoadFromNode( *child );
		e.sFromDir = sProfileDir;
		if( e.sScoreKey.empty() || e.sChartKey.empty() )
			continue;
		out.push_back( e );
	}
}

bool WifeScores::Add( const CString &sProfileDir, const WifeScoreEntry &e )
{
	const CString sFile = GetScoresFile( sProfileDir );

	XNode xml;
	if( IsAFile(sFile) )
	{
		if( !xml.LoadFromFile(sFile) || xml.name != "WifeScores" )
		{
			LOG->Warn( "\"%s\" couldn't be read; starting it over", sFile.c_str() );
			xml.Close();
			xml.name = "WifeScores";
		}
	}
	else
		xml.name = "WifeScores";

	xml.AppendChild( e.CreateNode() );

	DISP_OPT opts;
	opts.stylesheet = "";
	opts.write_tabs = false;
	if( !xml.SaveToFile(sFile, &opts) )
	{
		LOG->Warn( "Couldn't write \"%s\"", sFile.c_str() );
		return false;
	}
	FlushDirCache();
	return true;
}

/* Best first; on a tie the older score is ahead, and then by key so the order
 * never depends on where the scores came from. */
static bool Better( const WifeScoreEntry &a, const WifeScoreEntry &b )
{
	if( a.fWife != b.fWife )
		return a.fWife > b.fWife;
	if( !(a.dateTime == b.dateTime) )
		return a.dateTime < b.dateTime;
	return a.sScoreKey < b.sScoreKey;
}

void WifeScores::ForChart( const vector<WifeScoreEntry> &all, const CString &sChartKey, float fRate, vector<WifeScoreEntry> &out )
{
	std::set<CString> seen;
	for( unsigned i=0; i<all.size(); i++ )
	{
		const WifeScoreEntry &e = all[i];
		if( e.sChartKey != sChartKey )
			continue;
		if( fRate >= 0  &&  fabsf(e.fRate - fRate) >= 0.005f )
			continue;
		if( !seen.insert(e.sScoreKey).second )
			continue;
		out.push_back( e );
	}
	std::sort( out.begin(), out.end(), Better );
}

int WifeScores::GetRank( const vector<WifeScoreEntry> &sorted, const CString &sScoreKey )
{
	for( unsigned i=0; i<sorted.size(); i++ )
		if( sorted[i].sScoreKey == sScoreKey )
			return i+1;
	return 0;
}
