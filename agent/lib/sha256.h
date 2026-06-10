#ifndef PT_SHA256_H
#define PT_SHA256_H
#include <stddef.h>
void pt_sha256(const unsigned char *data, size_t len, unsigned char digest[32]);
void pt_hmac_sha256(const unsigned char *key, size_t keylen,
                    const unsigned char *msg, size_t msglen,
                    unsigned char digest[32]);
#endif
