/* LightsDriver_Win32Parallel - Parallel port-based lights for Windows */

#ifndef LightsDriver_Win32Parallel_H
#define LightsDriver_Win32Parallel_H

#include "LightsDriver.h"

/* Same output as LightsDriver_LinuxParallel: one byte with a bit per cabinet light.
 * Windows won't let a program touch I/O ports, so this goes through InpOut
 * (inpoutx64.dll or inpout32.dll), which is loaded when the driver starts.  If it's
 * not there the driver does nothing, like the null one. */
class LightsDriver_Win32Parallel : public LightsDriver
{
public:
	LightsDriver_Win32Parallel();
	virtual ~LightsDriver_Win32Parallel();

	virtual void Set( const LightsState *ls );

private:
	void *m_hDll;		// HMODULE
	void *m_pOut32;		// the DLL's Out32
	int m_iPort;
	int m_iLastOutput;	// -1 until something has been written
	float m_fLastWriteTime;
};

#endif
