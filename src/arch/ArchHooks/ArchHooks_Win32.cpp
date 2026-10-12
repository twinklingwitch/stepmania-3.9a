#include "global.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "ArchHooks_Win32.h"
#include "StepMania.h"
#include "RageUtil.h"

#include <windows.h>
#include <mmsystem.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/* Ctrl-C in a console, or the console being closed. */
static BOOL WINAPI ConsoleCtrlHandler( DWORD iType )
{
	switch( iType )
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		ExitGame();
		return TRUE;
	default:
		return FALSE;
	}
}

/* There's no crash handler, but at least get the exception and where it happened into
 * the log, so a crash report is something more than "it vanished".  Then let Windows
 * deal with it. */
static LONG WINAPI UnhandledExceptionLogger( EXCEPTION_POINTERS *pInfo )
{
	if( LOG == NULL )
		return EXCEPTION_CONTINUE_SEARCH;

	const void *pAddress = pInfo->ExceptionRecord->ExceptionAddress;
	char szModule[MAX_PATH] = "";
	HMODULE hModule = NULL;
	unsigned long iOffset = 0;
	if( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR) pAddress, &hModule ) )
	{
		GetModuleFileNameA( hModule, szModule, sizeof(szModule) );
		iOffset = (unsigned long) ((const char *) pAddress - (const char *) hModule);
	}

	LOG->Warn( "Unhandled exception 0x%08lx at %p (%s+0x%lx)", pInfo->ExceptionRecord->ExceptionCode,
		pAddress, szModule, iOffset );
	LOG->Warn( "Checkpoints:\n%s", Checkpoints::GetLogs("\n") );
	LOG->Flush();
	return EXCEPTION_CONTINUE_SEARCH;
}

#if defined(HAVE_TLS)
static __thread int g_iTestTLS = 0;

static int TestTLSThread( void *p )
{
	g_iTestTLS = 2;
	return 0;
}

static void TestTLS()
{
	g_iTestTLS = 1;

	RageThread TestThread;
	TestThread.SetName( "TestTLS" );
	TestThread.Create( TestTLSThread, NULL );
	TestThread.Wait();

	if( g_iTestTLS == 1 )
		RageThread::SetSupportsTLS( true );
}
#endif

static bool g_bTimerPeriodSet = false;

ArchHooks_Win32::ArchHooks_Win32()
{
	SetConsoleCtrlHandler( ConsoleCtrlHandler, TRUE );
	SetUnhandledExceptionFilter( UnhandledExceptionLogger );

	/* Make Sleep() and the multimedia timers tick every millisecond.  SDL wants this too. */
	g_bTimerPeriodSet = timeBeginPeriod(1) == TIMERR_NOERROR;

#if defined(HAVE_TLS)
	TestTLS();
#endif
}

ArchHooks_Win32::~ArchHooks_Win32()
{
	if( g_bTimerPeriodSet )
		timeEndPeriod(1);
}

static HANDLE GetSleepTimer();

static CString GetWindowsVersion()
{
	HMODULE hNtdll = GetModuleHandleA( "ntdll.dll" );
	if( hNtdll == NULL )
		return "(unknown)";

	/* GetVersionEx lies to programs without a compatibility manifest; ask ntdll. */
	typedef LONG (WINAPI *RtlGetVersionFn)( OSVERSIONINFOW * );
	RtlGetVersionFn pRtlGetVersion = (RtlGetVersionFn) (void *) GetProcAddress( hNtdll, "RtlGetVersion" );
	CString sRet = "Windows";
	if( pRtlGetVersion != NULL )
	{
		OSVERSIONINFOW vi;
		memset( &vi, 0, sizeof(vi) );
		vi.dwOSVersionInfoSize = sizeof(vi);
		if( pRtlGetVersion(&vi) == 0 )
			sRet += ssprintf( " %lu.%lu build %lu", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber );
	}

	typedef const char *(CDECL *WineGetVersionFn)();
	WineGetVersionFn pWineGetVersion = (WineGetVersionFn) (void *) GetProcAddress( hNtdll, "wine_get_version" );
	if( pWineGetVersion != NULL )
		sRet += ssprintf( " (Wine %s)", pWineGetVersion() );

	return sRet;
}

void ArchHooks_Win32::DumpDebugInfo()
{
	LOG->Info( "OS: %s", GetWindowsVersion().c_str() );
	LOG->Info( "Crash handler: not available on Windows (unhandled exceptions are logged)" );
	LOG->Info( "Timer period: %s", g_bTimerPeriodSet? "1ms":"default" );
	LOG->Info( "Sleeping with: %s", GetSleepTimer() != NULL? "high resolution timer":"Sleep()" );
	LOG->Info( "Threads library: winpthreads" );
}

void ArchHooks_Win32::SetTime( tm newtime )
{
	SYSTEMTIME st;
	memset( &st, 0, sizeof(st) );
	st.wYear = (WORD) (newtime.tm_year+1900);
	st.wMonth = (WORD) (newtime.tm_mon+1);
	st.wDay = (WORD) newtime.tm_mday;
	st.wHour = (WORD) newtime.tm_hour;
	st.wMinute = (WORD) newtime.tm_min;
	st.wSecond = (WORD) newtime.tm_sec;

	LOG->Trace( "SetLocalTime( %04i-%02i-%02i %02i:%02i:%02i )",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
	if( !SetLocalTime(&st) )
		LOG->Warn( "SetLocalTime failed (error %lu); setting the clock needs administrator rights", GetLastError() );
}

/* Use the performance counter, not the wall clock: input timestamps and the music
 * position are compared against this, so it must never jump. */
static int64_t GetMicrosecondsMonotonic()
{
	static LARGE_INTEGER iFrequency;
	if( iFrequency.QuadPart == 0 )
		QueryPerformanceFrequency( &iFrequency );

	LARGE_INTEGER iCounter;
	QueryPerformanceCounter( &iCounter );

	/* Split it up so a long uptime can't overflow the multiplication. */
	const int64_t iSecs = iCounter.QuadPart / iFrequency.QuadPart;
	const int64_t iRemainder = iCounter.QuadPart % iFrequency.QuadPart;
	return iSecs*1000000 + iRemainder*1000000 / iFrequency.QuadPart;
}

int64_t ArchHooks::GetMicrosecondsSinceStart( bool bAccurate )
{
	static int64_t iStartTime = GetMicrosecondsMonotonic();
	int64_t ret = GetMicrosecondsMonotonic() - iStartTime;
	if( bAccurate )
		ret = FixupTimeIfBackwards( ret );
	return ret;
}

/* A high resolution waitable timer wakes up on time to within a fraction of a
 * millisecond (Windows 10 1803 and later).  Several threads sleep at once, so each
 * gets its own. */
static HANDLE GetSleepTimer()
{
	static __thread HANDLE hTimer = NULL;
	static __thread bool bTried = false;
	if( !bTried )
	{
		bTried = true;
		hTimer = CreateWaitableTimerExW( NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
	}
	return hTimer;
}

void ArchUSleep( unsigned long iMicroseconds )
{
	HANDLE hTimer = GetSleepTimer();
	if( hTimer != NULL )
	{
		/* Negative means relative, in units of 100ns. */
		LARGE_INTEGER due;
		due.QuadPart = -(LONGLONG) iMicroseconds * 10;
		if( SetWaitableTimer(hTimer, &due, 0, NULL, NULL, FALSE) )
		{
			WaitForSingleObject( hTimer, INFINITE );
			return;
		}
	}

	Sleep( (iMicroseconds + 999) / 1000 );
}
