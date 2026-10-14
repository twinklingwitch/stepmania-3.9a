/* Checks the MinaCalc library through its C interface, and the rows the game
 * builds for it.
 *
 * The expected MSDs and SSRs come from Etterna's own MinaCalc sources (commit
 * 2d70614, calc version 527) built on their own, fed the same rows; on x86-64
 * Linux the vendored copy gives exactly the same floats.  The tolerance is
 * there for other platforms' maths libraries.
 *
 * Other CPUs get a wider one.  The calc's fastsqrt() is SSE's approximate
 * reciprocal square root, which sse2neon turns into ARM's (as in Etterna's own
 * ARM builds), and that is a different approximation: emulating it moves the
 * MSDs and SSRs below by up to 0.23.  AMD's RSQRTSS isn't Intel's either, and
 * moves them as much, so x86 CPUs that aren't Intel's get the wide one too. */
#include "global.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "CalcRows.h"
#include "NoteData.h"
#include "TimingData.h"
#include "minacalc_c.h"
#include "test_misc.h"

#include <math.h>

static int g_iFailures = 0;

static float CalcTolerance()
{
#if defined(__x86_64__) || defined(__i386__)
	__builtin_cpu_init();
	if( __builtin_cpu_is("intel") )
		return 0.01f;
#endif
	return 0.3f;
}
static const float CALC_TOLERANCE = CalcTolerance();

static void Check( const char *szWhat, float fGot, float fExpected, float fTolerance )
{
	if( !(fabsf(fGot - fExpected) <= fTolerance) )
	{
		LOG->Warn( "FAIL %s: got %.6f, expected %.6f", szWhat, fGot, fExpected );
		++g_iFailures;
	}
}

static void CheckInt( const char *szWhat, int iGot, int iExpected )
{
	if( iGot != iExpected )
	{
		LOG->Warn( "FAIL %s: got %i, expected %i", szWhat, iGot, iExpected );
		++g_iFailures;
	}
}

static void AddRow( vector<minacalc_row> &v, unsigned notes, float time )
{
	minacalc_row r;
	r.notes = notes;
	r.time = time;
	v.push_back( r );
}

/* Charts made up on the spot, the same every time. */
enum { STREAM, JUMPSTREAM, JACKS, CHORDJACK, NUM_CHARTS };
static const char *g_szCharts[NUM_CHARTS] = { "stream", "jumpstream", "jacks", "chordjack" };

static void MakeChart( int iChart, vector<minacalc_row> &v )
{
	v.clear();
	unsigned seed = 12345;
	int iLast = -1;
	for( int i = 0; i < 640; ++i )
	{
		seed = seed * 1103515245u + 12345u;
		const unsigned rnd = seed >> 16;
		switch( iChart )
		{
		case STREAM:	/* 16ths at 190 BPM, no jacks */
		{
			int c = rnd % 3;
			if( c >= iLast )
				++c;
			iLast = c;
			AddRow( v, 1u << c, i * (60.0f / 190 / 4) );
			break;
		}
		case JUMPSTREAM:	/* 16ths at 180 BPM, a jump every other note */
		{
			int c = rnd % 4;
			unsigned m = 1u << c;
			if( i % 2 == 0 )
				m |= 1u << ((c + 1 + rnd / 4 % 3) % 4);
			AddRow( v, m, i * (60.0f / 180 / 4) );
			break;
		}
		case JACKS:	/* 16ths at 150 BPM, four on each column in turn */
			AddRow( v, 1u << (i / 4 % 4), i * (60.0f / 150 / 4) );
			break;
		case CHORDJACK:	/* 16ths at 160 BPM, hands and jumps that share columns */
		{
			static const unsigned chords[] = { 7, 14, 11, 13, 3, 6, 12, 9 };
			AddRow( v, chords[rnd % 8], i * (60.0f / 160 / 4) );
			break;
		}
		}
	}
}

static void TestCalc()
{
	CheckInt( "calc version", minacalc_version(), 527 );

	/* MSD at 1.0x (overall, stream, jumpstream, handstream, stamina, jackspeed,
	 * chordjack, technical), then overall at 0.7x and 2.0x. */
	static const float fExpectedMSD[NUM_CHARTS][MINACALC_NUM_SKILLSETS + 2] =
	{
		{ 19.4085f, 18.6030f, 9.6200f, 9.4600f, 18.0061f, 12.5800f, 6.2625f, 19.4085f, 14.6435f, 35.5755f },
		{ 25.9629f, 19.5400f, 23.9050f, 13.1400f, 24.0199f, 25.0055f, 19.9955f, 25.6630f, 19.3815f, 45.2658f },
		{ 24.1435f, 11.7000f, 6.6600f, 6.7400f, 14.3702f, 24.1435f, 4.5979f, 10.2600f, 16.9680f, 40.9665f },
		{ 32.7094f, 20.1800f, 22.9000f, 19.2200f, 30.4785f, 24.8200f, 32.7094f, 22.3400f, 23.4617f, 53.5575f },
	};
	/* SSRs (all skillsets) for 96.5% at 1.0x and 90% at 1.3x. */
	static const float fExpectedSSR[NUM_CHARTS][2][MINACALC_NUM_SKILLSETS] =
	{
		{ { 19.4867f, 18.6789f, 9.7867f, 9.7110f, 17.9969f, 12.5883f, 6.3501f, 19.4867f },
		  { 21.1937f, 20.3742f, 10.5100f, 10.4376f, 19.5014f, 14.4897f, 6.9128f, 21.1656f } },
		{ { 26.6107f, 19.8251f, 24.1849f, 13.3250f, 25.0642f, 25.4414f, 20.8210f, 26.3370f },
		  { 30.3823f, 20.8080f, 28.1051f, 15.3815f, 17.8981f, 30.0708f, 22.9819f, 29.4068f } },
		{ { 24.1117f, 12.3275f, 7.0869f, 7.0869f, 14.3513f, 24.1117f, 4.8055f, 15.2655f },
		  { 29.9625f, 16.8816f, 9.4219f, 9.4219f, 17.8337f, 29.9625f, 6.2802f, 14.9390f } },
		{ { 33.7237f, 20.8549f, 24.0118f, 20.0657f, 31.3815f, 24.8799f, 33.7237f, 23.2226f },
		  { 36.8572f, 24.0272f, 26.3431f, 22.6377f, 33.8061f, 30.4345f, 36.8572f, 26.1887f } },
	};

	minacalc_handle *h = minacalc_create();
	if( h == NULL )
	{
		LOG->Warn( "FAIL: minacalc_create" );
		++g_iFailures;
		return;
	}

	vector<minacalc_row> v;
	for( int c = 0; c < NUM_CHARTS; ++c )
	{
		MakeChart( c, v );
		float msd[MINACALC_NUM_RATES][MINACALC_NUM_SKILLSETS];
		CheckInt( "minacalc_msd", minacalc_msd(h, &v[0], v.size(), 4, &msd[0][0]), MINACALC_OK );
		for( int s = 0; s < MINACALC_NUM_SKILLSETS; ++s )
			Check( ssprintf("%s msd 1.0x skillset %i", g_szCharts[c], s), msd[3][s], fExpectedMSD[c][s], CALC_TOLERANCE );
		Check( ssprintf("%s msd 0.7x", g_szCharts[c]), msd[0][0], fExpectedMSD[c][MINACALC_NUM_SKILLSETS], CALC_TOLERANCE );
		Check( ssprintf("%s msd 2.0x", g_szCharts[c]), msd[13][0], fExpectedMSD[c][MINACALC_NUM_SKILLSETS+1], CALC_TOLERANCE );

		float ssr[2][MINACALC_NUM_SKILLSETS];
		CheckInt( "minacalc_ssr", minacalc_ssr(h, &v[0], v.size(), 4, 1.0f, 0.965f, ssr[0]), MINACALC_OK );
		CheckInt( "minacalc_ssr", minacalc_ssr(h, &v[0], v.size(), 4, 1.3f, 0.90f, ssr[1]), MINACALC_OK );
		for( int g = 0; g < 2; ++g )
		{
			for( int s = 0; s < MINACALC_NUM_SKILLSETS; ++s )
				Check( ssprintf("%s ssr %i skillset %i", g_szCharts[c], g, s), ssr[g][s], fExpectedSSR[c][g][s], CALC_TOLERANCE );
		}

		/* One rate of the table: the first rate is exactly the table's, the
		 * others agree up to what the table's calc carries over from the rate
		 * before. */
		float one[MINACALC_NUM_SKILLSETS];
		CheckInt( "minacalc_msd_at_rate", minacalc_msd_at_rate(h, &v[0], v.size(), 4, 0.7f, one), MINACALC_OK );
		for( int s = 0; s < MINACALC_NUM_SKILLSETS; ++s )
			Check( ssprintf("%s msd_at_rate 0.7x skillset %i", g_szCharts[c], s), one[s], msd[0][s], 0 );
		minacalc_msd_at_rate( h, &v[0], v.size(), 4, 1.0f, one );
		Check( ssprintf("%s msd_at_rate 1.0x", g_szCharts[c]), one[0], msd[3][0], CALC_TOLERANCE );
		minacalc_msd_at_rate( h, &v[0], v.size(), 4, 2.0f, one );
		Check( ssprintf("%s msd_at_rate 2.0x", g_szCharts[c]), one[0], msd[13][0], CALC_TOLERANCE );

		/* Under 0.7x it still rates, and a slower chart is easier. */
		float slow[MINACALC_NUM_SKILLSETS];
		CheckInt( "msd_at_rate 0.5x", minacalc_msd_at_rate(h, &v[0], v.size(), 4, 0.5f, slow), MINACALC_OK );
		if( !(slow[0] > 0 && slow[0] < msd[0][0]) )
		{
			LOG->Warn( "FAIL %s msd_at_rate 0.5x: %.4f isn't between 0 and the 0.7x %.4f", g_szCharts[c], slow[0], msd[0][0] );
			++g_iFailures;
		}

		/* A goal above 96.5% counts as 96.5%. */
		float capped[MINACALC_NUM_SKILLSETS];
		minacalc_ssr( h, &v[0], v.size(), 4, 1.0f, 0.99f, capped );
		Check( ssprintf("%s ssr cap", g_szCharts[c]), capped[0], ssr[0][0], 0 );
	}

	/* What the calculator won't rate gives zeros, and bad arguments an error. */
	float msd[MINACALC_NUM_RATES * MINACALC_NUM_SKILLSETS];
	v.clear();
	AddRow( v, 1, 0 );
	CheckInt( "one row", minacalc_msd(h, &v[0], v.size(), 4, msd), MINACALC_OK );
	Check( "one row msd", msd[3 * MINACALC_NUM_SKILLSETS], 0, 0 );
	CheckInt( "no rows", minacalc_msd(h, NULL, 0, 4, msd), MINACALC_OK );
	Check( "no rows msd", msd[0], 0, 0 );
	MakeChart( STREAM, v );
	swap( v[10], v[11] );
	CheckInt( "unsorted", minacalc_msd(h, &v[0], v.size(), 4, msd), MINACALC_OK );
	Check( "unsorted msd", msd[3 * MINACALC_NUM_SKILLSETS], 0, 0 );
	CheckInt( "key count 1", minacalc_msd(h, &v[0], v.size(), 1, msd), MINACALC_ERR_ARGS );
	CheckInt( "key count 17", minacalc_msd(h, &v[0], v.size(), 17, msd), MINACALC_ERR_ARGS );
	CheckInt( "no output", minacalc_msd(h, &v[0], v.size(), 4, NULL), MINACALC_ERR_ARGS );
	CheckInt( "rate 0", minacalc_ssr(h, &v[0], v.size(), 4, 0, 0.93f, msd), MINACALC_ERR_ARGS );
	CheckInt( "msd rate 0", minacalc_msd_at_rate(h, &v[0], v.size(), 4, 0, msd), MINACALC_ERR_ARGS );
	CheckInt( "msd rate no output", minacalc_msd_at_rate(h, &v[0], v.size(), 4, 0.5f, NULL), MINACALC_ERR_ARGS );
	v.clear();
	AddRow( v, 1, 0 );
	CheckInt( "msd rate one row", minacalc_msd_at_rate(h, &v[0], v.size(), 4, 0.5f, msd), MINACALC_OK );
	Check( "msd rate one row msd", msd[0], 0, 0 );

	minacalc_destroy( h );
}

static void TestSkillRating()
{
	/* Etterna's aggregate_skill(ssrs, 0.1, 1.05, 0, 10.24). */
	static const float ssrs[] = { 25.1f, 24.3f, 23.9f, 23.0f, 22.2f, 20.5f, 19.0f, 18.75f, 12.0f, 30.2f };
	Check( "skill rating", minacalc_skill_rating(ssrs, ARRAYSIZE(ssrs)), 24.4545f, 0.001f );
	Check( "skill rating of one", minacalc_skill_rating(ssrs, 1), 19.4985f, 0.001f );
	/* With nothing it isn't quite 0. */
	Check( "skill rating of none", minacalc_skill_rating(NULL, 0), 0.0105f, 0.0001f );
}

static void CheckRow( const vector<minacalc_row> &v, unsigned i, unsigned notes, float time )
{
	if( i >= v.size() )
	{
		LOG->Warn( "FAIL: row %u missing", i );
		++g_iFailures;
		return;
	}
	CheckInt( ssprintf("row %u notes", i), v[i].notes, notes );
	Check( ssprintf("row %u time", i), v[i].time, time, 0.00001f );
}

static void TestRows()
{
	NoteData nd;
	nd.SetNumTracks( 4 );
	/* Beat 0: only a mine, which sets time 0 but isn't a row of its own. */
	nd.SetTapNote( 0, BeatToNoteRow(0), TAP_ORIGINAL_MINE );
	/* Beat 1: a tap next to a mine. */
	nd.SetTapNote( 1, BeatToNoteRow(1), TAP_ORIGINAL_TAP );
	nd.SetTapNote( 2, BeatToNoteRow(1), TAP_ORIGINAL_MINE );
	/* Beat 2: a hold head and a tap; the hold's end isn't a row. */
	nd.SetTapNote( 0, BeatToNoteRow(2), TAP_ORIGINAL_TAP );
	nd.AddHoldNote( HoldNote(3, BeatToNoteRow(2), BeatToNoteRow(3)) );
	/* Beat 7.5: a mine left of a tap. */
	nd.SetTapNote( 0, BeatToNoteRow(7.5f), TAP_ORIGINAL_MINE );
	nd.SetTapNote( 1, BeatToNoteRow(7.5f), TAP_ORIGINAL_TAP );
	/* Beat 10, after the BPM doubles at beat 8: a jump. */
	nd.SetTapNote( 1, BeatToNoteRow(10), TAP_ORIGINAL_TAP );
	nd.SetTapNote( 2, BeatToNoteRow(10), TAP_ORIGINAL_TAP );

	TimingData td;
	td.m_fBeat0OffsetInSeconds = -0.2f;
	td.AddBPMSegment( BPMSegment(0, 120) );
	td.AddBPMSegment( BPMSegment(8, 240) );

	vector<minacalc_row> v;
	if( !CalcRowsTimingIsExact(td) )
	{
		LOG->Warn( "FAIL: BPM changes alone should be exact" );
		++g_iFailures;
	}
	CalcRowsBuild( nd, td, v );
	CheckInt( "rows", v.size(), 4 );
	CheckRow( v, 0, 2, 0.5f );
	CheckRow( v, 1, 1 | 8, 1.0f );
	CheckRow( v, 2, 2, 3.75f );
	CheckRow( v, 3, 2 | 4, 4.5f );

	/* A stop: 3.9's own timing. */
	td.AddStopSegment( StopSegment(5, 0.25f) );
	if( CalcRowsTimingIsExact(td) )
	{
		LOG->Warn( "FAIL: a stop can't be exact" );
		++g_iFailures;
	}
	CalcRowsBuild( nd, td, v );
	CheckInt( "rows with a stop", v.size(), 4 );
	CheckRow( v, 1, 1 | 8, 1.0f );
	CheckRow( v, 2, 2, 4.0f );
	CheckRow( v, 3, 2 | 4, 4.75f );

	/* A zero-length stop is dropped, as Etterna does. */
	td.m_StopSegments[0].m_fStopSeconds = 0;
	if( !CalcRowsTimingIsExact(td) )
	{
		LOG->Warn( "FAIL: a stop of 0 seconds should be ignored" );
		++g_iFailures;
	}

	/* No notes, no rows. */
	NoteData empty;
	empty.SetNumTracks( 4 );
	CalcRowsBuild( empty, td, v );
	CheckInt( "no rows", v.size(), 0 );
}

int main( int argc, char *argv[] )
{
	test_handle_args( argc, argv );
	test_init();

	TestCalc();
	TestSkillRating();
	TestRows();

	if( g_iFailures )
		LOG->Warn( "%i checks failed", g_iFailures );
	else
		LOG->Info( "minacalc: all checks passed" );

	test_deinit();
	return g_iFailures? 1: 0;
}
