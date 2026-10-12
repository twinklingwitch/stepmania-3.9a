// Parameters:
//    public_key - X.509 standard SubjectPublicKeyInfo key in binary format
//    data_file - the data file whose signature to verify
//    signature - the signature of data_file in binary format

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace std;

static bool ReadFile(const char *filename, string &out)
{
	ifstream f(filename, ios::binary);
	if (!f)
		return false;
	out.assign(istreambuf_iterator<char>(f), istreambuf_iterator<char>());
	return true;
}

bool RSAVerifyFile(const char *pubFilename, const char *messageFilename, const char *signatureFilename)
{
	string pubKey, message, signature;
	if (!ReadFile(pubFilename, pubKey) || !ReadFile(messageFilename, message) || !ReadFile(signatureFilename, signature))
		return false;

	const unsigned char *p = (const unsigned char *) pubKey.data();
	EVP_PKEY *pkey = d2i_PUBKEY(NULL, &p, pubKey.size());
	if (pkey == NULL)
		return false;

	bool ok = false;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (ctx != NULL &&
		(int) signature.size() == EVP_PKEY_size(pkey) &&
		EVP_DigestVerifyInit(ctx, NULL, EVP_sha1(), NULL, pkey) > 0 &&
		EVP_DigestVerifyUpdate(ctx, message.data(), message.size()) > 0 &&
		EVP_DigestVerifyFinal(ctx, (const unsigned char *) signature.data(), signature.size()) == 1)
		ok = true;

	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(pkey);
	return ok;
}

int main(int argc, char *argv[])
{
	if (argc != 4)
	{
		cout << "\nUsage:    " << argv[0] << " publickey_fn data_fn signature_fn" << endl;
		return -1;
	}

	if (RSAVerifyFile(argv[1], argv[2], argv[3]))
		cout << "The signature is valid." << endl;
	else
		cout << "The signature is not valid." << endl;
	return 0;
}
