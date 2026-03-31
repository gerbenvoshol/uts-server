#include "http.h"
#include <civetweb.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <time.h>
#include <unistd.h>

/* Maximum allowed size of a timestamp query body (64 KiB).
 * A well-formed RFC 3161 TimeStampReq is never more than a few hundred bytes;
 * this generous cap prevents memory-exhaustion via a crafted Content-Length. */
#define MAX_QUERY_SIZE (64 * 1024)

extern int g_uts_sig_up;
extern int g_uts_sig;

/* Valid HTML5 static page served for non-timestamp requests. The content is
 * defined here as a const char[] so that strlen() can compute the correct
 * Content-Length at runtime, avoiding the stale hard-coded value that was
 * previously in the STATIC_PAGE macro.  Using mg_write() instead of
 * mg_printf() also prevents CSS percentage signs from being misinterpreted
 * as printf format specifiers. */
static const char STATIC_HTML[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "  <meta charset=\"utf-8\">\n"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    "  <title>RFC 3161 Timestamp Server</title>\n"
    "  <link rel=\"icon\" type=\"image/x-icon\" href=\"/favicon.ico\">\n"
    "  <style>\n"
    "    * { box-sizing: border-box; }\n"
    "    body {\n"
    "      margin: 0;\n"
    "      font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\","
    " Roboto, sans-serif;\n"
    "      background: #f7f8fa;\n"
    "      color: #333;\n"
    "      line-height: 1.6;\n"
    "    }\n"
    "    header {\n"
    "      background: #1a3c6e;\n"
    "      color: #fff;\n"
    "      padding: 24px 20px;\n"
    "      text-align: center;\n"
    "    }\n"
    "    header h1 { margin: 0 0 6px; font-size: 1.8rem; }\n"
    "    header p  { margin: 0; opacity: 0.85; }\n"
    "    main {\n"
    "      max-width: 860px;\n"
    "      margin: 28px auto;\n"
    "      padding: 0 16px 40px;\n"
    "    }\n"
    "    section {\n"
    "      background: #fff;\n"
    "      border-radius: 8px;\n"
    "      box-shadow: 0 2px 6px rgba(0,0,0,0.08);\n"
    "      padding: 22px 26px;\n"
    "      margin-bottom: 20px;\n"
    "    }\n"
    "    h2 {\n"
    "      margin-top: 0;\n"
    "      font-size: 1.1rem;\n"
    "      border-bottom: 2px solid #1a3c6e;\n"
    "      padding-bottom: 6px;\n"
    "      color: #1a3c6e;\n"
    "    }\n"
    "    p { margin: 0 0 10px; }\n"
    "    pre {\n"
    "      background: #1e1e2e;\n"
    "      color: #cdd6f4;\n"
    "      border-radius: 6px;\n"
    "      padding: 14px 16px;\n"
    "      overflow-x: auto;\n"
    "      font-size: 0.875rem;\n"
    "      line-height: 1.55;\n"
    "      margin: 10px 0;\n"
    "      white-space: pre;\n"
    "    }\n"
    "    .var     { color: #89dceb; font-style: italic; }\n"
    "    .comment { color: #6c7086; }\n"
    "    .btn {\n"
    "      display: inline-block;\n"
    "      background: #1a3c6e;\n"
    "      color: #fff;\n"
    "      text-decoration: none;\n"
    "      padding: 9px 18px;\n"
    "      border-radius: 5px;\n"
    "      font-size: 0.875rem;\n"
    "      margin: 8px 6px 0 0;\n"
    "    }\n"
    "    .grid {\n"
    "      display: grid;\n"
    "      grid-template-columns: 1fr 1fr;\n"
    "      gap: 14px;\n"
    "      margin-top: 10px;\n"
    "    }\n"
    "    @media (max-width: 560px) { .grid { grid-template-columns: 1fr; } }\n"
    "    .kv strong {\n"
    "      display: block;\n"
    "      font-size: 0.72rem;\n"
    "      text-transform: uppercase;\n"
    "      letter-spacing: 0.06em;\n"
    "      color: #666;\n"
    "      margin-bottom: 2px;\n"
    "    }\n"
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <header>\n"
    "    <h1>RFC 3161 Timestamp Server</h1>\n"
    "    <p>A free, open-source Time-Stamp Authority (TSA)"
    " &mdash; RFC&nbsp;3161 compliant</p>\n"
    "  </header>\n"
    "  <main>\n"

    "    <section>\n"
    "      <h2>What is an RFC 3161 Timestamp?</h2>\n"
    "      <p>An <strong>RFC 3161 timestamp</strong> provides cryptographic proof that a"
    " piece of data existed in its exact form at a specific point in time. A trusted"
    " <em>Time-Stamp Authority (TSA)</em> signs a hash of your data together with the"
    " current time, producing a token you can verify later using the TSA&rsquo;s"
    " public certificate chain.</p>\n"
    "      <p>Common uses include timestamping signed documents and contracts, log files"
    " at rotation time, code-signing binaries, and any workflow that requires an"
    " unforgeable record of when data existed.</p>\n"
    "    </section>\n"

    "    <section>\n"
    "      <h2>How to Timestamp a File</h2>\n"
    "      <p>Use <strong>OpenSSL</strong> and <strong>curl</strong>."
    " Replace the <span class=\"var\">highlighted</span> placeholders with your values:</p>\n"
    "      <pre>"
    "<span class=\"comment\"># 1. Generate a timestamp request (SHA-256)</span>\n"
    "openssl ts -query \\\n"
    "    -data  \"<span class=\"var\">$FILE</span>\" \\\n"
    "    -sha256 -cert \\\n"
    "    -out   ts_req.tsq\n"
    "\n"
    "<span class=\"comment\"># 2. Send the request to this server</span>\n"
    "curl \"<span class=\"var\">$UTS_SERVER_URL</span>\" \\\n"
    "    -H \"Content-Type: application/timestamp-query\" \\\n"
    "    -f -g --data-binary \"@ts_req.tsq\" \\\n"
    "    -o \"<span class=\"var\">$FILE</span>.tsr\""
    "</pre>\n"
    "    </section>\n"

    "    <section>\n"
    "      <h2>How to Verify a Timestamp</h2>\n"
    "      <p>Download the CA and signer certificates below, then run:</p>\n"
    "      <pre>"
    "<span class=\"comment\"># Verify the timestamp response against the original file</span>\n"
    "openssl ts -verify \\\n"
    "    -data      \"<span class=\"var\">$FILE</span>\" \\\n"
    "    -in        \"<span class=\"var\">$FILE</span>.tsr\" \\\n"
    "    -CAfile    ca.pem \\\n"
    "    -untrusted tsa_cert.pem\n"
    "\n"
    "<span class=\"comment\"># Inspect the timestamp token content</span>\n"
    "openssl ts -reply -in \"<span class=\"var\">$FILE</span>.tsr\" -text"
    "</pre>\n"
    "      <a class=\"btn\" href=\"./ca.pem\" download>Download CA Certificate</a>\n"
    "      <a class=\"btn\" href=\"./tsa_cert.pem\" download>"
    "Download TSA Certificate</a>\n"
    "    </section>\n"

    "    <section>\n"
    "      <h2>Service Information</h2>\n"
    "      <div class=\"grid\">\n"
    "        <div class=\"kv\"><strong>Protocol</strong>"
    "RFC 3161 Time-Stamp Protocol (TSP)</div>\n"
    "        <div class=\"kv\"><strong>Transport</strong>"
    "HTTP POST to this URL</div>\n"
    "        <div class=\"kv\"><strong>Request Content-Type</strong>"
    "application/timestamp-query</div>\n"
    "        <div class=\"kv\"><strong>Response Content-Type</strong>"
    "application/timestamp-reply</div>\n"
    "        <div class=\"kv\"><strong>Accepted Hash Algorithms</strong>"
    "SHA-256, SHA-384, SHA-512</div>\n"
    "        <div class=\"kv\"><strong>Software</strong>"
    "uts-server (MIT License, open source)</div>\n"
    "      </div>\n"
    "    </section>\n"

    "  </main>\n"
    "</body>\n"
    "</html>\n";

/* Minimal 1x1 transparent ICO served at /favicon.ico.
 * Layout: ICONDIR (6 bytes) + ICONDIRENTRY (16 bytes) +
 *         BITMAPINFOHEADER (40 bytes) + BGRA pixel (4 bytes) +
 *         AND mask padded to DWORD (4 bytes) = 70 bytes total. */
static const unsigned char FAVICON_ICO[] = {
    /* ICONDIR */
    0x00, 0x00,             /* Reserved, must be 0        */
    0x01, 0x00,             /* Type: 1 = ICO               */
    0x01, 0x00,             /* Number of images: 1         */
    /* ICONDIRENTRY */
    0x01,                   /* Width: 1 px                 */
    0x01,                   /* Height: 1 px                */
    0x00,                   /* Color count: 0 (32-bit)     */
    0x00,                   /* Reserved                    */
    0x01, 0x00,             /* Planes: 1                   */
    0x20, 0x00,             /* Bit count: 32               */
    0x30, 0x00, 0x00, 0x00, /* Image data size: 48 bytes   */
    0x16, 0x00, 0x00, 0x00, /* Image data offset: 22 bytes */
    /* BITMAPINFOHEADER */
    0x28, 0x00, 0x00, 0x00, /* Header size: 40             */
    0x01, 0x00, 0x00, 0x00, /* Width: 1 px                 */
    0x02, 0x00, 0x00, 0x00, /* Height: 2 (XOR+AND, ICO)    */
    0x01, 0x00,             /* Planes: 1                   */
    0x20, 0x00,             /* Bit depth: 32               */
    0x00, 0x00, 0x00, 0x00, /* Compression: none           */
    0x00, 0x00, 0x00, 0x00, /* Image data size: 0          */
    0x00, 0x00, 0x00, 0x00, /* X pixels/meter: 0           */
    0x00, 0x00, 0x00, 0x00, /* Y pixels/meter: 0           */
    0x00, 0x00, 0x00, 0x00, /* Colors used: 0              */
    0x00, 0x00, 0x00, 0x00, /* Important colors: 0         */
    /* Pixel data: 1 fully transparent pixel (BGRA)        */
    0x00, 0x00, 0x00, 0x00,
    /* AND mask: 1 pixel padded to DWORD boundary          */
    0x00, 0x00, 0x00, 0x00
};

static char *rand_string(char *str, size_t size) {
    const char charset[] = "1234567890ABCDEF";
    if (size > 1) {
        size_t n = size - 1;
        unsigned char *randbuf = calloc(n, sizeof(unsigned char));
        if (randbuf != NULL) {
            RAND_bytes(randbuf, (int)n);
            for (size_t i = 0; i < n; i++) {
                str[i] = charset[randbuf[i] % (sizeof charset - 1)];
            }
            free(randbuf);
        }
        str[n] = '\0';
    }
    return str;
}

static char *sdup(const char *str) {
    size_t len;
    char *p;

    len = strlen(str) + 1;
    if ((p = (char *)malloc(len)) != NULL) {
        memcpy(p, str, len);
    }
    return p;
}

static int log_civetweb(const struct mg_connection *conn, const char *message) {
    const struct mg_context *ctx = mg_get_context(conn);
    struct tuser_data *ud = (struct tuser_data *)mg_get_user_data(ctx);

    if (ud->first_message == NULL) {
        ud->first_message = sdup(message);
    }

    return 0;
}

void log_request_debug(const struct mg_request_info *request_info,
                       char *request_id, rfc3161_context *context) {
    if (LOG_DEBUG > context->loglevel && !context->stdout_dbg)
        return;

    for (int i = 0; i < request_info->num_headers; i++) {
        uts_logger(context, LOG_DEBUG, "Request[%s], Header[%s]: %s",
                   request_id, request_info->http_headers[i].name,
                   null_undef(request_info->http_headers[i].value));
    }
    uts_logger(context, LOG_DEBUG, "Request[%s], request_method: %s",
               request_id, null_undef(request_info->request_method));
    uts_logger(context, LOG_DEBUG, "Request[%s], request_uri: %s", request_id,
               null_undef(request_info->request_uri));
    uts_logger(context, LOG_DEBUG, "Request[%s], local_uri: %s", request_id,
               null_undef(request_info->local_uri));
    uts_logger(context, LOG_DEBUG, "Request[%s], http_version: %s", request_id,
               null_undef(request_info->http_version));
    uts_logger(context, LOG_DEBUG, "Request[%s], query_string: %s", request_id,
               null_undef(request_info->query_string));
    uts_logger(context, LOG_DEBUG, "Request[%s], remote_addr: %s", request_id,
               null_undef(request_info->remote_addr));
    uts_logger(context, LOG_DEBUG, "Request[%s], is_ssl: %d", request_id,
               request_info->is_ssl);
    uts_logger(context, LOG_DEBUG, "Request[%s], content_length: %d",
               request_id, request_info->content_length);
    uts_logger(context, LOG_DEBUG, "Request[%s], remote_port: %d", request_id,
               request_info->remote_port);
}

void log_request(const struct mg_request_info *request_info, char *request_id,
                 rfc3161_context *context, int response_code, int timer) {
    if (LOG_INFO > context->loglevel && !context->stdout_dbg)
        return;

    const char *user_agent = NULL;
    const char *content_type = NULL;

    for (int i = 0; i < request_info->num_headers; i++) {
        if (strcasecmp(request_info->http_headers[i].name, "User-Agent") == 0) {
            user_agent = request_info->http_headers[i].value;
        }
        if (strcasecmp(request_info->http_headers[i].name, "Content-Type") ==
            0) {
            content_type = request_info->http_headers[i].value;
        }
    }

    uts_logger(context, LOG_INFO,
               "Request[%s], remote_addr[%s] ssl[%d] "
               "uri[%s] http_resp_code[%d] duration[%d us] "
               "user-agent[%s] content-type[%s]",
               request_id, null_undef(request_info->remote_addr),
               request_info->is_ssl, null_undef(request_info->local_uri),
               response_code, timer, null_undef(user_agent),
               null_undef(content_type));
}

/*
 * Append security-hardening HTTP response headers to an in-progress response.
 * Call AFTER the status line and content-specific headers but BEFORE the
 * blank line (\r\n) that ends the header block.
 *
 * Headers sent on every response (HTTP and HTTPS):
 *   X-Content-Type-Options  – prevent MIME-type sniffing
 *   X-Frame-Options         – deny framing (clickjacking protection)
 *   Referrer-Policy         – suppress Referer on outbound navigations
 *   Content-Security-Policy – restrict resource origins; allow inline styles
 *                             (needed for the built-in HTML status page)
 *   Cache-Control           – prevent caching of timestamp tokens
 *
 * Header sent only when the connection is TLS (is_ssl != 0):
 *   Strict-Transport-Security – HSTS: instruct clients to always use HTTPS
 *
 * Note: HTTP Public Key Pinning (HPKP, RFC 7469) is intentionally omitted.
 * It was removed from all major browsers (Chrome 72+, Firefox) and is not
 * implemented by the CLI tools (openssl ts, curl) or language libraries
 * (BouncyCastle, .NET) that typically talk to a TSA.  Sending a header that
 * nothing honours adds no security but would introduce real operational risk:
 * a misconfigured or expired pin could permanently lock out every client that
 * does happen to enforce it.  Better alternatives for certificate trust in a
 * TSA context are CAA DNS records (restrict which CAs may issue for your
 * domain) and Certificate Transparency (CT) monitoring.
 */
static void add_security_headers(struct mg_connection *conn,
                                 const rfc3161_context *ct) {
    const struct mg_request_info *ri = mg_get_request_info(conn);
    (void)ct; /* reserved for future per-context header customisation */

    mg_printf(conn,
              "X-Content-Type-Options: nosniff\r\n"
              "X-Frame-Options: DENY\r\n"
              "Referrer-Policy: no-referrer\r\n"
              "Content-Security-Policy: default-src 'none';"
              " style-src 'unsafe-inline'\r\n"
              "Cache-Control: no-store\r\n");

    if (ri->is_ssl) {
        /* HSTS: one year, apply to all subdomains */
        mg_printf(conn,
                  "Strict-Transport-Security:"
                  " max-age=31536000; includeSubDomains\r\n");
    }
}

/*
 * Open a regular file and stream it as an HTTP 200 response with the given
 * Content-Type, injecting all security headers.  Returns 1 if the file was
 * served, 0 if it could not be opened (caller should send a 404).
 */
static int serve_file_with_headers(struct mg_connection *conn,
                                   const rfc3161_context *ct,
                                   const char *filename,
                                   const char *content_type,
                                   const char *disposition_name) {
    struct stat st;
    if (stat(filename, &st) != 0 || !S_ISREG(st.st_mode))
        return 0;

    FILE *f = fopen(filename, "rb");
    if (f == NULL)
        return 0;

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: %s\r\n"
              "Content-Disposition: attachment; filename=\"%s\"\r\n"
              "Content-Length: %lld\r\n",
              content_type, disposition_name, (long long)st.st_size);
    add_security_headers(conn, ct);
    mg_printf(conn, "\r\n");

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        mg_write(conn, buf, n);

    fclose(f);
    return 1;
}

int rfc3161_handler(struct mg_connection *conn, void *context) {
    // some timer stuff
    clock_t start = clock(), diff;
    /* Handler may access the request info using mg_get_request_info */
    const struct mg_request_info *request_info = mg_get_request_info(conn);
    rfc3161_context *ct = (rfc3161_context *)context;
    int ret;
    int resp_code;
    ct->query_counter++;
    uint64_t query_id = ct->query_counter;

    bool is_tsq = 0;

    // go through every headers to find Content-Type
    // and check if it's set to "application/timestamp-query"
    // if it's the case, set is_tsq (is time-stamp query) to True
    for (int i = 0; i < request_info->num_headers; i++) {
        const char *h_name = request_info->http_headers[i].name;
        const char *h_value = request_info->http_headers[i].value;
        if (strcasecmp(h_name, "Content-Type") == 0 &&
            strcasecmp(h_value, "application/timestamp-query") == 0)
            is_tsq = 1;
    }

    unsigned char *content = NULL;
    size_t content_length = 0;

    char *serial_id = NULL;

    // Send HTTP reply to the client.
    //
    // If it's a time-stamp query.
    if (is_tsq) {
        // Reject requests with a missing, zero, or oversized body to prevent
        // memory exhaustion (DoS via a crafted Content-Length header).
        if (request_info->content_length <= 0 ||
            request_info->content_length > MAX_QUERY_SIZE) {
            uts_logger(ct, LOG_WARNING,
                       "Rejected request: invalid content_length %ld",
                       (long)request_info->content_length);
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n"
                            "Content-Type: text/plain\r\n"
                            "Content-Length: 12\r\n");
            add_security_headers(conn, ct);
            mg_printf(conn, "\r\nclient error");
            resp_code = 400;
            goto log_and_return;
        }
        // Recover query content from http request.
        char *query = calloc(request_info->content_length, sizeof(char));
        int query_len = mg_read(conn, query, request_info->content_length);

        // Log the query as DEBUG log in hexadecimal format
        log_hex(ct, LOG_DEBUG, "query hexdump content", (unsigned char *)query,
                request_info->content_length);
        // Get an OpenSSL TS_RESP_CTX (wrapped inside ts_resp_ctx_wrapper
        // structure).
        // get_ctxw recovers the first unused TS_RESP_CTX
        // in the ct->ts_ctx_pool pool of TS_RESP_CTX.
        // (TS_RESP_CTX are not thread safe)
        ts_resp_ctx_wrapper *ctx_w = get_ctxw(ct);
        if (ctx_w == NULL) {
            resp_code = 500;
            uts_logger(context, LOG_WARNING,
                       "Unable to get an OpenSSL ts_context in the pool");

        } else {
            // create the response
            resp_code = create_response(ct, query, query_len, ctx_w->ts_ctx,
                                        &content_length, &content, &serial_id);
            // free the TS_RESP_CTX used
            ctx_w->available = 1;
        }
        // respond according to create_response return code
        switch (resp_code) {
        case 200:
            mg_printf(conn,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/timestamp-reply\r\n"
                      "Content-Length: %d\r\n",
                      (int)content_length);
            add_security_headers(conn, ct);
            mg_printf(conn, "\r\n");
            mg_write(conn, content, content_length);
            log_hex(ct, LOG_DEBUG, "response hexdump content", content,
                    content_length);
            break;
        case 400:
            mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n"
                            "Content-Type: text/plain\r\n"
                            "Content-Length: 12\r\n");
            add_security_headers(conn, ct);
            mg_printf(conn, "\r\nclient error");
            break;
        default:
            mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n"
                            "Content-Type: text/plain\r\n"
                            "Content-Length: 17\r\n");
            add_security_headers(conn, ct);
            mg_printf(conn, "\r\nuts-server error");
        }
        OPENSSL_cleanse(query, (size_t)query_len);
        free(query);
        free(content);
    } else {
        // default reply if we don't have a time-stamp request
        resp_code = 200;
        size_t html_len = strlen(STATIC_HTML);
        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/html; charset=utf-8\r\n"
                  "Content-Length: %zu\r\n",
                  html_len);
        add_security_headers(conn, ct);
        mg_printf(conn, "\r\n");
        mg_write(conn, STATIC_HTML, html_len);
    }

log_and_return:
    // initialize a serial_id if not created by create_response
    if (serial_id == NULL) {
        serial_id = calloc(9, sizeof(char));
        serial_id = rand_string(serial_id, 8);
    }
    // some debugging logs
    log_request_debug(request_info, serial_id, ct);
    // end of some timer stuff
    diff = clock() - start;
    // log the request
    log_request(request_info, serial_id, ct, resp_code,
                (diff * 1000000 / CLOCKS_PER_SEC));
    free(serial_id);
    return 1;
}

int ca_serve_handler(struct mg_connection *conn, void *context) {
    const struct mg_request_info *request_info = mg_get_request_info(conn);
    clock_t start = clock(), diff;
    rfc3161_context *ct = (rfc3161_context *)context;
    const char *filename = ct->ca_file;

    if (strlen(filename) == 0) {
        uts_logger(context, LOG_NOTICE,
                   "'certs' param in '[ tsa ]' section not filed");
        mg_send_http_error(conn, 404, "CA file not available");
        diff = clock() - start;
        log_request(request_info, "CA_DL  ", ct, 404,
                    (diff * 1000000 / CLOCKS_PER_SEC));
        return 1;
    }

    if (serve_file_with_headers(conn, ct, filename,
                                "application/x-pem-file", "ca.pem")) {
        diff = clock() - start;
        log_request(request_info, "CA_DL  ", ct, 200,
                    (diff * 1000000 / CLOCKS_PER_SEC));
    } else {
        uts_logger(context, LOG_NOTICE, "CA file '%s' not available", filename);
        mg_send_http_error(conn, 404, "CA file not available");
        diff = clock() - start;
        log_request(request_info, "CA_DL  ", ct, 404,
                    (diff * 1000000 / CLOCKS_PER_SEC));
    }
    return 1;
}

int cert_serve_handler(struct mg_connection *conn, void *context) {
    const struct mg_request_info *request_info = mg_get_request_info(conn);
    clock_t start = clock(), diff;
    rfc3161_context *ct = (rfc3161_context *)context;
    const char *filename = ct->cert_file;

    if (strlen(filename) == 0) {
        uts_logger(context, LOG_NOTICE,
                   "'signer_cert' param in '[ tsa ]' section not filed");
        mg_send_http_error(conn, 404, "Signer certificate not available");
        diff = clock() - start;
        log_request(request_info, "CERT_DL", ct, 404,
                    (diff * 1000000 / CLOCKS_PER_SEC));
        return 1;
    }

    if (serve_file_with_headers(conn, ct, filename,
                                "application/x-pem-file", "tsa_cert.pem")) {
        diff = clock() - start;
        log_request(request_info, "CERT_DL", ct, 200,
                    (diff * 1000000 / CLOCKS_PER_SEC));
    } else {
        uts_logger(context, LOG_NOTICE,
                   "Signer certificate file '%s' not available", filename);
        mg_send_http_error(conn, 404, "Signer certificate not available");
        diff = clock() - start;
        log_request(request_info, "CERT_DL", ct, 404,
                    (diff * 1000000 / CLOCKS_PER_SEC));
    }
    return 1;
}

int favicon_handler(struct mg_connection *conn, void *context) {
    rfc3161_context *ct = (rfc3161_context *)context;
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: image/x-icon\r\n"
              "Content-Length: %zu\r\n",
              sizeof(FAVICON_ICO));
    add_security_headers(conn, ct);
    mg_printf(conn, "\r\n");
    mg_write(conn, FAVICON_ICO, sizeof(FAVICON_ICO));
    return 1;
}

int http_server_start(char *conffile, char *conf_wd, bool stdout_dbg) {
    struct mg_context *ctx;
    struct mg_callbacks callbacks;
    struct tuser_data user_data;

    rfc3161_context *ct = (rfc3161_context *)calloc(1, sizeof(rfc3161_context));
    ct->stdout_dbg = stdout_dbg;
    ct->loglevel = 8;
    init_ssl();
    if (!set_params(ct, conffile, conf_wd))
        return EXIT_FAILURE;

    // Disable stdout buffering if logging to stdout
    if (ct->stdout_logging || ct->stdout_dbg)
        setbuf(stdout, NULL);

    // Prepare callbacks structure. We have only one callback, the rest are
    // NULL.
    memset(&callbacks, 0, sizeof(callbacks));
    memset(&user_data, 0, sizeof(user_data));
    callbacks.log_message = &log_civetweb;

    // Start the web server.
    ctx = mg_start(&callbacks, &user_data, ct->http_options);
    if (ctx != NULL) {
        mg_set_request_handler(ctx, "/", rfc3161_handler, (void *)ct);
        mg_set_request_handler(ctx, "/favicon.ico", favicon_handler, (void *)ct);
        mg_set_request_handler(ctx, "/ca.pem", ca_serve_handler, (void *)ct);
        mg_set_request_handler(ctx, "/tsa_cert.pem", cert_serve_handler,
                               (void *)ct);

        // Wait until some signals are received
        while (g_uts_sig == 0) {
            sleep(1);
        }
    } else {
        uts_logger(ct, LOG_ERR, "Failed to start uts-server: %s",
                   ((user_data.first_message == NULL)
                        ? "unknown reason"
                        : user_data.first_message));
    }

    // Stop the server.
    mg_stop(ctx);
    free_uts_context(ct);
    free_ssl();

    return 0;
}
