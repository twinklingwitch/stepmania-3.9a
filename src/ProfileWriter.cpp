#include "global.h"
#include "ProfileWriter.h"
#include "RageThreads.h"
#include "RageLog.h"
#include "RageUtil.h"

#include <list>

/* Saving a profile takes tens of milliseconds, or seconds on a memory card, so
 * the main thread builds what is to be written and hands it over here.  There
 * is one thread and one FIFO queue: saves are written in the order they were
 * made and never overlap, so the last one is the one left on disk.
 *
 * Anything that reads a profile directory back has to call WaitForDir first;
 * nothing else stops it from seeing a half-written directory. */

static RageMutex g_Mutex( "ProfileWriter" );
static RageSemaphore g_Wake( "ProfileWriterWake" );
static RageThread g_Thread;
static uint64_t g_iThreadID = 0;
static bool g_bShutdown = false;

static list<ProfileWriteJob *> g_Queue;		// not started yet, oldest first
static bool g_bRunning = false;
static CString g_sRunningDir;

struct Waiter
{
	CString sDir;		// empty: wait for everything
	RageSemaphore sem;
	Waiter( const CString &s ): sDir(s), sem("ProfileWriterWaiter") { }
};
static list<Waiter *> g_Waiters;

static bool OnWriterThread()
{
	return g_iThreadID != 0 && RageThread::GetCurrentThreadID() == g_iThreadID;
}

/* True if a read of sA could see a write to sB, or the other way around:
 * they're the same path, or one is inside the other. */
static bool PathsOverlap( const CString &sA, const CString &sB )
{
	const unsigned n = min( sA.size(), sB.size() );
	return !sA.Left(n).CompareNoCase( sB.Left(n) );
}

/* g_Mutex must be held. */
static bool IsPending( const CString &sDir )
{
	if( g_bRunning && (sDir.empty() || PathsOverlap(sDir, g_sRunningDir)) )
		return true;
	for( list<ProfileWriteJob *>::const_iterator it = g_Queue.begin(); it != g_Queue.end(); ++it )
		if( sDir.empty() || PathsOverlap(sDir, (*it)->m_sDir) )
			return true;
	return false;
}

static int WriterMain( void *pUnused )
{
	g_iThreadID = RageThread::GetCurrentThreadID();

	for(;;)
	{
		g_Wake.Wait();

		ProfileWriteJob *pJob = NULL;
		{
			LockMut( g_Mutex );
			if( g_Queue.empty() )
			{
				if( g_bShutdown )
					break;
				continue;
			}
			pJob = g_Queue.front();
			g_Queue.pop_front();
			g_bRunning = true;
			g_sRunningDir = pJob->m_sDir;
		}

		pJob->Run();

		{
			LockMut( g_Mutex );
			g_bRunning = false;
			g_sRunningDir = "";

			for( list<Waiter *>::iterator it = g_Waiters.begin(); it != g_Waiters.end(); )
			{
				if( IsPending( (*it)->sDir ) )
				{
					++it;
					continue;
				}
				(*it)->sem.Post();
				it = g_Waiters.erase( it );
			}
		}

		/* Freeing the copy of the profile isn't worth making anyone wait for. */
		delete pJob;
	}

	return 0;
}

void ProfileWriter::Queue( ProfileWriteJob *pJob )
{
	{
		LockMut( g_Mutex );
		if( !g_bShutdown )
		{
			if( !g_Thread.IsCreated() )
			{
				g_Thread.SetName( "ProfileWriter" );
				g_Thread.Create( WriterMain, NULL );
			}
			g_Queue.push_back( pJob );
			g_Wake.Post();
			return;
		}
	}

	/* The thread is gone; this is a late save during shutdown. */
	pJob->Run();
	delete pJob;
}

static void WaitFor( const CString &sDir )
{
	if( OnWriterThread() )
		return;

	Waiter w( sDir );
	{
		LockMut( g_Mutex );
		if( !IsPending(sDir) )
			return;
		g_Waiters.push_back( &w );
	}
	w.sem.Wait();
}

void ProfileWriter::WaitForDir( const CString &sDir )
{
	if( sDir.empty() )
		return;
	WaitFor( sDir );
}

void ProfileWriter::WaitForAll()
{
	WaitFor( "" );
}

void ProfileWriter::Shutdown()
{
	WaitForAll();

	{
		LockMut( g_Mutex );
		g_bShutdown = true;
	}

	if( g_Thread.IsCreated() )
	{
		g_Wake.Post();
		g_Thread.Wait();
	}
}
