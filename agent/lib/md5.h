#ifndef PT_MD5_H
#define PT_MD5_H
#include <stddef.h>
void pt_md5(const unsigned char *data, size_t len, unsigned char digest[16]);
#endif
