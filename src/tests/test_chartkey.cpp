/* Checks chart keys against the ones Etterna's algorithm gives for the .sm charts
 * in chartkey/.  Etterna isn't built here, so the expected keys come from
 * chartkey/reference.py, a separate reading of Etterna's code (see there), and
 * for tiny.sm from working the string out by hand:
 *
 *   tiny.sm is dance-single, BPM 125.5 from beat 0 and 90.1 from beat 4, and
 *   int(bpm + 0.374643) makes those 125 and 90.  Only rows with notes count:
 *
 *   row   0  1000  tap                     -> "1000" "125"
 *   row  48  0M00  mine                    -> "0400" "125"
 *   row  96  2001  hold head and a tap     -> "2001" "125"
 *   row 192  3000  tail of that hold: no row of its own
 *   row 240  0004  roll head, never ended: dropped, no row
 *   row 288  00L0  lift                    -> "0050" "90"
 *   row 336  0F00  fake                    -> "0700" "90"
 *   row 384  0K[7]00  keysound, [7] skipped -> "0600" "90"
 *   row 432  2000  hold head, never ended: dropped, no row
 *   row 480  0030  tail with no hold: ignored, no row
 *
 *   100012504001252001125005090070090060090, whose SHA-1 is the key. */
#include "global.h"
#include "RageLog.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageUtil.h"
#include "ChartKey.h"
#include "CryptManager.h"
#include "RageTimer.h"
#include "test_misc.h"

#include <math.h>
#include <stdio.h>
#include <algorithm>

static int g_iFailures = 0;

static const struct
{
	const char *szFile;
	int iChart;
	const char *szKey;
} g_Expected[] = {
	{ "attacks.sm", 0, "X17288213933cbe45642110bd9b4033dbdf370c8b" },
	{ "attacks.sm", 1, "Xc4f82e60edb11339284162f815809dc8313557e4" },
	{ "fancy-one.sm", 0, "X9a63db4c9882f7db9d8bede01ef5e77db2f5e3a2" },
	{ "fancy-one.sm", 1, "X4d3e6b799a0324408a5dfbcd007d2de4edcc4867" },
	{ "fancy-one.sm", 2, "Xdbaa77bf4bf3598445d63dbea5bb79340bcdb1be" },
	{ "fancy-one.sm", 3, "Xef49b9d8772d9982a0738f74950a1b6d6646371c" },
	{ "rich-000.sm", 0, "Xab08bb28a0d5658c05b32cbade3c5a3322026bcf" },
	{ "rich-014.sm", 0, "Xb9d209039a66bb774bd43cad255cca7bc310c4f8" },
	{ "rich-014.sm", 1, "Xdfbce787a11e850b956bc740430617631fcbecda" },
	{ "rich-014.sm", 2, "X96833aab91afecc521c102e3206399d952c34e01" },
	{ "rich-014.sm", 3, "X94776208eb860c708e416b68da40f6f1d0bffc11" },
	{ "rich-014.sm", 4, "Xb0b152d63d5f82b35b9785b6999fb0fbcaa2f41c" },
	{ "rich-021.sm", 0, "X043c69ead8ada1658f5654c7234cd51ec39cdf98" },
	{ "rich-021.sm", 1, "X63c06cd94d610373d15d3bed9c6a31e2d5407eb3" },
	{ "rich-035.sm", 0, "Xbf3bff174fe3f1ebc70cc51b9f70c7479d04dace" },
	{ "rich-035.sm", 1, "X58927bc677a22dce25be6622d0c58e8ddba374d9" },
	{ "tiny.sm", 0, "X23673fbaede27f8b79aca8368e0fcdf9dbe94db7" },
};

static bool ReadWholeFile( const CString &sPath, CString &sOut )
{
	FILE *f = fopen( sPath.c_str(), "rb" );
	if( f == NULL )
		return false;
	char buf[4096];
	size_t n;
	sOut = "";
	while( (n = fread(buf, 1, sizeof(buf), f)) > 0 )
		sOut.append( buf, n );
	fclose( f );
	return true;
}

static int NumTracksFor( const CString &sType )
{
	static const struct { const char *sz; int n; } t[] = {
		{ "dance-single", 4 }, { "dance-double", 8 }, { "dance-couple", 8 },
		{ "dance-solo", 6 }, { "dance-threepanel", 3 }, { "pump-single", 5 },
		{ "pump-halfdouble", 6 }, { "pump-double", 10 }, { "para-single", 5 },
		{ "ez2-single", 5 },
	};
	for( unsigned i=0; i<ARRAYSIZE(t); i++ )
		if( sType == t[i].sz )
			return t[i].n;
	return 0;
}

/* The text of the first #TAG:...; in the file. */
static CString GetTag( const CString &sFile, const CString &sTag )
{
	size_t i = sFile.find( "#" + sTag + ":" );
	if( i == CString::npos )
		return "";
	i += sTag.size() + 2;
	return sFile.substr( i, sFile.find(';', i) - i );
}

static bool CompareBeat( const std::pair<float,float> &a, const std::pair<float,float> &b )
{
	return a.first < b.first;
}

/* The BPM changes a song ends up with in Etterna, for positive BPMs: see
 * SMLoader::ProcessBPMsAndStops.  If there's nothing at beat 0 it takes the
 * value of the second change as the starting BPM and drops the first. */
static void ParseBPMs( const CString &sBPMs, vector<ChartKeyBPM> &out )
{
	vector< std::pair<float,float> > v;
	CStringArray asChanges;
	split( sBPMs, ",", asChanges );
	for( unsigned i=0; i<asChanges.size(); i++ )
	{
		CStringArray asValues;
		split( asChanges[i], "=", asValues, false );
		if( asValues.size() != 2 )
			continue;
		float fBeat = strtof( asValues[0], NULL );
		float fBPM = strtof( asValues[1], NULL );
		if( fBPM == 0 )
			continue;
		v.push_back( std::make_pair(fBeat, fBPM) );
	}
	std::stable_sort( v.begin(), v.end(), CompareBeat );

	unsigned i = 0;
	float fBPM = 0;
	while( i < v.size() && v[i].first <= 0 )
		fBPM = v[i++].second;
	if( fBPM == 0 )
	{
		if( i == v.size() )
			fBPM = 60;
		else
		{
			i++;
			fBPM = i < v.size()? v[i].second: 60;
		}
	}
	out.push_back( ChartKeyBPM(0, fBPM) );
	for( ; i < v.size(); i++ )
	{
		int iRow = (int) lroundf( v[i].first * 48.0f );
		if( out.back().iRow == iRow )
			out.back().fBPM = v[i].second;
		else
			out.push_back( ChartKeyBPM(iRow, v[i].second) );
	}
}

static bool GetChart( const CString &sFile, int iChart, CString &sNotes, int &iTracks, vector<ChartKeyBPM> &vBPMs )
{
	ParseBPMs( GetTag(sFile, "BPMS"), vBPMs );

	size_t i = 0;
	for( int n = 0; ; n++ )
	{
		i = sFile.find( "#NOTES:", i );
		if( i == CString::npos )
			return false;
		i += 7;
		size_t iEnd = sFile.find( ';', i );
		if( n != iChart )
			continue;

		CStringArray asParams;
		split( sFile.substr(i, iEnd-i), ":", asParams, false );
		if( asParams.size() < 6 )
			return false;
		CString sType = asParams[0];
		TrimLeft( sType );
		TrimRight( sType );
		iTracks = NumTracksFor( sType );
		sNotes = asParams[5];
		return iTracks > 0;
	}
}

static void TestKeys( const CString &sDir )
{
	for( unsigned i=0; i<ARRAYSIZE(g_Expected); i++ )
	{
		CString sFile;
		if( !ReadWholeFile(sDir + "/" + g_Expected[i].szFile, sFile) )
		{
			LOG->Warn( "FAIL: can't read %s/%s", sDir.c_str(), g_Expected[i].szFile );
			++g_iFailures;
			continue;
		}

		CString sNotes;
		int iTracks = 0;
		vector<ChartKeyBPM> vBPMs;
		if( !GetChart(sFile, g_Expected[i].iChart, sNotes, iTracks, vBPMs) )
		{
			LOG->Warn( "FAIL: no chart %i in %s", g_Expected[i].iChart, g_Expected[i].szFile );
			++g_iFailures;
			continue;
		}

		CString sKey = ChartKeyGenerate( sNotes, iTracks, vBPMs );
		if( sKey != g_Expected[i].szKey )
		{
			LOG->Warn( "FAIL: %s chart %i: got %s, expected %s", g_Expected[i].szFile,
				g_Expected[i].iChart, sKey.c_str(), g_Expected[i].szKey );
			++g_iFailures;
		}
	}
}

/* The string for tiny.sm, and a few things the keys depend on. */
static void TestTiny( const CString &sDir )
{
	CString sFile, sNotes;
	int iTracks = 0;
	vector<ChartKeyBPM> vBPMs;
	if( !ReadWholeFile(sDir + "/tiny.sm", sFile) || !GetChart(sFile, 0, sNotes, iTracks, vBPMs) )
	{
		LOG->Warn( "FAIL: can't load tiny.sm" );
		++g_iFailures;
		return;
	}

	CString s = ChartKeyBuildString( sNotes, iTracks, vBPMs );
	if( s != "100012504001252001125005090070090060090" )
	{
		LOG->Warn( "FAIL: tiny.sm string is %s", s.c_str() );
		++g_iFailures;
	}

	/* SHA-1 of "abc", a known answer. */
	if( CryptManager::GetSHA1ForString("abc") != "a9993e364706816aba3e25717850c26c9cd0d89d" )
	{
		LOG->Warn( "FAIL: SHA-1 of abc is %s", CryptManager::GetSHA1ForString("abc").c_str() );
		++g_iFailures;
	}

	/* The BPM goes in as int(bpm + 0.374643): 125.0 to 125.6 are 125, 125.7 up
	 * to 126.6 are 126. */
	vector<ChartKeyBPM> v;
	v.push_back( ChartKeyBPM(0, 125.0f) );
	CString a = ChartKeyGenerate( sNotes, iTracks, v );
	v[0].fBPM = 125.6f;
	CString b = ChartKeyGenerate( sNotes, iTracks, v );
	v[0].fBPM = 125.7f;
	CString c = ChartKeyGenerate( sNotes, iTracks, v );
	v[0].fBPM = 126.0f;
	CString d = ChartKeyGenerate( sNotes, iTracks, v );
	if( a != b || b == c || c != d )
	{
		LOG->Warn( "FAIL: BPM rounding: %s %s %s %s", a.c_str(), b.c_str(), c.c_str(), d.c_str() );
		++g_iFailures;
	}

	/* Rows count only by their notes: more empty lines don't change the key. */
	CString d2 = ChartKeyGenerate( "1000\n0000\n0000\n0000\n,\n0100\n0000\n0000\n0000\n", 4, vBPMs );
	CString e = ChartKeyGenerate( "1000\n0000\n,\n0100\n0000\n0000\n0000\n0000\n0000\n0000\n0000\n", 4, vBPMs );
	if( d2 != e )
	{
		LOG->Warn( "FAIL: empty lines changed the key" );
		++g_iFailures;
	}
}

/* A very long chart takes a moment, not seconds: 4000 measures of 16th notes is
 * about 64000 rows. */
static void TestSpeed()
{
	CString sNotes;
	for( int m=0; m<4000; m++ )
	{
		if( m )
			sNotes += ",\n";
		for( int l=0; l<16; l++ )
			sNotes += l%4==0? "1001\n": (l%4==2? "0110\n": "0000\n");
	}
	vector<ChartKeyBPM> v;
	v.push_back( ChartKeyBPM(0, 180) );
	v.push_back( ChartKeyBPM(48*400, 200) );

	RageTimer t;
	CString s = ChartKeyGenerate( sNotes, 4, v );
	const float fSeconds = t.GetDeltaTime();
	LOG->Info( "chart key of %u bytes of notes took %.1f ms: %s", (unsigned) sNotes.size(), fSeconds*1000, s.c_str() );
	if( fSeconds > 2 )
	{
		LOG->Warn( "FAIL: chart keys are too slow" );
		++g_iFailures;
	}
}

int main( int argc, char *argv[] )
{
	test_handle_args( argc, argv );
	test_init();

	CString sDir = argc > 1? argv[1]: "chartkey";

	TestKeys( sDir );
	TestTiny( sDir );
	TestSpeed();

	if( g_iFailures )
		LOG->Warn( "%i checks failed", g_iFailures );
	else
		LOG->Info( "chartkey: all checks passed" );

	test_deinit();
	return g_iFailures? 1: 0;
}
