#ifndef GLOBAL_H
#define GLOBAL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Platform-specific fixes. */
#if defined(UNIX)
#include "archutils/Unix/arch_setup.h"
#elif defined(WINDOWS)
#include "archutils/Win32/arch_setup.h"
#endif

/* Set one of these in arch_setup.h.  (Don't bother trying to fall back on BYTE_ORDER
 * if it was already set; too many systems are missing endian.h.) */
#if !defined(ENDIAN_LITTLE) && !defined(ENDIAN_BIG)
#error Neither ENDIAN_LITTLE nor ENDIAN_BIG defined
#endif

/* Define standard endianness macros, if they're missing. */
#if defined(HAVE_ENDIAN_H)
#include <endian.h>
#elif defined(HAVE_MACHINE_ENDIAN_H)
#include <machine/endian.h>
#else
#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN 4321
#if defined(ENDIAN_LITTLE)
#define BYTE_ORDER LITTLE_ENDIAN
#elif defined(ENDIAN_BIG)
#define BYTE_ORDER BIG_ENDIAN
#endif

#endif

/* Make sure everyone has min and max: */
#include <algorithm>

/* Everything will need string for one reason or another: */
#include <string>

/* And vector: */
#include <vector>

#include <climits>
#include <cstring>
#include <strings.h>

#include <stdint.h>

using namespace std;

#ifdef ASSERT
#undef ASSERT
#endif

/* RageThreads defines (don't pull in all of RageThreads.h here) */
namespace Checkpoints
{
	void SetCheckpoint( const char *file, int line, const char *message );
};
#define CHECKPOINT (Checkpoints::SetCheckpoint(__FILE__, __LINE__, NULL))
#define CHECKPOINT_M(m) (Checkpoints::SetCheckpoint(__FILE__, __LINE__, m))


/* Define a macro to tell the compiler that a function doesn't return.  This just
 * improves compiler warnings. */
#define NORETURN __attribute__ ((__noreturn__))

void NORETURN sm_crash( const char *reason = "Internal error" );

/* Assertion that sets an optional message and brings up the crash handler, so
 * we get a backtrace.  This should probably be used instead of throwing an
 * exception in most cases we expect never to happen (but not in cases that
 * we do expect, such as DSound init failure.) */
#define FAIL_M(MESSAGE) { CHECKPOINT_M(MESSAGE); sm_crash(MESSAGE); }
#define ASSERT_M(COND, MESSAGE) { if(!(COND)) { FAIL_M(MESSAGE); } }
#define ASSERT(COND) ASSERT_M((COND), "Assertion '" #COND "' failed")

#ifdef DEBUG
#define DEBUG_ASSERT(x)	ASSERT(x)
#else
#define DEBUG_ASSERT(x)
#endif

/* Define a macro to tell the compiler that a function has printf() semantics, to
 * aid warning output. */
#define PRINTF(a,b) __attribute__((format(__printf__,a,b)))

/* Use CStdString: */
#include "StdString.h"

typedef const CString& CCStringRef;

/* Include this here to make sure our assertion handler is always
 * used.  (This file is a dependency of most everything anyway,
 * so there's no real problem putting it here.) */
#include "RageException.h"

#define stricmp strcasecmp
#define strnicmp strncasecmp

#include <cmath>

/* Don't include our own headers here, since they tend to change often. */

#endif

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
