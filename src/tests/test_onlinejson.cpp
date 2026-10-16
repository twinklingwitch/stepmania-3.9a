/* Checks the JSON reader and writer used for the online server: what the
 * server sends (escapes, surrogate pairs, nesting, numbers) reads correctly,
 * broken input is refused, and floats go out with enough digits to come back
 * as the same float. */
#include "global.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "OnlineHttp.h"
#include "OnlineInputLog.h"
#include "OnlineJson.h"
#include "OnlineRequest.h"
#include "test_misc.h"

#include <stdlib.h>

static int g_iFailures = 0;

static void Check( bool b, const char *szWhat )
{
	if( b )
		return;
	LOG->Warn( "FAIL: %s", szWhat );
	++g_iFailures;
}

static bool Parses( const char *sz, JsonValue &v )
{
	CString sError;
	return JsonValue::Parse( sz, v, sError );
}

int main( int argc, char *argv[] )
{
	test_handle_args( argc, argv );
	test_init();

	{
		JsonValue v;
		Check( Parses("{ \"error\": { \"code\": \"rate_limited\", \"message\": \"Too many\", \"details\": { \"retry_after\": 30 } } }", v), "error envelope parses" );
		Check( v["error"]["code"].GetString() == "rate_limited", "nested string" );
		Check( v["error"]["details"]["retry_after"].GetInt() == 30, "nested number" );
		Check( v["error"]["nothing"]["deeper"].IsNull(), "missing members are null" );
		Check( v["error"]["code"].GetInt(7) == 7, "wrong type gives the fallback" );
	}

	{
		JsonValue v;
		Check( Parses("[1, -2.5, 1e3, true, false, null, \"a\\\"b\\\\c\\/d\\n\"]", v), "array parses" );
		Check( v.size() == 7, "array size" );
		Check( v[1].GetNumber() == -2.5, "negative float" );
		Check( v[2].GetInt() == 1000, "exponent" );
		Check( v[3].GetBool() && !v[4].GetBool(true), "booleans" );
		Check( v[5].IsNull(), "null" );
		Check( v[6].GetString() == "a\"b\\c/d\n", "escapes" );
		Check( v[99].IsNull(), "out of range is null" );
	}

	{
		JsonValue v;
		Check( Parses("\"\\u00e9\\u65e5\\ud83c\\udfb5\"", v), "unicode escapes parse" );
		Check( v.GetString() == "\xc3\xa9\xe6\x97\xa5\xf0\x9f\x8e\xb5", "unicode escapes become UTF-8" );
	}

	{
		static const char *bad[] = {
			"", "{", "[1,]", "{\"a\" 1}", "{\"a\":1,}", "tru", "\"abc", "\"\\x\"", "\"\\ud800\"",
			"01x", "[1] 2", "-", "\"a\nb\"", "{1:2}", "nan", "1e999"
		};
		for( unsigned i = 0; i < ARRAYSIZE(bad); ++i )
		{
			JsonValue v;
			Check( !Parses(bad[i], v), ssprintf("refuses %s", bad[i]) );
		}

		CString sDeep;
		for( int i = 0; i < 200; ++i )
			sDeep += "[";
		JsonValue v;
		Check( !Parses(sDeep, v), "refuses deep nesting" );
	}

	{
		JsonValue o = JsonValue::Object();
		o.Set( "score_key", JsonValue::String("S123") );
		o.Set( "rate", JsonValue::Float(1.0f) );
		o.Set( "wife", JsonValue::Float(0.93121f) );
		o.Set( "n", JsonValue::Int(-5) );
		JsonValue &a = o.Set( "list", JsonValue::Array() );
		a.Append( JsonValue::Bool(true) );
		a.Append( JsonValue() );
		o.Set( "raw", JsonValue::Raw("{\"x\":[1,2]}") );
		o.Set( "text", JsonValue::String("tab\there \"q\" \x01") );
		o.Set( "n", JsonValue::Int(6) );	// replaces, keeps its place
		const CString s = o.Write();
		Check( s == "{\"score_key\":\"S123\",\"rate\":1,\"wife\":0.931209981,\"n\":6,\"list\":[true,null],\"raw\":{\"x\":[1,2]},\"text\":\"tab\\there \\\"q\\\" \\u0001\"}",
			ssprintf("writes %s", s.c_str()) );

		JsonValue back;
		Check( Parses(s, back), "reads what it wrote" );
		Check( (float) back["wife"].GetNumber() == 0.93121f, "a float comes back as the same float" );
		Check( back["raw"]["x"][1].GetInt() == 2, "raw JSON is kept" );
		Check( back.Write() == s, "numbers keep the text they were read from" );
	}

	{
		/* Text that isn't UTF-8 goes out as Latin-1 escapes. */
		Check( JsonQuote("\x82\xa0") == "\"\\u0082\\u00a0\"", "Shift-JIS bytes are escaped" );
		Check( JsonQuote("\xc3\xa9") == "\"\xc3\xa9\"", "UTF-8 is kept" );
		Check( JsonFloatText(0.1f) == "0.100000001", "%.9g" );
	}

	{
		/* The input log: every kind of event, as section 7.1 writes them. */
		OnlineLogSettings s;
		memset( &s, 0, sizeof(s) );
		s.fJudgeScale = 1; s.fMarvelous = 0.0225f; s.fBoo = 0.18f; s.bShowMarvelous = true;
		OnlineInputLog log;
		log.Begin( 4, 1.5f, -0.012f, "{\"offset\":0.048,\"bpms\":[[0,175]],\"stops\":[]}", s );
		log.Release( 2, 0.5f );	// never pressed: not logged
		log.Press( false, 1, 1.25f, 1.2f, 2.9166667f, 96, 0.0123f, TNS_MARVELOUS );
		log.Press( false, 3, 1.5f, 1.4f, 2.9166667f, -1, 0.5f, TNS_NONE );
		log.Press( true, 1, 1.6f, 1.6f, 2.9166667f, 120, -0.01f, TNS_HIT_MINE );
		Check( log.IsHeld(1) && log.IsHeld(3) && !log.IsHeld(0), "held columns" );
		log.Release( 1, 1.75f );
		log.Miss( 0, 144, 2.0f );
		log.Hold( 3, 96, false, 2.5f );
		log.End( 3.0f );
		const CString sJson = log.ToJson();
		JsonValue v;
		Check( Parses(sJson, v), "the input log is JSON" );
		Check( v["rate"].GetNumber() == 1.5 && v["columns"].GetInt() == 4, "log header" );
		Check( v["settings"]["windows"]["marvelous"].Write() == "0.0225000009", "windows as %.9g" );
		Check( v["settings"]["scoring_type"].GetString() == "max2", "scoring type" );
		const JsonValue &e = v["events"];
		Check( e.size() == 7, ssprintf("7 events, not %u", e.size()) );
		Check( e[0u].Write() == "[\"P\",1,1.25,1.20000005,2.91666675,96,0.0122999996,\"marvelous\"]", "press: " + e[0u].Write() );
		Check( e[1].Write() == "[\"P\",3,1.5,1.39999998,2.91666675,-1,0,\"none\"]", "ghost tap: " + e[1].Write() );
		Check( e[2][0u].GetString() == "C" && e[2][7].GetString() == "hit_mine", "mine cross" );
		Check( e[3].Write() == "[\"R\",1,1.75]", "release: " + e[3].Write() );
		Check( e[4].Write() == "[\"X\",0,144,2]", "miss: " + e[4].Write() );
		Check( e[5].Write() == "[\"H\",3,96,\"NG\",2.5]", "hold: " + e[5].Write() );
		Check( e[6].Write() == "[\"E\",3]", "end: " + e[6].Write() );
	}

	{
		Check( OnlineHttp::CheckURL("https://scores.example.org", false).empty(), "https is fine" );
		Check( OnlineHttp::CheckURL("HTTPS://Scores.example.org:8443/x", false).empty(), "https with a port" );
		Check( OnlineHttp::CheckURL("http://127.0.0.1:8080", false).empty(), "http to this machine" );
		Check( OnlineHttp::CheckURL("http://localhost", false).empty(), "http to localhost" );
		Check( OnlineHttp::CheckURL("http://[::1]:80/", false).empty(), "http to ::1" );
		Check( !OnlineHttp::CheckURL("http://scores.example.org", false).empty(), "no plain http elsewhere" );
		Check( OnlineHttp::CheckURL("http://scores.example.org", true).empty(), "unless allowed" );
		Check( !OnlineHttp::CheckURL("http://localhost.example.org", false).empty(), "localhost lookalike" );
		Check( !OnlineHttp::CheckURL("http://127.0.0.1@evil.example.org", false).empty(), "user@host" );
		Check( !OnlineHttp::CheckURL("ftp://scores.example.org", false).empty(), "other schemes" );
		Check( !OnlineHttp::CheckURL("scores.example.org", false).empty(), "no scheme" );

		CString sZ;
		Check( OnlineHttp::Gzip(CString(1000, 'a'), sZ) && sZ.size() < 100 && (unsigned char) sZ[0] == 0x1f && (unsigned char) sZ[1] == 0x8b, "gzip" );
	}

	{
		/* The server's times, in 64-bit seconds: long is 32 bits on Windows. */
		Check( OnlineParseTime("1970-01-01T00:00:00Z") == 0, "the epoch" );
		Check( OnlineParseTime("2026-10-16T12:00:00Z") == 1792152000, "this year" );
		Check( OnlineParseTime("2038-01-19T03:14:08Z") == 2147483648LL, "past 2038" );
		Check( OnlineParseTime("2100-01-01T00:00:00Z") == 4102444800LL, "2100" );
		Check( OnlineParseTime("1969-12-31T23:59:59Z") == -1, "just before the epoch" );
		Check( OnlineParseTime("2026-10-16T12:00:00.250Z") == 1792152000, "fractions of a second are left off" );
		Check( OnlineParseTime("soon") == 0, "not a time" );
	}

	if( g_iFailures )
	{
		LOG->Warn( "%i failures", g_iFailures );
		exit( 1 );
	}
	LOG->Info( "test_onlinejson: all passed" );
	exit( 0 );
}
