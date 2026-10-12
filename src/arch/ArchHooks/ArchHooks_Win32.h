#ifndef ARCH_HOOKS_WIN32_H
#define ARCH_HOOKS_WIN32_H

#include "ArchHooks.h"
class ArchHooks_Win32: public ArchHooks
{
public:
	ArchHooks_Win32();
	~ArchHooks_Win32();
	void DumpDebugInfo();

	void SetTime( tm newtime );
};

#undef ARCH_HOOKS
#define ARCH_HOOKS ArchHooks_Win32

#endif
