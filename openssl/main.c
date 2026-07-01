/*
 * OpenSSL libcrypto smoke test on the FreeBSD-libc Unikraft port.
 *
 * Network-free, allocation-light: hashes the canonical NIST SHA-256 test
 * vector "abc" with OpenSSL's low-level SHA256 API and compares the digest
 * against the published expected value. Prints PASS/FAIL at boot.
 *
 *   SHA-256("abc") =
 *     ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad
 */

#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>

static const unsigned char expected[SHA256_DIGEST_LENGTH] = {
	0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
	0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
	0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
	0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
};

static void print_hex(const char *label, const unsigned char *p, size_t n)
{
	printf("%s", label);
	for (size_t i = 0; i < n; i++)
		printf("%02x", p[i]);
	printf("\n");
}

int main(int argc, char *argv[])
{
	const char msg[] = "abc";
	unsigned char digest[SHA256_DIGEST_LENGTH];
	SHA256_CTX ctx;

	(void)argc; (void)argv;

	printf("OpenSSL libcrypto on FreeBSD libc\n");
	printf("Version: %s\n", OpenSSL_version(OPENSSL_VERSION));

	SHA256_Init(&ctx);
	SHA256_Update(&ctx, msg, strlen(msg));
	SHA256_Final(digest, &ctx);

	print_hex("SHA-256(\"abc\") = ", digest, sizeof(digest));

	if (memcmp(digest, expected, sizeof(digest)) == 0) {
		printf("SHA-256 self-test: PASS\n");
		return 0;
	}

	print_hex("expected       = ", expected, sizeof(expected));
	printf("SHA-256 self-test: FAIL\n");
	return 1;
}
