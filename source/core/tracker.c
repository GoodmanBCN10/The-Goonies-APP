#include "tracker.h"
#include "bencode.h"
#include "util.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

/* ---- helpers ---- */
static void url_encode_hash(char *out, size_t outsz, const uint8_t *hash, size_t len) {
    size_t off = 0;
    for (size_t i = 0; i < len && off + 4 < outsz; i++) {
        if ((hash[i] >= '0' && hash[i] <= '9') ||
            (hash[i] >= 'A' && hash[i] <= 'Z') ||
            (hash[i] >= 'a' && hash[i] <= 'z') ||
            hash[i] == '-' || hash[i] == '_' || hash[i] == '.' || hash[i] == '~') {
            out[off++] = (char)hash[i];
        } else {
            off += snprintf(out+off, outsz-off, "%%%02X", hash[i]);
        }
    }
    out[off] = 0;
}

static void tracker_result_init(tracker_announce_result_t *result) {
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
}

static void tracker_result_failure(tracker_announce_result_t *result,
                                   const char *reason) {
    if (!result || !reason)
        return;
    result->request_ok = 1;
    result->tracker_failure = 1;
    strncpy(result->failure_reason, reason, sizeof(result->failure_reason) - 1);
    result->failure_reason[sizeof(result->failure_reason) - 1] = 0;
}

/* ---- HTTP tracker via libcurl ---- */
#include <curl/curl.h>

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} curl_buf_t;

typedef struct {
    tracker_cancel_cb callback;
    void *user;
} tracker_cancel_t;

static int tracker_cancelled(const tracker_cancel_t *cancel) {
    return cancel && cancel->callback && cancel->callback(cancel->user);
}

static int curl_progress_cb(void *user, curl_off_t download_total,
                            curl_off_t download_now, curl_off_t upload_total,
                            curl_off_t upload_now) {
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    return tracker_cancelled((const tracker_cancel_t *)user);
}

static size_t curl_write_cb(void *ptr, size_t sz, size_t nmemb, void *ud) {
    curl_buf_t *b = (curl_buf_t*)ud;
    size_t total = sz * nmemb;
    uint8_t *newbuf = (uint8_t*)realloc(b->data, b->len + total + 1);
    if (!newbuf) return 0;
    b->data = newbuf;
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    return total;
}

static uint32_t http_announce_once(const char *url,
                                   const uint8_t *info_hash,
                                   const uint8_t *peer_id,
                                   uint16_t listen_port,
                                   int64_t downloaded, int64_t left,
                                   uint8_t *compact_out, uint32_t max_peers,
                                   int *request_ok,
                                   tracker_announce_result_t *result,
                                   const tracker_cancel_t *cancel) {
    *request_ok = 0;
    if (tracker_cancelled(cancel))
        return 0;
    char ih_enc[64], pid_enc[64];
    url_encode_hash(ih_enc, sizeof(ih_enc), info_hash, 20);
    url_encode_hash(pid_enc, sizeof(pid_enc), peer_id, 20);

    char full_url[1024];
    snprintf(full_url, sizeof(full_url),
             "%s%cinfo_hash=%s&peer_id=%s&port=%u"
             "&uploaded=0&downloaded=%lld&left=%lld"
             "&compact=1&event=started&numwant=200",
             url, strchr(url, '?') ? '&' : '?', ih_enc, pid_enc,
             (unsigned)listen_port,
             (long long)downloaded, (long long)left);

    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    curl_buf_t buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL, full_url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TheGooniesAPP/2.1.23");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    long tls12_only = (long)CURL_SSLVERSION_TLSv1_2 |
                      (long)CURL_SSLVERSION_MAX_TLSv1_2;
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, tls12_only);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    if (cancel && cancel->callback) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)cancel);
    }
    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || status < 200 || status >= 300) {
        if (rc == CURLE_ABORTED_BY_CALLBACK && tracker_cancelled(cancel))
            log_msg("[tracker] HTTP %s: cancelled\n", url);
        else if (rc != CURLE_OK)
            log_msg("[tracker] HTTP %s: %s\n", url, curl_easy_strerror(rc));
        else
            log_msg("[tracker] HTTP %s: status %ld\n", url, status);
        free(buf.data);
        return 0;
    }

    if (!buf.data) return 0;

    /* Parse bencode response */
    const char *p = (const char*)buf.data;
    const char *end = p + buf.len;
    be_node_t root;
    if (!be_decode(&p, end, &root) || root.type != BE_DICT) {
        free(buf.data);
        return 0;
    }
    *request_ok = 1;

    /* Check for failure */
    be_node_t fail;
    if (be_dict_get(root.buf, root.buf+root.raw_len, "failure reason", 14, &fail)
        && fail.type == BE_STR) {
        char tmp[128];
        size_t n = fail.slen < 127 ? fail.slen : 127;
        memcpy(tmp, fail.sval, n); tmp[n] = 0;
        tracker_result_failure(result, tmp);
        log_msg("[tracker] HTTP failure: %s\n", tmp);
        free(buf.data);
        return 0;
    }

    /* Extract compact peers */
    be_node_t peers;
    uint32_t count = 0;
    if (be_dict_get(root.buf, root.buf+root.raw_len, "peers", 5, &peers) && peers.type == BE_STR) {
        count = (uint32_t)(peers.slen / 6);
        if (count > max_peers) count = max_peers;
        memcpy(compact_out, peers.sval, count * 6);
        log_msg("[tracker] HTTP %s: %u peers\n", url, count);
    }
    if (result) {
        result->request_ok = 1;
        result->peers = count;
    }

    free(buf.data);
    return count;
}

static uint32_t http_announce(const char *url,
                              const uint8_t *info_hash,
                              const uint8_t *peer_id,
                              uint16_t listen_port,
                              int64_t downloaded, int64_t left,
                              uint8_t *compact_out, uint32_t max_peers,
                              tracker_announce_result_t *result,
                              const tracker_cancel_t *cancel) {
    int request_ok = 0;
    return http_announce_once(url, info_hash, peer_id, listen_port,
                              downloaded, left, compact_out, max_peers,
                              &request_ok, result, cancel);
}

/* ---- UDP tracker (BEP15) ---- */
#define UDP_MAGIC  0x41727101980ULL
#define UDP_CONNECT  0
#define UDP_ANNOUNCE 1

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)(v >> 32));
    put32(p + 4, (uint32_t)v);
}
static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int request_reply(int sock, const uint8_t *req, size_t reqlen,
                         uint8_t *resp, size_t respcap, ssize_t *resplen,
                         uint32_t expect_txid, uint8_t expect_action,
                         const tracker_cancel_t *cancel) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (tracker_cancelled(cancel)) return -1;
        if (send(sock, (const char*)req, reqlen, 0) < 0) return -1;

        for (int wait_slice = 0; wait_slice < 6; wait_slice++) {
            if (tracker_cancelled(cancel)) return -1;
            ssize_t n = recv(sock, (char*)resp, respcap, 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                break;
            }
            if (n < 16) continue;

            uint32_t action = get32(resp);
            uint32_t txid = get32(resp + 4);
            if (txid != expect_txid) continue;
            if (action == 3) return -2; // ACTION_ERROR
            if (action != expect_action) continue;

            *resplen = n;
            return 0;
        }
    }
    return -1;
}

static uint32_t udp_announce(const char *host, uint16_t tport,
                             const uint8_t *info_hash,
                             const uint8_t *peer_id,
                             uint16_t listen_port,
                             int64_t downloaded __attribute__((unused)),
                             int64_t left,
                             uint8_t *compact_out, uint32_t max_peers,
                             const tracker_cancel_t *cancel) {
    if (tracker_cancelled(cancel)) return 0;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)tport);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        return 0;
    }
    if (tracker_cancelled(cancel)) {
        freeaddrinfo(res);
        return 0;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef __SWITCH__
    if (sock < 0 && errno == 105) {
        svcSleepThread(50000000ULL);
        sock = socket(AF_INET, SOCK_DGRAM, 0);
    }
#endif
    if (sock < 0) {
        freeaddrinfo(res);
        return 0;
    }
#ifdef __SWITCH__
    int rcvbuf = 16 * 1024;
    int sndbuf = 8 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
#endif
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close(sock);
        return 0;
    }
    freeaddrinfo(res);

    srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)&sock);

    // --- Step 1: connect ---
    uint8_t creq[16];
    uint32_t ctxid = (uint32_t)rand();
    put64(creq, UDP_MAGIC);
    put32(creq + 8, UDP_CONNECT);
    put32(creq + 12, ctxid);

    uint8_t cresp[64];
    ssize_t clen = 0;
    int rc = request_reply(sock, creq, sizeof(creq), cresp, sizeof(cresp), &clen,
                           ctxid, UDP_CONNECT, cancel);
    if (rc != 0) {
        close(sock);
        return 0;
    }
    uint64_t connection_id = ((uint64_t)get32(cresp + 8) << 32) | get32(cresp + 12);

    // --- Step 2: announce ---
    uint8_t areq[98];
    uint32_t atxid = (uint32_t)rand();
    put64(areq + 0, connection_id);
    put32(areq + 8, UDP_ANNOUNCE);
    put32(areq + 12, atxid);
    memcpy(areq + 16, info_hash, 20);
    memcpy(areq + 36, peer_id, 20);
    put64(areq + 56, 0);
    put64(areq + 64, (uint64_t)left);
    put64(areq + 72, 0);
    put32(areq + 80, 2); // event: started
    put32(areq + 84, 0);
    put32(areq + 88, (uint32_t)rand());
    put32(areq + 92, (uint32_t)max_peers);
    areq[96] = (uint8_t)(listen_port >> 8);
    areq[97] = (uint8_t)(listen_port & 0xff);

    uint8_t aresp[20 + 6 * 512];
    ssize_t alen = 0;
    rc = request_reply(sock, areq, sizeof(areq), aresp, sizeof(aresp), &alen,
                       atxid, UDP_ANNOUNCE, cancel);
    close(sock);
    if (rc != 0) {
        return 0;
    }

    uint32_t count = 0;
    for (ssize_t off = 20; off + 6 <= alen && count < max_peers; off += 6) {
        memcpy(compact_out + count * 6, aresp + off, 6);
        count++;
    }
    log_msg("[tracker] UDP %s:%u: %u peers\n", host, (unsigned)tport, count);
    return count;
}

/* ---- public API ---- */
uint32_t tracker_announce_url_ex(const char *url,
                                 const uint8_t *info_hash,
                                 const uint8_t *peer_id,
                                 uint16_t listen_port,
                                 int64_t downloaded, int64_t left,
                                 uint8_t *compact_out, uint32_t max_peers,
                                 tracker_announce_result_t *result) {
    return tracker_announce_url_ex_cancel(
        url, info_hash, peer_id, listen_port, downloaded, left, compact_out,
        max_peers, result, NULL, NULL);
}

uint32_t tracker_announce_url_ex_cancel(
                                 const char *url,
                                 const uint8_t *info_hash,
                                 const uint8_t *peer_id,
                                 uint16_t listen_port,
                                 int64_t downloaded, int64_t left,
                                 uint8_t *compact_out, uint32_t max_peers,
                                 tracker_announce_result_t *result,
                                 tracker_cancel_cb cancel_callback,
                                 void *cancel_user) {
    tracker_result_init(result);
    if (!url || !info_hash || !peer_id || !compact_out || !max_peers)
        return 0;
    uint32_t count = 0;
    tracker_cancel_t cancel = { cancel_callback, cancel_user };
    if (tracker_cancelled(&cancel))
        return 0;
    if (strncmp(url, "http", 4) == 0) {
        count = http_announce(url, info_hash, peer_id, listen_port,
                              downloaded, left, compact_out, max_peers,
                              result, &cancel);
        if (result)
            result->peers = count;
        return count;
    }
    if (strncmp(url, "udp://", 6) == 0) {
        char host[128] = "";
        uint16_t port = 80;
        if (sscanf(url + 6, "%127[^:/]:%hu", host, &port) < 1)
            return 0;
        count = udp_announce(host, port, info_hash, peer_id, listen_port,
                             downloaded, left, compact_out, max_peers, &cancel);
        if (result)
            result->peers = count;
        return count;
    }
    return 0;
}

uint32_t tracker_announce_url(const char *url,
                              const uint8_t *info_hash,
                              const uint8_t *peer_id,
                              uint16_t listen_port,
                              int64_t downloaded, int64_t left,
                              uint8_t *compact_out, uint32_t max_peers) {
    return tracker_announce_url_ex(url, info_hash, peer_id, listen_port,
                                   downloaded, left, compact_out, max_peers,
                                   NULL);
}

uint32_t tracker_announce(const metainfo_t *mi,
                          const uint8_t *peer_id,
                          uint16_t listen_port,
                          int64_t downloaded, int64_t left,
                          uint8_t *compact_out, uint32_t max_peers) {
    uint32_t total = 0;
    uint8_t tmp[200*6];

    for (uint32_t t = 0; t < mi->num_trackers && total < max_peers; t++) {
        const char *url = mi->trackers[t];
        uint32_t n = 0;

        n = tracker_announce_url(url, mi->info_hash, peer_id, listen_port,
                                 downloaded, left, tmp, 200);

        uint32_t can = (total + n <= max_peers) ? n : max_peers - total;
        memcpy(compact_out + total*6, tmp, can*6);
        total += can;
    }
    return total;
}

