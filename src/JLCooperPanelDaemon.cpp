// JLCooperPanelDaemon - unofficial native (arm64 / x86_64) reimplementation
// ---------------------------------------------------------------------------
// Copyright (c) 2026 Sebastiano Greco. MIT License (see LICENSE).
//
// Unofficial and not affiliated with, endorsed by or supported by Blackmagic
// Design or JLCooper. "DaVinci Resolve", "Blackmagic Design" and "JLCooper"
// are trademarks of their respective owners.
//
// This program is an independent reimplementation of the behaviour of the
// "JLCooperPanelDaemon" 7.1 helper (x86_64 only) that ships inside DaVinci
// Resolve, written so that a JLCooper Eclipse panel keeps working on systems
// without Rosetta 2. It was written from an analysis of the original's
// observable behaviour and wire protocols; it contains no code from the
// original. The lookup tables below (LED ids, button -> key and knob -> pot
// maps, LCD labels) describe the behaviour of the original and were taken
// from it.
//
// It translates between a JLCooper Eclipse panel (TCP, SysEx-like frames
// F0 15 25 ... F7) and DaVinci Resolve (unix socket, 12-byte-header packets).
// See docs/PROTOCOL.md.
//
// Beyond the original: an optional mapping file (docs/MAPPING.md) adds a SHIFT layer to the knobs and a
// probe mode for finding Resolve control ids. Without that file the behaviour is that of the original.
//
// Build (macOS, Apple Silicon or Intel; or `make`):
//     clang++ -std=c++17 -O2 -Wall -o JLCooperPanelDaemon JLCooperPanelDaemon.cpp
//
// Usage is compatible with the original:
//     JLCooperPanelDaemon [-hvVdr] [-s fd] [-l logfile] [hostname[:port]]
//
// THERE IS NO WARRANTY OF ANY KIND; see LICENSE.
// ---------------------------------------------------------------------------
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define VERSION_STRING "7.1"
#define SOCKET_PATH "/var/tmp/davinci_socket"
#define DEFAULT_PANEL "192.168.254.101:23"

struct Pair { unsigned a, b; };
/* Behaviour tables: they mirror what the original daemon does (see the note in the file header). */
static const unsigned char kLedIds[46] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x30, 0x31, 0x32, 0x33, 0x34, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x4f, 0x50, 0x51, 0x52, 0x53};

static const struct Pair kPlainKeys[59] = {
    {0x37, 0x140},
    {0x38, 0x141},
    {0x39, 0x304},
    {0x3a, 0x305},
    {0x3b, 0x133},
    {0x3c, 0x132},
    {0x3d, 0x160},
    {0x3e, 0x161},
    {0x1f, 0x31b},
    {0x20, 0x31c},
    {0x21, 0x31d},
    {0x22, 0x31e},
    {0x23, 0x31f},
    {0x24, 0x32a},
    {0x25, 0x32b},
    {0x17, 0x346},
    {0x18, 0x1a8},
    {0x19, 0x1b2},
    {0x1a, 0x1b3},
    {0x1b, 0x192},
    {0x1c, 0x194},
    {0x1d, 0x195},
    {0x1e, 0x350},
    {0x9, 0x211},
    {0x8, 0x210},
    {0xb, 0x214},
    {0xa, 0x213},
    {0xd, 0x217},
    {0xc, 0x216},
    {0x3f, 0x3a7},
    {0x40, 0x396},
    {0x41, 0x397},
    {0x42, 0x398},
    {0x43, 0x386},
    {0x44, 0x387},
    {0x45, 0x388},
    {0x46, 0x376},
    {0x47, 0x377},
    {0x48, 0x378},
    {0x4a, 0x3a9},
    {0x26, 0x355},
    {0x27, 0x345},
    {0x28, 0x1b0},
    {0x29, 0x3b7},
    {0x2a, 0x191},
    {0x50, 0x3c5},
    {0x4f, 0x3c0},
    {0x51, 0x3c2},
    {0x52, 0x3c4},
    {0x53, 0x395},
    {0x30, 0x326},
    {0x31, 0x327},
    {0x32, 0x328},
    {0x33, 0x329},
    {0x34, 0x366},
    {0x2c, 0x182},
    {0x2e, 0x183},
    {0x2d, 0x184},
    {0x2f, 0x185},
};

static const struct Pair kShiftedKeys[17] = {
    {0x17, 0x2346},
    {0x18, 0x21a8},
    {0x19, 0x143},
    {0x1a, 0x144},
    {0x1b, 0x121b},
    {0x1c, 0x2194},
    {0x1d, 0x390},
    {0x1f, 0x113},
    {0x20, 0x112},
    {0x23, 0x30b},
    {0x24, 0x32d},
    {0x25, 0x32c},
    {0x30, 0x336},
    {0x31, 0x337},
    {0x32, 0x338},
    {0x33, 0x339},
    {0x34, 0x366},
};

static const struct Pair kModeMap[8] = {
    {0x0, 0x0},
    {0x1, 0x1},
    {0x2, 0x2},
    {0x3, 0x3},
    {0x4, 0x4},
    {0x5, 0x5},
    {0x6, 0x6},
    {0x7, 0x7},
};

static const struct Pair kResetPlain[6] = {
    {0xf, 0x1},
    {0x10, 0x2},
    {0x11, 0x3},
    {0x12, 0x4},
    {0x13, 0x5},
    {0x14, 0x6},
};

static const struct Pair kResetShift[6] = {
    {0x9, 0x1},
    {0x8, 0x2},
    {0xb, 0x3},
    {0xa, 0x4},
    {0xd, 0x5},
    {0xc, 0x6},
};

static const struct Pair kKnobPotsA[24] = {
    {0x1, 0x300},
    {0x2, 0x301},
    {0x3, 0x302},
    {0x1, 0x240},
    {0x2, 0x230},
    {0x3, 0x220},
    {0x1, 0x25b},
    {0x2, 0x25c},
    {0x3, 0x25d},
    {0x1, 0x100},
    {0x2, 0x101},
    {0x3, 0x103},
    {0x1, 0x100},
    {0x2, 0x101},
    {0x3, 0x106},
    {0x1, 0x308},
    {0x2, 0x309},
    {0x3, 0x30a},
    {0x1, 0x310},
    {0x2, 0x311},
    {0x3, 0x312},
    {0x1, 0x30c},
    {0x2, 0x30d},
    {0x3, 0x30e},
};

static const struct Pair kKnobPotsB[24] = {
    {0x4, 0x303},
    {0x5, 0x304},
    {0x6, 0x305},
    {0x4, 0x221},
    {0x5, 0x231},
    {0x6, 0x241},
    {0x4, 0x25a},
    {0x5, 0x251},
    {0x6, 0x261},
    {0x4, 0x102},
    {0x5, 0x104},
    {0x6, 0x105},
    {0x4, 0x107},
    {0x5, 0x306},
    {0x6, 0x307},
    {0x4, 0x30b},
    {0x5, 0x3d3},
    {0x6, 0x3d1},
    {0x4, 0x313},
    {0x5, 0x3d2},
    {0x6, 0x3d0},
    {0x4, 0x30f},
    {0x5, 0x3d5},
    {0x6, 0x3d4},
};

static const char* kLcdTop[16] = {
    "  Pan    Tilt     Zoom  ",
    " Image   Image    Image ",
    "                        ",
    "  Lift   Gamma     Gain ",
    "  Red    Green     Blue ",
    " Offset  Offset   Offset",
    "  Zoom            Rotate",
    " Window  Aspect   Window",
    "  Zoom           Inside ",
    " Window  Aspect Softness",
    " Hue      Hue      Hue  ",
    " Center   Width    Soft ",
    " Lum      Lum      Lum  ",
    "LowClip  LowSoft  HiSoft",
    " Sat      Sat      Sat  ",
    "LowClip  LowSoft  HiSoft",
};

static const char* kLcdBottom[16] = {
    " Rotate                 ",
    " Image    HSize   VSize ",
    "                   Lum  ",
    "  Sat      Hue     Mix  ",
    "Master  Contrast  Pivot ",
    "Offset                  ",
    "          Pan     Tilt  ",
    "Softness Window  Window ",
    " Outside  Blur     H/V  ",
    "Softness Radius   Ratio ",
    "  Hue    White    Clean ",
    "  Sym    Clip     White ",
    " Lum     Black    Clean ",
    "HiClip   Clip     Black ",
    " Sat      Blur    In/Out",
    "HiClip   Radius   Ratio ",
};

static const unsigned char kSegTable[19][2] = {{0x20,0x00}, {0x30,0x7d}, {0x31,0x28}, {0x32,0x37}, {0x33,0x2f}, {0x34,0x6a}, {0x35,0x4f}, {0x36,0x5f}, {0x37,0x29}, {0x38,0x7f}, {0x39,0x6f}, {0x61,0x7a}, {0x62,0x02}, {0x63,0x02}, {0x64,0x02}, {0x65,0x02}, {0x68,0x02}, {0x2d,0x02}, {0x5f,0x02}};

static const char* kInfoLine1 = " ---------- <- Prev/Next -> ----------- ";
static const char* kInfoLine2 = "  Node   Still   Scene  KeyFrame Version";


// ------------------------------------------------------------------ logging
static bool g_verbose = false;
static bool g_debugDisplay = false;
static volatile sig_atomic_t g_quit = 0;

static void vlog(const char* fmt, ...) {
    if (!g_verbose) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fflush(stderr);
}
static void elog(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); fputs("ERROR: ", stderr); vfprintf(stderr, fmt, ap); va_end(ap); fflush(stderr);
}

// Always printed (not only with -V): configuration events are rare and worth seeing in panel.log.
static void ilog(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fflush(stderr);
}

static int64_t nowMs() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool sendAll(int fd, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    while (len) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pf = {fd, POLLOUT, 0};
                if (poll(&pf, 1, 2000) <= 0) return false;
                continue;
            }
            return false;
        }
        p += n; len -= (size_t)n;
    }
    return true;
}

// ------------------------------------------------------------------ Eclipse
// DeviceIdForPanelType(): panel 1 -> 0x2000, 2 -> 0x1000, 0 -> 0, anything else -> 0x4000
static uint16_t panelBits(int panel) {
    switch (panel) { case 1: return 0x2000; case 2: return 0x1000; case 0: return 0; default: return 0x4000; }
}

class Controller;

class Eclipse {
public:
    int fd = -1;
    uint8_t b31 = 0;          // LED 21/22 shadow byte (sticky in the original)
    bool initDone = false;    // handshake (mode + ID request) and controller init finished
    int64_t initAt = 0;       // when the handshake is due (1 s after the TCP connect, like the original)
    Controller* ctl = nullptr;
    std::vector<uint8_t> rx;

    bool connected() const { return fd != -1; }

    bool sendFrame(const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> f; f.reserve(payload.size() + 4);
        f.push_back(0xF0); f.push_back(0x15); f.push_back(0x25);
        f.insert(f.end(), payload.begin(), payload.end());
        f.push_back(0xF7);
        if (fd < 0) return false;
        if (!sendAll(fd, f.data(), f.size())) { elog("send to panel failed: %s\n", strerror(errno)); return false; }
        return true;
    }
    bool requestId() { return sendFrame({0x03}); }
    bool setMode2() { return sendFrame({0x02, 0x02, 0x02, 0x00, 0x00}); }

    bool ledSet(int led, int on) {
        std::vector<uint8_t> p;
        if (led >= 0 && led <= 0x0d) {
            static const uint8_t m8[6] = {0x09, 0x08, 0x0d, 0x0c, 0x11, 0x10};
            uint8_t id = (led < 8) ? (uint8_t)led : m8[led - 8];
            p = {0x55, 0x03, id, (uint8_t)(on ^ 1)};
        } else if (led == 0x15 || led == 0x16) {
            if (led == 0x16) b31 |= (uint8_t)(on * 2);
            b31 |= (uint8_t)on;
            p = {0x4B, b31};
        } else if (led >= 0x17 && led <= 0x53) {
            p = {0x42, (uint8_t)(led - 0x17), (uint8_t)on};
        } else {
            elog("ledSet: invalid LED specifier %i\n", led);
            return false;
        }
        return sendFrame(p);
    }
    bool shuttleLeds(int a, int b) { return sendFrame({0xBF, 0x6B, (uint8_t)(a | (b * 2))}); }
    bool allLeds(int on) {
        bool ok = true;
        for (unsigned char id : kLedIds) ok &= ledSet(id, on);
        ok &= shuttleLeds(on, on);
        return ok;
    }
    bool cmd4A(unsigned v) { return sendFrame({0x4A, (uint8_t)(v >> 8), (uint8_t)v}); }

    static uint8_t seg(char c) {
        for (auto& e : kSegTable) if (e[0] == (unsigned char)c) return e[1];
        return 0;
    }
    bool segText(const std::string& s0) {            // 8 characters, last character first
        std::string s = (s0 + "        ").substr(0, 8);
        std::vector<uint8_t> p = {0x49};
        for (int i = 7; i >= 0; --i) p.push_back(seg(s[i]));
        return sendFrame(p);
    }
    bool segNumbers(unsigned A, unsigned B, unsigned C, unsigned D) {   // HH MM SS FF
        static const uint8_t digit[10] = {0x7d, 0x28, 0x37, 0x2f, 0x6a, 0x4f, 0x5f, 0x29, 0x7f, 0x6f};
        std::vector<uint8_t> p = {0x49};
        unsigned v[4] = {D, C, B, A};
        for (unsigned x : v) { x %= 100; p.push_back(digit[x % 10]); p.push_back(digit[x / 10]); }
        return sendFrame(p);
    }
    bool lcdWrite(int disp, int col, int line, const std::string& s) {
        if (line > 1 || line < 0) { elog("lcdWrite: bad line %d\n", line); return false; }
        std::vector<uint8_t> p;
        if (disp == 2) {
            p = {0x43, (uint8_t)((line << 6) + col), (uint8_t)s.size()};
            p.insert(p.end(), s.begin(), s.end());
        } else {
            size_t n = std::min<size_t>(s.size(), 0x18);
            p = {0x55, 0x01, (uint8_t)((disp << 6) | std::min(col, 0x17) | (line << 5)), (uint8_t)n};
            p.insert(p.end(), s.begin(), s.begin() + n);
        }
        return sendFrame(p);
    }

    void disconnect() {
        if (fd != -1) { close(fd); fd = -1; }
        rx.clear(); initDone = false;
    }

    bool connectTo(const std::string& hostport);
    void readReady();                      // fd readable
    void processFrame(const std::vector<uint8_t>& f);
};

// ------------------------------------------------------------------ Resolve
class Controller;

class Resolve {
public:
    int listenFd = -1;     // unix listening socket (when not given -s)
    int fd = -1;           // connection to Resolve
    int providedFd = -1;   // -s fd (never closed by us)
    std::vector<uint8_t> buf;
    Controller* ctl = nullptr;

    bool open(int sfd);
    void closeConn() {
        if (fd != -1 && fd != providedFd) close(fd);
        if (fd != -1) vlog("Resolve connection closed\n");
        fd = -1; buf.clear();
    }
    void acceptReady() {
        int nfd = accept(listenFd, nullptr, nullptr);
        if (nfd < 0) return;
#ifdef SO_NOSIGPIPE
        int one = 1; setsockopt(nfd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        if (fd != -1) closeConn();
        fd = nfd; buf.clear();
        vlog("Resolve connected\n");
    }
    bool write(const void* data, size_t len) {
        if (fd == -1) return true;                  // nobody to talk to: silently dropped
        if (!sendAll(fd, data, len)) {
            if (errno == EPIPE || errno == ECONNRESET) closeConn(); else elog("write to Resolve failed: %s\n", strerror(errno));
            return false;
        }
        return true;
    }
    // Steinway message: u16 dest=0, u16 cmd, u16 target, u16 0, u32 len, payload   (little endian)
    bool message(uint16_t cmd, uint16_t target, const void* payload, uint32_t len) {
        std::vector<uint8_t> m(12 + len, 0);
        m[2] = cmd & 0xff; m[3] = cmd >> 8; m[4] = target & 0xff; m[5] = target >> 8;
        m[8] = len & 0xff; m[9] = (len >> 8) & 0xff; m[10] = (len >> 16) & 0xff; m[11] = len >> 24;
        if (len) memcpy(&m[12], payload, len);
        return write(m.data(), m.size());
    }
    bool message16(uint16_t cmd, uint16_t target, int16_t v) {
        uint8_t p[2] = {(uint8_t)(v & 0xff), (uint8_t)((uint16_t)v >> 8)};
        return message(cmd, target, p, 2);
    }
    void readReady();                       // fd readable
    void handlePacket(const uint8_t* pkt, size_t total);
};

// ------------------------------------------------------------ mapping file
// Optional text file (see docs/MAPPING.md) that adds a SHIFT layer to the knobs and a probe mode for
// finding the id of a Resolve control. It is re-read whenever it changes, so no restart is needed.
//
//     shiftpot   <mode 0-7> <knob 1-6> <pot>        pot: 0x25b or <panel>/<control>, e.g. 2/91
//     shiftlabel <mode 0-7> <knob 1-6> <upper>|<lower>   (up to 8 characters each, shown while SHIFT is held)
//     probe      on|off
//     probestart <pot>
//     send       <pot> <delta>                      \ one-shot commands, executed each time the file is re-read
//     reset      <pot>                              / (not at start-up, and only while 'probe on'); for scripts
//     ballcolors <left> <centre> <right>             off|green|blue|red for each trackball light
//     keylights  off|on|all                          backlight the keys: 'on' = every key LED except the 8 mode keys
//                                                    (those keep showing the active mode), 'all' = mode keys too
// Timecode display: if a file 'timecode.txt' (one line 'HH:MM:SS:FF' or 'HH:MM:SS;FF') exists next to the mapping
// file, its content is shown on the panel's 7-segment display instead of the dashes (see docs/TIMECODE.md).
struct Action { bool reset; unsigned pot; int delta; };

struct Mapping {
    std::map<int, unsigned> shiftPot;                                  // key = mode * 8 + knob
    std::map<int, std::pair<std::string, std::string>> shiftLabel;     // key = mode * 8 + knob
    bool probe = false;
    unsigned probeStart = 0x250;
    std::vector<Action> actions;
    bool balls = false;                    // ballcolors: 0 off, 1 green, 2 blue, 3 red (left, centre, right ball)
    int ball[3] = {0, 0, 0};
    int keylights = 0;                     // 0 off, 1 on (mode keys keep showing the active mode), 2 all
};

struct FileStamp {
    bool exists = false; int64_t size = 0; int64_t sec = 0; int64_t nsec = 0;
    bool operator==(const FileStamp& o) const { return exists == o.exists && size == o.size && sec == o.sec && nsec == o.nsec; }
};

static FileStamp stampOf(const std::string& path) {
    FileStamp s; struct stat st;
    if (path.empty() || stat(path.c_str(), &st) != 0) return s;
    s.exists = true; s.size = (int64_t)st.st_size; s.sec = (int64_t)st.st_mtime;
#ifdef __APPLE__
    s.nsec = (int64_t)st.st_mtimespec.tv_nsec;
#else
    s.nsec = (int64_t)st.st_mtim.tv_nsec;
#endif
    return s;
}

// "0x25b" (raw id: panel in bits 8..11, control in the low byte) or "2/91" (panel/control, panel 0..2)
static bool parsePot(const char* s, unsigned& out) {
    char* end = nullptr;
    const char* slash = strchr(s, '/');
    if (slash) {
        long p = strtol(s, &end, 0);
        if (end != slash || p < 0 || p > 2) return false;
        long k = strtol(slash + 1, &end, 0);
        if (end == slash + 1 || *end || k < 0 || k > 255) return false;
        out = (unsigned)(p * 0x100 + k); return true;
    }
    long v = strtol(s, &end, 0);
    if (end == s || *end || v < 0 || v > 0x3ff) return false;
    out = (unsigned)v; return true;
}

static std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// Returns false only when the file could not be opened. Bad lines are reported and skipped.
static bool loadMapping(const std::string& path, Mapping& m) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[512]; int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        if (!strchr(line, '\n') && !feof(f)) {            // over-long line: drop the rest of it
            int ch; while ((ch = fgetc(f)) != EOF && ch != '\n') {}
            elog("%s:%d: line too long, ignored\n", path.c_str(), ln); continue;
        }
        std::string text = line;
        size_t hash = text.find('#'); if (hash != std::string::npos) text.erase(hash);
        text = trimStr(text);
        if (text.empty()) continue;
        char cmd[32] = {0}; int a = 0, b = 0, n = 0; char arg[64] = {0};
        if (sscanf(text.c_str(), "%31s", cmd) != 1) continue;
        for (char* c = cmd; *c; ++c) *c = (char)tolower((unsigned char)*c);
        if (!strcmp(cmd, "shiftpot")) {
            unsigned pot;
            if (sscanf(text.c_str(), "%*s %d %d %63s", &a, &b, arg) != 3 || a < 0 || a > 7 || b < 1 || b > 6 || !parsePot(arg, pot)) {
                elog("%s:%d: expected 'shiftpot <mode 0-7> <knob 1-6> <pot>'\n", path.c_str(), ln); continue;
            }
            m.shiftPot[a * 8 + b] = pot;
        } else if (!strcmp(cmd, "shiftlabel")) {
            if (sscanf(text.c_str(), "%*s %d %d %n", &a, &b, &n) < 2 || n <= 0 || a < 0 || a > 7 || b < 1 || b > 6) {
                elog("%s:%d: expected 'shiftlabel <mode 0-7> <knob 1-6> <upper>|<lower>'\n", path.c_str(), ln); continue;
            }
            std::string rest = text.substr((size_t)n), up = rest, lo;
            size_t bar = rest.find('|');
            if (bar != std::string::npos) { up = rest.substr(0, bar); lo = rest.substr(bar + 1); }
            up = trimStr(up).substr(0, 8); lo = trimStr(lo).substr(0, 8);
            m.shiftLabel[a * 8 + b] = std::make_pair(up, lo);
        } else if (!strcmp(cmd, "probe")) {
            if (sscanf(text.c_str(), "%*s %63s", arg) != 1) { elog("%s:%d: expected 'probe on|off'\n", path.c_str(), ln); continue; }
            std::string v = arg; for (auto& c : v) c = (char)tolower((unsigned char)c);
            if (v == "on" || v == "1" || v == "yes") m.probe = true;
            else if (v == "off" || v == "0" || v == "no") m.probe = false;
            else elog("%s:%d: expected 'probe on|off'\n", path.c_str(), ln);
        } else if (!strcmp(cmd, "probestart")) {
            unsigned pot;
            if (sscanf(text.c_str(), "%*s %63s", arg) != 1 || !parsePot(arg, pot)) { elog("%s:%d: expected 'probestart <pot>'\n", path.c_str(), ln); continue; }
            m.probeStart = pot;
        } else if (!strcmp(cmd, "send")) {
            unsigned pot;
            if (sscanf(text.c_str(), "%*s %63s %d", arg, &a) != 2 || !parsePot(arg, pot) || a < -2000 || a > 2000) {
                elog("%s:%d: expected 'send <pot> <delta -2000..2000>'\n", path.c_str(), ln); continue;
            }
            if (m.actions.size() < 64) m.actions.push_back(Action{false, pot, a});
        } else if (!strcmp(cmd, "reset")) {
            unsigned pot;
            if (sscanf(text.c_str(), "%*s %63s", arg) != 1 || !parsePot(arg, pot)) { elog("%s:%d: expected 'reset <pot>'\n", path.c_str(), ln); continue; }
            if (m.actions.size() < 64) m.actions.push_back(Action{true, pot, 0});
        } else if (!strcmp(cmd, "ballcolors")) {
            char c1[16] = {0}, c2[16] = {0}, c3[16] = {0};
            if (sscanf(text.c_str(), "%*s %15s %15s %15s", c1, c2, c3) != 3) {
                elog("%s:%d: expected 'ballcolors <left> <centre> <right>' (off, green, blue or red)\n", path.c_str(), ln); continue;
            }
            const char* names[3] = {c1, c2, c3}; int col[3]; bool ok = true;
            for (int i = 0; i < 3; i++) {
                std::string v = names[i]; for (auto& ch : v) ch = (char)tolower((unsigned char)ch);
                if (v == "off" || v == "none") col[i] = 0; else if (v == "green") col[i] = 1; else if (v == "blue") col[i] = 2; else if (v == "red") col[i] = 3; else ok = false;
            }
            if (!ok) { elog("%s:%d: expected 'ballcolors <left> <centre> <right>' (off, green, blue or red)\n", path.c_str(), ln); continue; }
            m.balls = true; for (int i = 0; i < 3; i++) m.ball[i] = col[i];
        } else if (!strcmp(cmd, "keylights")) {
            char v[16] = {0};
            if (sscanf(text.c_str(), "%*s %15s", v) != 1) { elog("%s:%d: expected 'keylights off|on|all'\n", path.c_str(), ln); continue; }
            std::string w = v; for (auto& ch : w) ch = (char)tolower((unsigned char)ch);
            if (w == "off") m.keylights = 0; else if (w == "on") m.keylights = 1; else if (w == "all") m.keylights = 2;
            else { elog("%s:%d: expected 'keylights off|on|all'\n", path.c_str(), ln); continue; }
        } else {
            elog("%s:%d: unknown directive '%s'\n", path.c_str(), ln, cmd);
        }
    }
    fclose(f);
    return true;
}

// -------------------------------------------------------------- Controller
class Controller {
public:
    Eclipse E;
    Resolve R;
    int mode = -1;
    int shuttle = 0;
    int shuttleCnt = 0;
    bool plusminus = true;
    bool shift = false;
    std::set<uint16_t> images[3];
    Mapping map;                 // user mapping (SHIFT layer, probe settings)
    bool probeActive = false;
    unsigned probeId = 0x250;    // id under test in probe mode
    bool ballsApplied = false;   // trackball lights were set by the mapping file
    int keylightsApplied = 0;    // key backlight state last sent to the panel (0 off, 1 on, 2 all)
    // timecode display, fed through a small text file (see pollTimecode)
    bool tcShown = false; unsigned tcV[4] = {0, 0, 0, 0};
    FileStamp tcStamp;

    Controller() { E.ctl = this; R.ctl = this; }

    // ---- Steinway senders ----
    void swKey(int keyPanel, int ctrl) { R.message16(3, (uint16_t)(ctrl | 0x200 | panelBits(keyPanel)), 1); }
    static int panelOfId(unsigned id) { unsigned c = id & 0xf00; return c == 0x100 ? 1 : c == 0x200 ? 2 : 0; }
    void sendKeyRaw(unsigned id) { swKey(panelOfId(id), id & 0xff); }
    bool sendKey(unsigned key) {
        if ((key & 0xf000) == 0x1000) sendKeyRaw(0x340);
        else if ((key & 0xf000) == 0x2000) sendKeyRaw(0x3a5);
        sendKeyRaw(key);
        return true;
    }
    void swPot(unsigned pot, int v) { R.message16(4, (uint16_t)((pot & 0xff) | 0x100 | panelBits(panelOfId(pot))), (int16_t)v); }
    void swPotReset(unsigned pot) { R.message16(6, (uint16_t)((pot & 0xff) | 0x100 | panelBits(panelOfId(pot))), 1); }
    void swBallZ(int n, int v) { R.message16(4, (uint16_t)(n | 0x310 | panelBits(2)), (int16_t)v); }
    void swBallXY(int n, int a, int b) {
        int16_t p[2] = {(int16_t)(-(int16_t)a), (int16_t)b};
        uint8_t raw[4] = {(uint8_t)(p[0] & 0xff), (uint8_t)((uint16_t)p[0] >> 8), (uint8_t)(p[1] & 0xff), (uint8_t)((uint16_t)p[1] >> 8)};
        R.message(5, (uint16_t)(n | 0x300 | panelBits(2)), raw, 4);
    }
    void swJog(int v) { R.message16(4, 0x400 | panelBits(0), (int16_t)v); }

    // ---- tables ----
    static unsigned knobEntry(int rotor, int mode) {
        if (mode < 0 || mode > 7) return 0xffff;
        if (rotor >= 1 && rotor <= 3) return kKnobPotsA[mode * 3 + rotor - 1].b;
        if (rotor >= 4 && rotor <= 6) return kKnobPotsB[mode * 3 + rotor - 4].b;
        return 0xffff;
    }
    static const Pair* modeTranslate(int mode) {
        for (auto& e : kModeMap) if ((int)e.b == mode) return &e;
        return nullptr;
    }

    // ---- user mapping: SHIFT layer and probe mode ----
    unsigned shiftPotFor(int knob) const {
        auto it = map.shiftPot.find(mode * 8 + knob);
        return it == map.shiftPot.end() ? 0xffff : it->second;
    }
    bool hasShiftLabels() const {
        for (auto& kv : map.shiftLabel) if (kv.first / 8 == mode) return true;
        return false;
    }
    static std::string center8(const std::string& t) {
        std::string s = t.substr(0, 8); size_t pad = (8 - s.size()) / 2;
        return std::string(pad, ' ') + s + std::string(8 - s.size() - pad, ' ');
    }
    // Draws the labels of the current mode; while SHIFT is held the labels of knobs that have a SHIFT
    // function replace the normal ones. Display 0 carries knobs 1-3 and display 1 knobs 4-6; each knob owns
    // an 8-character column with a name on the upper line and a qualifier on the lower line.
    void drawLabels() {
        if (mode < 0 || mode > 7) return;
        E.lcdWrite(0, 0, 0, kLcdTop[mode * 2]);
        E.lcdWrite(0, 0, 1, kLcdTop[mode * 2 + 1]);
        E.lcdWrite(1, 0, 0, kLcdBottom[mode * 2]);
        E.lcdWrite(1, 0, 1, kLcdBottom[mode * 2 + 1]);
        if (!shift) return;
        for (auto& kv : map.shiftLabel) {
            if (kv.first / 8 != mode) continue;
            int knob = kv.first % 8, disp = knob <= 3 ? 0 : 1, col = ((knob - 1) % 3) * 8;
            E.lcdWrite(disp, col, 0, center8(kv.second.first));
            E.lcdWrite(disp, col, 1, center8(kv.second.second));
        }
    }
    // trackball lights: two LEDs per ball (ids base and base+1): green = first, blue = second, red = both
    void setBall(int n, int colour) {
        static const uint8_t base[3] = {10, 14, 18};
        E.sendFrame({0x55, 0x03, base[n], (uint8_t)((colour == 1 || colour == 3) ? 0 : 1)});
        E.sendFrame({0x55, 0x03, (uint8_t)(base[n] + 1), (uint8_t)((colour == 2 || colour == 3) ? 0 : 1)});
    }
    void applyBalls() {
        if (map.balls) { for (int i = 0; i < 3; i++) setBall(i, map.ball[i]); ballsApplied = true; }
        else if (ballsApplied) { for (int i = 0; i < 3; i++) setBall(i, 0); ballsApplied = false; }
    }
    // key backlight: every key LED on (ids 0..7 are the mode keys, which keep showing the active mode unless 'all')
    void applyKeyLights() {
        int want = map.keylights;
        if (want == keylightsApplied) return;
        for (unsigned char id : kLedIds) if (id > 7) E.ledSet(id, want ? 1 : 0);
        for (auto& e : kModeMap) E.ledSet((int)e.a, (want == 2 || (int)e.b == mode) ? 1 : 0);
        keylightsApplied = want;
    }
    void shutdownLeds() {
        if (!E.connected()) return;
        if (ballsApplied) { for (int i = 0; i < 3; i++) setBall(i, 0); }
        if (keylightsApplied) { map.keylights = 0; applyKeyLights(); }
    }
    // the 7-segment display when probe mode is off: the timecode if one is being fed, else dashes
    void showIdle() {
        if (tcShown) E.segNumbers(tcV[0], tcV[1], tcV[2], tcV[3]);
        else E.segText("--------");
    }
    // timecode file: one line "HH:MM:SS:FF" (or "HH:MM:SS;FF" for drop frame), rewritten by a script.
    // Removing the file restores the dashes. Ignored while probe mode owns the display.
    void pollTimecode(const std::string& path) {
        FileStamp st = stampOf(path);
        if (st == tcStamp) return;
        tcStamp = st;
        if (!st.exists) { if (tcShown) { tcShown = false; if (!probeActive) showIdle(); } return; }
        char buf[64] = {0};
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return;
        size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
        unsigned v[4];
        int got = sscanf(buf, "%u%*[:;.]%u%*[:;.]%u%*[:;.]%u", &v[0], &v[1], &v[2], &v[3]);
        if (got != 4) return;                       // half-written or garbage: keep what is shown
        for (int i = 0; i < 4; i++) v[i] %= 100;
        bool changed = !tcShown || memcmp(v, tcV, sizeof v) != 0;
        memcpy(tcV, v, sizeof v); tcShown = true;
        if (changed && !probeActive && E.connected() && E.initDone) showIdle();
    }
    void showProbe() {
        char b[16]; snprintf(b, sizeof b, "%d   %03d ", (int)(probeId >> 8), (int)(probeId & 0xff));
        E.segText(b);
    }
    void probeStep(int v) {
        if (v == 0) return;
        int id = (int)probeId + (v > 0 ? 1 : -1) * (shift ? 16 : 1);
        id %= 0x300; if (id < 0) id += 0x300;
        probeId = (unsigned)id;
        showProbe();
        vlog("probe: panel %d control %d (0x%03x)\n", id >> 8, id & 0xff, (unsigned)id);
    }
    // runActions: this is a re-read of a changed file (not the start-up load), so its one-shot commands may run
    void applyMapping(const Mapping& m, bool runActions = false) {
        bool startChanged = m.probeStart != map.probeStart;
        map = m;
        if (m.probe && (!probeActive || startChanged)) probeId = m.probeStart;
        bool wasProbe = probeActive;
        probeActive = m.probe;
        if (probeActive) showProbe(); else if (wasProbe) showIdle();
        drawLabels();
        if (E.connected() && E.initDone) { applyBalls(); applyKeyLights(); }
        if (runActions && probeActive) for (const Action& a : m.actions) runAction(a);
    }
    void runAction(const Action& a) {
        if (a.pot <= 0x2ff) { probeId = a.pot; showProbe(); }
        if (a.reset) { vlog("action: reset pot 0x%03x\n", a.pot); swPotReset(a.pot); }
        else { vlog("action: pot 0x%03x delta %d\n", a.pot, a.delta); swPot(a.pot, a.delta); }
    }

    // ---- behaviours ----
    void setMode(int m) {
        mode = m;
        drawLabels();
    }

    // Called once the panel connection is up
    void panelInit() {
        shuttleCnt = 0; shuttle = 0; mode = -1; plusminus = true;
        E.allLeds(0);
        setMode(0);
        if (const Pair* p = modeTranslate(mode)) E.ledSet((int)p->a, 1);
        showIdle();
        if (probeActive) showProbe();
        E.cmd4A(0);
        applyBalls();
        keylightsApplied = 0;                            // allLeds(0) above switched everything off
        applyKeyLights();
        E.lcdWrite(2, 0, 0, kInfoLine1);
        E.lcdWrite(2, 0, 1, kInfoLine2);
    }

    bool sendPot(int rotor, int v) {
        if (probeActive && rotor == 5) {                 // probe mode: knob 5 drives the id under test
            vlog("probe: pot 0x%03x delta %i\n", probeId, v);
            swPot(probeId, v);
            return true;
        }
        if (probeActive && rotor == 6) { probeStep(v); return true; }    // knob 6 chooses the id
        unsigned pot = shift ? shiftPotFor(rotor) : 0xffff;
        if (pot == 0xffff) pot = knobEntry(rotor, mode);
        if (pot == 0xffff) return false;
        vlog("Equivalent SteinwayPot is 0x%x: %i\n", pot, v);
        swPot(pot, v);
        return true;
    }
    bool encoderKeys(int rotor, int v) {
        static const unsigned base[5] = {0x3a0, 0x343, 0x3b4, 0x23b2, 0x170};
        int i = rotor - 0x12;
        if (i < 0 || i > 4) return false;
        return sendKey(base[i] + (v > 0 ? 1 : 0));
    }

    void rotorTurned(int rotor, int v) {
        vlog("Rotor [%d] has been turned by [%i]\n", rotor, v);
        if (rotor >= 7 && rotor <= 15) {
            int n = (rotor - 7) / 3, kind = (rotor - 7) % 3;
            if (kind == 0) swBallZ(n, v);
            else if (kind == 1) swBallXY(n, 0, -v);       // vertical:   {0, -v}
            else swBallXY(n, v, 0);                      // horizontal: {-v, 0}
            return;
        }
        if (rotor == 16) { swJog(v); return; }
        if (rotor == 17) {
            shuttle = v;
            if (v == 0) E.shuttleLeds(0, 0); else if (v > 0) E.shuttleLeds(0, 1); else E.shuttleLeds(1, 0);
            // falls through to the generic path in the original (no mapping exists for rotor 17)
        }
        if (sendPot(rotor, v)) return;
        if (encoderKeys(rotor, v)) return;
        vlog("could not find corresponding action for rotor %i\n", rotor);
    }

    bool buttonToKey(int btn) {
        if (shift) {
            for (auto& e : kShiftedKeys) if ((int)e.a == btn) { if (e.b == 0xffff) return false; return sendKey(e.b); }
        } else {
            for (auto& e : kPlainKeys) if ((int)e.a == btn) { if (e.b == 0xffff) return false; return sendKey(e.b); }
        }
        return false;
    }
    bool knobReset(int btn) {
        const Pair* t = shift ? kResetShift : kResetPlain;
        unsigned rotor = 0;
        for (int i = 0; i < 6; i++) if ((int)t[i].a == btn) { rotor = t[i].b; break; }
        if (!rotor && shift) {            // SHIFT + a knob's push button resets its SHIFT function, if it has one
            for (int i = 0; i < 6; i++) if ((int)kResetPlain[i].a == btn && shiftPotFor((int)kResetPlain[i].b) != 0xffff) { rotor = kResetPlain[i].b; break; }
        }
        if (!rotor) return false;
        if (probeActive && rotor == 5) {  // probe mode: reset the id under test
            vlog("probe: resetting pot 0x%03x\n", probeId);
            swPotReset(probeId);
            return true;
        }
        unsigned pot = shift ? shiftPotFor((int)rotor) : 0xffff;
        if (pot == 0xffff) pot = knobEntry((int)rotor, mode);
        if (pot == 0xffff) return false;
        vlog("Resetting pot 0x%x\n", pot);
        if (pot >= 0x3d0 && pot <= 0x3d5) sendKeyRaw(pot);
        swPotReset(pot);
        return true;
    }
    bool modeSwitch(int btn) {
        for (auto& e : kModeMap) {
            if ((int)e.a != btn) continue;
            const Pair* cur = modeTranslate(mode);
            if (!cur) return false;
            if (map.keylights != 2) E.ledSet((int)cur->a, 0);
            setMode((int)e.b);
            E.ledSet((int)e.a, 1);
            return true;
        }
        return false;
    }
    bool plusMinus(int btn) {
        if (btn != 0x49) return false;
        bool old = plusminus; plusminus = !plusminus;
        sendKeyRaw(old ? 0x3a8 : 0x3a6);        // old==0 -> 0x3a6 ; old==1 -> 0x3a8
        return true;
    }
    void buttonPressed(int btn, bool pressed) {
        vlog("Received button %i %s\n", btn, pressed ? "press" : "release");
        bool ok = true;
        if (btn == 0x2b) { shift = pressed; if (hasShiftLabels()) drawLabels(); }
        else if (pressed) {
            if (!(buttonToKey(btn) || knobReset(btn) || modeSwitch(btn) || plusMinus(btn))) {
                vlog("could not find corresponding action for button %i\n", btn);
                ok = false;
            }
        }
        if (g_debugDisplay) E.segNumbers(ok ? 1 : 0, 0, pressed ? 1 : 0, (unsigned)btn);
    }

    // ---- Eclipse callbacks / Steinway commands from Resolve ----
    void onPanelDisconnect() { vlog("panel disconnected\n"); E.disconnect(); }
};

// ---- Eclipse implementation ----
static int parseHostPort(const std::string& s, std::string& host, int& port) {
    size_t c = s.find(':');
    if (c == std::string::npos) { host = s; port = 23; return 1; }
    host = s.substr(0, c); port = atoi(s.c_str() + c + 1);
    return port > 0;
}

bool Eclipse::connectTo(const std::string& hostport) {
    disconnect();
    vlog("Connecting to %s\n", hostport.c_str());
    std::string host; int port;
    if (!parseHostPort(hostport, host, port)) { elog("bad host:port '%s'\n", hostport.c_str()); return false; }
    struct addrinfo hints; memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    char ps[16]; snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res) { elog("cannot resolve '%s'\n", host.c_str()); return false; }
    int s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) { elog("socket: %s\n", strerror(errno)); freeaddrinfo(res); return false; }
    int fl = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, fl | O_NONBLOCK);
    int r = connect(s, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (r < 0 && errno == EINPROGRESS) {
        struct pollfd pf = {s, POLLOUT, 0};
        if (poll(&pf, 1, 4000) <= 0) { elog("connect to %s timed out\n", hostport.c_str()); close(s); return false; }
        int err = 0; socklen_t el = sizeof err; getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err) { elog("connect to %s failed: %s\n", hostport.c_str(), strerror(err)); close(s); return false; }
    } else if (r < 0) { elog("connect to %s failed: %s\n", hostport.c_str(), strerror(errno)); close(s); return false; }
    fcntl(s, F_SETFL, fl);
    int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    struct timeval tv = {2, 0}; setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    fd = s; rx.clear(); initDone = false; initAt = nowMs() + 1000;   // the original sleeps one second here
    vlog("Connected\n");
    return true;
}

void Eclipse::readReady() {
    uint8_t buf[256];
    ssize_t n = recv(fd, buf, sizeof buf, 0);
    if (n <= 0) {
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
        elog("panel connection lost (%s)\n", n == 0 ? "closed" : strerror(errno));
        ctl->onPanelDisconnect();
        return;
    }
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == 0xF7) { processFrame(rx); rx.clear(); }
        else { rx.push_back(buf[i]); if (rx.size() > 4096) rx.clear(); }
    }
}

static int sext7(unsigned b) { int e = (-(int)(b & 0x40)) | (int)b; return (int8_t)(e & 0xff); }

void Eclipse::processFrame(const std::vector<uint8_t>& f) {
    if (f.size() <= 2 || f[0] != 0xF0 || f[1] != 0x15 || f[2] != 0x25) { elog("invalid packet from panel\n"); return; }
    if (f.size() < 4) return;
    if (!initDone && f[3] != 0x03) return;
    unsigned cmd = f[3], b4 = f.size() > 4 ? f[4] : 0, b5 = f.size() > 5 ? f[5] : 0;
    switch (cmd) {
    case 0x55:
        if (b4 <= 0x0e) ctl->buttonPressed((int)b4, b5 == 0);
        else if (b4 >= 0x26 && b4 <= 0x2b) ctl->buttonPressed((int)b4 - 0x17, b5 == 0);
        else if (b4 >= 0x20 && b4 <= 0x25) ctl->rotorTurned((int)b4 - 0x1f, sext7(b5));
        else if (b4 >= 0x2d && b4 <= 0x35) ctl->rotorTurned((int)b4 - 0x26, sext7(b5));
        break;
    case 0x42: ctl->buttonPressed((int)b4 + 0x17, b5 == 1); break;
    case 0x47: ctl->rotorTurned(16, sext7(b4)); break;
    case 0x48: ctl->rotorTurned(17, sext7(b4)); break;
    case 0x46: ctl->rotorTurned((int)b4 + 0x12, sext7(b5)); break;
    case 0x03:
        vlog("Panel ID response:");
        for (size_t i = 4; i < f.size() && i < 10; i++) vlog(" %02x", f[i]);
        vlog("\n");
        break;
    default: vlog("Unhandled panel command 0x%02x\n", cmd); break;
    }
}

// ---- Resolve implementation ----
static int createListenUDS(const char* path) {
    mode_t old = umask(0);
    unlink(path);
    struct sockaddr_un a; memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX; strncpy(a.sun_path, path, sizeof a.sun_path - 1);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) { umask(old); return -1; }
#ifdef SO_NOSIGPIPE
    int one = 1; setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    if (bind(s, (struct sockaddr*)&a, sizeof a) < 0 || listen(s, 1) < 0) {
        elog("cannot listen on %s: %s\n", path, strerror(errno)); close(s); umask(old); return -1;
    }
    umask(old);
    return s;
}

bool Resolve::open(int sfd) {
    if (sfd != -1) { providedFd = sfd; fd = sfd; vlog("Using socket %d for Resolve\n", sfd); return true; }
    vlog("Opening communication to resolve..\n");
    listenFd = createListenUDS(SOCKET_PATH);
    return listenFd != -1;
}

void Resolve::readReady() {
    static uint8_t tmp[32768];
    ssize_t n = recv(fd, tmp, sizeof tmp, 0);
    if (n == 0) { closeConn(); return; }
    if (n < 0) { if (errno != EINTR && errno != EAGAIN) { elog("recv from Resolve: %s\n", strerror(errno)); closeConn(); } return; }
    buf.insert(buf.end(), tmp, tmp + n);
    for (;;) {
        if (buf.size() < 12) break;
        uint32_t len = buf[8] | (buf[9] << 8) | (buf[10] << 16) | ((uint32_t)buf[11] << 24);
        if (len > (16u << 20)) { elog("absurd packet length %u from Resolve, dropping buffer\n", len); buf.clear(); break; }
        size_t total = 12 + (size_t)len;
        if (buf.size() < total) break;
        std::vector<uint8_t> pkt(buf.begin(), buf.begin() + total);
        buf.erase(buf.begin(), buf.begin() + total);
        handlePacket(pkt.data(), total);
        if (fd == -1) break;
    }
}

void Resolve::handlePacket(const uint8_t* p, size_t total) {
    auto u16 = [&](int o) { return (unsigned)(p[o] | (p[o + 1] << 8)); };
    unsigned dest = u16(0), cmd = u16(2), target = u16(4);
    uint32_t len = (uint32_t)(total - 12);
    const uint8_t* pl = p + 12;

    auto mapPanel = [](unsigned v, bool bits) -> int {
        if (bits) { switch (v & 0xf000) { case 0: return 0; case 0x1000: return 2; case 0x2000: return 1; case 0x4000: return -1; default: return -1; } }
        switch (v) { case 0: return 0; case 1: return 2; case 2: return 1; case 4: return -1; default: return -1; }
    };
    int panel = mapPanel(dest, false);
    int tpanel = mapPanel(target, true);
    if (dest == 3 || dest > 4) { elog("unsupported destination %u\n", dest); return; }
    if (panel != tpanel) vlog("destination %u and target 0x%04x disagree\n", dest, target);

    std::vector<int> panels;
    if (panel == -1) { if (ctl->E.connected()) panels = {0, 1, 2}; } else panels = {panel};

    for (int pn : panels) {
        switch (cmd) {
        case 1: {                                       // LCD image download
            if (len < 0x22) { elog("short LCD image packet\n"); break; }
            uint32_t dl = pl[8] | (pl[9] << 8) | (pl[10] << 16) | ((uint32_t)pl[11] << 24);
            if (len < (uint64_t)dl + 0x22) { elog("truncated LCD image packet\n"); break; }
            ctl->images[pn].insert((uint16_t)(pl[0] | (pl[1] << 8)));
            break;
        }
        case 10: {                                      // status request
            uint8_t reply[8] = {0};
            uint16_t absent = ctl->E.connected() ? 0 : 1;
            uint16_t count = ctl->E.connected() ? (uint16_t)ctl->images[pn].size() : 0;
            reply[0] = absent & 0xff; reply[1] = absent >> 8; reply[4] = count & 0xff; reply[5] = count >> 8;
            message(10, 0, reply, 8);
            break;
        }
        case 13: ctl->images[pn].clear(); break;
        case 14: break;
        default:
            vlog("Unimplemented Resolve command %u (len %u)\n", cmd, len);
            break;
        }
    }
}

// ---------------------------------------------------------------------- main
static void onSignal(int sig) { (void)sig; g_quit = 1; }

static void usage(const char* argv0) {
    printf("Usage: %s [options] [hostname[:port]]\n", argv0);
    puts("Options:");
    puts("  -h\t\t\tDisplay this help page");
    puts("  -v\t\t\tDisplay version and exit");
    puts("  -V\t\t\tOutput verbose information");
    puts("  -d\t\t\tOutput debug information on the panel's timecode display");
    puts("  -r\t\t\tRetry the connection indefinitely (this build always does)");
    puts("  -l filename\t\tOutput errors and logs to file 'filename'");
    puts("  -s fd\t\t\tUse the already-connected socket 'fd' to talk to Resolve");
    puts("  -c filename\t\tRead the optional mapping file 'filename' (default:");
    puts("  \t\t\t$HOME/Library/Application Support/JLCooperPanelDaemon/mapping.conf, or $JLCD_CONFIG)");
    puts("  hostname:\t\tthe hostname/ip of the panel (optional)'");
    puts("  port:\t\t\tthe port of the panel (optional)'");
}

int main(int argc, char** argv) {
    int sfd = -1; int c;
    std::string cfgPath;
    while ((c = getopt(argc, argv, "s:rdvVl:c:h")) != -1) {
        switch (c) {
        case 'c': cfgPath = optarg; break;
        case 'V': g_verbose = true; break;
        case 'v': printf("%s version %s\nUnofficial native reimplementation (MIT), not affiliated with Blackmagic Design\n", argv[0], VERSION_STRING); return 0;
        case 'd': g_debugDisplay = true; printf("Debugging via panel displays enabled\n"); break;
        case 'r': break;
        case 'l': if (!freopen(optarg, "w", stderr)) { fprintf(stdout, "Failed to open '%s' for writing as stderr\n", optarg); return 1; } break;
        case 's': sfd = atoi(optarg); if (sfd < 0) sfd = -1; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }
    std::string hostport = DEFAULT_PANEL;
    if (optind == argc - 1) hostport = argv[argc - 1];

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = onSignal;
    sigaction(SIGTERM, &sa, nullptr); sigaction(SIGINT, &sa, nullptr); sigaction(SIGHUP, &sa, nullptr);

    vlog("JLCooperPanelDaemon %s (native reimplementation), panel %s\n", VERSION_STRING, hostport.c_str());
    pid_t ppid0 = getppid();
    vlog("Watching parent pid %d\n", (int)ppid0);

    Controller C;
    if (!C.R.open(sfd)) elog("could not open the communication channel to Resolve\n");

    // optional mapping file: -c, else $JLCD_CONFIG, else ~/Library/Application Support/JLCooperPanelDaemon/mapping.conf
    if (cfgPath.empty()) {
        if (const char* e = getenv("JLCD_CONFIG")) cfgPath = e;
        else {
            // Resolve may start the helper without HOME in its environment: ask the system for the home directory
            std::string home;
            if (const char* h = getenv("HOME")) home = h;
            if (home.empty()) if (struct passwd* pw = getpwuid(getuid())) if (pw->pw_dir) home = pw->pw_dir;
            if (!home.empty()) cfgPath = home + "/Library/Application Support/JLCooperPanelDaemon/mapping.conf";
        }
    }
    std::string tcPath;
    { size_t sl = cfgPath.rfind('/'); if (!cfgPath.empty()) tcPath = (sl == std::string::npos ? std::string() : cfgPath.substr(0, sl + 1)) + "timecode.txt"; }
    FileStamp cfgStamp;
    bool cfgSeen = false;
    auto reloadMapping = [&](bool first) {
        FileStamp s = stampOf(cfgPath);
        if (!first && s == cfgStamp) return;
        cfgStamp = s;
        Mapping m;
        if (!s.exists || !loadMapping(cfgPath, m)) {
            if (cfgSeen) ilog("mapping: %s is gone, using the built-in mapping\n", cfgPath.c_str());
            else if (first) ilog("mapping: no file at %s, using the built-in mapping (it is picked up as soon as it appears)\n", cfgPath.c_str());
            cfgSeen = false; C.applyMapping(Mapping()); return;
        }
        cfgSeen = true;
        if (first || m.actions.empty())          // a script driving 'send' lines would flood the log
            ilog("mapping: loaded %s (%d SHIFT knob(s), %d label(s), probe %s)\n", cfgPath.c_str(),
                 (int)m.shiftPot.size(), (int)m.shiftLabel.size(), m.probe ? "ON" : "off");
        C.applyMapping(m, !first);
    };
    if (!cfgPath.empty()) reloadMapping(true);
    else ilog("mapping: no home directory found, mapping file disabled (use -c <file> or JLCD_CONFIG)\n");

    int64_t nextConnect = 0, nextTick = nowMs(), nextParent = nowMs() + 1000, nextCfg = nowMs() + 200, nextTc = nowMs() + 200;
    bool wasConnected = false;
    while (!g_quit) {
        int64_t t = nowMs();
        if (wasConnected && !C.E.connected()) nextConnect = nowMs() + 1000;
        wasConnected = C.E.connected();
        if (!C.E.connected() && t >= nextConnect) {
            if (!C.E.connectTo(hostport)) nextConnect = nowMs() + 2000;
            wasConnected = C.E.connected();
            t = nowMs();
        }
        if (C.E.connected() && !C.E.initDone && t >= C.E.initAt) {
            if (!C.E.setMode2() || !C.E.requestId()) { elog("panel handshake failed\n"); C.E.disconnect(); }
            else { C.E.initDone = true; C.panelInit(); }
        }
        std::vector<struct pollfd> pf;
        enum { K_LISTEN, K_RESOLVE, K_PANEL };
        std::vector<int> kinds;
        if (C.R.listenFd != -1) { pf.push_back({C.R.listenFd, POLLIN, 0}); kinds.push_back(K_LISTEN); }
        if (C.R.fd != -1) { pf.push_back({C.R.fd, POLLIN, 0}); kinds.push_back(K_RESOLVE); }
        if (C.E.fd != -1) { pf.push_back({C.E.fd, POLLIN, 0}); kinds.push_back(K_PANEL); }
        int timeout = 200;
        if (C.tcShown) timeout = 40;                  // timecode being fed: look at the file about 25 times a second
        if (C.shuttle != 0) timeout = (int)std::max<int64_t>(0, nextTick - t);
        if (!C.E.connected()) timeout = (int)std::min<int64_t>(timeout, std::max<int64_t>(1, nextConnect - t));
        else if (!C.E.initDone) timeout = (int)std::min<int64_t>(timeout, std::max<int64_t>(1, C.E.initAt - t));
        int r = poll(pf.data(), pf.size(), timeout);
        if (r > 0) {
            for (size_t i = 0; i < pf.size(); i++) {
                if (!(pf[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                if (kinds[i] == K_LISTEN) C.R.acceptReady();
                else if (kinds[i] == K_RESOLVE) { if (C.R.fd == pf[i].fd) C.R.readReady(); }
                else if (kinds[i] == K_PANEL) { if (C.E.fd == pf[i].fd) C.E.readReady(); }
            }
        }
        t = nowMs();
        if (C.E.connected() && C.shuttle != 0) {            // 5 ms shuttle repeat timer
            int guard = 0;
            while (t >= nextTick && guard++ < 20) {
                nextTick += 5;
                if (C.shuttle != 0) {
                    C.shuttleCnt++;
                    if (C.shuttleCnt > std::abs(C.shuttle) + 12) { C.swJog(C.shuttle > 0 ? 1 : -1); C.shuttleCnt = 0; }
                }
            }
            if (t >= nextTick) nextTick = t + 5;
        } else nextTick = t + 5;
        if (!cfgPath.empty() && t >= nextCfg) { nextCfg = t + 200; reloadMapping(false); }
        if (!tcPath.empty() && t >= nextTc) { nextTc = t + (C.tcShown ? 40 : 200); C.pollTimecode(tcPath); }
        if (t >= nextParent) {
            nextParent = t + 1000;
            if (ppid0 != 1 && getppid() != ppid0) { vlog("Parent process died, quitting\n"); break; }
        }
    }
    if (C.R.listenFd != -1) { close(C.R.listenFd); unlink(SOCKET_PATH); }
    C.shutdownLeds();
    C.E.disconnect();
    vlog("Daemon shuts down!\n");
    return 0;
}
