/* Checks that a file that can't be written whole doesn't replace the one it was
 * going to replace.  RageFile writes to a temporary file and renames it over the
 * old one when it's closed; if the disk fills up (or a quota runs out) part way
 * through, write() writes what fits and returns less than it was asked to, and
 * the part must not be taken for the whole file.  The limit on the size of a file
 * stands in for the full disk. */
#include "global.h"
#include "RageLog.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageUtil.h"
#include "test_misc.h"

#if !defined(WINDOWS)
#include <signal.h>
#include <sys/resource.h>
#endif

static int g_iFailures = 0;

#if !defined(WINDOWS)
static void Check( bool b, const CString &sWhat )
{
	if( b )
		return;
	LOG->Warn( "FAIL: %s", sWhat.c_str() );
	++g_iFailures;
}

static void SetFileSizeLimit( rlim_t iBytes )
{
	struct rlimit rl;
	getrlimit( RLIMIT_FSIZE, &rl );
	rl.rlim_cur = iBytes;
	setrlimit( RLIMIT_FSIZE, &rl );
}

/* Write sData to sFile, with files limited to iLimit bytes, and return what's in the
 * file after. */
static CString WriteWithLimit( const CString &sFile, const CString &sData, rlim_t iLimit, bool bStreamed )
{
	RageFile f;
	if( !f.Open(sFile, RageFile::WRITE | (bStreamed? RageFile::STREAMED: 0)) )
	{
		LOG->Warn( "Couldn't open %s: %s", sFile.c_str(), f.GetError().c_str() );
		return "";
	}
	SetFileSizeLimit( iLimit );
	f.Write( sData );
	f.Close();
	SetFileSizeLimit( RLIM_INFINITY );

	CString sRead;
	RageFile r;
	if( r.Open(sFile) )
		r.Read( sRead, r.GetFileSize() );
	return sRead;
}
#endif

int main( int argc, char *argv[] )
{
	test_handle_args( argc, argv );
	test_init();

#if !defined(WINDOWS)
	/* The limit is enforced with a signal as well as an error. */
	signal( SIGXFSZ, SIG_IGN );

	const CString sFile = "test.write.out";
	const CString sOld = "the old contents\n";

	/* More than the file buffer holds, so that part of it goes out before Close,
	 * and a write to the disk comes up short at the limit. */
	CString sBig;
	for( int i = 0; i < 20000; ++i )
		sBig += ssprintf( "line %05i of the new contents\n", i );

	{
		RageFile f;
		f.Open( sFile, RageFile::WRITE );
		f.Write( sOld );
	}

	/* Writing it all works with no limit. */
	{
		CString s = WriteWithLimit( sFile, sBig, RLIM_INFINITY, false );
		Check( s == sBig, "a file with room is written whole" );
	}

	/* Short of room, the new file isn't taken for the whole of it: what's left is
	 * the old file or the whole new one, never part of the new one. */
	for( int iLimit = 1000; iLimit < 600000; iLimit += 49999 )
	{
		{
			RageFile f;
			f.Open( sFile, RageFile::WRITE );
			f.Write( sOld );
		}
		CString s = WriteWithLimit( sFile, sBig, iLimit, false );
		Check( s == sOld || s == sBig, ssprintf("a limit of %i bytes: the file has %i bytes, not the old %i or the new %i",
			iLimit, int(s.size()), int(sOld.size()), int(sBig.size())) );
	}

	FlushDirCache();
	FILEMAN->Remove( sFile );
	/* The temporary file a failed write leaves behind. */
	FILEMAN->Remove( "new." + sFile + ".new" );
#else
	LOG->Info( "test_file_write: not on Windows" );
#endif

	if( g_iFailures )
	{
		LOG->Warn( "%i failures", g_iFailures );
		test_deinit();
		exit( 1 );
	}
	LOG->Info( "test_file_write: all passed" );
	test_deinit();
	exit( 0 );
}
