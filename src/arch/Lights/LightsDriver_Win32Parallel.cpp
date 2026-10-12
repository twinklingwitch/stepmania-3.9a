//
//    LightsDriver_Win32Parallel - Parallel Port Based Lights Driver for Windows
//
//    Windows 10 and 11 don't let programs touch I/O ports, so this uses InpOut
//    (https://www.highrez.co.uk/downloads/inpout32/), a signed kernel driver with a
//    DLL in front of it.  The driver is installed the first time the DLL is used from
//    an administrator account.  The bits are the same as in LightsDriver_LinuxParallel.
//

#include "global.h"
#include "LightsDriver_Win32Parallel.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "PrefsManager.h"

#include <windows.h>
#include <cstdlib>

#if defined(_WIN64)
static const char *INPOUT_DLL = "inpoutx64.dll";
#else
static const char *INPOUT_DLL = "inpout32.dll";
#endif

static const int DEFAULT_PORT_ADDRESS = 0x378;

/* Lights don't change often, so don't call into the driver on every frame.  Send the
 * byte again now and then anyway, in case the lights board was switched on later. */
static const float REFRESH_SECONDS = 1.0f;

typedef void (WINAPI OUT32)( short iPort, short iData );
typedef unsigned int (WINAPI ISINPOUTDRIVEROPEN)();

/* The port is in hex, with or without the 0x.  Ports on PCI(e) cards are well above 0x378. */
static int GetPortAddress()
{
	const CString &s = PREFSMAN->m_sParallelLightsPort;
	char *pEnd = NULL;
	long iPort = strtol( s.c_str(), &pEnd, 16 );
	if( s.empty() || *pEnd != '\0' || iPort <= 0 || iPort > 0xFFFF )
	{
		LOG->Warn( "Win32Parallel lights: invalid ParallelLightsPort \"%s\"; using 0x%x", s.c_str(), DEFAULT_PORT_ADDRESS );
		return DEFAULT_PORT_ADDRESS;
	}
	return (int) iPort;
}

LightsDriver_Win32Parallel::LightsDriver_Win32Parallel()
{
	m_hDll = NULL;
	m_pOut32 = NULL;
	m_iPort = GetPortAddress();
	m_iLastOutput = -1;
	m_fLastWriteTime = 0;

	HMODULE hDll = LoadLibraryExA( INPOUT_DLL, NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
	if( hDll == NULL )
	{
		LOG->Warn( "Win32Parallel lights: couldn't load %s (error %lu), so there will be no lights.  "
			"Get InpOut from https://www.highrez.co.uk/downloads/inpout32/ and put %s next to "
			"stepmania.exe.  Its driver is installed the first time it runs as an administrator.",
			INPOUT_DLL, (unsigned long) GetLastError(), INPOUT_DLL );
		return;
	}

	OUT32 *pOut32 = (OUT32 *) GetProcAddress( hDll, "Out32" );
	ISINPOUTDRIVEROPEN *pIsDriverOpen = (ISINPOUTDRIVEROPEN *) GetProcAddress( hDll, "IsInpOutDriverOpen" );
	if( pOut32 == NULL )
	{
		LOG->Warn( "Win32Parallel lights: %s has no Out32, so there will be no lights.  Is it really InpOut?", INPOUT_DLL );
		FreeLibrary( hDll );
		return;
	}

	if( pIsDriverOpen != NULL && !pIsDriverOpen() )
	{
		LOG->Warn( "Win32Parallel lights: the InpOut driver isn't running, so there will be no lights.  "
			"Run the game once as an administrator, which installs it." );
		FreeLibrary( hDll );
		return;
	}

	m_hDll = hDll;
	m_pOut32 = (void *) pOut32;
	LOG->Info( "Win32Parallel lights: using %s on port 0x%x", INPOUT_DLL, m_iPort );

	// Reset all bits to zero
	pOut32( (short) m_iPort, 0 );
	m_iLastOutput = 0;
	m_fLastWriteTime = RageTimer::GetTimeSinceStart();
}

LightsDriver_Win32Parallel::~LightsDriver_Win32Parallel()
{
	if( m_hDll == NULL )
		return;

	// Reset all bits to zero
	((OUT32 *) m_pOut32)( (short) m_iPort, 0 );
	FreeLibrary( (HMODULE) m_hDll );
}

void LightsDriver_Win32Parallel::Set( const LightsState *ls )
{
	if( m_pOut32 == NULL )
		return;

	// Cabinet Lights, one bit each
	int i = 0;
	unsigned char output = 0;
	FOREACH_CabinetLight( cl )
	{
		if( ls->m_bCabinetLights[cl] )
			output |= (unsigned char) (1 << i);
		i++;
	}

	float fNow = RageTimer::GetTimeSinceStart();
	if( output == m_iLastOutput && fNow - m_fLastWriteTime < REFRESH_SECONDS )
		return;

	// Send byte to port
	((OUT32 *) m_pOut32)( (short) m_iPort, (short) output );
	m_iLastOutput = output;
	m_fLastWriteTime = fNow;
}
