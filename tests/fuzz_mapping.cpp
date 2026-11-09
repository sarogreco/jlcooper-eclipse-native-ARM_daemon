// Fuzzes the mapping-file parser with random and half-valid files. Build with sanitizers (`make fuzz-mapping`).
// The daemon source is included directly so that the real parser is exercised.
#define main daemon_main
#include "../src/JLCooperPanelDaemon.cpp"
#undef main

#include <random>

int main(int argc, char** argv) {
    int iterations = argc > 1 ? atoi(argv[1]) : 20000;
    const char* path = argc > 2 ? argv[2] : "fuzz_mapping.tmp";
    if (!freopen("/dev/null", "w", stderr)) return 2;    // the parser reports every bad line
    std::mt19937 rng(12345);
    const char* words[] = {"shiftpot", "shiftlabel", "probe", "probestart", "on", "off", "yes", "0", "1", "2", "7", "8", "-1", "99999999999",
                           "2/91", "0x25b", "3/1", "2/", "/5", "0x", "1/2/3", "#", "|", "a|b", "Color|Temp", "|", "%s%n", "\t", "  ", "\r", "SHIFTPOT", "Probe", "send", "reset", "send 2/33 20", "reset 0x221", "-2000", "2001"};
    long parsed = 0, good = 0;
    for (int it = 0; it < iterations; it++) {
        std::string text;
        int lines = (int)(rng() % 12);
        for (int l = 0; l < lines; l++) {
            int toks = (int)(rng() % 7);
            for (int t = 0; t < toks; t++) {
                if (rng() % 10 == 0) { int n = (int)(rng() % 40); for (int i = 0; i < n; i++) text += (char)(rng() % 256); }
                else text += words[rng() % (sizeof words / sizeof *words)];
                text += (rng() % 8) ? " " : "";
            }
            if (rng() % 40 == 0) text += std::string(300 + rng() % 900, 'x');     // over-long line
            text += (rng() % 12) ? "\n" : "";
        }
        FILE* f = fopen(path, "wb"); if (!f) return 2;
        fwrite(text.data(), 1, text.size(), f); fclose(f);
        Mapping m;
        bool ok = loadMapping(path, m);
        parsed++; if (ok) good++;
        for (auto& kv : m.shiftPot) if (kv.first % 8 < 1 || kv.first % 8 > 6 || kv.first / 8 > 7 || kv.second > 0x3ff) { printf("invariant broken: pot key %d value 0x%x\n", kv.first, kv.second); return 1; }
        for (auto& kv : m.shiftLabel) if (kv.first % 8 < 1 || kv.first % 8 > 6 || kv.first / 8 > 7 || kv.second.first.size() > 8 || kv.second.second.size() > 8) { puts("invariant broken: label too long"); return 1; }
        if (m.probeStart > 0x3ff) { puts("invariant broken: probestart"); return 1; }
        if (m.actions.size() > 64) { puts("invariant broken: too many actions"); return 1; }
        for (auto& a : m.actions) if (a.pot > 0x3ff || a.delta < -2000 || a.delta > 2000) { puts("invariant broken: action"); return 1; }
    }
    unlink(path);
    printf("mapping fuzz: %ld files parsed (%ld opened), no crash, invariants hold\n", parsed, good);
    return 0;
}
