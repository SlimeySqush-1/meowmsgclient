#include "tls.h"
#include <openssl/err.h>
#include <stdio.h>

SSL_CTX* tls_init(void)
{
    SSL_library_init();
    SSL_load_error_strings();

    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    return ctx;
}

SSL* tls_connect(SSL_CTX *ctx, int sock, const char *hostname) {
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, hostname);
    SSL_set1_host(ssl, hostname);

    while (1) {
        int ret = SSL_connect(ssl);

        if (ret == 1)
            break;

        int err = SSL_get_error(ssl, ret);

        if (err == SSL_ERROR_WANT_READ ||
            err == SSL_ERROR_WANT_WRITE) {
            continue;
        }

        fprintf(stderr, "SSL_connect fatal error: %d\n", err);
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    return ssl;
}
void tls_cleanup(SSL_CTX *ctx, SSL *ssl)
{
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }

    if (ctx)
        SSL_CTX_free(ctx);
}
