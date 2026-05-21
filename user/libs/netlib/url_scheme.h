/**
 * @file url_scheme.h
 * @brief URL scheme classification for the user-space netlib dispatch path.
 *
 * Purpose:
 *   Provides a tiny, dependency-free helper that classifies the leading
 *   scheme of a URL into the set of transports the netlib stack is allowed
 *   to dispatch to. Used as the first gate in the shared http/https dispatch
 *   path so non-http(s) URLs (file://, ftp://, javascript:, ...) are
 *   explicitly denied before any DNS/TCP/TLS work is attempted.
 *
 * Interactions:
 *   - api.c uses netlib_url_classify_scheme() to fail closed on unsupported
 *     schemes inside netlib_http_get and netlib_https_get.
 *   - http.c uses it as a defence-in-depth check inside http_request.
 *   - tests/netlib_url_scheme_test.c links this header directly.
 *
 * Launched by:
 *   Compiled into the shared netlib library and the kernel-side netlib
 *   build alongside the rest of the netlib protocol sources.
 */

#ifndef SECUREOS_USER_NETLIB_URL_SCHEME_H
#define SECUREOS_USER_NETLIB_URL_SCHEME_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  NETLIB_URL_SCHEME_HTTP = 0,
  NETLIB_URL_SCHEME_HTTPS = 1,
  /*
   * NETLIB_URL_SCHEME_UNKNOWN is the deny-by-default classification used for
   * any URL whose leading characters do not match a supported transport.
   * Empty strings, NULL pointers and protocol-relative ("//host/path") URLs
   * are all classified as UNKNOWN so callers must explicitly pick a
   * transport before any network I/O is attempted.
   */
  NETLIB_URL_SCHEME_UNKNOWN = 2
} netlib_url_scheme_t;

/*
 * Classify the leading scheme of a URL string.
 *
 * Behaviour:
 *   - NULL or empty input returns NETLIB_URL_SCHEME_UNKNOWN.
 *   - "http://..." returns NETLIB_URL_SCHEME_HTTP.
 *   - "https://..." returns NETLIB_URL_SCHEME_HTTPS.
 *   - Anything else (including "//host/..", "file://", "ftp://",
 *     "javascript:", "/relative/path") returns NETLIB_URL_SCHEME_UNKNOWN.
 *
 * The function does no allocation, no network access, and no logging. It is
 * safe to call from any context including capability gate paths.
 */
netlib_url_scheme_t netlib_url_classify_scheme(const char *url);

#ifdef __cplusplus
}
#endif

#endif
