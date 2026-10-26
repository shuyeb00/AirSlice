#include <vector>
#include <cmath>
#include <cstdint>
#include "Sound.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {

constexpr int    RATE = 22050;        // samples per second
constexpr double TWO_PI = 6.283185307179586;

std::vector<int16_t> bank[(int)Sfx::Count];   // one generated sound per effect
bool enabled = true;

// ---------- Small synthesis helpers ----------
uint32_t seed = 12345;
double noise() {                      // white noise in -1..1 (xorshift random numbers)
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (seed / 4294967295.0) * 2.0 - 1.0;
}

// Converts float samples to 16-bit, scaling the loudest sample to 'peak'
std::vector<int16_t> toPcm(const std::vector<double>& s, double peak = 0.6) {
    double maxAbs = 1e-9;
    for (double v : s) maxAbs = std::max(maxAbs, std::fabs(v));
    std::vector<int16_t> out(s.size());
    for (size_t i = 0; i < s.size(); i++)
        out[i] = (int16_t)(s[i] / maxAbs * peak * 32767.0);
    return out;
}

// A short sine note with a quick fade-in and exponential fade-out
void addNote(std::vector<double>& s, double start, double dur, double freq, double vol = 1.0) {
    int a = (int)(start * RATE), n = (int)(dur * RATE);
    if ((int)s.size() < a + n) s.resize(a + n, 0.0);
    for (int i = 0; i < n; i++) {
        double t = (double)i / RATE;
        double env = std::min(1.0, t / 0.005) * std::exp(-4.0 * t / dur);
        s[a + i] += vol * env * (std::sin(TWO_PI * freq * t) + 0.3 * std::sin(TWO_PI * 2 * freq * t));
    }
}

// Slice: a "swish" (filtered noise) followed by a soft wet thump
std::vector<double> makeSlice() {
    int n = (int)(0.22 * RATE);
    std::vector<double> s(n);
    double lp1 = 0, lp2 = 0, phase = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / RATE;
        // band-pass noise = difference of two low-pass filters
        double x = noise();
        lp1 += 0.45 * (x - lp1);
        lp2 += 0.08 * (x - lp2);
        double swish = (lp1 - lp2) * std::min(1.0, t / 0.01) * std::exp(-t / 0.05);
        // thump: falling pitch sine
        double f = 180.0 - 400.0 * t;
        phase += TWO_PI * std::max(60.0, f) / RATE;
        double thump = (t > 0.02) ? std::sin(phase) * std::exp(-(t - 0.02) / 0.05) * 0.6 : 0.0;
        s[i] = swish * 1.4 + thump;
    }
    return s;
}

// Bomb: low rumbling noise plus a deep boom
std::vector<double> makeBomb() {
    int n = (int)(0.9 * RATE);
    std::vector<double> s(n);
    double lp = 0, phase = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / RATE;
        lp += 0.04 * (noise() - lp);
        phase += TWO_PI * (70.0 - 30.0 * t) / RATE;
        double env = std::min(1.0, t / 0.005) * std::exp(-t / 0.22);
        s[i] = (lp * 3.0 + std::sin(phase) * 0.8) * env;
    }
    return s;
}

// Miss: a short falling tone
std::vector<double> makeMiss() {
    int n = (int)(0.3 * RATE);
    std::vector<double> s(n);
    double phase = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / RATE;
        phase += TWO_PI * (440.0 - 600.0 * t) / RATE;
        s[i] = (std::sin(phase) + 0.3 * std::sin(3 * phase)) * std::exp(-t / 0.12);
    }
    return s;
}

std::vector<double> makeNotes(const std::vector<double>& freqs, double step, double dur) {
    std::vector<double> s;
    for (size_t k = 0; k < freqs.size(); k++) addNote(s, k * step, dur, freqs[k]);
    return s;
}

#ifdef _WIN32
// ---------- Playback: several waveOut channels so sounds can overlap ----------
constexpr int CHANNELS = 6;
struct Channel {
    HWAVEOUT h = nullptr;
    WAVEHDR  hdr{};
    bool     prepared = false;
};
Channel ch[CHANNELS];
int nextChannel = 0;

bool channelFree(const Channel& c) {
    return !c.prepared || (c.hdr.dwFlags & WHDR_DONE);
}
#endif

} // namespace

void soundInit() {
    bank[(int)Sfx::Slice]    = toPcm(makeSlice(), 0.55);
    bank[(int)Sfx::Bomb]     = toPcm(makeBomb(), 0.8);
    bank[(int)Sfx::Miss]     = toPcm(makeMiss(), 0.45);
    bank[(int)Sfx::Combo]    = toPcm(makeNotes({660, 880, 1320}, 0.07, 0.14), 0.5);
    bank[(int)Sfx::LevelUp]  = toPcm(makeNotes({523, 659, 784, 1047}, 0.09, 0.18), 0.5);
    bank[(int)Sfx::GameOver] = toPcm(makeNotes({392, 330, 262, 196}, 0.2, 0.35), 0.55);
    bank[(int)Sfx::Blip]     = toPcm(makeNotes({880, 1175}, 0.06, 0.1), 0.4);

#ifdef _WIN32
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = RATE * 2;
    for (Channel& c : ch)
        if (waveOutOpen(&c.h, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            c.h = nullptr;
#endif
}

void soundShutdown() {
#ifdef _WIN32
    for (Channel& c : ch) {
        if (!c.h) continue;
        waveOutReset(c.h);
        if (c.prepared) waveOutUnprepareHeader(c.h, &c.hdr, sizeof(WAVEHDR));
        waveOutClose(c.h);
        c.h = nullptr;
        c.prepared = false;
    }
#endif
}

void soundPlay(Sfx s) {
    if (!enabled) return;
#ifdef _WIN32
    // Use a channel that has finished playing; if all are busy, take the next one in turn
    int pick = -1;
    for (int k = 0; k < CHANNELS; k++) {
        int i = (nextChannel + k) % CHANNELS;
        if (ch[i].h && channelFree(ch[i])) { pick = i; break; }
    }
    if (pick < 0) pick = nextChannel;
    nextChannel = (pick + 1) % CHANNELS;

    Channel& c = ch[pick];
    if (!c.h) return;
    waveOutReset(c.h);
    if (c.prepared) waveOutUnprepareHeader(c.h, &c.hdr, sizeof(WAVEHDR));

    std::vector<int16_t>& data = bank[(int)s];
    c.hdr = WAVEHDR{};
    c.hdr.lpData = (LPSTR)data.data();
    c.hdr.dwBufferLength = (DWORD)(data.size() * sizeof(int16_t));
    waveOutPrepareHeader(c.h, &c.hdr, sizeof(WAVEHDR));
    waveOutWrite(c.h, &c.hdr, sizeof(WAVEHDR));
    c.prepared = true;
#else
    (void)s;   // no sound on other systems
#endif
}

void soundSetEnabled(bool on) { enabled = on; }
bool soundEnabled() { return enabled; }
