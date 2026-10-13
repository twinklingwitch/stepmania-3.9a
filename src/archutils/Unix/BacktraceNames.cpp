/* for dladdr: */
#define __USE_GNU
#include "global.h"
#include "BacktraceNames.h"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "RageUtil.h"

#if defined(HAVE_CXA_DEMANGLE)
#include <cxxabi.h>

void BacktraceNames::Demangle()
{
	/* demangle the name using __cxa_demangle() if needed */
	if( Symbol.substr(0, 2) != "_Z" )
		return;
	
	int status = 0;
	char *name = abi::__cxa_demangle( Symbol, 0, 0, &status );
	if( name )
	{
		Symbol = name;
		free( name );
		return;
	}

	switch( status )
	{
	case -1:
		fprintf( stderr, "Out of memory\n" );
		break;
	case -2:
		fprintf( stderr, "Invalid mangled name: %s.\n", Symbol.c_str() );
		break;
	case -3:
		fprintf( stderr, "Invalid arguments.\n" );
		break;
	default:
		fprintf( stderr, "Unexpected __cxa_demangle status: %i", status );
		break;
	}
}
#else
void BacktraceNames::Demangle() { }
#endif


CString BacktraceNames::Format() const
{
	CString ShortenedPath = File;
	if( ShortenedPath != "" )
	{
		/* Abbreviate the module name. */
		size_t slash = ShortenedPath.rfind('/');
		if( slash != ShortenedPath.npos )
			ShortenedPath = ShortenedPath.substr(slash+1);
		ShortenedPath = CString("(") + ShortenedPath + ")";
	}

	CString ret = ssprintf( "%0*lx: ", int(sizeof(void*)*2), Address );
	if( Symbol != "" )
		ret += Symbol + " ";
	ret += ShortenedPath;

	return ret;
}


#if defined(BACKTRACE_LOOKUP_METHOD_DLADDR)
/* This version simply asks libdl, which is more robust. */
#include <dlfcn.h>
void BacktraceNames::FromAddr( const void *p )
{
    Address = (intptr_t) p;

    /*
     * When calling a function that doesn't return, gcc will truncate a function.
     * This results in our return addresses lying just beyond the end of a function.
     * Those addresses are correct; our backtrace addresses are usually a list of
     * return addresses (that's the same thing you'll see in gdb).  dladdr won't work,
     * because they're not actually within the bounds of the function.
     *
     * A generic fix for this would be to adjust the backtrace to be a list of
     * places where control was lost, instead of where it'll come back to; to point
     * to the CALL instead of just beyond the CALL.  That's hard to do generically;
     * the stack only contains the return pointers.  (Some archs don't even contain
     * that: on archs where gcc has to manually store the return address--by pushing
     * the IP--it will omit the push when calling a function that never returns.)
     *
     * Let's try the address we're given, and if that fails to find a symbol, try
     * moving up a few bytes.  This usually makes "__cxa_throw, std::terminate,
     * gsignal" stacks come out right.  It probably won't work if there's no space
     * between one function and the next, because the first lookup will succeed.
     */
    Dl_info di;
    if( !dladdr(p, &di) || di.dli_sname == NULL )
    {
	if( !dladdr( ((char *) p) - 8, &di) )
	    return;
    }

    Symbol = di.dli_sname;
    File = di.dli_fname;
    Offset = (char*)(p)-(char*)di.dli_saddr;
}

#elif defined(BACKTRACE_LOOKUP_METHOD_BACKTRACE_SYMBOLS)
/* This version parses backtrace_symbols(), an doesn't need libdl. */
#include <execinfo.h>
void BacktraceNames::FromAddr( const void *p )
{
    Address = (intptr_t) p;

    char **foo = backtrace_symbols(&p, 1);
    if( foo == NULL )
        return;
    FromString( foo[0] );
    free(foo);
}

/* "path(mangled name+offset) [address]" */
void BacktraceNames::FromString( CString s )
{
    /* Hacky parser.  I don't want to use regexes in the crash handler. */
    CString MangledAndOffset, sAddress;
    unsigned pos = 0;
    while( pos < s.size() && s[pos] != '(' && s[pos] != '[' )
        File += s[pos++];
    TrimRight( File );
    TrimLeft( File );

    if( pos < s.size() && s[pos] == '(' )
    {
        pos++;
        while( pos < s.size() && s[pos] != ')' )
            MangledAndOffset += s[pos++];
    }

    if( MangledAndOffset != "" )
    {
        size_t plus = MangledAndOffset.rfind('+');

        if(plus == MangledAndOffset.npos)
        {
            Symbol = MangledAndOffset;
            Offset = 0;
        }
        else
        {
            Symbol = MangledAndOffset.substr(0, plus);
            CString str = MangledAndOffset.substr(plus);
            if( sscanf(str, "%i", &Offset) != 1 )
                Offset=0;
        }
    }
}
#else
#warning Undefined BACKTRACE_LOOKUP_METHOD_*
void BacktraceNames::FromAddr( const void *p )
{
    Address = intptr_t(p);
    Offset = 0;
    Symbol = "";
    File = "";
}
#endif

/*
 * (c) 2003-2004 Glenn Maynard
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
