#include "global.h"
#include "CryptManager.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "PrefsManager.h"
#include "RageFileManager.h"

#include "CryptHelpers.h"

#include <openssl/evp.h>

static const CString PRIVATE_KEY_PATH = "Data/private.rsa";
static const CString PUBLIC_KEY_PATH = "Data/public.rsa";
static const int KEY_LENGTH = 1024;
#define MAX_SIGNATURE_SIZE_BYTES 1024	// 1 KB

CryptManager*	CRYPTMAN	= NULL;	// global and accessable from anywhere in our program

CryptManager::CryptManager()
{
	//
	// generate keys if none are available
	//
	if( PREFSMAN->m_bSignProfileData )
	{
		if( !DoesFileExist(PRIVATE_KEY_PATH) || !DoesFileExist(PUBLIC_KEY_PATH) )
		{
			LOG->Warn( "Keys missing.  Generating new keys" );
			GenerateRSAKey( KEY_LENGTH, PRIVATE_KEY_PATH, PUBLIC_KEY_PATH, "aoksdjaksd" );
			FlushDirCache();
		}
	}
}

CryptManager::~CryptManager()
{

}

static bool ReadFile( CString sFile, CString &sOut )
{
	if( !IsAFile(sFile) )
		return false;

	RageFile file;
	if( !file.Open(sFile) )
	{
		LOG->Warn( "ReadFile: opening %s failed: %s", sFile.c_str(), file.GetError().c_str() );
		return false;
	}

	if( file.Read(sOut, file.GetFileSize()) == -1 )
	{
		LOG->Warn( "ReadFile: reading %s failed: %s", sFile.c_str(), file.GetError().c_str() );
		return false;
	}

	return true;
}

static bool WriteFile( CString sFile, CString sBuf )
{
	RageFile output;
	if( !output.Open(sFile, RageFile::WRITE) )
	{
		LOG->Warn( "WriteFile: opening %s failed: %s", sFile.c_str(), output.GetError().c_str() );
		return false;
	}
	
	if( output.Write(sBuf) == -1 || output.Flush() == -1 )
	{
		LOG->Warn( "WriteFile: writing %s failed: %s", sFile.c_str(), output.GetError().c_str() );
		output.Close();
		FILEMAN->Remove( sFile );
		return false;
	}

	return true;
}

void CryptManager::GenerateRSAKey( unsigned int keyLength, CString privFilename, CString pubFilename, CString seed )
{
	ASSERT( PREFSMAN->m_bSignProfileData );

	CString sPubKey, sPrivKey;

	if( !CryptHelpers::GenerateRSAKey(keyLength, seed, sPubKey, sPrivKey) )
		return;

	if( !WriteFile(pubFilename, sPubKey) )
		return;

	if( !WriteFile(privFilename, sPrivKey) )
	{
		FILEMAN->Remove( pubFilename );
		return;
	}
}

void CryptManager::SignFileToFile( CString sPath, CString sSignatureFile )
{
	ASSERT( PREFSMAN->m_bSignProfileData );

	CString sPrivFilename = PRIVATE_KEY_PATH;
	CString sMessageFilename = sPath;
	if( sSignatureFile.empty() )
		sSignatureFile = sPath + SIGNATURE_APPEND;

	CString sPrivKey;
	if( !ReadFile(sPrivFilename, sPrivKey) )
		return;

	if( !IsAFile(sMessageFilename) )
		return;

	RageFile file;
	if( !file.Open(sMessageFilename) )
	{
		LOG->Warn( "SignFileToFile: open(%s) failed: %s", sPath.c_str(), file.GetError().c_str() );
		return;
	}

	CString sSignature;
	CString sError;
	if( !CryptHelpers::SignFile(file, sPrivKey, sSignature, sError) )
	{
		LOG->Warn( "SignFileToFile failed: %s", sError.c_str() );
		return;
	}

	WriteFile( sSignatureFile, sSignature );
}

bool CryptManager::VerifyFileWithFile( CString sPath, CString sSignatureFile )
{
	ASSERT( PREFSMAN->m_bSignProfileData );

	CString sPubFilename = PUBLIC_KEY_PATH;
	CString sMessageFilename = sPath;
	if( sSignatureFile.empty() )
		sSignatureFile = sPath + SIGNATURE_APPEND;

	CString sPublicKey;
	if( !ReadFile(sPubFilename, sPublicKey) )
		return false;

	if( !IsAFile(sSignatureFile) )
		return false;

	int iBytes = FILEMAN->GetFileSizeInBytes( sSignatureFile );
	if( iBytes > MAX_SIGNATURE_SIZE_BYTES )
		return false;

	CString sSignature;
	if( !ReadFile(sSignatureFile, sSignature) )
		return false;

	RageFile file;
	if( !file.Open(sMessageFilename) )
	{
		LOG->Warn( "VerifyFileWithFile: open(%s) failed: %s", sPath.c_str(), file.GetError().c_str() );
		return false;
	}

	CString sError;
	if( !CryptHelpers::VerifyFile(file, sSignature, sPublicKey, sError) )
	{
		LOG->Warn( "VerifyFileWithFile(%s,%s) failed: %s", sPath.c_str(), sSignatureFile.c_str(), sError.c_str() );
		return false;
	}

	return true;
}

bool CryptManager::Verify( CString sPath, CString sSignature )
{
	ASSERT( PREFSMAN->m_bSignProfileData );

	CString sPubFilename = PUBLIC_KEY_PATH;
	CString sMessageFilename = sPath;

	CString sPublicKey;
	if( !ReadFile(sPubFilename, sPublicKey) )
		return false;

	RageFile file;
	if( !file.Open(sMessageFilename) )
	{
		LOG->Warn( "Verify: open(%s) failed: %s", sPath.c_str(), file.GetError().c_str() );
		return false;
	}

	CString sError;
	if( !CryptHelpers::VerifyFile(file, sSignature, sPublicKey, sError) )
	{
		LOG->Warn( "Verify(%s,sig) failed: %s", sPath.c_str(), sError.c_str() );
		return false;
	}

	return true;
}

static CString BinaryToHex( const unsigned char *string, int iNumBytes )
{
	CString s;
	for( int i=0; i<iNumBytes; i++ )
	{
		unsigned val = string[i];
		s += ssprintf( "%x", val );
	}
	return s;
}

CString CryptManager::GetMD5( CString fn )
{
	RageFile file;
	if( !file.Open( fn, RageFile::READ ) )
	{
		LOG->Warn( "GetMD5: Failed to open file '%s'", fn.c_str() );
		return "";
	}

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if( ctx == NULL || !EVP_DigestInit_ex(ctx, EVP_md5(), NULL) )
	{
		LOG->Warn( "GetMD5: Failed to initialize MD5" );
		EVP_MD_CTX_free( ctx );
		return "";
	}

	unsigned char buffer[1024];
	while( !file.AtEOF() && file.GetError().empty() )
	{
		int iBytesRead = file.Read( buffer, sizeof(buffer) );
		if( iBytesRead == -1 )
		{
			LOG->Warn( "GetMD5: Failed to read file '%s': %s", fn.c_str(), file.GetError().c_str() );
			EVP_MD_CTX_free( ctx );
			return "";
		}
		EVP_DigestUpdate( ctx, buffer, iBytesRead );
	}

	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int iDigestSize = 0;
	EVP_DigestFinal_ex( ctx, digest, &iDigestSize );
	EVP_MD_CTX_free( ctx );

	return BinaryToHex( digest, iDigestSize );
}

CString CryptManager::GetSHA1ForString( const CString &sData )
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int iDigestSize = 0;
	if( !EVP_Digest(sData.data(), sData.size(), digest, &iDigestSize, EVP_sha1(), NULL) )
	{
		LOG->Warn( "GetSHA1ForString: SHA-1 failed" );
		return "";
	}

	CString s;
	for( unsigned i=0; i<iDigestSize; i++ )
		s += ssprintf( "%02x", digest[i] );
	return s;
}

/*
 * (c) 2004 Chris Danford
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
