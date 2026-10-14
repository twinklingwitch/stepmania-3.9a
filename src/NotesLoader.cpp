#include "global.h"
#include "NotesLoader.h"
#include "NoteTypes.h"
#include "GameManager.h"
#include "RageUtil.h"

#include <algorithm>

void NotesLoader::GetSongFiles( const CString &sDir, const CString &sExtension, CStringArray &out )
{
	GetDirListing( sDir + "*." + sExtension, out );
	for( int i = (int) out.size()-1; i >= 0; --i )
		if( out[i].Left(1) == "." )
			out.erase( out.begin()+i );
}

static bool CompareNoCase( const CString &a, const CString &b )
{
	return a.CompareNoCase( b ) < 0;
}

void NotesLoader::GetSimfiles( const CString &sDir, const CString &sExtension, CStringArray &out )
{
	GetSongFiles( sDir, sExtension, out );
	sort( out.begin(), out.end(), CompareNoCase );

	CString sFolder = sDir;
	if( sFolder.Right(1) == "/" )
		sFolder.erase( sFolder.size()-1 );
	sFolder = Basename( sFolder );
	for( unsigned i = 1; i < out.size(); ++i )
	{
		if( SetExtension(out[i], "").CompareNoCase(sFolder) != 0 )
			continue;
		const CString s = out[i];
		out.erase( out.begin()+i );
		out.insert( out.begin(), s );
		break;
	}
}

bool NotesLoader::Loadable( CString sPath )
{
	CStringArray list;
	GetApplicableFiles( sPath, list );
	return !list.empty();
}

void NotesLoader::GetMainAndSubTitlesFromFullTitle( const CString sFullTitle, CString &sMainTitleOut, CString &sSubTitleOut )
{
	const CString sLeftSeps[]  = { " -", " ~", " (", " [" };

	for( unsigned i=0; i<ARRAYSIZE(sLeftSeps); i++ )
	{
		int iBeginIndex = sFullTitle.Find( sLeftSeps[i] );
		if( iBeginIndex == -1 )
			continue;
		sMainTitleOut = sFullTitle.Left( iBeginIndex );
		sSubTitleOut = sFullTitle.substr( iBeginIndex+1, sFullTitle.GetLength()-iBeginIndex+1 );
		return;
	}
	sMainTitleOut = sFullTitle; 
	sSubTitleOut = ""; 
};	

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
