#include "global.h"
#include "MemoryCardDriverThreaded_Windows.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageFileManager.h"
#include "Profile.h"
#include "PrefsManager.h"
#include "Foreach.h"

#include <windows.h>

const CString TEMP_MOUNT_POINT = "@mctemp/";

/* How often to look for cards.  The mounting thread holds the pause lock while it
 * sleeps, so this is also the longest the game can wait when it pauses the thread. */
static const int POLL_MICROSECONDS = 200*1000;

/* Look at the type of every drive this often (in polls) even if none appeared or went away. */
static const int TYPE_SCAN_POLLS = 10;

MemoryCardDriverThreaded_Windows::MemoryCardDriverThreaded_Windows()
{
	m_iLastLogicalDrives = 0;
	m_iPollsSinceTypeScan = 0;
	m_cIgnoredDrive = 0;
	m_bRetest = false;

	/* Without this, touching a drive with no disk in it, or a card that was pulled out,
	 * pops up a "There is no disk in the drive" box and blocks the caller until someone
	 * clicks it.  Threads inherit the process setting. */
	m_iOldErrorMode = GetErrorMode();
	SetErrorMode( m_iOldErrorMode | SEM_FAILCRITICALERRORS );

	this->StartThread();
}

MemoryCardDriverThreaded_Windows::~MemoryCardDriverThreaded_Windows()
{
	this->StopThread();
	SetErrorMode( m_iOldErrorMode );
}

/* The drive the game runs from, as an upper case letter, or 0.  If that is a removable
 * drive, it is not a memory card. */
static char GetProgramDriveLetter()
{
	char szPath[MAX_PATH];
	DWORD iLen = GetModuleFileNameA( NULL, szPath, sizeof(szPath) );
	if( iLen < 2 || iLen >= sizeof(szPath) || szPath[1] != ':' )
		return 0;
	return (char) toupper( szPath[0] );
}

/* Get the volume serial number, if there is a readable volume in this drive. */
static bool GetVolumeSerial( char cLetter, unsigned long &iSerialOut )
{
	const char szRoot[] = { cLetter, ':', '\\', '\0' };
	DWORD iSerial = 0;
	if( !GetVolumeInformationA( szRoot, NULL, 0, &iSerial, NULL, NULL, NULL, 0 ) )
		return false;
	iSerialOut = iSerial;
	return true;
}

/* Make sure we can really create a file and have it written out.  Use a name that
 * nobody else is likely to have, since we delete it again. */
static bool TestWrite( const CString &sRoot )
{
	CString sFile = sRoot + "StepMania write test.tmp";
	HANDLE h = CreateFileA( sFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL );
	if( h == INVALID_HANDLE_VALUE )
		return false;

	DWORD iWritten = 0;
	bool bOK = !!WriteFile( h, "x", 1, &iWritten, NULL ) && iWritten == 1;
	bOK = bOK && FlushFileBuffers( h );
	CloseHandle( h );
	return bOK;
}

void MemoryCardDriverThreaded_Windows::ResetUsbStorage()
{
	/* The mounting thread will read the names of the cards again. */
	LockMut( m_mutexStorageDevices );
	m_bRetest = true;
}

void MemoryCardDriverThreaded_Windows::MountThreadDoOneUpdate()
{
	/* Asking for the drive letters is cheap.  Look at the type of the drives only when
	 * one appeared or went away, and now and then in case a drive changed its type.  The
	 * volume of a removable drive has to be asked for every time, since a card reader
	 * keeps its letter when the card is taken out. */
	DWORD iLogicalDrives = GetLogicalDrives();
	if( iLogicalDrives != m_iLastLogicalDrives || ++m_iPollsSinceTypeScan >= TYPE_SCAN_POLLS )
	{
		m_iPollsSinceTypeScan = 0;
		const char cProgramDrive = GetProgramDriveLetter();

		m_vRemovableLetters.clear();
		for( int i=2; i<26; i++ )	// skip a: and b:
		{
			if( !(iLogicalDrives & (1 << i)) )
				continue;
			const char cLetter = (char) ('A'+i);
			const char szRoot[] = { cLetter, ':', '\\', '\0' };
			if( GetDriveTypeA(szRoot) != DRIVE_REMOVABLE )
				continue;
			if( cLetter == cProgramDrive )
			{
				if( m_cIgnoredDrive != cLetter )
					LOG->Trace( "Ignoring removable drive %c: because the game runs from it", cLetter );
				m_cIgnoredDrive = cLetter;
				continue;
			}
			m_vRemovableLetters.push_back( cLetter );
		}
		m_iLastLogicalDrives = iLogicalDrives;
	}

	/* Which of them have a card in them right now? */
	vector<Drive> vNow;
	FOREACH_CONST( char, m_vRemovableLetters, c )
	{
		Drive d;
		d.bRereadName = false;
		if( !GetVolumeSerial( *c, d.iVolumeSerial ) )
			continue;
		d.cLetter = *c;
		vNow.push_back( d );
	}

	bool bChanged = false;

	/* Cards that were taken out, or swapped for another one between two looks.  A swapped
	 * card is dropped now and picked up as a new card on the next update, so that the game
	 * sees the old one leave. */
	vector<char> vDroppedNow;
	for( unsigned i=0; i<m_vDrives.size(); i++ )
	{
		const Drive &old = m_vDrives[i];
		bool bStillThere = false;
		for( unsigned j=0; j<vNow.size(); j++ )
			if( vNow[j].cLetter == old.cLetter && vNow[j].iVolumeSerial == old.iVolumeSerial )
				bStillThere = true;
		if( bStillThere )
			continue;

		LOG->Trace( "Memory card disconnected from %c:", old.cLetter );
		vDroppedNow.push_back( old.cLetter );
		m_vDrives.erase( m_vDrives.begin()+i );
		--i;
		bChanged = true;
	}

	/* New cards. */
	for( unsigned i=0; i<vNow.size(); i++ )
	{
		Drive &d = vNow[i];

		bool bKnown = find( vDroppedNow.begin(), vDroppedNow.end(), d.cLetter ) != vDroppedNow.end();
		for( unsigned j=0; j<m_vDrives.size(); j++ )
			if( m_vDrives[j].cLetter == d.cLetter )
				bKnown = true;
		if( bKnown )
			continue;

		d.usbd.SetOsMountDir( ssprintf("%c:\\", d.cLetter) );
		d.usbd.sSerial = ssprintf( "%08lX", d.iVolumeSerial );
		LOG->Trace( "Memory card connected to %c: (volume serial %s)", d.cLetter, d.usbd.sSerial.c_str() );
		m_vDrives.push_back( d );
		bChanged = true;
	}

	bool bRetest;
	{
		LockMut( m_mutexStorageDevices );
		bRetest = m_bRetest;
		m_bRetest = false;
	}
	if( bRetest )
	{
		for( unsigned i=0; i<m_vDrives.size(); i++ )
			m_vDrives[i].bRereadName = true;
	}

	/* Test the cards and read the profile names, unless the cards are locked in. */
	if( ShouldDoOsMount() )
	{
		for( unsigned i=0; i<m_vDrives.size(); i++ )
		{
			UsbStorageDevice &d = m_vDrives[i].usbd;
			if( !d.bNeedsWriteTest && !m_vDrives[i].bRereadName )
				continue;

			/* A card the game has flushed is only read again, not written to. */
			if( d.bNeedsWriteTest )
				d.bWriteTestSucceeded = TestWrite( d.sOsMountDir );
			d.bNeedsWriteTest = false;
			m_vDrives[i].bRereadName = false;

			// read name
			this->Mount( &d, TEMP_MOUNT_POINT );
			FILEMAN->FlushDirCache( TEMP_MOUNT_POINT );
			Profile profile;
			CString sProfileDir = TEMP_MOUNT_POINT + PREFSMAN->m_sMemoryCardProfileSubdir + '/';
			profile.LoadEditableDataFromDir( sProfileDir );
			d.sName = profile.GetDisplayName();
			UnmountMountPoint( TEMP_MOUNT_POINT );

			LOG->Trace( "WriteTest: %s, Name: %s", d.bWriteTestSucceeded ? "succeeded" : "failed", d.sName.c_str() );
			bChanged = true;
		}
	}

	if( bChanged )
	{
		LockMut( m_mutexStorageDevices );
		m_bStorageDevicesChanged = true;
		m_vStorageDevices.clear();
		for( unsigned i=0; i<m_vDrives.size(); i++ )
			m_vStorageDevices.push_back( m_vDrives[i].usbd );
	}

	CHECKPOINT;

	usleep( POLL_MICROSECONDS );
}

void MemoryCardDriverThreaded_Windows::Mount( UsbStorageDevice* pDevice, CString sMountPoint )
{
	ASSERT( !pDevice->sOsMountDir.empty() );

	UnmountMountPoint( sMountPoint );

	FILEMAN->Mount( "dir", pDevice->sOsMountDir, sMountPoint.c_str() );
	LOG->Trace( "FILEMAN->Mount %s %s", pDevice->sOsMountDir.c_str(), sMountPoint.c_str() );
}

void MemoryCardDriverThreaded_Windows::Unmount( UsbStorageDevice* pDevice, CString sMountPoint )
{
	if( pDevice->sOsMountDir.empty() )
		return;

	/* The card is gone.  Don't leave its drive mounted in the game's file tree, where the
	 * letter could be handed out to something else.  There is nothing to do for Windows. */
	UnmountMountPoint( sMountPoint );
}

/* Flush every file under a directory.  Returns how many were flushed. */
static int FlushTree( const CString &sDir, int iDepth )
{
	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA( sDir + "*", &fd );
	if( hFind == INVALID_HANDLE_VALUE )
		return 0;

	int iFlushed = 0;
	do
	{
		const CString sName = fd.cFileName;
		if( sName == "." || sName == ".." )
			continue;

		if( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		{
			if( iDepth < 8 )
				iFlushed += FlushTree( sDir + sName + "/", iDepth+1 );
			continue;
		}

		HANDLE h = CreateFileA( sDir + sName, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, 0, NULL );
		if( h == INVALID_HANDLE_VALUE )
			continue;
		if( FlushFileBuffers(h) )
			++iFlushed;
		CloseHandle( h );
	} while( FindNextFileA(hFind, &fd) );
	FindClose( hFind );

	return iFlushed;
}

void MemoryCardDriverThreaded_Windows::Flush( UsbStorageDevice* pDevice )
{
	if( pDevice->sOsMountDir.empty() )
		return;

	/* Windows holds on to what was written for a while, so a card pulled right after a
	 * save can lose it.  Flushing a handle to the volume writes out everything dirty on
	 * it.  That wants write access to the volume, which an ordinary user has for
	 * removable media but not necessarily for every kind of device. */
	const CString sVolume = ssprintf( "\\\\.\\%c:", pDevice->sOsMountDir[0] );
	DWORD iError = 0;
	HANDLE h = CreateFileA( sVolume, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL );
	if( h != INVALID_HANDLE_VALUE )
	{
		if( FlushFileBuffers(h) )
		{
			CloseHandle( h );
			LOG->Trace( "Flushed volume %s", sVolume.c_str() );
			return;
		}
		iError = GetLastError();
		CloseHandle( h );
	}
	else
	{
		iError = GetLastError();
	}

	/* Flush the files we wrote instead, which are the ones under the profile directory. */
	const CString sDir = pDevice->sOsMountDir + PREFSMAN->m_sMemoryCardProfileSubdir + "/";
	int iFlushed = FlushTree( sDir, 0 );
	LOG->Trace( "Couldn't flush volume %s (error %lu); flushed %d files in %s instead",
		sVolume.c_str(), (unsigned long) iError, iFlushed, sDir.c_str() );
}
