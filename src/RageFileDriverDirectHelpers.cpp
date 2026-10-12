#include "global.h"
#include "RageFileDriverDirectHelpers.h"
#include "RageUtil.h"
#include "RageLog.h"

#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <fcntl.h>

#if defined(WINDOWS)
#include <windows.h>

int DoRename( const char *sOld, const char *sNew )
{
	/* Paths are UTF-8 and the process uses a UTF-8 code page (see stepmania.manifest),
	 * so the A functions are fine.  Like rename() on Unix, replace an existing file;
	 * try to write it through so it's no less safe. */
	if( MoveFileExA(sOld, sNew, MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) )
		return 0;

	switch( GetLastError() )
	{
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
		errno = ENOENT;
		break;
	case ERROR_ACCESS_DENIED:
	case ERROR_SHARING_VIOLATION:
		errno = EACCES;
		break;
	default:
		errno = EIO;
		break;
	}
	return -1;
}
#endif

bool PathReady( CString path )
{
	// Try to create directory before writing a temp file.
	CreateDirectories( path );
	
	// Try to write a file.
	const CString sFile = path + "temp";
	int fd = DoOpen( sFile, O_WRONLY|O_CREAT|O_TRUNC, 0644 ); //last arg is a bogus one
	if( fd == -1 )
		return false;

	close( fd );
	remove( sFile );
	return true;
}

/* mkdir -p.  Doesn't fail if Path already exists and is a directory. */
bool CreateDirectories( CString Path )
{
	/* XXX: handle "//foo/bar" paths in Windows */
	CStringArray parts;
	CString curpath;

	/* If Path is absolute, add the initial slash ("ignore empty" will remove it). */
	if( Path.Left(1) == "/" )
		curpath = "/";

	/* Ignore empty, so eg. "/foo/bar//baz" doesn't try to create "/foo/bar" twice. */
	split( Path, "/", parts, true );

	for(unsigned i = 0; i < parts.size(); ++i)
	{
		if( i )
			curpath += "/";
		curpath += parts[i];

#if defined(WINDOWS)
		/* Don't try to create the drive. */
		if( i == 0 && parts[i].size() == 2 && parts[i][1] == ':' )
			continue;
#endif

		if( DoMkdir(curpath, 0755) == 0 )
			continue;

		/* When creating a directory that already exists over Samba, Windows is
		 * returning ENOENT instead of EEXIST. */
		/* I can't reproduce this anymore.  If we get ENOENT, log it but keep
		 * going. */
		if( errno == ENOENT && LOG )
			LOG->Warn("Couldn't create %s: %s", curpath.c_str(), strerror(errno) );
		if( errno == EEXIST || errno == ENOENT )
			continue;		// we expect to see these errors

		if( LOG )
		{
			LOG->Warn("Couldn't create %s: %s", curpath.c_str(), strerror(errno) );
			return false;
		}

		/* Make sure it's a directory. */
		struct stat st;
		DoStat( curpath, &st );
		if( !(st.st_mode & S_IFDIR) )
		{
			if( LOG )
				LOG->Warn("Couldn't create %s: path exists and is not a directory", curpath.c_str() );
			
			return false;
		}
	}
	
	return true;
}

DirectFilenameDB::DirectFilenameDB( CString root_ )
{
	ExpireSeconds = 30;
	root = root_;
	if( root.Right(1) != "/" )
		root += '/';
	if( root == "./" )
		root = "";
}


#if defined(WINDOWS)
/* There's no fchdir() here, so read the directory by its full path instead. */
void DirectFilenameDB::PopulateFileSet( FileSet &fs, const CString &path )
{
	CString sPath = path;

	/* Resolve path cases (path/Path -> PATH/path). */
	ResolvePath( sPath );

	fs.age.GetDeltaTime(); /* reset */
	fs.files.clear();

	CString sDir = root+sPath;
	if( sDir == "" )
		sDir = ".";
	if( sDir.Right(1) != "/" )
		sDir += '/';

	DIR *d = opendir( sDir );
	if( d == NULL )
	{
		/* Only log once per dir. */
		if( LOG && errno != ENOENT )
			LOG->MapLog("opendir " + sDir, "Couldn't opendir(%s): %s", sDir.c_str(), strerror(errno) );
		return;
	}

	while(struct dirent *ent = readdir(d))
	{
		if(!strcmp(ent->d_name, ".")) continue;
		if(!strcmp(ent->d_name, "..")) continue;

		File f;
		f.SetName( ent->d_name );

		struct stat st;
		if( DoStat( sDir + ent->d_name, &st ) == -1 )
		{
			if(LOG)
				LOG->Warn("Got file '%s' in '%s' from list, but can't stat? (%s)",
					ent->d_name, sPath.c_str(), strerror(errno));
			continue;
		}

		f.dir = (st.st_mode & S_IFDIR);
		f.size = st.st_size;
		f.hash = st.st_mtime;

		fs.files.insert(f);
	}

	closedir(d);
}
#else
void DirectFilenameDB::PopulateFileSet( FileSet &fs, const CString &path )
{
	CString sPath = path;

	/* Resolve path cases (path/Path -> PATH/path). */
	ResolvePath( sPath );

	fs.age.GetDeltaTime(); /* reset */
	fs.files.clear();

	int OldDir = DoOpen(".", O_RDONLY);
	if( OldDir == -1 )
		RageException::Throw( "Couldn't open(.): %s", strerror(errno) );

	if( chdir(root+sPath) == -1 )
	{
		/* Only log once per dir. */
		if( LOG && errno != ENOENT )
			LOG->MapLog("chdir " + root+"/"+sPath, "Couldn't chdir(%s%s): %s", root.c_str(), sPath.c_str(), strerror(errno) );
		close( OldDir );
		return;
	}

	DIR *d = opendir(".");
	if( d == NULL )
	{
		LOG->MapLog("opendir " + root+"/"+sPath, "Couldn't opendir(%s%s): %s", root.c_str(), sPath.c_str(), strerror(errno) );
		if( fchdir( OldDir ) == -1 )
			RageException::Throw( "Couldn't fchdir(): %s", strerror(errno) );
		close( OldDir );
		return;
	}

	while(struct dirent *ent = readdir(d))
	{
		if(!strcmp(ent->d_name, ".")) continue;
		if(!strcmp(ent->d_name, "..")) continue;
		
		File f;
		f.SetName( ent->d_name );
		
		struct stat st;
		if( DoStat(ent->d_name, &st) == -1 )
		{
			/* If it's a broken symlink, ignore it.  Otherwise, warn. */
			if( lstat(ent->d_name, &st) == 0 )
				continue;
			
			/* Huh? */
			if(LOG)
				LOG->Warn("Got file '%s' in '%s' from list, but can't stat? (%s)",
					ent->d_name, sPath.c_str(), strerror(errno));
			continue;
		} else {
			f.dir = (st.st_mode & S_IFDIR);
			f.size = st.st_size;
			f.hash = st.st_mtime;
		}

		fs.files.insert(f);
	}
	       
	closedir(d);
	if( fchdir( OldDir ) == -1 )
		RageException::Throw( "Couldn't fchdir(): %s", strerror(errno) );
	close( OldDir );
}
#endif

/*
 * Copyright (c) 2003-2004 Glenn Maynard, Chris Danford
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
