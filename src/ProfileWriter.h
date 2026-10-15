/* ProfileWriter - Writes saved profiles to disk on a background thread, in order. */

#ifndef ProfileWriter_H
#define ProfileWriter_H

/* One save of one profile directory.  It owns copies of everything it writes,
 * so Run() never looks at the live Profile. */
class ProfileWriteJob
{
public:
	virtual ~ProfileWriteJob() { }
	virtual void Run() = 0;

	CString m_sDir;
};

namespace ProfileWriter
{
	/* Take ownership of pJob and run it on the writer thread, after every job
	 * queued before it. */
	void Queue( ProfileWriteJob *pJob );

	/* Block until every job that touches sDir (or something inside or around it)
	 * has finished.  Call this before reading, listing, deleting or unmounting
	 * anything in a profile directory.  Does nothing on the writer thread. */
	void WaitForDir( const CString &sDir );
	void WaitForAll();

	/* Finish everything and stop the thread.  Queue() runs jobs immediately
	 * afterwards. */
	void Shutdown();
}

#endif
