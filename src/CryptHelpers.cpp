#include "global.h"
#include "CryptHelpers.h"
#include "RageFile.h"
#include "RageUtil.h"
#include "RageLog.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

template<typename T, void (*Free)(T *)>
class Owned
{
public:
	Owned( T *p = NULL ): m_p(p) { }
	~Owned() { if( m_p ) Free( m_p ); }
	T *Get() const { return m_p; }

private:
	Owned( const Owned & );
	Owned &operator=( const Owned & );
	T *m_p;
};

typedef Owned<EVP_PKEY, EVP_PKEY_free> PKey;
typedef Owned<EVP_PKEY_CTX, EVP_PKEY_CTX_free> PKeyCtx;
typedef Owned<EVP_MD_CTX, EVP_MD_CTX_free> MDCtx;
typedef Owned<PKCS8_PRIV_KEY_INFO, PKCS8_PRIV_KEY_INFO_free> PKCS8;

static CString GetOpenSSLError()
{
	char szBuf[256];
	ERR_error_string_n( ERR_get_error(), szBuf, sizeof(szBuf) );
	ERR_clear_error();
	return szBuf;
}

static EVP_PKEY *LoadPublicKey( const CString &sKey )
{
	const unsigned char *p = (const unsigned char *) sKey.data();
	return d2i_PUBKEY( NULL, &p, sKey.size() );
}

static EVP_PKEY *LoadPrivateKey( const CString &sKey )
{
	const unsigned char *p = (const unsigned char *) sKey.data();
	PKCS8 p8( d2i_PKCS8_PRIV_KEY_INFO(NULL, &p, sKey.size()) );
	if( p8.Get() == NULL )
		return NULL;
	return EVP_PKCS82PKEY( p8.Get() );
}

bool CryptHelpers::GenerateRSAKey( unsigned int keyLength, CString sSeed, CString &sPublicKey, CString &sPrivateKey )
{
	PKeyCtx ctx( EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL) );
	EVP_PKEY *pGenerated = NULL;
	if( ctx.Get() == NULL ||
		EVP_PKEY_keygen_init( ctx.Get() ) <= 0 ||
		EVP_PKEY_CTX_set_rsa_keygen_bits( ctx.Get(), keyLength ) <= 0 ||
		EVP_PKEY_keygen( ctx.Get(), &pGenerated ) <= 0 )
	{
		LOG->Warn( "GenerateRSAKey failed: %s", GetOpenSSLError().c_str() );
		return false;
	}
	PKey pkey( pGenerated );

	PKCS8 p8( EVP_PKEY2PKCS8(pkey.Get()) );
	if( p8.Get() == NULL )
	{
		LOG->Warn( "GenerateRSAKey failed: %s", GetOpenSSLError().c_str() );
		return false;
	}

	unsigned char *pPriv = NULL;
	int iPrivLen = i2d_PKCS8_PRIV_KEY_INFO( p8.Get(), &pPriv );
	if( iPrivLen <= 0 )
	{
		LOG->Warn( "GenerateRSAKey failed: %s", GetOpenSSLError().c_str() );
		return false;
	}

	unsigned char *pPub = NULL;
	int iPubLen = i2d_PUBKEY( pkey.Get(), &pPub );
	if( iPubLen <= 0 )
	{
		OPENSSL_free( pPriv );
		LOG->Warn( "GenerateRSAKey failed: %s", GetOpenSSLError().c_str() );
		return false;
	}

	sPrivateKey.assign( (const char *) pPriv, iPrivLen );
	sPublicKey.assign( (const char *) pPub, iPubLen );
	OPENSSL_free( pPriv );
	OPENSSL_free( pPub );
	return true;
}

bool CryptHelpers::SignFile( RageFile &file, CString sPrivKey, CString &sSignatureOut, CString &sError )
{
	PKey pkey( LoadPrivateKey(sPrivKey) );
	if( pkey.Get() == NULL )
	{
		sError = "Invalid private key: " + GetOpenSSLError();
		return false;
	}

	MDCtx ctx( EVP_MD_CTX_new() );
	if( ctx.Get() == NULL || EVP_DigestSignInit(ctx.Get(), NULL, EVP_sha1(), NULL, pkey.Get()) <= 0 )
	{
		sError = GetOpenSSLError();
		return false;
	}

	char buf[4096];
	int iGot;
	while( (iGot = file.Read(buf, sizeof(buf))) > 0 )
	{
		if( EVP_DigestSignUpdate(ctx.Get(), buf, iGot) <= 0 )
		{
			sError = GetOpenSSLError();
			return false;
		}
	}
	if( iGot == -1 )
	{
		sError = "Read error: " + file.GetError();
		return false;
	}

	size_t iSigLen = 0;
	if( EVP_DigestSignFinal(ctx.Get(), NULL, &iSigLen) <= 0 )
	{
		sError = GetOpenSSLError();
		return false;
	}

	vector<unsigned char> sig( iSigLen );
	if( EVP_DigestSignFinal(ctx.Get(), &sig[0], &iSigLen) <= 0 )
	{
		sError = GetOpenSSLError();
		return false;
	}

	sSignatureOut.assign( (const char *) &sig[0], iSigLen );
	return true;
}

bool CryptHelpers::VerifyFile( RageFile &file, CString sSignature, CString sPublicKey, CString &sError )
{
	PKey pkey( LoadPublicKey(sPublicKey) );
	if( pkey.Get() == NULL )
	{
		sError = "Invalid public key: " + GetOpenSSLError();
		return false;
	}

	const int iSignatureLength = EVP_PKEY_size( pkey.Get() );
	if( (int) sSignature.size() != iSignatureLength )
	{
		sError = ssprintf( "Invalid signature length: got %i, expected %i", (int) sSignature.size(), iSignatureLength );
		return false;
	}

	MDCtx ctx( EVP_MD_CTX_new() );
	if( ctx.Get() == NULL || EVP_DigestVerifyInit(ctx.Get(), NULL, EVP_sha1(), NULL, pkey.Get()) <= 0 )
	{
		sError = GetOpenSSLError();
		return false;
	}

	char buf[4096];
	int iGot;
	while( (iGot = file.Read(buf, sizeof(buf))) > 0 )
	{
		if( EVP_DigestVerifyUpdate(ctx.Get(), buf, iGot) <= 0 )
		{
			sError = GetOpenSSLError();
			return false;
		}
	}
	if( iGot == -1 )
	{
		sError = "Read error: " + file.GetError();
		return false;
	}

	if( EVP_DigestVerifyFinal(ctx.Get(), (const unsigned char *) sSignature.data(), sSignature.size()) != 1 )
	{
		ERR_clear_error();
		sError = "Signature mismatch";
		return false;
	}

	return true;
}

/*
 * (c) 2001-2004 Chris Danford
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
