#ifndef TLS_H
#define TLS_H

#include <openssl/ssl.h>

SSL_CTX* tls_init(void);
SSL* tls_connect(SSL_CTX* ctx, int sock, const char *hostname);
void tls_cleanup(SSL_CTX* ctx, SSL* ssl);

#endif
