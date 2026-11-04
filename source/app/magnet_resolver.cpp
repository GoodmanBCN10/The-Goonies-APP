#include "magnet_resolver.hpp"

extern "C" {
#include "../core/bencode.h"
#include "../core/dht.h"
#include "../core/metainfo.h"
#include "../core/net.h"
#include "../core/sha1.h"
#include "../core/tracker.h"
#include "../core/util.h"
}

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace goonies {
namespace {

int trackerCancelled(void* user) {
    return static_cast<std::atomic<bool>*>(user)->load() ? 1 : 0;
}

// The ut_metadata id we advertise in our extension handshake. Peers address
// extended messages back to us using this id (BEP 10), regardless of the id
// the peer advertised for its own ut_metadata implementation.
constexpr uint8_t kLocalUtMetadataId = 1;

constexpr size_t kMetadataPieceSize = 16 * 1024;
constexpr size_t kMetadataLimit = 8 * 1024 * 1024;
constexpr size_t kMaxFrameSize = 2 * 1024 * 1024;
constexpr uint64_t kOverallTimeoutMs = 90 * 1000;
constexpr int kIoTimeoutMs = 6000;
constexpr uint32_t kMaxPeersPerTracker = 64;
constexpr uint32_t kMaxMergedPeers = 192;
constexpr uint32_t kMaxConcurrentPeers = 5;
constexpr uint32_t kRequestPipeline = 8;
constexpr uint64_t kDhtSearchTimeoutMs = 60 * 1000;
constexpr uint32_t kDhtTargetPeers = 32;
constexpr int kDhtPollIntervalMs = 250;
constexpr uint16_t kDhtPort = 6881;

// PEX amplification: when the tracker/DHT phase yields a
// thin peer list, ask each of the few peers we have for their swarm view.
constexpr uint32_t kPexThinThreshold = 3;
constexpr uint8_t kLocalUtPexId = 2;
constexpr int kPexPeerTimeoutMs = 5000;

// Well-known public UDP trackers from TorrentShopNX, appended to every torrent
// so we discover far more peers than the torrent's own (often blocked/stale) tracker.
static const char* kDefaultTrackers[] = {
    "udp://open.stealth.si:80/announce",
    "udp://exodus.desync.com:6969/announce",
    "udp://tracker.dler.org:6969/announce",
    "udp://tracker.torrent.eu.org:451/announce",
};

bool hexNibble(char c, uint8_t& value) {
    if (c >= '0' && c <= '9')
        value = static_cast<uint8_t>(c - '0');
    else if (c >= 'a' && c <= 'f')
        value = static_cast<uint8_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
        value = static_cast<uint8_t>(c - 'A' + 10);
    else
        return false;
    return true;
}

std::string urlDecode(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            uint8_t hi = 0, lo = 0;
            if (hexNibble(input[i + 1], hi) && hexNibble(input[i + 2], lo)) {
                result.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        result.push_back(input[i] == '+' ? ' ' : input[i]);
    }
    return result;
}

bool allowedTracker(const std::string& url) {
    if (url.empty())
        return false;
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.rfind("http://", 0) == 0 ||
           lower.rfind("https://", 0) == 0 ||
           lower.rfind("udp://", 0) == 0;
}

std::vector<std::string> getAllTrackerCandidates(const std::string& original) {
    std::vector<std::string> urls;
    auto add = [&](const std::string& url) {
        if (!allowedTracker(url))
            return;
        if (std::find(urls.begin(), urls.end(), url) == urls.end())
            urls.push_back(url);
    };
    add(original);
    for (const char* tracker : kDefaultTrackers) {
        add(tracker);
    }
    add("http://bt.t-ru.org/ann?magnet");
    add("http://bt2.t-ru.org/ann?magnet");
    add("http://bt3.t-ru.org/ann?magnet");
    add("http://bt4.t-ru.org/ann?magnet");
    return urls;
}


bool containsPeer(const std::vector<uint8_t>& peers, const uint8_t* compact) {
    for (size_t offset = 0; offset + 6 <= peers.size(); offset += 6) {
        if (std::memcmp(peers.data() + offset, compact, 6) == 0)
            return true;
    }
    return false;
}

void appendUniquePeers(std::vector<uint8_t>& peers,
                       const uint8_t* compact,
                       uint32_t count) {
    for (uint32_t i = 0; i < count && peers.size() / 6 < kMaxMergedPeers; ++i) {
        const uint8_t* item = compact + i * 6;
        if (containsPeer(peers, item))
            continue;
        peers.insert(peers.end(), item, item + 6);
    }
}

bool waitFd(socket_t fd, short events, int timeoutMs) {
    pollfd item{fd, events, 0};
    int result;
    do {
        result = poll(&item, 1, timeoutMs);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (item.revents & events) != 0;
}

/* Compact IPv4 peers discovered by the short-lived DHT search that runs in
   parallel with the tracker announces (RF_ACCESS_PLAN П1.1). With RuTracker
   blocked, DHT is the only peer source, so tracker failures alone no longer
   abort the resolve. */
struct DhtSearch {
    std::mutex mutex;
    std::vector<uint8_t> peers;
};

void dhtPeerFound(void* user, uint32_t ipBe, uint16_t portBe) {
    DhtSearch* search = static_cast<DhtSearch*>(user);
    uint8_t compact[6];
    std::memcpy(compact, &ipBe, 4);
    std::memcpy(compact + 4, &portBe, 2);
    std::lock_guard<std::mutex> lock(search->mutex);
    appendUniquePeers(search->peers, compact, 1);
}

uint32_t dhtPeerCount(DhtSearch& search) {
    std::lock_guard<std::mutex> lock(search.mutex);
    return static_cast<uint32_t>(search.peers.size() / 6);
}

bool writeTorrentAtomic(const std::string& path,
                        const std::vector<uint8_t>& torrent,
                        std::string& error) {
    std::string temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Unable to create the resolved torrent file.";
            return false;
        }
        output.write(reinterpret_cast<const char*>(torrent.data()),
                     static_cast<std::streamsize>(torrent.size()));
        output.flush();
        if (!output.good()) {
            unlink(temporary.c_str());
            error = "Unable to write the resolved torrent file.";
            return false;
        }
    }
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        error = "Unable to replace the resolved torrent file.";
        return false;
    }
    return true;
}

void ensureDir(const char* dir_path) {
    if (!dir_path || !*dir_path) return;
    char dir[1024];
    size_t len = std::strlen(dir_path);
    if (len >= sizeof(dir)) return;
    std::memcpy(dir, dir_path, len + 1);
    for (size_t i = 0; i < len; ++i) {
        if (dir[i] == '\\') dir[i] = '/';
    }
    while (len > 0 && dir[len - 1] == '/') {
        dir[--len] = '\0';
    }
    if (len == 0) return;

    char cur[1024];
    size_t pos = 0;
    cur[0] = '\0';
    if (std::strncmp(dir, "sdmc:/", 6) == 0) {
        std::snprintf(cur, sizeof(cur), "sdmc:");
        pos = 6;
    }
    while (pos <= len) {
        size_t next = pos;
        while (dir[next] && dir[next] != '/') next++;
        if (next > pos) {
            size_t clen = std::strlen(cur);
            if (clen && cur[clen - 1] != '/') cur[clen++] = '/';
            if (clen + (next - pos) + 1 <= sizeof(cur)) {
                std::memcpy(cur + clen, dir + pos, next - pos);
                cur[clen + (next - pos)] = '\0';
                mkdir(cur, 0777);
            }
        }
        if (!dir[next]) break;
        pos = next + 1;
    }
}

bool metaCacheLoad(const uint8_t infoHash[20], const std::string& infoHashHex, std::vector<uint8_t>& out) {
    out.clear();
    char lowerHex[41];
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 20; i++) {
        lowerHex[i * 2]     = digits[infoHash[i] >> 4];
        lowerHex[i * 2 + 1] = digits[infoHash[i] & 0x0f];
    }
    lowerHex[40] = '\0';

    static const char* kDirs[] = {
        "sdmc:/switch/thegoonies/cache/meta",
        "sdmc:/switch/thegoonies/meta",
        "sdmc:/switch/TorrentShopNX/cache/meta",
        "sdmc:/switch/TorrentShop/meta",
        "/switch/thegoonies/cache/meta",
        "/switch/TorrentShopNX/cache/meta"
    };

    for (const char* dir : kDirs) {
        std::string paths[2] = {
            std::string(dir) + "/" + lowerHex + ".meta",
            std::string(dir) + "/" + infoHashHex + ".meta"
        };
        for (const std::string& p : paths) {
            FILE* f = fopen(p.c_str(), "rb");
            if (!f) continue;
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (fsize <= 0 || fsize > static_cast<long>(kMetadataLimit)) {
                fclose(f);
                continue;
            }
            std::vector<uint8_t> buf(fsize);
            if (fread(buf.data(), 1, fsize, f) != static_cast<size_t>(fsize)) {
                fclose(f);
                continue;
            }
            fclose(f);

            uint8_t digest[20];
            sha1(buf.data(), buf.size(), digest);
            if (std::memcmp(digest, infoHash, 20) == 0) {
                out = std::move(buf);
                log_msg("[magnet] metadata cache hit from %s (%zu bytes)\n", p.c_str(), out.size());
                return true;
            }
        }
    }
    return false;
}

bool metaCacheSave(const uint8_t infoHash[20], const std::string& infoHashHex, const std::vector<uint8_t>& data) {
    (void)infoHashHex;
    if (data.empty() || data.size() > kMetadataLimit) return false;
    const char* cacheDir = "sdmc:/switch/thegoonies/cache/meta";
    ensureDir(cacheDir);

    char lowerHex[41];
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 20; i++) {
        lowerHex[i * 2]     = digits[infoHash[i] >> 4];
        lowerHex[i * 2 + 1] = digits[infoHash[i] & 0x0f];
    }
    lowerHex[40] = '\0';

    std::string path = std::string(cacheDir) + "/" + lowerHex + ".meta";
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = (fwrite(data.data(), 1, data.size(), f) == data.size());
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        unlink(tmp.c_str());
        return false;
    }
    unlink(path.c_str());
    rename(tmp.c_str(), path.c_str());
    log_msg("[magnet] saved metadata to cache (%zu bytes): %s\n", data.size(), path.c_str());
    return true;
}

void runDhtSearch(const uint8_t infoHash[20],
                  std::atomic<bool>& cancelled,
                  std::atomic<bool>& stop,
                  DhtSearch& search) {
    if (cancelled || stop)
        return;
    uint8_t nodeId[20];
    rand_bytes(nodeId, 20);
    /* Fails when a running download already owns the DHT singleton or the
       UDP port; the resolve then proceeds on trackers alone. */
    dht_engine_t* engine = dht_engine_create(kDhtPort, nodeId);
    if (!engine) {
        log_msg("[magnet] dht unavailable, resolving without it\n");
        return;
    }
    dht_engine_bootstrap(engine);
    dht_engine_search(engine, infoHash, dhtPeerFound, &search);
    uint64_t deadline = now_ms() + kDhtSearchTimeoutMs;
    while (!cancelled && !stop && now_ms() < deadline &&
           dhtPeerCount(search) < kDhtTargetPeers) {
        waitFd(dht_engine_fd(engine), POLLIN, kDhtPollIntervalMs);
        dht_engine_tick(engine);
    }
    dht_engine_destroy(engine);
}

bool sendAll(socket_t fd, const uint8_t* data, size_t size) {
    size_t sent = 0;
    uint64_t deadline = now_ms() + kIoTimeoutMs;
    while (sent < size) {
        int remMs = static_cast<int>(deadline > now_ms() ? deadline - now_ms() : 0);
        if (remMs <= 0 || !waitFd(fd, POLLOUT, remMs))
            return false;
        ssize_t count = send(fd, data + sent, size - sent, 0);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return false;
        }
        if (count == 0)
            return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

bool recvAll(socket_t fd, uint8_t* data, size_t size, int timeoutMs) {
    size_t received = 0;
    uint64_t deadline = now_ms() + timeoutMs;
    while (received < size) {
        int remMs = static_cast<int>(deadline > now_ms() ? deadline - now_ms() : 0);
        if (remMs <= 0 || !waitFd(fd, POLLIN, remMs))
            return false;
        ssize_t count = recv(fd, data + received, size - received, 0);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return false;
        }
        if (count == 0)
            return false;
        received += static_cast<size_t>(count);
    }
    return true;
}

bool sendFrame(socket_t fd, const std::vector<uint8_t>& payload) {
    uint32_t size = static_cast<uint32_t>(payload.size());
    uint8_t header[4] = {
        static_cast<uint8_t>(size >> 24),
        static_cast<uint8_t>(size >> 16),
        static_cast<uint8_t>(size >> 8),
        static_cast<uint8_t>(size),
    };
    return sendAll(fd, header, sizeof(header)) &&
           sendAll(fd, payload.data(), payload.size());
}

bool recvFrame(socket_t fd, std::vector<uint8_t>& payload) {
    uint8_t header[4];
    if (!recvAll(fd, header, sizeof(header), kIoTimeoutMs))
        return false;
    uint32_t size = (static_cast<uint32_t>(header[0]) << 24) |
                    (static_cast<uint32_t>(header[1]) << 16) |
                    (static_cast<uint32_t>(header[2]) << 8) |
                    static_cast<uint32_t>(header[3]);
    if (size > kMaxFrameSize)
        return false;
    payload.resize(size);
    return size == 0 || recvAll(fd, payload.data(), size, kIoTimeoutMs);
}

bool connectPeer(const uint8_t* compact, const uint8_t infoHash[20],
                 const uint8_t peerId[20], socket_t& fd) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    std::memcpy(&address.sin_addr.s_addr, compact, 4);
    std::memcpy(&address.sin_port, compact + 4, 2);
    fd = net_tcp_connect(&address);
    if (fd == INVALID_SOCK)
        return false;

    // Buffer options aligned with TorrentShopNX for Switch performance
    int rcvbuf = 64 * 1024;
    int sndbuf = 16 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    // Fast 3s connect timeout like TorrentShopNX (dead peers fail immediately)
    if (!waitFd(fd, POLLOUT, 3000)) {
        net_close(fd);
        fd = INVALID_SOCK;
        return false;
    }
    int socketError = 0;
    socklen_t errorSize = sizeof(socketError);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &errorSize) != 0 ||
        socketError != 0) {
        net_close(fd);
        fd = INVALID_SOCK;
        return false;
    }

    uint8_t handshake[68]{};
    handshake[0] = 19;
    std::memcpy(handshake + 1, "BitTorrent protocol", 19);
    handshake[25] = 0x10;
    std::memcpy(handshake + 28, infoHash, 20);
    std::memcpy(handshake + 48, peerId, 20);
    if (!sendAll(fd, handshake, sizeof(handshake))) {
        net_close(fd);
        fd = INVALID_SOCK;
        return false;
    }

    uint8_t response[68];
    if (!recvAll(fd, response, sizeof(response), 4000) ||
        response[0] != 19 ||
        std::memcmp(response + 1, "BitTorrent protocol", 19) != 0 ||
        std::memcmp(response + 28, infoHash, 20) != 0 ||
        (response[25] & 0x10) == 0) {
        net_close(fd);
        fd = INVALID_SOCK;
        return false;
    }
    return true;
}

bool negotiateMetadata(socket_t fd, uint8_t& peerExtension,
                       size_t& metadataSize) {
    static const char handshake[] = "d1:md11:ut_metadatai1eee";
    std::vector<uint8_t> request{20, 0};
    request.insert(request.end(), handshake, handshake + sizeof(handshake) - 1);
    if (!sendFrame(fd, request))
        return false;

    for (int message = 0; message < 32; ++message) {
        std::vector<uint8_t> frame;
        if (!recvFrame(fd, frame))
            return false;
        if (frame.size() < 3 || frame[0] != 20 || frame[1] != 0)
            continue;
        const char* begin = reinterpret_cast<const char*>(frame.data() + 2);
        const char* end = reinterpret_cast<const char*>(frame.data() + frame.size());
        be_node_t root;
        const char* cursor = begin;
        if (!be_decode(&cursor, end, &root) || root.type != BE_DICT)
            continue;
        be_node_t map;
        be_node_t extension;
        be_node_t size;
        if (!be_dict_get(root.buf, root.buf + root.raw_len, "m", 1, &map) ||
            map.type != BE_DICT ||
            !be_dict_get(map.buf, map.buf + map.raw_len, "ut_metadata", 11,
                         &extension) ||
            extension.type != BE_INT || extension.ival <= 0 ||
            extension.ival > 255)
            continue;
        if (!be_dict_get(root.buf, root.buf + root.raw_len, "metadata_size", 13,
                         &size) ||
            size.type != BE_INT || size.ival <= 0 ||
            size.ival > static_cast<int64_t>(kMetadataLimit))
            return false;
        peerExtension = static_cast<uint8_t>(extension.ival);
        metadataSize = static_cast<size_t>(size.ival);
        return true;
    }
    return false;
}

bool sendMetadataRequest(socket_t fd, uint8_t extension, uint32_t piece) {
    std::string body = "d8:msg_typei0e5:piecei" + std::to_string(piece) + "ee";
    std::vector<uint8_t> frame{20, extension};
    frame.insert(frame.end(), body.begin(), body.end());
    return sendFrame(fd, frame);
}

/* Per-cause counters so the on-screen error says WHERE the swarm failed
   (connect / handshake / no ut_metadata / incomplete / SHA-1 / parser). */
struct FetchStats {
    std::atomic<uint32_t> tried{0};
    std::atomic<uint32_t> connectFail{0};
    std::atomic<uint32_t> socketFail{0};
    std::atomic<int> lastSocketErrno{0};
    std::atomic<uint32_t> handshakeFail{0};
    std::atomic<uint32_t> noMetadata{0};
    std::atomic<uint32_t> incomplete{0};
    std::atomic<uint32_t> hashMismatch{0};
    std::atomic<uint32_t> rejected{0};
    std::mutex mutex;
    std::string lastReject;
};

/* ---- Clone of TorrentShopNX engine/bt_peer.c metadata path ----
   Blocking TCP socket with SO_RCVTIMEO/SO_SNDTIMEO = 10 s, non-blocking
   connect + poll() bounded to PEER_CONNECT_SECS (3 s), then plain blocking
   recv()/send() loops. */
constexpr int kTsnxPeerConnectSecs = 3;
constexpr int kTsnxSocketTimeoutSecs = 10;

int tsnxConnectTimeout(int sock, const sockaddr* sa, socklen_t salen,
                       int seconds) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(sock, sa, salen);
    if (rc == 0) {
        fcntl(sock, F_SETFL, flags);
        return 0;
    }
    if (errno != EINPROGRESS) {
        fcntl(sock, F_SETFL, flags);
        return -1;
    }
    pollfd pfd{};
    pfd.fd = sock;
    pfd.events = POLLOUT;
    rc = poll(&pfd, 1, seconds * 1000);
    if (rc <= 0) {
        fcntl(sock, F_SETFL, flags);
        return -1;
    }
    int soerr = 0;
    socklen_t len = sizeof(soerr);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &soerr, &len) < 0 || soerr != 0) {
        fcntl(sock, F_SETFL, flags);
        return -1;
    }
    fcntl(sock, F_SETFL, flags);  // back to blocking for the transfer phase
    return 0;
}

bool tsnxReadFull(int sock, void* buf, size_t len) {
    uint8_t* b = static_cast<uint8_t*>(buf);
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(sock, b + got, len - got, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

bool tsnxWriteFull(int sock, const void* buf, size_t len) {
    const uint8_t* b = static_cast<const uint8_t*>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, b + sent, len - sent, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

/* Reads one message, skipping keep-alives. Returns the id or -1. */
int tsnxRecvMsg(int sock, std::vector<uint8_t>& payload) {
    for (;;) {
        uint8_t hdr[4];
        if (!tsnxReadFull(sock, hdr, 4)) return -1;
        uint32_t len = (static_cast<uint32_t>(hdr[0]) << 24) |
                       (static_cast<uint32_t>(hdr[1]) << 16) |
                       (static_cast<uint32_t>(hdr[2]) << 8) |
                       static_cast<uint32_t>(hdr[3]);
        if (len == 0) continue;  // keep-alive
        if (len > kMaxFrameSize) return -1;
        uint8_t id = 0;
        if (!tsnxReadFull(sock, &id, 1)) return -1;
        payload.resize(len - 1);
        if (len > 1 && !tsnxReadFull(sock, payload.data(), len - 1)) return -1;
        return id;
    }
}

bool tsnxSendExt(int sock, uint8_t extId, const char* data, size_t n) {
    uint32_t len = static_cast<uint32_t>(2 + n);
    uint8_t hdr[6] = {
        static_cast<uint8_t>(len >> 24), static_cast<uint8_t>(len >> 16),
        static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len),
        20, extId,
    };
    return tsnxWriteFull(sock, hdr, sizeof(hdr)) &&
           (n == 0 || tsnxWriteFull(sock, data, n));
}

bool fetchMetadataFromPeer(const uint8_t* compact,
                           const MagnetSpec& spec,
                           const uint8_t peerId[20],
                           uint32_t peerIndex,
                           uint32_t peerCount,
                           uint64_t deadline,
                           std::atomic<bool>& cancelled,
                           std::atomic<bool>& stopWorkers,
                           const MagnetResolver::ProgressCallback& progress,
                           std::vector<uint8_t>& metadata,
                           bool& handshakeVerified,
                           FetchStats& stats) {
    handshakeVerified = false;
    if (cancelled || stopWorkers || now_ms() >= deadline)
        return false;
    stats.tried++;
    if (progress)
        progress({MagnetProgress::Stage::Connecting, 0, 0, peerIndex + 1,
                  peerCount});

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        stats.socketFail++;
        stats.lastSocketErrno = errno;
        log_msg("[magnet] peer %u/%u socket() failed errno=%d\n",
                peerIndex + 1, peerCount, errno);
        return false;
    }
    int rcvbuf = 64 * 1024;
    int sndbuf = 16 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    timeval tv{};
    tv.tv_sec = kTsnxSocketTimeoutSecs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    std::memcpy(&sa.sin_addr.s_addr, compact, 4);
    std::memcpy(&sa.sin_port, compact + 4, 2);
    if (tsnxConnectTimeout(sock, reinterpret_cast<sockaddr*>(&sa), sizeof(sa),
                           kTsnxPeerConnectSecs) != 0) {
        close(sock);
        stats.connectFail++;
        log_msg("[magnet] peer %u/%u connection refused/timeout\n",
                peerIndex + 1, peerCount);
        return false;
    }

    uint8_t hs[68];
    hs[0] = 19;
    std::memcpy(hs + 1, "BitTorrent protocol", 19);
    std::memset(hs + 20, 0, 8);
    hs[25] = 0x10;
    std::memcpy(hs + 28, spec.infoHash, 20);
    std::memcpy(hs + 48, peerId, 20);
    uint8_t resp[68];
    if (!tsnxWriteFull(sock, hs, sizeof(hs)) ||
        !tsnxReadFull(sock, resp, sizeof(resp)) ||
        std::memcmp(resp + 28, spec.infoHash, 20) != 0 ||
        !(resp[25] & 0x10)) {
        close(sock);
        stats.handshakeFail++;
        log_msg("[magnet] peer %u/%u handshake failed\n",
                peerIndex + 1, peerCount);
        return false;
    }
    handshakeVerified = true;
    log_msg("[magnet] peer %u/%u BitTorrent handshake ok\n",
            peerIndex + 1, peerCount);

    static const char myEhs[] = "d1:md11:ut_metadatai1eee";
    if (!tsnxSendExt(sock, 0, myEhs, sizeof(myEhs) - 1)) {
        close(sock);
        stats.handshakeFail++;
        return false;
    }

    std::vector<uint8_t> payload;
    int peerUtId = -1;
    int64_t metaSize = -1;
    for (int i = 0; i < 16; ++i) {
        int id = tsnxRecvMsg(sock, payload);
        if (id < 0) break;
        if (id != 20 || payload.size() < 1 || payload[0] != 0) continue;
        const char* cursor = reinterpret_cast<const char*>(payload.data() + 1);
        const char* end = reinterpret_cast<const char*>(payload.data() + payload.size());
        be_node_t root;
        if (be_decode(&cursor, end, &root) && root.type == BE_DICT) {
            be_node_t m, utm, msize;
            if (be_dict_get(root.buf, root.buf + root.raw_len, "m", 1, &m) &&
                m.type == BE_DICT &&
                be_dict_get(m.buf, m.buf + m.raw_len, "ut_metadata", 11, &utm) &&
                utm.type == BE_INT)
                peerUtId = static_cast<int>(utm.ival);
            if (be_dict_get(root.buf, root.buf + root.raw_len, "metadata_size",
                            13, &msize) && msize.type == BE_INT)
                metaSize = msize.ival;
        }
        break;
    }
    if (peerUtId <= 0 || peerUtId > 255 || metaSize <= 0 ||
        metaSize > static_cast<int64_t>(kMetadataLimit)) {
        close(sock);
        stats.noMetadata++;
        log_msg("[magnet] peer %u/%u does not share metadata (ut=%d size=%lld)\n",
                peerIndex + 1, peerCount, peerUtId, (long long)metaSize);
        return false;
    }
    log_msg("[magnet] peer %u/%u ut_metadata=%d size=%lld\n",
            peerIndex + 1, peerCount, peerUtId, (long long)metaSize);

    size_t metadataSize = static_cast<size_t>(metaSize);
    uint32_t npieces = static_cast<uint32_t>(
        (metadataSize + kMetadataPieceSize - 1) / kMetadataPieceSize);
    std::vector<uint8_t> local(metadataSize);
    std::vector<bool> got(npieces, false);

    // Request every metadata piece up front.
    for (uint32_t i = 0; i < npieces; ++i) {
        std::string req = "d8:msg_typei0e5:piecei" + std::to_string(i) + "ee";
        tsnxSendExt(sock, static_cast<uint8_t>(peerUtId), req.data(), req.size());
    }

    uint32_t received = 0;
    int budget = static_cast<int>(npieces * 4 + 64);
    while (received < npieces && budget-- > 0 && !cancelled && !stopWorkers &&
           now_ms() < deadline) {
        int id = tsnxRecvMsg(sock, payload);
        if (id < 0) break;
        if (id != 20 || payload.size() < 1 || payload[0] != kLocalUtMetadataId)
            continue;
        const char* cursor = reinterpret_cast<const char*>(payload.data() + 1);
        const char* end = reinterpret_cast<const char*>(payload.data() + payload.size());
        be_node_t header;
        if (!be_decode(&cursor, end, &header) || header.type != BE_DICT)
            continue;
        be_node_t type, index;
        if (!be_dict_get(header.buf, header.buf + header.raw_len, "msg_type", 8, &type) ||
            !be_dict_get(header.buf, header.buf + header.raw_len, "piece", 5, &index) ||
            type.type != BE_INT || index.type != BE_INT || index.ival < 0)
            continue;
        if (type.ival == 2) {
            log_msg("[magnet] peer %u/%u rejected metadata piece %lld\n",
                    peerIndex + 1, peerCount, (long long)index.ival);
            break;
        }
        if (type.ival != 1) continue;
        uint32_t piece = static_cast<uint32_t>(index.ival);
        if (piece >= npieces || got[piece]) continue;
        size_t offset = static_cast<size_t>(piece) * kMetadataPieceSize;
        size_t expected = std::min(kMetadataPieceSize, metadataSize - offset);
        size_t dataLen = static_cast<size_t>(end - cursor);
        if (dataLen < expected) continue;
        std::memcpy(local.data() + offset, cursor, expected);
        got[piece] = true;
        ++received;
        if (progress)
            progress({MagnetProgress::Stage::FetchingMetadata, received,
                      npieces, peerIndex + 1, peerCount});
    }
    close(sock);

    if (received != npieces) {
        stats.incomplete++;
        log_msg("[magnet] peer %u/%u metadata incomplete %u/%u\n",
                peerIndex + 1, peerCount, received, npieces);
        return false;
    }
    uint8_t hash[20];
    sha1(local.data(), local.size(), hash);
    if (std::memcmp(hash, spec.infoHash, 20) != 0) {
        stats.hashMismatch++;
        log_msg("[magnet] peer %u/%u metadata SHA-1 mismatch\n",
                peerIndex + 1, peerCount);
        return false;
    }
    metadata = std::move(local);
    return true;
}

// Advertise both ut_metadata and ut_pex so the peer will push its swarm view
// to us on our advertised ut_pex id (kLocalUtPexId). BEP10 handshake.
bool sendPexHandshake(socket_t fd) {
    static const char handshake[] =
        "d1:md11:ut_metadatai1e6:ut_pexi2eee";
    std::vector<uint8_t> request{20, 0};
    request.insert(request.end(), handshake, handshake + sizeof(handshake) - 1);
    return sendFrame(fd, request);
}

// Connect to one thin-list peer, advertise ut_pex, and harvest the compact
// peers it pushes in any ut_pex "added" field (RF_ACCESS_PLAN П1.2). Best
// effort: peers emit PEX on their own cadence, so an empty result is normal.
// Merges into `out` (never the live `peers` vector — appending there while its
// data() is being iterated would dangle).
void harvestPexFromPeer(const uint8_t* compact, const MagnetSpec& spec,
                        const uint8_t peerId[20],
                        std::atomic<bool>& cancelled,
                        std::vector<uint8_t>& out) {
    socket_t fd = INVALID_SOCK;
    if (!connectPeer(compact, spec.infoHash, peerId, fd)) {
        net_close(fd);
        return;
    }
    if (!sendPexHandshake(fd)) {
        net_close(fd);
        return;
    }
    uint64_t deadline = now_ms() + kPexPeerTimeoutMs;
    while (!cancelled && now_ms() < deadline) {
        std::vector<uint8_t> frame;
        if (!recvFrame(fd, frame))
            break;
        // Extended messages the peer sends to us carry our advertised id, not
        // the peer's (BEP 10). Skip the peer's extension handshake (id 0) and
        // anything that is not our ut_pex channel.
        if (frame.size() < 3 || frame[0] != 20 || frame[1] != kLocalUtPexId)
            continue;
        const char* begin = reinterpret_cast<const char*>(frame.data() + 2);
        const char* end =
            reinterpret_cast<const char*>(frame.data() + frame.size());
        be_node_t root;
        const char* cursor = begin;
        if (!be_decode(&cursor, end, &root) || root.type != BE_DICT)
            break;
        be_node_t added;
        if (be_dict_get(root.buf, root.buf + root.raw_len, "added", 5, &added) &&
            added.type == BE_STR && added.slen >= 6) {
            appendUniquePeers(out,
                              reinterpret_cast<const uint8_t*>(added.sval),
                              static_cast<uint32_t>(added.slen / 6));
        }
    }
    net_close(fd);
}

std::string bencodeString(const std::string& value) {
    return std::to_string(value.size()) + ":" + value;
}

bool runMetadataPass(const MagnetSpec& spec,
                     const uint8_t peerId[20],
                     const std::vector<uint8_t>& peers,
                     uint64_t deadline,
                     std::atomic<bool>& cancelled,
                     const MagnetResolver::ProgressCallback& progress,
                     std::vector<uint8_t>& outMetadata,
                     std::vector<uint8_t>& outVerifiedEndpoints,
                     FetchStats& stats) {
    uint32_t peerCount = static_cast<uint32_t>(peers.size() / 6);
    if (peerCount == 0)
        return false;

    std::mutex mutex;
    uint32_t nextPeer = 0;
    bool resolved = false;
    std::atomic<bool> stopWorkers{false};
    std::vector<uint8_t> metadata;
    std::vector<uint8_t> verifiedEndpoints;
    std::vector<nx::thread> workers;
    uint32_t workerCount = std::min(peerCount, kMaxConcurrentPeers);
    for (uint32_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&] {
            while (!cancelled && !stopWorkers && now_ms() < deadline) {
                uint32_t peerIndex = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (resolved || nextPeer >= peerCount)
                        return;
                    peerIndex = nextPeer++;
                }

                std::vector<uint8_t> candidate;
                bool handshakeVerified = false;
                bool fetched = fetchMetadataFromPeer(
                    peers.data() + peerIndex * 6, spec, peerId, peerIndex,
                    peerCount, deadline, cancelled, stopWorkers, progress,
                    candidate, handshakeVerified, stats);
                if (handshakeVerified) {
                    std::lock_guard<std::mutex> lock(mutex);
                    appendUniquePeers(verifiedEndpoints,
                                      peers.data() + peerIndex * 6, 1);
                }
                if (!fetched)
                    continue;

                std::vector<uint8_t> torrentProbe;
                std::string probeError;
                if (!MagnetResolver::buildTorrent(spec, candidate, torrentProbe, probeError)) {
                    log_msg("[magnet] peer %u/%u metadata rejected: %s\n",
                            peerIndex + 1, peerCount, probeError.c_str());
                    stats.rejected++;
                    {
                        std::lock_guard<std::mutex> lk(stats.mutex);
                        stats.lastReject = probeError;
                    }
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!resolved) {
                        metadata = std::move(candidate);
                        resolved = true;
                        stopWorkers.store(true);
                    }
                }
                return;
            }
        });
    }
    for (nx::thread& worker : workers)
        worker.join();

    if (resolved && !metadata.empty()) {
        outMetadata = std::move(metadata);
        outVerifiedEndpoints = std::move(verifiedEndpoints);
        return true;
    }
    return false;
}

} // namespace

bool MagnetResolver::parse(const std::string& uri, MagnetSpec& spec,
                           std::string& error) {
    spec = {};
    if (uri.rfind("magnet:?", 0) != 0) {
        error = "Catalog entry has an invalid magnet URI.";
        return false;
    }
    bool haveHash = false;
    size_t start = 8;
    while (start <= uri.size()) {
        size_t end = uri.find('&', start);
        std::string pair = uri.substr(start, end - start);
        size_t equals = pair.find('=');
        std::string key = pair.substr(0, equals);
        std::string value = equals == std::string::npos
                          ? "" : urlDecode(pair.substr(equals + 1));
        if (key == "xt" && value.rfind("urn:btih:", 0) == 0) {
            std::string hash = value.substr(9);
            if (hash.size() != 40 || haveHash) {
                error = "Only one hexadecimal BitTorrent v1 hash is supported.";
                return false;
            }
            for (size_t i = 0; i < 20; ++i) {
                uint8_t hi = 0, lo = 0;
                if (!hexNibble(hash[i * 2], hi) ||
                    !hexNibble(hash[i * 2 + 1], lo)) {
                    error = "The magnet info hash is not hexadecimal.";
                    return false;
                }
                spec.infoHash[i] = static_cast<uint8_t>((hi << 4) | lo);
            }
            std::transform(hash.begin(), hash.end(), hash.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::toupper(c));
                           });
            spec.infoHashHex = hash;
            haveHash = true;
        } else if (key == "tr" && spec.trackerUrl.empty() &&
                   allowedTracker(value)) {
            spec.trackerUrl = value;
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (!haveHash) {
        error = "The magnet does not contain a BitTorrent v1 info hash.";
        return false;
    }
    if (spec.trackerUrl.empty()) {
        spec.trackerUrl = "http://bt.t-ru.org/ann?magnet";
    }
    return true;
}

bool MagnetResolver::buildTorrent(const MagnetSpec& spec,
                                  const std::vector<uint8_t>& info,
                                  std::vector<uint8_t>& torrent,
                                  std::string& error) {
    if (info.empty() || info.size() > kMetadataLimit) {
        error = "Peer metadata is empty or too large.";
        return false;
    }
    const char* cursor = reinterpret_cast<const char*>(info.data());
    const char* end = cursor + info.size();
    be_node_t root;
    if (!be_decode(&cursor, end, &root) || root.type != BE_DICT ||
        cursor != end) {
        error = "Peer metadata is not a valid info dictionary.";
        return false;
    }
    uint8_t digest[20];
    sha1(info.data(), info.size(), digest);
    if (std::memcmp(digest, spec.infoHash, 20) != 0) {
        error = "Peer metadata does not match the magnet info hash.";
        return false;
    }

    /* Bake public UDP trackers and RuTracker mirrors into the torrent as an
       announce-list so torrent.c announces to all of them during download. */
    std::vector<std::string> trackers =
        getAllTrackerCandidates(spec.trackerUrl);
    std::string announceList = "13:announce-listl";
    for (const std::string& tracker : trackers)
        announceList += "l" + bencodeString(tracker) + "e";
    announceList += "e";
    std::string primary = !spec.trackerUrl.empty() ? spec.trackerUrl : kDefaultTrackers[0];
    std::string prefix = "d8:announce" + bencodeString(primary) +
                         announceList + "4:info";
    torrent.assign(prefix.begin(), prefix.end());
    torrent.insert(torrent.end(), info.begin(), info.end());
    torrent.push_back('e');

    metainfo_t parsed;
    if (!metainfo_parse(torrent.data(), torrent.size(), &parsed)) {
        error = "Resolved metadata is not a supported safe torrent.";
        return false;
    }
    bool hashMatches = std::memcmp(parsed.info_hash, spec.infoHash, 20) == 0;
    metainfo_free(&parsed);
    if (!hashMatches) {
        error = "Generated torrent failed info-hash validation.";
        return false;
    }
    return true;
}

bool MagnetResolver::resolveToFile(const std::string& uri,
                                   const std::string& path,
                                   std::atomic<bool>& cancelled,
                                   const ProgressCallback& progress,
                                   std::string& error,
                                   std::vector<uint8_t>* verifiedPeers,
                                   const std::vector<uint8_t>* presetInfo)
    const {
    if (verifiedPeers)
        verifiedPeers->clear();
    MagnetSpec spec;
    if (!parse(uri, spec, error))
        return false;

    /* Pre-resolved info dictionary from the catalog */
    if (presetInfo && !presetInfo->empty()) {
        if (progress)
            progress({MagnetProgress::Stage::Validating, 1, 1});
        std::vector<uint8_t> torrent;
        std::string presetError;
        if (buildTorrent(spec, *presetInfo, torrent, presetError)) {
            log_msg("[magnet] resolved from catalog info_dict (%zu bytes)\n",
                    presetInfo->size());
            return writeTorrentAtomic(path, torrent, error);
        }
        log_msg("[magnet] catalog info_dict rejected: %s\n",
                presetError.c_str());
    }

    /* Step 2: Check persistent metadata cache on SD card (instant 1ms resolution) */
    {
        std::vector<uint8_t> cached;
        if (metaCacheLoad(spec.infoHash, spec.infoHashHex, cached)) {
            if (progress)
                progress({MagnetProgress::Stage::Validating, 1, 1});
            std::vector<uint8_t> torrent;
            std::string cacheError;
            if (buildTorrent(spec, cached, torrent, cacheError)) {
                log_msg("[magnet] resolved from persistent cache (%zu bytes)\n", cached.size());
                return writeTorrentAtomic(path, torrent, error);
            }
        }
    }

    uint8_t peerId[20];
    std::memcpy(peerId, "-PN0001-", 8);
    rand_bytes(peerId + 8, 12);

    if (progress)
        progress({MagnetProgress::Stage::FindingPeers});

    /* Step 3: Announce to all trackers in parallel with worker pool.
       TorrentShopNX does META_ANNOUNCE_ROUNDS = 2: trackers hand out rotating
       swarm subsets, so a second round ~3 s later often yields the reachable
       peer the first one did not. */
    auto trackers = getAllTrackerCandidates(spec.trackerUrl);
    std::vector<uint8_t> trackerPeers;
    std::mutex trackerMtx;
    std::vector<uint8_t> metadata;
    std::vector<uint8_t> verifiedEndpoints;
    uint64_t deadline = now_ms() + kOverallTimeoutMs;
    FetchStats stats;
    size_t triedUpTo = 0;  // peers already handed to a metadata pass
    for (int round = 0; round < 2 && metadata.empty() && !cancelled; ++round) {
    if (round > 0) {
        log_msg("[magnet] no metadata from tracker round %d; announcing again\n", round);
        for (int i = 0; i < 6 && !cancelled; ++i)
            usleep(500000);
        if (progress)
            progress({MagnetProgress::Stage::FindingPeers});
    }
    size_t nextTrackerIdx = 0;
    size_t numTrackers = trackers.size();
    const size_t kMaxAnnounceWorkers = 3;
    size_t numAnnounceWorkers = std::min(numTrackers, kMaxAnnounceWorkers);
    std::vector<nx::thread> announceWorkers;
    for (size_t w = 0; w < numAnnounceWorkers; ++w) {
        announceWorkers.emplace_back([&] {
            while (!cancelled) {
                std::string tracker;
                {
                    std::lock_guard<std::mutex> lock(trackerMtx);
                    if (nextTrackerIdx >= numTrackers) break;
                    tracker = trackers[nextTrackerIdx++];
                }
                uint8_t batch[kMaxPeersPerTracker * 6];
                tracker_announce_result_t result;
                uint32_t count = tracker_announce_url_ex_cancel(
                    tracker.c_str(), spec.infoHash, peerId, 6881, 0, 0,
                    batch, kMaxPeersPerTracker, &result,
                    trackerCancelled, &cancelled);
                if (cancelled) break;
                if (count > 0) {
                    std::lock_guard<std::mutex> lock(trackerMtx);
                    appendUniquePeers(trackerPeers, batch, count);
                    log_msg("[magnet] tracker %s added %u peers, total=%u\n",
                            tracker.c_str(), count,
                            static_cast<unsigned>(trackerPeers.size() / 6));
                }
            }
        });
    }
    for (auto& t : announceWorkers) {
        t.join();
    }

    if (cancelled) {
        error = "Metadata resolution was cancelled.";
        return false;
    }

    /* PEX amplification if tracker peers are thin (1-3) */
    uint32_t trackerPeerCount = static_cast<uint32_t>(trackerPeers.size() / 6);
    if (trackerPeerCount > 0 && trackerPeerCount <= kPexThinThreshold && !cancelled) {
        std::vector<uint8_t> pexPeers;
        for (uint32_t i = 0; i < trackerPeerCount && !cancelled; ++i)
            harvestPexFromPeer(trackerPeers.data() + i * 6, spec, peerId, cancelled, pexPeers);
        if (!pexPeers.empty())
            appendUniquePeers(trackerPeers, pexPeers.data(), static_cast<uint32_t>(pexPeers.size() / 6));
        trackerPeerCount = static_cast<uint32_t>(trackerPeers.size() / 6);
    }

    /* Step 4: PASS 1 - Fetch metadata from the NEW tracker peers of this round. */
    if (trackerPeers.size() > triedUpTo) {
        std::vector<uint8_t> fresh(trackerPeers.begin() + triedUpTo, trackerPeers.end());
        triedUpTo = trackerPeers.size();
        log_msg("[magnet] PASS 1 round %d: metadata fetch from %u tracker peers\n",
                round, static_cast<unsigned>(fresh.size() / 6));
        if (runMetadataPass(spec, peerId, fresh, deadline, cancelled, progress,
                            metadata, verifiedEndpoints, stats)) {
            log_msg("[magnet] PASS 1 succeeded! Metadata fetched from tracker swarm.\n");
        }
    }
    }  // announce rounds
    uint32_t dhtFound = 0;

    /* Step 5: PASS 2 - If no metadata yet, query DHT fallback */
    if (metadata.empty() && !cancelled) {
        deadline = now_ms() + kOverallTimeoutMs;
        log_msg("[magnet] PASS 2: tracker peers gave no metadata, querying DHT fallback...\n");
        if (progress)
            progress({MagnetProgress::Stage::FindingPeers});
        DhtSearch dhtSearch;
        std::atomic<bool> dhtStop{false};
        nx::thread dhtThread([&spec, &cancelled, &dhtStop, &dhtSearch] {
            runDhtSearch(spec.infoHash, cancelled, dhtStop, dhtSearch);
        });

        // TorrentShopNX META_DHT_BUDGET_MS = 45 s; runDhtSearch itself stops
        // once it has kDhtTargetPeers.
        uint64_t dhtDeadline = now_ms() + 45000;
        while (!cancelled && now_ms() < dhtDeadline) {
            if (dhtPeerCount(dhtSearch) >= kDhtTargetPeers) break;
            usleep(200000);
        }
        dhtStop.store(true);
        dhtThread.join();

        std::vector<uint8_t> dhtPeers;
        {
            std::lock_guard<std::mutex> lock(dhtSearch.mutex);
            dhtPeers = dhtSearch.peers;
        }

        uint32_t dhtCount = static_cast<uint32_t>(dhtPeers.size() / 6);
        dhtFound = dhtCount;
        log_msg("[magnet] PASS 2: DHT found %u peers\n", dhtCount);
        if (dhtCount > 0) {
            // Only peers not tried yet.
            std::vector<uint8_t> unionPeers;
            for (uint32_t i = 0; i < dhtCount; ++i)
                if (!containsPeer(trackerPeers, dhtPeers.data() + i * 6))
                    appendUniquePeers(unionPeers, dhtPeers.data() + i * 6, 1);
            runMetadataPass(spec, peerId, unionPeers, deadline, cancelled, progress,
                            metadata, verifiedEndpoints, stats);
        }
    }

    if (cancelled) {
        error = "Metadata resolution was cancelled.";
        return false;
    }

    if (metadata.empty()) {
        if (stats.tried.load() == 0) {
            error = "Trackers and DHT returned no peers for this magnet.";
        } else {
            char buf[384];
            std::string last;
            {
                std::lock_guard<std::mutex> lk(stats.mutex);
                last = stats.lastReject;
            }
            std::snprintf(buf, sizeof(buf),
                "No metadata from %u peers (trk %u, dht %u): sock %u/e%d, "
                "conn %u, hs %u, no-meta %u, incompl %u, sha1 %u, parser %u%s%s",
                stats.tried.load(),
                static_cast<unsigned>(trackerPeers.size() / 6), dhtFound,
                stats.socketFail.load(), stats.lastSocketErrno.load(),
                stats.connectFail.load(),
                stats.handshakeFail.load(), stats.noMetadata.load(),
                stats.incomplete.load(), stats.hashMismatch.load(),
                stats.rejected.load(), last.empty() ? "" : " - ",
                last.c_str());
            error = buf;
        }
        log_msg("[magnet] %s\n", error.c_str());
        return false;
    }

    /* Persist metadata to cache on SD card */
    metaCacheSave(spec.infoHash, spec.infoHashHex, metadata);

    if (progress)
        progress({MagnetProgress::Stage::Validating, 1, 1});
    std::vector<uint8_t> torrent;
    if (!buildTorrent(spec, metadata, torrent, error))
        return false;
    if (!writeTorrentAtomic(path, torrent, error))
        return false;
    if (verifiedPeers)
        *verifiedPeers = std::move(verifiedEndpoints);
    return true;
}

} // namespace goonies
