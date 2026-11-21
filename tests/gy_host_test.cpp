/*
 * Gullay - a tiny CLAP host that loads the built plugin the way a DAW does and
 * checks it end to end.   usage: clap_host_test <path to the plugin binary>
 */
#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <set>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <cstdlib>

static int failures = 0;
static void check(bool ok, const char *name, const std::string &detail = "")
{
    std::printf("%-5s %-34s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    if (!ok)
        ++failures;
}
static std::string fmt(const char *f, ...)
{
    char b[512];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof(b), f, a);
    va_end(a);
    return b;
}

static const void *hostExt(const clap_host_t *, const char *) { return nullptr; }
static void hostNop(const clap_host_t *) {}
static const clap_host_t host = {CLAP_VERSION_INIT, nullptr, "gullay host test", "", "", "1", hostExt, hostNop, hostNop, hostNop};

struct EventList
{
    std::vector<clap_event_param_value_t> ev;
    clap_input_events_t in;
    EventList()
    {
        in.ctx = this;
        in.size = [](const clap_input_events_t *l) { return (uint32_t)((EventList *)l->ctx)->ev.size(); };
        in.get = [](const clap_input_events_t *l, uint32_t i) { return (const clap_event_header_t *)&((EventList *)l->ctx)->ev[i]; };
    }
    void set(uint32_t time, clap_id id, double v)
    {
        clap_event_param_value_t e{};
        e.header.size = sizeof(e);
        e.header.time = time;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.param_id = id;
        e.note_id = -1;
        e.port_index = e.channel = e.key = -1;
        e.value = v;
        ev.push_back(e);
    }
};
static int outValues = 0, outGestures = 0; // what the plugin tells the host
static bool pushNop(const clap_output_events_t *, const clap_event_header_t *h)
{
    if (h->type == CLAP_EVENT_PARAM_VALUE)
        ++outValues;
    if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END)
        ++outGestures;
    return true;
}
static const clap_output_events_t outEvents = {nullptr, pushNop};

struct Mem
{
    std::string data;
    size_t pos{0};
};
static int64_t memWrite(const clap_ostream_t *s, const void *b, uint64_t n)
{
    auto *m = (Mem *)s->ctx;
    n = std::min<uint64_t>(n, 100); // short writes, as a host is allowed to do
    m->data.append((const char *)b, (size_t)n);
    return (int64_t)n;
}
static int64_t memRead(const clap_istream_t *s, void *b, uint64_t n)
{
    auto *m = (Mem *)s->ctx;
    n = std::min<uint64_t>({n, (uint64_t)(m->data.size() - m->pos), (uint64_t)77});
    std::memcpy(b, m->data.data() + m->pos, (size_t)n);
    m->pos += (size_t)n;
    return (int64_t)n;
}

struct Rng
{
    uint32_t s{99};
    float pm1()
    {
        s = s * 1664525u + 1013904223u;
        return (float)((s >> 8) / 8388608.0 - 1.0);
    }
};

static long gSineAge = 0; // samples since the test tone (re)started
// Runs noise through the plugin in blocks; returns peak and whether everything was finite.
static double runAudio(const clap_plugin_t *p, int blocks, int frames, EventList *events, double *rmsOut = nullptr,
                       float amp = 0.5f, double startBeats = -1, std::vector<float> *record = nullptr,
                       double *rmsRight = nullptr)
{
    clap_event_transport_t tr{};
    tr.header.size = sizeof(tr);
    tr.header.type = CLAP_EVENT_TRANSPORT;
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_IS_PLAYING;
    tr.tempo = 120;
    tr.tsig_num = tr.tsig_denom = 4;
    double sumR = 0;
    std::vector<float> il(frames), ir(frames), ol(frames), orr(frames);
    float *in[2] = {il.data(), ir.data()}, *out[2] = {ol.data(), orr.data()};
    clap_audio_buffer_t ib{in, nullptr, 2, 0, 0}, ob{out, nullptr, 2, 0, 0};
    EventList none;
    Rng g;
    double peak = 0, sum = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < frames; ++i)
            if (amp < 0) // a negative amplitude asks for a steady 220 Hz sine (clicks show up against it)
            {
                static double ph = 0;
                if (gSineAge == 0)
                    ph = 0; // a fresh tone starts at the same phase every time
                ph += 2 * 3.14159265358979 * 220.0 / 48000.0;
                const float fade = std::min(1.f, (float)gSineAge++ / 2400.f); // a 50 ms fade-in, so the test tone has no edge of its own
                il[i] = ir[i] = (float)(-amp * fade * std::sin(ph));
            }
            else
                il[i] = amp * g.pm1(), ir[i] = amp * g.pm1();
        clap_process_t pr{};
        pr.frames_count = (uint32_t)frames;
        pr.audio_inputs = &ib;
        pr.audio_outputs = &ob;
        pr.audio_inputs_count = pr.audio_outputs_count = 1;
        pr.in_events = (b == 0 && events) ? &events->in : &none.in;
        if (startBeats >= 0)
        {
            tr.song_pos_beats = (clap_beattime)std::llround((startBeats + (double)b * frames / 48000.0 * 2.0) * CLAP_BEATTIME_FACTOR);
            pr.transport = &tr;
        }
        pr.out_events = &outEvents;
        if (p->process(p, &pr) == CLAP_PROCESS_ERROR)
            return -1;
        for (int i = 0; i < frames; ++i)
        {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i]))
                return -1;
            peak = std::max({peak, (double)std::fabs(ol[i]), (double)std::fabs(orr[i])});
            if (b >= blocks / 2)
                sum += (double)ol[i] * ol[i], sumR += (double)orr[i] * orr[i];
        }
        if (record)
            record->insert(record->end(), ol.begin(), ol.end());
    }
    if (rmsOut)
        *rmsOut = std::sqrt(sum / (double)((blocks - blocks / 2) * frames));
    if (rmsRight)
        *rmsRight = std::sqrt(sumR / (double)((blocks - blocks / 2) * frames));
    return peak;
}


// one MIDI message for the plugin's note port
struct MidiList
{
    std::vector<clap_event_midi_t> ev;
    clap_input_events_t in;
    MidiList()
    {
        in.ctx = this;
        in.size = [](const clap_input_events_t *l) { return (uint32_t)((MidiList *)l->ctx)->ev.size(); };
        in.get = [](const clap_input_events_t *l, uint32_t i) { return (const clap_event_header_t *)&((MidiList *)l->ctx)->ev[i]; };
    }
    void add(uint8_t a, uint8_t b, uint8_t c)
    {
        clap_event_midi_t e{};
        e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        e.port_index = 0;
        e.data[0] = a, e.data[1] = b, e.data[2] = c;
        ev.push_back(e);
    }
};
static void runEvents(const clap_plugin_t *p, const clap_input_events_t *in, int frames = 256)
{
    std::vector<float> l(frames), r(frames), ol(frames), orr(frames);
    float *ins[2] = {l.data(), r.data()}, *outs[2] = {ol.data(), orr.data()};
    clap_audio_buffer_t ib{ins, nullptr, 2, 0, 0}, ob{outs, nullptr, 2, 0, 0};
    clap_process_t pr{};
    pr.frames_count = (uint32_t)frames;
    pr.audio_inputs = &ib, pr.audio_outputs = &ob, pr.audio_inputs_count = pr.audio_outputs_count = 1;
    pr.in_events = in, pr.out_events = &outEvents;
    p->process(p, &pr);
}
// an impulse through the plugin: where the first echo lands (samples after the click), and the output
static int firstEcho(const clap_plugin_t *p, int total, std::vector<float> *outL = nullptr, double *rmsTail = nullptr)
{
    const int frames = 256;
    std::vector<float> l(frames), r(frames), ol(frames), orr(frames);
    float *ins[2] = {l.data(), r.data()}, *outs[2] = {ol.data(), orr.data()};
    clap_audio_buffer_t ib{ins, nullptr, 2, 0, 0}, ob{outs, nullptr, 2, 0, 0};
    EventList none;
    clap_event_transport_t tr{};
    tr.header.size = sizeof(tr), tr.header.type = CLAP_EVENT_TRANSPORT;
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_IS_PLAYING;
    tr.tempo = 120, tr.tsig_num = tr.tsig_denom = 4;
    int found = -1;
    const int click = 1000;
    double sum = 0;
    long cnt = 0;
    for (int pos = 0; pos < total; pos += frames)
    {
        for (int i = 0; i < frames; ++i)
            l[i] = r[i] = (pos + i == click) ? 0.9f : 0.f;
        clap_process_t pr{};
        pr.frames_count = frames;
        pr.audio_inputs = &ib, pr.audio_outputs = &ob, pr.audio_inputs_count = pr.audio_outputs_count = 1;
        pr.in_events = &none.in, pr.out_events = &outEvents;
        tr.song_pos_beats = (clap_beattime)std::llround((double)pos / 48000.0 * 2.0 * CLAP_BEATTIME_FACTOR);
        pr.transport = &tr;
        p->process(p, &pr);
        for (int i = 0; i < frames; ++i)
        {
            int t = pos + i;
            float a = std::max(std::fabs(ol[i]), std::fabs(orr[i]));
            if (outL)
                outL->push_back(ol[i]);
            if (found < 0 && t > click + 300 && a > 0.02f)
                found = t - click;
            if (t > total / 2)
                sum += (double)ol[i] * ol[i], ++cnt;
        }
    }
    if (rmsTail)
        *rmsTail = cnt ? std::sqrt(sum / (double)cnt) : 0;
    return found;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: gy_host_test <plugin binary>\n");
        return 2;
    }
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib)
    {
        std::printf("FAIL  cannot load %s: %s\n", argv[1], dlerror());
        return 1;
    }
    auto *entry = (const clap_plugin_entry_t *)dlsym(lib, "clap_entry");
    check(entry && clap_version_is_compatible(entry->clap_version), "entry point");
    if (!entry)
        return 1;
    check(entry->init(argv[1]), "entry init");
    auto *factory = (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    check(factory && factory->get_plugin_count(factory) == 1, "one plugin in the factory");
    auto *desc = factory->get_plugin_descriptor(factory, 0);
    check(desc && desc->id && desc->name && !std::strcmp(desc->name, "Gullay"), "descriptor",
          desc ? fmt("%s / %s / %s / %s", desc->name, desc->vendor, desc->version, desc->id) : "");
    const clap_plugin_t *p = factory->create_plugin(factory, &host, desc->id);
    check(p && p->init(p), "create and init");
    if (!p)
        return 1;
    auto *params = (const clap_plugin_params_t *)p->get_extension(p, CLAP_EXT_PARAMS);
    auto *state = (const clap_plugin_state_t *)p->get_extension(p, CLAP_EXT_STATE);
    auto *ports = (const clap_plugin_audio_ports_t *)p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
    auto *notes = (const clap_plugin_note_ports_t *)p->get_extension(p, CLAP_EXT_NOTE_PORTS);
    auto *lat = (const clap_plugin_latency_t *)p->get_extension(p, CLAP_EXT_LATENCY);
    check(params && state && ports && notes && lat, "extensions: params, state, ports, note port, latency");
    clap_note_port_info_t ni{};
    check(notes && notes->count(p, true) == 1 && notes->get(p, 0, true, &ni) && (ni.supported_dialects & CLAP_NOTE_DIALECT_MIDI), "MIDI input port");

    // every parameter: sane info, unique id and name, text both ways
    uint32_t n = params->count(p);
    std::set<clap_id> ids;
    std::set<std::string> names;
    bool infoOk = true, textOk = true;
    std::string firstBad, badInfo;
    std::vector<clap_param_info_t> infos(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        auto &inf = infos[i];
        bool ok = params->get_info(p, i, &inf) && inf.max_value > inf.min_value && inf.default_value >= inf.min_value &&
                  inf.default_value <= inf.max_value && inf.name[0];
        double v = 0;
        ok = ok && params->get_value(p, inf.id, &v) && std::fabs(v - inf.default_value) < 1e-4;
        if (!ok && badInfo.empty())
            badInfo = inf.name;
        infoOk = infoOk && ok;
        ids.insert(inf.id);
        names.insert(inf.name);
        for (double t : {0.0, 0.31, 0.5, 1.0})
        {
            double val = inf.min_value + t * (inf.max_value - inf.min_value);
            if (std::string(inf.name).rfind("Lock ", 0) == 0 && val < 0)
                val = -1;
            if (inf.flags & CLAP_PARAM_IS_STEPPED)
                val = std::round(val);
            char txt[128];
            double back = 0;
            bool tok = params->value_to_text(p, inf.id, val, txt, sizeof(txt)) && txt[0] && params->text_to_value(p, inf.id, txt, &back);
            bool variant = std::strstr(inf.name, "Variant") != nullptr;
            tok = tok && (variant || std::fabs(back - val) <= 0.015 * (inf.max_value - inf.min_value) + 1e-9);
            if (!tok && firstBad.empty())
                firstBad = fmt("%s: %g -> '%s' -> %g", inf.name, val, txt, back);
            textOk = textOk && tok;
        }
    }
    check(infoOk && ids.size() == n && names.size() == n, "parameter list",
          fmt("%u parameters, %zu ids, %zu names %s", n, ids.size(), names.size(), badInfo.c_str()));
    check(textOk, "values to text and back", firstBad);
    auto idOf = [&](const char *name) -> clap_id
    {
        for (auto &inf : infos)
            if (!std::strcmp(inf.name, name))
                return inf.id;
        std::printf("FAIL  no parameter named %s\n", name);
        ++failures;
        return CLAP_INVALID_ID;
    };
    check(p->activate(p, 48000, 1, 512) && p->start_processing(p), "activate at 48 kHz");
    check(lat->get(p) == 96, "latency is one Juba block plus the limiter look-ahead", fmt("%u samples", lat->get(p)));

    // defaults: Dual Looping Delay, A at one beat (24000 samples at 120 bpm) plus the 64 latency
    {
        int e = firstEcho(p, 60000);
        check(std::abs(e - 24064) < 40, "default: first echo one beat later", fmt("at %d samples", e));
    }
    double rms = 0;
    double pk = runAudio(p, 300, 256, nullptr, &rms, 0.4f, 0);
    check(pk > 0 && pk <= 1.05, "noise through the defaults: finite, under the limiter", fmt("peak %.3f rms %.3f", pk, rms));

    auto set = [&](std::initializer_list<std::pair<const char *, double>> kv)
    {
        EventList e;
        for (auto &x : kv)
            e.set(0, idOf(x.first), x.second);
        runEvents(p, &e.in);
    };
    auto reset = [&]() {
        EventList e;
        for (auto &inf : infos)
            e.set(0, inf.id, inf.default_value);
        runEvents(p, &e.in);
        p->reset(p);
    };
    // ----- GY_DIAG=1: diagnostics for the v0.8 review (does each control change the sound, on every preset?)
    if (std::getenv("GY_DIAG"))
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        auto getv = [&](const char *n) { double v = 0; params->get_value(p, idOf(n), &v); return v; };
        tb->send(p, "ready");
        std::string list = tb->poll(p);
        int nPre = 0;
        { size_t a = list.find("\"presets\":["), e = list.find("]]", a); for (size_t k = list.find("[\"", a); k != std::string::npos && k < e; k = list.find("[\"", k + 1)) ++nPre; }
        std::printf("presets: %d\n", nPre);
        auto load = [&](int i) { char b[32]; std::snprintf(b, sizeof b, "preset %d", i); tb->send(p, b);
            EventList e; if (std::getenv("GY_WET")) e.set(0, idOf("Par Mix"), 1.0), e.set(0, idOf("Ser Mix"), 1.0); if (std::getenv("GY_NOKILL")) e.set(0, idOf("Auto Kill"), 0.0); if (std::getenv("GY_JUBA")) e.set(0, idOf("Engine 1"), 1.0), e.set(0, idOf("Juba Pattern"), std::atof(std::getenv("GY_JUBA"))); if (const char *gs = std::getenv("GY_SET")) { std::string all = gs; size_t q = 0; while (q < all.size()) { size_t e2 = all.find(';', q); if (e2 == std::string::npos) e2 = all.size(); std::string kv = all.substr(q, e2 - q); size_t eq = kv.find('='); if (eq != std::string::npos) e.set(0, idOf(kv.substr(0, eq).c_str()), std::atof(kv.c_str() + eq + 1)); q = e2 + 1; } } runEvents(p, &e.in); p->reset(p); };
        const float sig = std::getenv("GY_SINE") ? -0.3f : 0.3f;
        const int blocks = std::getenv("GY_LONG") ? 2250 : 300; // 12 s or 1.6 s
        auto render = [&](std::vector<float> &rec) { rec.clear(); gSineAge = 0; runAudio(p, blocks, 256, nullptr, nullptr, sig, 0, &rec); };
        auto diffRel = [](const std::vector<float> &a, const std::vector<float> &b) { double d = 0, e = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i) d += (a[i] - b[i]) * (a[i] - b[i]), e += (double)a[i] * a[i]; return e > 0 ? std::sqrt(d / e) : -1.0; };
        const char *what = std::getenv("GY_DIAG");
        for (int i = 0; i < nPre; ++i)
        {
            load(i);
            std::string name = list; // the name, from the list
            { size_t a = 0; int k = -1; size_t s0 = list.find("\"presets\":["); a = s0; while (k < i && a != std::string::npos) { a = list.find("[\"", a + 1); ++k; } size_t e = list.find('"', a + 2); name = list.substr(a + 2, e - a - 2); }
            if (std::getenv("GY_ONLY") && name != std::getenv("GY_ONLY"))
                continue;
            if (std::string(what) == "glitch")
            {
                // clicks: the biggest jump in the second difference, Ancestors on (3 generations) against off
                // clicks after the first 8 s only, once every branch (up to ~7 s long) has played the test's start
                const size_t from = std::getenv("GY_LONG") ? (size_t)(8 * 48000) : 2;
                auto click = [from](const std::vector<float> &x) { double m = 0; for (size_t k = std::max<size_t>(from, 2); k < x.size(); ++k) m = std::max(m, (double)std::fabs(x[k] - 2 * x[k - 1] + x[k - 2])); return m; };
                std::vector<float> off, on, tog;
                load(i), set({{"Ancestors", 0}}), render(off);
                load(i), set({{"Ancestors", 1}, {"Ancestors Generations", 2}}); if (std::getenv("GY_DARK")) set({{"Ancestors Darken", std::atof(std::getenv("GY_DARK"))}}); if (std::getenv("GY_GENS")) set({{"Ancestors Generations", std::atof(std::getenv("GY_GENS"))}}); render(on);
                // switch it off half way through
                load(i), set({{"Ancestors", 1}, {"Ancestors Generations", 2}}); tog.clear(); gSineAge = 0; runAudio(p, 150, 256, nullptr, nullptr, sig, 0, &tog);
                { EventList ev; ev.set(0, idOf("Ancestors"), 0.0); std::vector<float> t2; runAudio(p, 150, 256, &ev, nullptr, sig, 150 * 256 / 24000.0, &t2); tog.insert(tog.end(), t2.begin(), t2.end()); }
                // a click at the switch: the second difference right there against the rest
                double atSw = 0; for (size_t k = 150 * 256 - 64; k < 150 * 256 + 2048 && k + 2 < tog.size(); ++k) atSw = std::max(atSw, (double)std::fabs(tog[k + 2] - 2 * tog[k + 1] + tog[k]));
                // the same moment with Ancestors never on: what the engine itself does there
                std::vector<float> ref; load(i), set({{"Ancestors", 0}}); gSineAge = 0; runAudio(p, 150, 256, nullptr, nullptr, sig, 0, &ref);
                { EventList ev; ev.set(0, idOf("Ancestors"), 0.0); std::vector<float> t2; runAudio(p, 150, 256, &ev, nullptr, sig, 150 * 256 / 24000.0, &t2); ref.insert(ref.end(), t2.begin(), t2.end()); }
                double atRef = 0; for (size_t k = 150 * 256 - 64; k < 150 * 256 + 2048 && k + 2 < ref.size(); ++k) atRef = std::max(atRef, (double)std::fabs(ref[k + 2] - 2 * ref[k + 1] + ref[k]));
                // steady: the on run against a second off run, so the engine's own randomness counts on both sides
                std::vector<float> off2; load(i), set({{"Ancestors", 0}}), render(off2);
                if (std::getenv("GY_WHEREOFF") && name == std::getenv("GY_WHEREOFF"))
                {
                    // where the plain preset (no Ancestors) jumps
                    int n = 0;
                    for (size_t k = std::max<size_t>(from, 2); k < off.size(); ++k) { double c = std::fabs(off[k] - 2 * off[k - 1] + off[k - 2]); if (c > 0.02) { if (n < 14 && !std::getenv("GY_CLUSTER")) std::printf("  off jump %.3f at %zu (%.4f s, beat %.3f, 16th %.3f, mod64 %zu)\n", c, k, k / 48000.0, k / 24000.0, k / 6000.0, (k - 96) % 64); ++n; } }
                    std::printf("  %d jumps over 0.02\n", n);
                    if (const char *at = std::getenv("GY_AT")) // print the plain preset's samples around a time (s)
                    {
                        size_t c = (size_t)(std::atof(at) * 48000.0);
                        double w = 0; size_t wk = c; for (size_t k = c - 480; k < c + 480 && k + 2 < off.size(); ++k) { double v = std::fabs(off[k + 2] - 2 * off[k + 1] + off[k]); if (v > w) w = v, wk = k + 2; }
                        for (size_t k = wk - 8; k < wk + 8; ++k) std::printf("    %zu (%.5f s): %+.5f\n", k, k / 48000.0, off[k]);
                    }
                    if (std::getenv("GY_CLUSTER")) // group the jumps into bursts: start, length, worst
                    {
                        long last = -100000, st = 0; double w = 0; int cnt = 0;
                        auto flush = [&]() { if (cnt) std::printf("  burst at %.3f s (beat %.2f) for %.1f ms, %d jumps, worst %.3f\n", st / 48000.0, st / 24000.0, (last - st) / 48.0, cnt, w); };
                        for (size_t k = std::max<size_t>(from, 2); k < off.size(); ++k) { double c = std::fabs(off[k] - 2 * off[k - 1] + off[k - 2]); if (c > 0.02) { if ((long)k - last > 480) { flush(); st = (long)k, w = 0, cnt = 0; } last = (long)k, w = std::max(w, c), ++cnt; } }
                        flush();
                    }
                }
                if (std::getenv("GY_WHERE") && name == std::getenv("GY_WHERE"))
                {
                    std::vector<float> d(on.size());
                    for (size_t k = 0; k < on.size() && k < off.size(); ++k) d[k] = on[k] - off[k];
                    size_t at = 0; double m = 0;
                    for (size_t k = std::max<size_t>(from, 2); k < d.size(); ++k) { double c = std::fabs(d[k] - 2 * d[k - 1] + d[k - 2]); if (c > m) m = c, at = k; }
                    std::printf("biggest jump %.4f at sample %zu (%.4f s, beat %.3f, in 1/16ths %.3f)\n", m, at, at / 48000.0, at / 24000.0, at / 6000.0);
                    for (size_t k = at - 6; k < at + 6 && k < d.size(); ++k) std::printf("  %zu: diff %+.5f on %+.5f off %+.5f\n", k, d[k], on[k], off[k]);
                    int n = 0; for (size_t k = 2; k < d.size(); ++k) if (std::fabs(d[k] - 2 * d[k - 1] + d[k - 2]) > 0.006) { if (n < 12) std::printf("  jump at %zu (%.3f s)\n", k, k / 48000.0); ++n; }
                    std::printf("  %d jumps over 0.006\n", n);
                }
                auto pk = [](const std::vector<float> &x) { double m = 0; for (float v : x) m = std::max(m, (double)std::fabs(v)); return m; };
                std::printf("%-22s steady off %.3f/%.3f on %.3f | switch-off %.3f vs never-on %.3f | peak off %.3f on %.3f\n", name.c_str(), click(off), click(off2), click(on), atSw, atRef, pk(off), pk(on));
                continue;
                std::printf("%-22s click off %.3f on %.3f (x%.1f)  switch-off %.3f\n", name.c_str(), click(off), click(on), click(on) / std::max(1e-9, click(off)), atSw);
                continue;
            }
            if (std::string(what) == "anc")
            {
                set({{"Ancestors", 1}});
                load(i);
                std::printf("%-22s Ancestors after load: %.0f\n", name.c_str(), getv("Ancestors"));
                continue;
            }
            const char *pre = getv("Wiring") >= 0.5 ? "Ser " : "Par ";
            std::vector<float> base, again, tw;
            render(base);
            load(i), render(again);
            double noise = diffRel(base, again);
            { double e = 0; for (float x : base) e += (double)x * x; std::printf("%-22s %s mem %.0f rms %.3f repeat %.3f |", name.c_str(), pre, getv("DLD Memory"), std::sqrt(e / std::max<size_t>(1, base.size())), noise); }
            std::vector<std::pair<std::string, double>> tweaks;
            if (std::string(what) == "lead")
                tweaks = {{std::string(pre) + "Lead Level", -18}, {std::string(pre) + "Lead Tone", 0.05}, {std::string(pre) + "Lead Feedback", 0.4}, {std::string(pre) + "Lead Time", 12}, {std::string(pre) + "Lead Drive", 1}, {std::string(pre) + "Lead Pan", -1}};
            else
                tweaks = {{"Tape Drive", 1}, {"Tape Saturation", 1}, {"Tape Bias", 1}, {"Memory Rate", 0.25}, {"Memory Tilt", 1}, {"Memory Noise", 1}, {"DLD Degrade", 1}, {"Loop High Cut", 0}};
            for (auto &t : tweaks)
            {
                load(i);
                EventList e; e.set(0, idOf(t.first.c_str()), t.second); runEvents(p, &e.in); p->reset(p);
                render(tw);
                double d = diffRel(base, tw);
                std::printf(" %s %.2f%s", t.first.substr(t.first.find_last_of(' ') + 1).c_str(), d, d < std::max(0.02, 3 * noise) ? "!" : "");
            }
            std::printf("\n");
        }
        return 0;
    }
    // the default is a plain quarter-note delay: A and B both one beat, in the middle
    {
        reset();
        double a = 0, b = 0, pa = 1, pb = 1;
        params->get_value(p, idOf("Par A Time"), &a), params->get_value(p, idOf("Par B Time"), &b);
        params->get_value(p, idOf("Par A Pan"), &pa), params->get_value(p, idOf("Par B Pan"), &pb);
        std::vector<float> rec;
        int e = firstEcho(p, 60000, &rec);
        if (std::getenv("GY_DEBUG"))
        {
            using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
            auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
            tb->poll(p);
            std::string u = tb->poll(p);
            size_t k = u.find("\"gy\"");
            std::printf("   %s\n", u.substr(k, 160).c_str());
            k = u.find("\"v\":");
            std::printf("   %s\n", u.substr(k, 300).c_str());
        }
        if (std::getenv("GY_DEBUG"))
            for (size_t i = 1300; i < rec.size(); ++i)
                if (std::fabs(rec[i]) > 0.02f) { std::printf("   %zu %.3f\n", i - 1000, rec[i]); i += 2000; }
        check(a == 0 && b == 0 && pa == 0 && pb == 0 && std::abs(e - 24064) < 40, "Init: a plain quarter-note delay", fmt("first echo %d", e));
    }
    // Sync snaps Time to note values: dotted eighth (3/4 beat), quarter triplet (2/3 beat); the text names them
    {
        struct { double st; int want; const char *name; } cases[] = {{-4.98, 18000 + 64, "1/8D"}, {-7.02, 16000 + 64, "1/4T"}, {-5.6, 18000 + 64, "1/8D"}};
        for (auto &c : cases)
        {
            reset();
            set({{"Par Mix", 1}, {"Par A Time", c.st}, {"Par B Time", c.st}});
            runAudio(p, 2, 256, nullptr, nullptr, 0.0f, 0);
            int e = firstEcho(p, 48000);
            char txt[64] = {};
            params->value_to_text(p, idOf("Par A Time"), c.st, txt, sizeof(txt));
            check(std::abs(e - c.want) < 40 && std::string(txt) == c.name, fmt("Sync: %+.2f st plays as %s", c.st, c.name).c_str(), fmt("first echo %d (want %d), shown '%s'", e, c.want, txt));
        }
    }
    // Memory Rate: at 5 kHz the echoes lose their highs (both engines), at 44.1 kHz they are untouched
    for (int slot : {0, 1})
    {
        auto hiEnergy = [&](double rateHz) {
            reset();
            set({{"Engine 1", (double)slot}, {"Par Mix", 1}, {"Memory Rate", std::log2(rateHz)}});
            std::vector<float> rec;
            runAudio(p, 300, 256, nullptr, nullptr, 0.4f, 0, &rec);
            double hi = 0;
            for (size_t i = 60000; i + 1 < rec.size(); ++i)
                hi += (rec[i + 1] - rec[i]) * (rec[i + 1] - rec[i]); // a first difference: the highs
            return hi;
        };
        double full = hiEnergy(44100), low = hiEnergy(5000);
        if (slot == 0)
        {
            // Where: In crushes what goes into the delays, so the echoes lose their highs too; Both as well
            auto at = [&](double place) {
                reset();
                set({{"Par Mix", 1}, {"Memory Rate", std::log2(5000.0)}, {"Crusher Place", place}});
                std::vector<float> rec;
                runAudio(p, 300, 256, nullptr, nullptr, 0.4f, 0, &rec);
                double hi = 0;
                for (size_t i = 60000; i + 1 < rec.size(); ++i)
                    hi += (rec[i + 1] - rec[i]) * (rec[i + 1] - rec[i]);
                return hi;
            };
            double in = at(2), both = at(3);
            check(in < full * 0.35 && both < full * 0.35, "Memory Rate at In and Both", fmt("high energy %.1f: In %.1f, Both %.1f", full, in, both));
        }
        check(full > 0 && low < full * 0.35, fmt("Memory Rate takes the highs off the echoes (%s)", slot ? "Juba" : "DLD").c_str(), fmt("high energy %.3f -> %.3f", full, low));
    }
    // each layout makes sound and stays finite
    struct Layout { const char *name; std::initializer_list<std::pair<const char *, double>> kv; };
    std::vector<std::pair<std::string, std::vector<std::pair<const char *, double>>>> layouts = {
        {"Juba alone", {{"Engine 1", 1}}},
        {"DLD + DLD parallel", {{"Engine 2", 1}}},
        {"DLD + Juba series", {{"Engine 2", 2}, {"Wiring", 1}}},
        {"Juba + DLD spectral", {{"Engine 1", 1}, {"Engine 2", 1}, {"Parallel Split", 1}}},
        {"Call and Response", {{"Shadow", 1}}},
        {"Mirror", {{"Shadow", 2}}},
        {"Juba + Mirror", {{"Engine 1", 1}, {"Shadow", 2}}},
        {"Shadow Stutter", {{"Shadow", 3}, {"Answer Chance", 1}}},
        {"Shadow Freeze", {{"Shadow", 4}, {"Answer Chance", 1}}},
        {"Shadow Octave", {{"Shadow", 5}}},
        {"Juba + Shadow Freeze", {{"Engine 1", 1}, {"Shadow", 4}}},
        {"Ancestors", {{"Ancestors", 1}, {"Ancestors Generations", 2}}},
        {"Juba + Ancestors from B", {{"Engine 1", 1}, {"Ancestors", 1}, {"Ancestors From", 2}}},
    };
    for (auto &L : layouts)
    {
        reset();
        EventList e;
        for (auto &x : L.second)
            e.set(0, idOf(x.first), x.second);
        runEvents(p, &e.in);
        double r = 0;
        int echo = firstEcho(p, 96000, nullptr, &r);
        double pk2 = runAudio(p, 200, 256, nullptr, nullptr, 0.4f, 0);
        check(echo > 0 && pk2 > 0 && pk2 <= 1.05, ("layout: " + L.first).c_str(), fmt("first echo %d, noise peak %.3f, latency %u", echo, pk2, lat->get(p)));
    }
    // Runaway: feedback past 100 % rings on but stays under the ceiling; Hold keeps the loop
    {
        reset();
        set({{"Par A Feedback", 1.2}, {"Par B Feedback", 1.2}, {"Par Lead Drive", 0.6}, {"Runaway Ceiling", -12}, {"Par Mix", 1}, {"Auto Kill", 0}});
        double r = 0;
        std::vector<float> rec;
        firstEcho(p, 48000 * 12, &rec, &r);
        if (std::getenv("GY_DEBUG"))
            for (size_t k = 0; k < rec.size(); k += 24000)
            {
                float m = 0;
                for (size_t j = k; j < std::min(rec.size(), k + 24000); ++j)
                    m = std::max(m, std::fabs(rec[j]));
                std::printf("      runaway t=%.1fs peak %.3f\n", k / 48000.0, m);
            }
        double pk3 = runAudio(p, 400, 256, nullptr, nullptr, 0.0f, 0);
        check(r > 0.001 && pk3 < 0.75, "runaway: self-oscillates, tamed", fmt("tail rms %.4f, peak with no input %.3f", r, pk3));
        set({{"Par A Feedback", 0.3}, {"Par B Feedback", 0.3}, {"Runaway Hold", 1}});
        double pk4 = runAudio(p, 300, 256, nullptr, nullptr, 0.0f, 0);
        check(pk4 > 0.001, "hold keeps the loop going", fmt("peak %.4f", pk4));
    }
    // Hymn's shape: Gate and Decay both make sound, and Gate is quieter than no shape once the input stops
    {
        double r[3];
        for (int k = 0; k < 3; ++k)
        {
            reset();
            set({{"Tune On", 1}, {"Tune Amount", 1}, {"Tune Only Ringing", 1}, {"Par Mix", 0}, {"Hymn Shape", (double)k}, {"Hymn Threshold", -20}, {"Hymn Decay", std::log2(40.0)}});
            runAudio(p, 60, 256, nullptr, nullptr, 0.4f, 0);
            runAudio(p, 10, 256, nullptr, nullptr, 0.0f, 0); // the input stops
            r[k] = 0;
            runAudio(p, 12, 256, nullptr, &r[k], 0.0f, 0); // what rings on
        }
        check(r[1] < r[0] * 0.5 && r[2] < r[0] * 0.5, "Hymn Gate / Decay cut the ringing when the sound stops", fmt("rms after: off %.4f, gate %.4f, decay %.4f", r[0], r[1], r[2]));
    }
    // a tapped pattern: four taps at 100, 250, 400, 700 ms -> the first echo near 100 ms (Sync off)
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        reset();
        set({{"Engine 1", 1}, {"Juba Pattern", 24}, {"Par Mix", 1}, {"Par A Time Sync", 0}, {"Juba Velocity", 1}});
        if (tb)
            tb->send(p, "taps 100 250 400 700");
        runAudio(p, 300, 256, nullptr, nullptr, 0.0f, 0);
        int e = firstEcho(p, 48000 * 2);
        check(std::abs(e - (4800 + 64)) < 300, "tapped pattern plays back as tapped", fmt("first echo %d samples (100 ms = 4800)", e));
        Mem mem;
        clap_ostream_t os{&mem, memWrite};
        state->save(p, &os);
        check(mem.data.find("TP 4 ") != std::string::npos, "tapped pattern saved with the project");
    }
    // the Snapshot sequencer: a stored setting plays over the knobs while it runs, and stops when it's off
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        reset();
        set({{"Par Mix", 1}, {"Par A Level", -60}, {"Par B Level", -60}});
        if (tb)
            tb->send(p, "snap store 1");
        set({{"Par A Level", 0}, {"Par B Level", 0}});
        double knobs = 0, seq = 0, off = 0;
        runAudio(p, 300, 256, nullptr, &knobs, 0.4f, 0);
        set({{"Snap Seq On", 1}, {"Snap Seq Length", 1}, {"Snap Step 1", 1}});
        runAudio(p, 20, 256, nullptr, nullptr, 0.4f, 0);
        runAudio(p, 200, 256, nullptr, &seq, 0.4f, 0);
        set({{"Snap Seq On", 0}});
        runAudio(p, 20, 256, nullptr, nullptr, 0.4f, 0);
        runAudio(p, 200, 256, nullptr, &off, 0.4f, 0);
        check(tb && knobs > 0.05 && seq < knobs * 0.05 && off > knobs * 0.5, "Snapshot sequencer plays S1 over the knobs, and lets go when off",
              fmt("echoes rms: knobs %.4f, S1 playing %.4f, off again %.4f", knobs, seq, off));
        // two snapshots a beat apart: the level alternates with the beat
        set({{"Par A Level", 0}, {"Par B Level", 0}});
        if (tb)
            tb->send(p, "snap store 2");
        set({{"Snap Seq On", 1}, {"Snap Seq Rate", 6}, {"Snap Seq Length", 2}, {"Snap Step 1", 1}, {"Snap Step 2", 2}});
        std::vector<float> rec;
        runAudio(p, 94 * 6, 256, nullptr, nullptr, 0.4f, 0, &rec);
        double beatRms[6] = {};
        for (int b = 1; b < 6; ++b)
        {
            double a2 = 0;
            for (int i = b * 24000 + 2000; i < (b + 1) * 24000 - 2000; ++i)
                a2 += rec[(size_t)i] * rec[(size_t)i];
            beatRms[b] = std::sqrt(a2 / 20000);
        }
        double lo = std::min({beatRms[1], beatRms[2], beatRms[3], beatRms[4]}), hi = std::max({beatRms[1], beatRms[2], beatRms[3], beatRms[4]});
        check(hi > 0.05 && lo < hi * 0.05, "Snapshot sequencer steps with the beat", fmt("rms per beat %.3f %.3f %.3f %.3f", beatRms[1], beatRms[2], beatRms[3], beatRms[4]));
        if (tb)
            tb->send(p, "snap recall 1");
        double lv = 0;
        params->get_value(p, idOf("Par A Level"), &lv);
        check(std::fabs(lv + 60) < 0.5, "Recall sets the knobs to S1", fmt("Par A Level %.1f", lv));
        Mem mem;
        clap_ostream_t os{&mem, memWrite};
        state->save(p, &os);
        check(mem.data.find("SN 1 ") != std::string::npos && mem.data.find("SN 2 ") != std::string::npos, "snapshots saved with the project");
        set({{"Snap Seq On", 0}});
        if (tb)
            tb->send(p, "snap clear 1"), tb->send(p, "snap clear 2");
    }
    // Tape memory (CHOW Tape's hysteresis): stays finite in a runaway, changes the echoes, and its cost
    {
        auto run = [&](double mem, double fb, std::vector<float> *rec, double *secs) {
            reset();
            set({{"Par Mix", 1}, {"DLD Memory", mem}, {"Par A Feedback", fb}, {"Par B Feedback", fb}, {"Tape Drive", 1.0}, {"Tape Saturation", 0.9}});
            auto t0 = std::chrono::steady_clock::now();
            double r = 0;
            runAudio(p, 600, 256, nullptr, &r, 0.5f, 0, rec);
            if (secs)
                *secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return r;
        };
        std::vector<float> a, b;
        double tClean = 0, tTape = 0;
        double rc = run(0, 0.7, &a, &tClean), rt = run(2, 0.7, &b, &tTape);
        double diff = 0, ref = 0;
        for (size_t i = 60000; i < a.size() && i < b.size(); ++i)
            diff += (a[i] - b[i]) * (a[i] - b[i]), ref += a[i] * a[i];
        bool fin = true;
        for (float x : b)
            fin = fin && std::isfinite(x);
        double rr = run(2, 1.2, &b, nullptr);
        for (float x : b)
            fin = fin && std::isfinite(x) && std::fabs(x) < 4.f;
        check(fin && rt > 0.01 && rr > 0.01, "Tape memory: finite and bounded, even running away", fmt("rms clean %.3f, tape %.3f, runaway %.3f", rc, rt, rr));
        check(diff > ref * 0.02, "Tape memory changes the echoes (Drive 100 %)", fmt("difference %.1f %% of the clean energy", 100.0 * diff / std::max(1e-9, ref)));
        std::printf("   (tape cost: %.2f s for 3.2 s of audio vs %.2f s clean)\n", tTape, tClean);
    }
    // the kill switch: a press empties every loop; auto kill ends a runaway after Auto Kill After seconds
    {
        reset();
        set({{"Par A Feedback", 1.2}, {"Par B Feedback", 1.2}, {"Par Lead Drive", 0.6}, {"Par Mix", 1}, {"Auto Kill", 0}});
        firstEcho(p, 48000 * 4);
        double before = runAudio(p, 40, 256, nullptr, nullptr, 0.0f, 0);
        set({{"Kill", 1}});
        set({{"Kill", 0}});
        double after = runAudio(p, 40, 256, nullptr, nullptr, 0.0f, 0);
        check(before > 0.01 && after < 1e-4, "Kill empties a runaway at once", fmt("peak %.3f before, %.6f after", before, after));
        set({{"Auto Kill", 1}, {"Auto Kill After", 5}});
        firstEcho(p, 48000 * 8);
        double tail = runAudio(p, 40, 256, nullptr, nullptr, 0.0f, 0);
        check(tail < 1e-4, "Auto kill ends a runaway after 5 s", fmt("peak afterwards %.6f", tail));
    }
    // Tapo: each pattern keeps its own timing, so changing it moves the echoes
    {
        reset();
        set({{"Engine 1", 1}, {"Juba Pattern", 0}, {"Par Mix", 1}});
        runAudio(p, 300, 256, nullptr, nullptr, 0.0f, 0); // let the pattern's morph finish
        int e0 = firstEcho(p, 48000 * 3);
        reset();
        set({{"Engine 1", 1}, {"Juba Pattern", 12}, {"Par Mix", 1}});
        runAudio(p, 300, 256, nullptr, nullptr, 0.0f, 0);
        int e1 = firstEcho(p, 48000 * 3);
        check(e0 > 0 && e1 > 0 && std::abs(e0 - e1) > 2000, "Juba patterns change the timing", fmt("first tap %d vs %d samples", e0, e1));
    }
    // Hymn only the ringing: the dry sound is replaced (Mix 0 = dry only, so the output is not the input)
    {
        reset();
        set({{"Tune On", 1}, {"Tune Amount", 1}, {"Tune Only Ringing", 1}, {"Par Mix", 0}});
        std::vector<float> rec;
        double r0 = 0;
        runAudio(p, 100, 256, nullptr, &r0, 0.4f, 0, &rec);
        check(r0 < 0.15, "only the ringing: the noise itself is gone", fmt("rms %.3f (noise in: 0.231)", r0));
    }
    // Amount at 100 % is only the ringing too, switch or not, at any Mix
    for (double mixv : {0.0, 0.35, 1.0})
    {
        reset();
        set({{"Tune On", 1}, {"Tune Amount", 1}, {"Tune Place", 1}, {"Par Mix", mixv}});
        double r0 = 0;
        runAudio(p, 100, 256, nullptr, &r0, 0.4f, 0);
        check(r0 < 0.15, fmt("Hymn at 100 %%: only the ringing (Mix %.0f %%)", mixv * 100).c_str(), fmt("rms %.3f (noise in: 0.231)", r0));
    }
    // Spectral: the three band levels at -60 dB silence the echoes
    {
        reset();
        set({{"Engine 2", 1}, {"Parallel Split", 1}, {"Par Mix", 1}, {"Spectral Low Level", -60}, {"Spectral Mid Level", -60}, {"Spectral High Level", -60}});
        double r1 = 0;
        runAudio(p, 300, 256, nullptr, &r1, 0.4f, 0);
        set({{"Spectral Low Level", 0}, {"Spectral Mid Level", 0}, {"Spectral High Level", 0}});
        double r2 = 0;
        runAudio(p, 300, 256, nullptr, &r2, 0.4f, 0);
        check(r1 < 0.01 && r2 > 0.05, "Spectral band levels reach the engines", fmt("rms %.4f at -60 dB, %.4f at 0 dB", r1, r2));
    }
    // each path has its own filter: a low-pass at the bottom on A (Out) takes A's echoes away and leaves B's;
    // on Lead it takes all of them; on B in the loop it takes B's
    {
        int lp = -1;
        clap_id tid = idOf("A Filter Type");
        clap_param_info_t ti{};
        for (uint32_t i = 0; i < params->count(p); ++i)
            if (params->get_info(p, i, &ti) && ti.id == tid)
                break;
        for (int m = 1; m <= (int)ti.max_value && lp < 0; ++m)
        {
            char txt[128] = {};
            if (params->value_to_text(p, tid, m, txt, sizeof(txt)) && (std::strstr(txt, "Low") || std::strstr(txt, "Ladder") || std::strstr(txt, "LP")))
                lp = m;
        }
        check(lp > 0, "a low-pass filter model is in the catalogue", fmt("model %d", lp));
        auto partRms = [&](std::initializer_list<std::pair<const char *, double>> kv, int blocks = 300) {
            reset();
            set({{"Par Mix", 1}, {"Par A Feedback", 0.5}, {"Par B Feedback", 0.5}});
            set(kv);
            double r = 0;
            runAudio(p, blocks, 256, nullptr, &r, 0.4f, 0);
            return r;
        };
        double both = partRms({}), aOnly = partRms({{"Par B Level", -60}}), bOnly = partRms({{"Par A Level", -60}});
        double aCut = partRms({{"Par B Level", -60}, {"A Filter Type", (double)lp}, {"A Filter Cutoff", -60}, {"A Filter Place", 1}});
        double bWithA = partRms({{"Par A Level", -60}, {"A Filter Type", (double)lp}, {"A Filter Cutoff", -60}, {"A Filter Place", 1}});
        double leadCut = partRms({{"Lead Filter Type", (double)lp}, {"Lead Filter Cutoff", -60}, {"Lead Filter Place", 1}});
        // the loop: feed noise, then silence; after the first echo has passed only the repeats are left
        auto tail = [&](std::initializer_list<std::pair<const char *, double>> kv) {
            reset();
            set({{"Par Mix", 1}, {"Par A Level", -60}, {"Par B Feedback", 0.8}});
            set(kv);
            runAudio(p, 200, 256, nullptr, nullptr, 0.4f, 0);
            runAudio(p, 120, 256, nullptr, nullptr, 0.f, 0); // past the first echo
            double r = 0;
            runAudio(p, 200, 256, nullptr, &r, 0.f, 0);
            return r;
        };
        double bLoopOff = tail({}), bLoop = tail({{"B Filter Type", (double)lp}, {"B Filter Cutoff", -60}, {"B Filter Place", 2}});
        double aIn = partRms({{"Par B Level", -60}, {"A Filter Type", (double)lp}, {"A Filter Cutoff", -60}, {"A Filter Place", 0}});
        check(aOnly > 0.01 && bOnly > 0.01 && both > 0.01, "A and B both echo", fmt("A %.4f B %.4f both %.4f", aOnly, bOnly, both));
        check(aCut < aOnly * 0.2, "A's filter (Out) shapes A's echoes", fmt("%.4f -> %.4f", aOnly, aCut));
        check(std::fabs(bWithA - bOnly) < bOnly * 0.1, "A's filter leaves B alone", fmt("B %.4f, with A's filter %.4f", bOnly, bWithA));
        check(leadCut < both * 0.2, "Lead's filter (Out) shapes every echo", fmt("%.4f -> %.4f", both, leadCut));
        check(bLoopOff > 0.01 && bLoop < bLoopOff * 0.1, "B's filter in the loop shapes B's repeats", fmt("tail %.4f -> %.4f", bLoopOff, bLoop));
        check(aIn < aOnly * 0.2, "A's filter (In) shapes what A hears", fmt("%.4f -> %.4f", aOnly, aIn));
    }
    // crusher, filter places, tilt, reverbs, shifts: all finite with noise
    {
        std::vector<std::pair<std::string, std::vector<std::pair<const char *, double>>>> fx = {
            {"crusher in the loop", {{"Crusher Bits", 6}, {"Crusher Mix", 1}, {"Crusher Rate", std::log2(8000.0)}}},
            {"chip mode out", {{"Chip Mode", 1}, {"Crusher Place", 1}, {"Crusher Mix", 1}}},
            {"16-bit degrade 11", {{"DLD Degrade", 11}}},
            {"filter in", {{"Lead Filter Type", 3}, {"Lead Filter Place", 0}, {"Lead Filter Cutoff", 20}}},
            {"filters on every path, every place", {{"A Filter Type", 4}, {"A Filter Place", 2}, {"B Filter Type", 6}, {"B Filter Place", 0}, {"Lead Filter Type", 5}, {"Lead Filter Place", 2}, {"Engine 1", 1}}},
            {"filter in the loop", {{"Lead Filter Type", 3}, {"Lead Filter Place", 2}, {"Lead Filter Cutoff", 30}, {"Lead Filter Reso", 0.8}}},
            {"tilts", {{"Tilt In", 9}, {"Tilt Out", -9}}},
                                                {"shift each repeat", {{"Lead Shift", 12}, {"Lead Shift Mode", 2}, {"Tune On", 1}}},
            {"shift once on Juba", {{"Engine 1", 1}, {"B Shift", 7}, {"B Shift Mode", 1}}},
            {"Tune into the delays", {{"Tune On", 1}, {"Tune Place", 0}}},
            {"reverse and INF", {{"Par A Reverse", 1}, {"Par B INF", 1}}},
            {"everything modulating", {{"Sway", 1}, {"Black Herman Influence", 1}, {"Mutate", 1}, {"LFO 1 Target", 40}, {"LFO 1 Amount", 1},
                                       {"LFO 2 Target", 1}, {"LFO 2 Amount", 1}, {"Mod 1 Source", 9}, {"Mod 1 Target", 7}, {"Mod 1 Amount", 1}, {"Engine 1", 1}}},
        };
        for (auto &F : fx)
        {
            reset();
            EventList e;
            for (auto &x : F.second)
                e.set(0, idOf(x.first), x.second);
            runEvents(p, &e.in);
            double r = 0;
            double pk5 = runAudio(p, 300, 256, nullptr, &r, 0.4f, 0);
            check(pk5 > 0 && pk5 <= 1.05 && r > 1e-4, F.first.c_str(), fmt("peak %.3f rms %.4f", pk5, r));
        }
    }
    // MIDI: Program Change picks a pattern, held notes set the key, a learnt CC moves a knob
    {
        reset();
        set({{"Engine 1", 1}, {"MIDI Picks Patterns", 1}, {"MIDI Sets Key", 1}});
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        MidiList m;
        m.add(0xc0, 5, 0);
        runEvents(p, &m.in);
        runAudio(p, 20, 256, nullptr);
        std::string s1 = tb ? (tb->poll(p), std::string(tb->poll(p))) : "";
        check(s1.find("\"pattern\":5") != std::string::npos, "Program Change 6 picks pattern A6", "");
        MidiList m2;
        m2.add(0x90, 40, 100); // C1..B2 picks patterns: 40 = E1 -> pattern 4
        m2.add(0x90, 64, 100); // E3 sets the key: E
        runEvents(p, &m2.in);
        runAudio(p, 20, 256, nullptr);
        std::string s2 = tb ? (tb->poll(p), std::string(tb->poll(p))) : "";
        check(s2.find("\"pattern\":4") != std::string::npos && s2.find("\"key\":4") != std::string::npos, "notes pick patterns and set the key", "");
        // Learn: arm Crusher Mix, move CC 20, then CC 20 drives it
        if (tb)
        {
            tb->send(p, fmt("learn %u 0", idOf("Crusher Mix")).c_str());
            MidiList a;
            a.add(0xb0, 20, 10);
            runEvents(p, &a.in);
            MidiList b;
            b.add(0xb0, 20, 127);
            outValues = 0;
            runEvents(p, &b.in);
            double v = 0;
            params->get_value(p, idOf("Crusher Mix"), &v);
            check(std::fabs(v - 1.0) < 1e-3 && outValues > 0, "MIDI Learn: CC 20 moves Crusher Mix", fmt("value %.3f, %d told to the host", v, outValues));
            // relative: re-map to two's complement, +5 steps
            tb->send(p, "maps");
            std::string mj = tb->poll(p);
            check(mj.find("Crusher Mix") != std::string::npos, "mapping listed for the window", mj.substr(0, 120));
            tb->send(p, "mapset 0 0 20 3 0 0 0 1");
            MidiList c;
            c.add(0xb0, 20, 123); // -5
            runEvents(p, &c.in);
            params->get_value(p, idOf("Crusher Mix"), &v);
            check(std::fabs(v - (1.0 - 5.0 / 127.0)) < 2e-3, "relative CC steps down", fmt("value %.4f", v));
            // an endless encoder learnt as a plain CC: its first few steps give it away, and it becomes relative
            tb->send(p, fmt("learn %u 0", idOf("Par Mix")).c_str());
            MidiList en0;
            en0.add(0xb0, 30, 1);
            runEvents(p, &en0.in);
            for (int k = 0; k < 6; ++k)
            {
                MidiList st;
                st.add(0xb0, 30, k % 2 ? 127 : 1);
                runEvents(p, &st.in);
            }
            tb->send(p, "maps");
            std::string mjr = tb->poll(p);
            const size_t at = mjr.find("Par Mix");
            check(at != std::string::npos && mjr.find("\"kind\":3", mjr.rfind('{', at)) < mjr.find('}', at), "an endless encoder is found by itself", mjr.substr(mjr.rfind('{', at == std::string::npos ? 0 : at), 90));
            // NRPN learn on Filter Cutoff
            tb->send(p, fmt("learn %u 2", idOf("Lead Filter Cutoff")).c_str());
            MidiList nr;
            nr.add(0xb1, 99, 1), nr.add(0xb1, 98, 2), nr.add(0xb1, 6, 64), nr.add(0xb1, 38, 0);
            runEvents(p, &nr.in);
            MidiList nr2;
            nr2.add(0xb1, 99, 1), nr2.add(0xb1, 98, 2), nr2.add(0xb1, 6, 127), nr2.add(0xb1, 38, 127);
            runEvents(p, &nr2.in);
            params->get_value(p, idOf("Lead Filter Cutoff"), &v);
            check(std::fabs(v - 82) < 0.01, "NRPN (14-bit) learnt and followed", fmt("cutoff %.3f", v));
            // mappings survive a save and load of the project
            Mem mem;
            clap_ostream_t os{&mem, memWrite};
            state->save(p, &os);
            tb->send(p, "mapdel -1");
            mem.pos = 0;
            clap_istream_t is{&mem, memRead};
            state->load(p, &is);
            tb->send(p, "maps");
            std::string mj2 = tb->poll(p);
            check(mj2.find("Crusher Mix") != std::string::npos && mj2.find("Lead Filter Cutoff") != std::string::npos, "mappings saved with the project");
            tb->send(p, "mapdel -1");
        }
    }
    // the 20 factory presets: each loads, names only real controls, and plays
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        setenv("GY_PRESET_CHECK", "1", 1);
        int good = 0;
        std::string bad;
        for (int k = 1; k <= 58 && tb; ++k)
        {
            tb->send(p, fmt("preset %d", k).c_str());
            double r = 0;
            double pk6 = runAudio(p, 400, 256, nullptr, &r, 0.3f, 0);
            if (pk6 > 0 && pk6 <= 1.05 && r > 1e-4)
                ++good;
            else if (bad.empty())
                bad = fmt("#%d peak %.3f rms %.5f", k, pk6, r);
            std::printf("      preset %2d  peak %.3f  rms %.4f  latency %u\n", k, pk6, r, lat->get(p));
        }
        check(good == 58, "58 factory presets play", bad);
        std::string init = tb ? (tb->send(p, "ready"), tb->poll(p)) : "";
        int count = 0;
        for (const char *g : {"Showcase", "Performance", "Warm & Subtle", "Rhythmic", "Lo-fi & Character", "Deep & Spectral", "Moving", "Basic Delays"})
            count += init.find(g) != std::string::npos;
        check(count == 8, "presets come in eight groups");
        {
            // v0.8.1: every factory preset says what it is: no ["name","group",""] entry in the list the window gets
            size_t empties = 0, at = 0;
            while ((at = init.find("\",\"\"]", at)) != std::string::npos) ++empties, ++at;
            check(empties == 0, "every factory preset has a description", fmt("%zu without", empties));
        }
    }
    // state round trip
    {
        set({{"Par A Time", 7}, {"Hymn Hold", 330}, {"Juba Pattern", 17}});
        Mem mem;
        clap_ostream_t os{&mem, memWrite};
        check(state->save(p, &os) && mem.data.rfind("GULLAY ", 0) == 0, "state saves");
        reset();
        mem.pos = 0;
        clap_istream_t is{&mem, memRead};
        double a = 0, b = 0, c = 0;
        bool ok = state->load(p, &is);
        params->get_value(p, idOf("Par A Time"), &a), params->get_value(p, idOf("Hymn Hold"), &b), params->get_value(p, idOf("Juba Pattern"), &c);
        check(ok && std::fabs(a - 7) < 1e-4 && std::fabs(b - 330) < 1e-3 && c == 17, "state loads back");
    }
    // ----- v0.8: no clicks. A steady sine (faded in) through presets that used to click, with and without Ancestors;
    // the biggest jump in the second difference after the start has played through every branch, against a ceiling.
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        tb->send(p, "ready");
        std::string list = tb->poll(p);
        auto indexOf = [&](const char *nm) { std::string key = std::string("[\"") + nm + "\""; size_t a = list.find("\"presets\":["), at = list.find(key, a); if (at == std::string::npos) return -1; int k = 0; for (size_t q = list.find("[\"", a); q < at; q = list.find("[\"", q + 1)) ++k; return k; };
        auto clicks = [&](const char *nm, bool anc) {
            char b[32]; std::snprintf(b, sizeof b, "preset %d", indexOf(nm)); tb->send(p, b);
            EventList e; e.set(0, idOf("Ancestors"), anc ? 1.0 : 0.0), e.set(0, idOf("Ancestors Generations"), 2.0); runEvents(p, &e.in); p->reset(p);
            std::vector<float> rec; gSineAge = 0; runAudio(p, 1500, 256, nullptr, nullptr, -0.3f, 0, &rec);
            double m = 0; for (size_t k = 7 * 48000; k < rec.size(); ++k) m = std::max(m, (double)std::fabs(rec[k] - 2 * rec[k - 1] + rec[k - 2]));
            return m;
        };
        std::string worst; double wv = 0; int n = 0;
        for (const char *nm : {"Clap Line", "Shout Back", "Pocket Shuffle", "Frozen Choir", "Stutter Ghost", "Spectral Field", "Shoreline", "Rising Water", "Tidewater Cloud", "Hymn Bloom"})
            for (bool anc : {false, true})
            {
                double c = clicks(nm, anc);
                ++n;
                if (c > wv) wv = c, worst = std::string(nm) + (anc ? " + Ancestors" : "");
            }
        check(wv < 0.03, "no clicks: Juba time moves, pattern changes, Shadow, Sparkle, Ancestors", fmt("%d runs, worst %.4f (%s)", n, wv, worst.c_str()));
        double st = clicks("Stomp Patterns", false);
        check(st < 0.1, "no bursts when an LFO steps Juba's pattern", fmt("worst %.4f (was 0.64)", st));
        // Ancestors fades out: switch it off in the middle of a run (the change inside the recorded audio)
        tb->send(p, (std::string("preset ") + std::to_string(indexOf("Init"))).c_str());
        { EventList e; e.set(0, idOf("Ancestors"), 1.0); runEvents(p, &e.in); p->reset(p); }
        std::vector<float> rec; gSineAge = 0; runAudio(p, 400, 256, nullptr, nullptr, -0.3f, 0, &rec);
        { EventList ev; ev.set(0, idOf("Ancestors"), 0.0); std::vector<float> t2; runAudio(p, 200, 256, &ev, nullptr, -0.3f, 400 * 256 / 24000.0, &t2); rec.insert(rec.end(), t2.begin(), t2.end()); }
        double sw = 0; for (size_t k = 400 * 256; k < 400 * 256 + 4800; ++k) sw = std::max(sw, (double)std::fabs(rec[k] - 2 * rec[k - 1] + rec[k - 2]));
        check(sw < 0.01, "Ancestors fades out when switched off", fmt("biggest jump %.4f", sw));
    }
    // ----- v0.8: Varispeed in Host sync (it only worked with Sync on Free): a time change bends instead of jumping
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        auto bendRun = [&](bool vari, double *slowest) {
            reset();
            set({{"Sync", 0}, {"Varispeed", vari ? 1.0 : 0.0}, {"Varispeed Bend Time", std::log2(800.0)}, {"Par A Feedback", 0.5}, {"Par Mix", 1.0}});
            std::vector<float> rec;
            gSineAge = 0; runAudio(p, 200, 256, nullptr, nullptr, -0.3f, 0, &rec);
            EventList e; e.set(0, idOf("Par A Time"), 12.0); // twice as long
            std::vector<float> t2; runAudio(p, 40, 256, &e, nullptr, -0.3f, 200 * 256 / 24000.0, &t2);
            tb->send(p, "ready"); tb->poll(p);
            std::string u = tb->poll(p);
            size_t at = u.find("\"vari\":[");
            *slowest = at == std::string::npos ? -1 : std::atof(u.c_str() + at + 8);
            std::vector<float> t3; runAudio(p, 160, 256, nullptr, nullptr, -0.3f, 240 * 256 / 24000.0, &t3);
            rec.insert(rec.end(), t2.begin(), t2.end()), rec.insert(rec.end(), t3.begin(), t3.end());
            return rec;
        };
        double s0 = 0, s1 = 0;
        auto a = bendRun(false, &s0), b = bendRun(true, &s1);
        double d = 0, e = 0;
        for (size_t k = 200 * 256; k < a.size() && k < b.size(); ++k) d += (a[k] - b[k]) * (a[k] - b[k]), e += (double)a[k] * a[k];
        check(e > 0 && std::sqrt(d / e) > 0.2, "Varispeed bends a time change in Host sync", fmt("difference %.2f", e > 0 ? std::sqrt(d / e) : 0.0));
        check(s1 > 0.0 && s1 < 0.98, "the tape-speed meter shows the bend", fmt("A's tape speed %.3f (steady 1)", s1));
    }
    // ----- v0.8: a snapshot is the whole sound. The knobs are Juba with Ancestors on and a longer A; a sequence that plays
    // Init as a snapshot must sound like Init (Ancestors off, Echo back), not keep what the knobs had
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        auto render = [&]() { std::vector<float> r; gSineAge = 0; runAudio(p, 400, 256, nullptr, nullptr, -0.3f, 0, &r); return r; };
        tb->send(p, "preset 0"); { EventList e; runEvents(p, &e.in); } p->reset(p);
        auto ref = render();
        tb->send(p, "preset 0");
        set({{"Engine 1", 1}, {"Ancestors", 1}, {"Ancestors Generations", 2}, {"Par A Time", 6}});
        tb->send(p, "snap preset 1 0");
        {
            EventList e;
            e.set(0, idOf("Snap Seq On"), 1.0);
            for (int k = 1; k <= 16; ++k)
                e.set(0, idOf(("Snap Step " + std::to_string(k)).c_str()), 1.0);
            runEvents(p, &e.in);
        }
        p->reset(p);
        auto seq = render();
        tb->send(p, "snap clear 1"); set({{"Snap Seq On", 0}});
        auto knobs = render();
        auto rel = [](const std::vector<float> &a, const std::vector<float> &b) { double d = 0, e = 0; for (size_t k = 48000; k < a.size() && k < b.size(); ++k) d += (a[k] - b[k]) * (a[k] - b[k]), e += (double)a[k] * a[k]; return e > 0 ? std::sqrt(d / e) : 9.0; };
        check(rel(ref, seq) < 0.05 && rel(ref, knobs) > 0.3, "a preset played as a snapshot brings its whole sound (engine, Ancestors off)", fmt("vs Init: snapshot %.3f, the knobs %.3f", rel(ref, seq), rel(ref, knobs)));
    }
    // ----- v0.8: the Performance presets bring their snapshots and steps. Tape Relay (preset 11) fills snapshots 1-4 and
    // empties the rest; its sequence changes the sound bar by bar (against the same preset with the sequencer off);
    // a preset without snapshots leaves them alone
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        auto saved = [&]() { Mem mem; clap_ostream_t os{&mem, memWrite}; state->save(p, &os); return mem.data; };
        auto has = [](const std::string &t, int k) { return t.find("\nSN " + std::to_string(k) + " ") != std::string::npos; };
        if (!std::getenv("GY_SKIP_PERF")) {
        tb->send(p, "snap preset 6 0");
        tb->send(p, "preset 11"); { EventList e; runEvents(p, &e.in); }
        std::string t = saved();
        check(has(t, 1) && has(t, 2) && has(t, 3) && has(t, 4) && !has(t, 5) && !has(t, 6), "a Performance preset brings snapshots 1-4 and clears the rest");
        auto render = [&]() { std::vector<float> r; gSineAge = 0; runAudio(p, 1000, 256, nullptr, nullptr, -0.3f, 0, &r); return r; };
        p->reset(p); auto seq = render();
        set({{"Snap Seq On", 0}}); p->reset(p); auto still = render();
        double d = 0, e = 0; for (size_t k = 96000; k < seq.size() && k < still.size(); ++k) d += (seq[k] - still[k]) * (seq[k] - still[k]), e += (double)still[k] * still[k];
        double rel = e > 0 ? std::sqrt(d / e) : 0;
        check(rel > 0.2, "its snapshot sequence changes the sound as it plays", fmt("difference %.3f", rel));
        // each of the three: what it names is audible. Its sequence, Reshape and the Memory each change the sound
        // (against the same preset with that one part off; 6 s of noise). The snapshots hold Reshape and the Memory
        // too, so those two are compared with the sequencer off
        const char *names[3] = {"Tape Relay", "Loose Clave", "Tide Turns"};
        int nOnly = std::getenv("GY_PERF_N") ? std::atoi(std::getenv("GY_PERF_N")) : 3;
        for (int n = 0; n < nOnly; ++n)
        {
            auto load = [&]() { tb->send(p, fmt("preset %d", 11 + n).c_str()); EventList e3; runEvents(p, &e3.in); p->reset(p); };
            auto diff = [](const std::vector<float> &a, const std::vector<float> &b) { double d = 0, e = 0; for (size_t k = 96000; k < a.size() && k < b.size(); ++k) d += (a[k] - b[k]) * (a[k] - b[k]), e += (double)b[k] * b[k]; return e > 0 ? std::sqrt(d / e) : 0.0; };
            // noise, not the sine: some of Juba's patterns band-pass their taps (a wood block), which a steady 220 Hz misses
            auto r6 = [&]() { std::vector<float> r; runAudio(p, 1100, 256, nullptr, nullptr, 0.3f, 0, &r); return r; };
            auto rmsOf = [](const std::vector<float> &v) { double e = 0; for (size_t k = 96000; k < v.size(); ++k) e += (double)v[k] * v[k]; return v.size() > 96000 ? std::sqrt(e / (double)(v.size() - 96000)) : 0.0; };
            load(); auto all = r6();
            load(); set({{"Snap Seq On", 0}}); p->reset(p); auto noSeq = r6();
            load(); set({{"Snap Seq On", 0}, {"Reshape", 0}}); p->reset(p); auto noRe = r6();
            // the Memory, all of it: the DLD's memory clean, no sample-rate drop, hiss, tilt or bit crush
            load(); set({{"Snap Seq On", 0}, {"DLD Memory", 0}, {"Memory Rate", std::log2(44100.0)}, {"Memory Noise", 0}, {"Memory Tilt", 0}, {"Crusher Bits", 16}, {"Crusher Mix", 0}}); p->reset(p); auto noMem = r6();
            double a1 = diff(all, noSeq), a2 = diff(noSeq, noRe), a3 = diff(noSeq, noMem);
            check(a1 > 0.1 && a2 > 0.1 && a3 > 0.05, fmt("%s: its sequence, Reshape and the Memory are each audible", names[n]).c_str(), fmt("sequence %.3f, Reshape %.3f, Memory %.3f (rms %.4f / %.4f)", a1, a2, a3, rmsOf(all), rmsOf(noSeq)));
        }
        tb->send(p, "preset 11"); tb->send(p, "preset 14"); { EventList e2; runEvents(p, &e2.in); }
        t = saved();
        check(has(t, 1) && has(t, 4), "a preset without snapshots keeps the project's");
        for (int k = 1; k <= 8; ++k) tb->send(p, fmt("snap clear %d", k).c_str());
        tb->send(p, "preset 0");
        }
    }
    // ----- v0.8.1: Reshape's knobs are modulation targets. LFO 1 (a sine, 1/4) on Reshape High Level, 100 %: the
    // output moves against the same settings with the route off
    {
        int tHigh = -1; double tMax = 0;
        params->get_value(p, idOf("Mod 1 Target"), &tMax);
        clap_param_info_t inf{}; for (uint32_t i = 0; i < params->count(p); ++i) { params->get_info(p, i, &inf); if (inf.id == (clap_id)idOf("Mod 1 Target")) tMax = inf.max_value; }
        for (int v = 0; v <= (int)tMax; ++v) { char t[64] = ""; params->value_to_text(p, idOf("Mod 1 Target"), v, t, sizeof(t)); if (std::string(t) == "Reshape High Level") tHigh = v; }
        auto render = [&](bool on) { reset(); set({{"Reshape", 1}, {"Reshape High Level", -12}, {"Par Mix", 1.0}, {"LFO 1 Shape", 1}, {"Mod 1 Source", on ? 4 : 0}, {"Mod 1 Target", (double)tHigh}, {"Mod 1 Amount", 1.0}}); p->reset(p);
            std::vector<float> r; runAudio(p, 600, 256, nullptr, nullptr, 0.3f, 0, &r); return r; };
        auto a1 = render(true), a0 = render(false);
        double d = 0, e = 0; for (size_t k = 48000; k < a1.size() && k < a0.size(); ++k) d += (a1[k] - a0[k]) * (a1[k] - a0[k]), e += (double)a0[k] * a0[k];
        double rel = e > 0 ? std::sqrt(d / e) : 0;
        check(tHigh > 0 && rel > 0.1, "Reshape's knobs are modulation targets (an LFO on High Level moves the sound)", fmt("target %d, difference %.3f", tHigh, rel));
        reset();
    }
    // ----- v0.8.1 Shadow: the four new modes each change B (against Shadow Off), and the trigger counts: with no
    // calls (Mutation off) Ghost on "Lead calls" stays shut, on "Juba taps" it opens on the pattern's rhythm
    {
        auto render = [&](int mode, int trig) { reset(); set({{"Shadow", (double)mode}, {"Shadow Trigger", (double)trig}, {"Par Mix", 1.0}, {"Par A Feedback", 0.5}, {"Answer Chance", 1.0}}); p->reset(p);
            std::vector<float> r; runAudio(p, 900, 256, nullptr, nullptr, 0.3f, 0, &r); return r; };
        auto diff = [](const std::vector<float> &a, const std::vector<float> &b) { double d = 0, e = 0; for (size_t k = 48000; k < a.size() && k < b.size(); ++k) d += (a[k] - b[k]) * (a[k] - b[k]), e += (double)b[k] * b[k]; return e > 0 ? std::sqrt(d / e) : 0.0; };
        auto rms = [](const std::vector<float> &a) { double e = 0; for (size_t k = 48000; k < a.size(); ++k) e += (double)a[k] * a[k]; return std::sqrt(e / std::max<size_t>(1, a.size() - 48000)); };
        auto off = render(1, 0); // Answer as the reference: B plays A's echoes as they are
        const char *nm[4] = {"Swell", "Smear", "Ghost", "Ping-pong"};
        std::string got; bool ok = true;
        for (int m = 6; m <= 9; ++m) { auto v = render(m, 0); double d = diff(v, off); bool bad = !(d > 0.1) || !std::isfinite(rms(v)) || rms(v) > 1.0; ok = ok && !bad; got += fmt("%s %.2f  ", nm[m - 6], d); }
        check(ok, "Shadow's new modes (Swell, Smear, Ghost, Ping-pong) each change B", got);
        auto aOnly = [&]() { reset(); set({{"Shadow", 8.0}, {"Par B Mute", 1}, {"Par Mix", 1.0}, {"Par A Feedback", 0.5}, {"Answer Chance", 1.0}}); p->reset(p);
            std::vector<float> r; runAudio(p, 900, 256, nullptr, nullptr, 0.3f, 0, &r); return r; }(); // A alone (B muted)
        auto calls = render(8, 2), juba = render(8, 1);
        double dc = diff(calls, aOnly), dj = diff(juba, aOnly);
        check(dc < 0.05 && dj > 0.1, "the Shadow fires on what it is set to: no Lead calls, no Ghost; Juba's taps open it", fmt("Lead calls %.3f, Juba taps %.3f (against A alone)", dc, dj));
        reset();
    }
    // ----- v0.8.1 River forks: with Forks on (Chance 100 %) River A takes side channels at the cities, so what it moves
    // (A's level here) differs from the plain river; at Chance 0 % it is the plain river again
    {
        auto render = [&](double forks, double chance) { reset(); set({{"River Pass", 0}, {"River Forks", forks}, {"Fork Chance", chance}, {"Fork Wildness", 0.8},
            {"Mod 1 Source", 8}, {"Mod 1 Target", 5}, {"Mod 1 Amount", 1.0}, {"Par Mix", 1.0}}); p->reset(p);
            std::vector<float> r; runAudio(p, 1500, 256, nullptr, nullptr, 0.3f, 0, &r); return r; };
        auto diff = [](const std::vector<float> &a, const std::vector<float> &b) { double d = 0, e = 0; for (size_t k = 48000; k < a.size() && k < b.size(); ++k) d += (a[k] - b[k]) * (a[k] - b[k]), e += (double)b[k] * b[k]; return e > 0 ? std::sqrt(d / e) : 0.0; };
        auto plain = render(0, 1), forked = render(1, 1), never = render(1, 0);
        double d1 = diff(forked, plain), d0 = diff(never, plain);
        check(d1 > 0.1 && d0 < 1e-6, "River forks: at Chance 100 % the currents detour at the cities; at 0 % the river is unchanged", fmt("forked %.3f, chance 0 %.6f", d1, d0));
        reset();
    }
    // ----- v0.8.1: the 15 Deep & Spectral additions are long (6 s after 2 s of noise stops, the tail is still there)
    // and evolve (with steady noise in, the output's level over 2-s windows keeps changing)
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        int shortOnes = 0, stillOnes = 0; std::string log;
        for (int k = 0; k < 15; ++k)
        {
            const int idx = std::getenv("GY_DEEP_FIRST") ? std::atoi(std::getenv("GY_DEEP_FIRST")) + k : 28 + k;
            tb->send(p, fmt("preset %d", idx).c_str()); { EventList e; runEvents(p, &e.in); } p->reset(p);
            std::vector<float> a, b;
            runAudio(p, 375, 256, nullptr, nullptr, 0.3f, 0, &a);    // 2 s of noise
            runAudio(p, 1125, 256, nullptr, nullptr, 0.f, 0, &b);    // 6 s of nothing
            double t = 0; for (size_t i = b.size() * 2 / 3; i < b.size(); ++i) t += (double)b[i] * b[i]; t = std::sqrt(t / std::max<size_t>(1, b.size() / 3));
            tb->send(p, fmt("preset %d", idx).c_str()); { EventList e; runEvents(p, &e.in); } p->reset(p);
            std::vector<float> c; runAudio(p, 3750, 256, nullptr, nullptr, 0.3f, 0, &c); // 20 s of steady noise
            // per 2-s window (left channel): its level, and its tone (the lows under 300 Hz against the rest). Evolving =
            // either moves by 1 dB or more over the 20 s
            std::vector<double> lv, tl; double lp = 0; const double ka = 1.0 - std::exp(-6.2831853 * 300.0 / 48000.0);
            for (size_t i = 0; i + 96000 <= c.size(); i += 96000)
            {
                double el = 0, eh = 0;
                for (size_t j = i; j < i + 96000; ++j) { lp += ka * (c[j] - lp); el += lp * lp; eh += (c[j] - lp) * (c[j] - lp); }
                if (i >= 96000 * 2) lv.push_back(10 * std::log10((el + eh) / 96000 + 1e-12)), tl.push_back(10 * std::log10((el + 1e-9) / (eh + 1e-9)));
            }
            auto range = [](const std::vector<double> &v) { double lo = 1e9, hi = -1e9; for (double x : v) lo = std::min(lo, x), hi = std::max(hi, x); return hi - lo; };
            double mv = std::max(range(lv), range(tl));
            if (t < 0.003) ++shortOnes; if (mv < 1.0) ++stillOnes;
            log += fmt("%d: tail %.3f, level %.1f dB, tone %.1f dB; ", idx, t, range(lv), range(tl));
        }
        if (std::getenv("GY_DEEP_LOG")) std::printf("   %s\n", log.c_str());
        check(shortOnes == 0 && stillOnes == 0, "the 15 new Deep & Spectral presets are long and keep moving", fmt("%d short, %d still", shortOnes, stillOnes));
        tb->send(p, "preset 0");
        for (int k = 1; k <= 8; ++k) tb->send(p, fmt("snap clear %d", k).c_str());
    }
    // ----- v0.8.1: Kill from the window is a command (it never misses), and any kill turns off what would fill the
    // loops again: Oscillate, INF, Hold All. Then the output falls silent. Tested from the command and the parameter.
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        for (int way = 0; way < 2; ++way)
        {
            reset();
            set({{"Oscillate", 1}, {"Par A INF", 1}, {"Par Lead Hold All", 1}, {"Par A Feedback", 0.9}});
            runAudio(p, 300, 256, nullptr, nullptr, 0.3f, 0);          // fill the loops
            if (way == 0) tb->send(p, "kill");
            else { EventList e; e.set(0, idOf("Kill"), 1.0); runEvents(p, &e.in); }
            runAudio(p, 2, 256, nullptr, nullptr, 0.f, 0);              // one block takes the kill
            if (way == 1) { EventList e; e.set(0, idOf("Kill"), 0.0); runEvents(p, &e.in); }
            double o = 1, a = 1, h = 1, tail = 0;
            params->get_value(p, idOf("Oscillate"), &o), params->get_value(p, idOf("Par A INF"), &a), params->get_value(p, idOf("Par Lead Hold All"), &h);
            runAudio(p, 200, 256, nullptr, &tail, 0.f, 0);              // no input: what is left must be silence
            check(o == 0 && a == 0 && h == 0 && tail < 1e-4, way == 0 ? "Kill (the window's command) empties the loops and turns off Oscillate, INF, Hold All" : "Kill (the parameter) does the same",
                  fmt("Oscillate %g, A INF %g, Hold All %g, rms after %.6f", o, a, h, tail));
        }
        reset();
    }
    // ----- v0.8 (brief item 10): a step's chance is automatable from 1 to 100 %, shows as a percentage, and counts.
    // Every step plays S1 (Init, Output 0 dB) over knobs at Output -24 dB: at 100 % chance the snapshot plays (louder);
    // automated down to 1 % the steps fail and the knobs play (quieter)
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        reset();
        tb->send(p, "snap preset 1 0");
        auto withChance = [&](double ch) {
            EventList e;
            e.set(0, idOf("Par Output"), -24.0), e.set(0, idOf("Snap Seq On"), 1.0), e.set(0, idOf("Snap Seq Rate"), 6.0);
            for (int k = 1; k <= 16; ++k)
                e.set(0, idOf(("Snap Step " + std::to_string(k)).c_str()), 1.0), e.set(0, idOf(("Snap Step " + std::to_string(k) + " Chance").c_str()), ch);
            runEvents(p, &e.in); p->reset(p);
            double r = 0; runAudio(p, 400, 256, nullptr, &r, 0.3f, 0); return r;
        };
        double v = 0, rare = withChance(0.01); // rare first: Hold last would keep S1 once it has played
        params->get_value(p, idOf("Snap Step 3 Chance"), &v);
        double full = withChance(1.0);
        char lo[32] = "", hi[32] = "";
        params->value_to_text(p, idOf("Snap Step 3 Chance"), 0.01, lo, sizeof(lo)), params->value_to_text(p, idOf("Snap Step 3 Chance"), 1.0, hi, sizeof(hi));
        check(std::fabs(v - 0.01) < 1e-6 && std::string(lo) == "1 %" && std::string(hi) == "100 %" && full > 1.3 * rare,
              "a step's chance follows automation (1-100 %) and decides whether the step plays", fmt("value %.2f, shown %s..%s; rms at 100 %% %.3f, at 1 %% %.3f", v, lo, hi, full, rare));
        tb->send(p, "snap clear 1"); reset();
    }
    // ----- v0.8: each filter's Send (how much goes in) and Mix (how much of the result). A low-pass at 100 Hz on A's echoes
    {
        auto wetRms = [&](double send, double mix) {
            reset(); set({{"Par Mix", 1.0}, {"Par A Feedback", 0.0}, {"Par B Level", -60}, {"A Filter Type", 1}, {"A Filter Cutoff", -24}, {"A Filter Send", send}, {"A Filter Mix", mix}});
            double r = 0; gSineAge = 0; runAudio(p, 400, 256, nullptr, &r, -0.3f, 0); return r;
        };
        double none = wetRms(0, 1), full = wetRms(1, 1), half = wetRms(0.5, 1), dry = wetRms(1, 0), mixHalf = wetRms(1, 0.5);
        check(full < 0.3 * none && half > 1.3 * full && half < 0.9 * none && std::fabs(dry - none) < 0.02 * none && mixHalf > 1.3 * full,
              "a filter's Send and Mix each blend it with what passes by", fmt("send 0 %.3f, 0.5 %.3f, 1 %.3f; mix 0 %.3f, 0.5 %.3f", none, half, full, dry, mixHalf));
    }
    // ----- v0.8 Juba's pattern editor: "custom" sets each tap's time, level and side; it is kept with the project
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        auto twoTaps = [&](std::vector<float> *rec) {
            reset(); set({{"Engine 1", 1}, {"Juba Pattern", 24}, {"Par Mix", 1.0}, {"Par A Feedback", 0.0}, {"Par Lead Feedback", 0.0}, {"Juba Velocity", 0.0}, {"Par A Time Sync", 0.0}});
            return firstEcho(p, 120000, rec);
        };
        tb->send(p, "custom 1 1 0.2 2 0.3 0.8");
        std::vector<float> rec;
        int e = twoTaps(&rec);
        // the second tap, about a beat after the first: its peak against the first's
        double p1 = 0, p2 = 0;
        for (int k = std::max(0, e - 200); k < e + 2000 && k < (int)rec.size(); ++k) p1 = std::max(p1, (double)std::fabs(rec[(size_t)k]));
        for (int k = e + 20000; k < e + 28000 && k < (int)rec.size(); ++k) p2 = std::max(p2, (double)std::fabs(rec[(size_t)k]));
        check(e > 0 && p1 > 0 && p2 > 0.01 * p1 && p2 < 0.5 * p1, "your pattern plays its taps at their levels", fmt("first echo at %d, second at %.0f%% of the first", e, p1 > 0 ? 100 * p2 / p1 : 0.0));
        Mem mem; clap_ostream_t os{&mem, memWrite}; state->save(p, &os);
        if (std::getenv("GY_DEBUG_TP")) { std::string all = mem.data; size_t a = all.find("\nTP "); std::printf("saved: %s\n", a == std::string::npos ? "(no TP)" : all.substr(a + 1, all.find('\n', all.find("\nTV") + 1) - a).c_str()); }
        tb->send(p, "custom 1 1 0.5"); // something else, then load the project back
        mem.pos = 0; clap_istream_t is{&mem, memRead}; state->load(p, &is);
        if (std::getenv("GY_DEBUG_TP")) { tb->poll(p); tb->send(p, "pats"); std::string u = tb->poll(p); size_t a = u.find("S.pats="); int depth = 0; size_t k = a + 7, start = 0; int idx = -1;
            for (; k < u.size(); ++k) { if (u[k] == '[') { ++depth; if (depth == 2) { ++idx; if (idx == 24) start = k; } } else if (u[k] == ']') { --depth; if (depth == 1 && idx == 24) break; } }
            std::printf("after load, pattern 24: %s\n", start ? u.substr(start, k - start + 1).c_str() : "?"); }
        std::vector<float> rec2; int e2 = twoTaps(&rec2);
        double q2 = 0; for (int k = e2 + 20000; k < e2 + 28000 && k < (int)rec2.size(); ++k) q2 = std::max(q2, (double)std::fabs(rec2[(size_t)k]));
        check(std::fabs(q2 - p2) < 0.05 * std::max(p2, 1e-6) + 1e-4, "your pattern's levels come back with the project", fmt("second tap %.4f then %.4f (first echo at %d, then %d)", p2, q2, e, e2));
    }
    // ----- v0.8: every modulation target is tied to the setting it moves (for the indicators on the controls)
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        tb->poll(p); tb->send(p, "targetmap");
        std::string u = tb->poll(p), missing;
        int n = 0;
        for (size_t a = u.find("[\""); a != std::string::npos; a = u.find("[\"", a + 1))
        {
            size_t e1 = u.find('"', a + 2), b = u.find('"', e1 + 1), e2 = u.find('"', b + 1);
            ++n;
            const std::string nm = u.substr(a + 2, e1 - a - 2);
            if (e2 == b + 1 && nm.rfind("Reverb ", 0) != 0) // the reverb targets are TC's, never offered in Gullay's window
                missing += (missing.empty() ? "" : ", ") + nm;
        }
        check(n > 50 && missing.empty(), "every modulation target is tied to a setting", missing.empty() ? fmt("%d targets", n) : "not found: " + missing);
    }
    // ----- v0.8 Reshape: the echoes in three bands after the engines
    {
        auto wetRms = [&](std::initializer_list<std::pair<const char *, double>> kv) {
            reset(); set({{"Par Mix", 1.0}, {"Par A Feedback", 0.3}}); set(kv);
            double r = 0; gSineAge = 0; runAudio(p, 400, 256, nullptr, &r, -0.3f, 0); return r;
        };
        double plain = wetRms({{"Reshape", 0}}), flat = wetRms({{"Reshape", 1}}), noLow = wetRms({{"Reshape", 1}, {"Reshape Low/Mid", std::log2(1000.0)}, {"Reshape Low Level", -60}}), noHigh = wetRms({{"Reshape", 1}, {"Reshape Low/Mid", std::log2(1000.0)}, {"Reshape High Level", -60}});
        check(std::fabs(20 * std::log10(flat / plain)) < 0.5, "Reshape at its defaults leaves the level alone", fmt("%.2f dB", 20 * std::log10(flat / plain)));
        check(noLow < 0.25 * plain && noHigh > 0.8 * plain, "Reshape's Low level takes a 220 Hz tone out of the echoes, High doesn't", fmt("plain %.3f, low off %.3f, high off %.3f", plain, noLow, noHigh));
        reset(); set({{"Par Mix", 1.0}, {"Reshape", 1}, {"Reshape Low Pan", -1}, {"Reshape Low Tone", -12}});
        std::vector<float> rec; gSineAge = 0; runAudio(p, 300, 256, nullptr, nullptr, -0.3f, 0, &rec);
        { EventList ev; ev.set(0, idOf("Reshape"), 0.0); std::vector<float> t2; runAudio(p, 100, 256, &ev, nullptr, -0.3f, 300 * 256 / 24000.0, &t2); rec.insert(rec.end(), t2.begin(), t2.end()); }
        double sw = 0; for (size_t k = 300 * 256; k < 300 * 256 + 4800; ++k) sw = std::max(sw, (double)std::fabs(rec[k] - 2 * rec[k - 1] + rec[k - 2]));
        check(sw < 0.01, "Reshape fades out when switched off", fmt("biggest jump %.4f", sw));
    }
    // ----- v0.7 Ancestors: off changes nothing; on, each echo has children later on; On Lead's calls with no calls stays quiet
    {
        auto tail = [&](std::initializer_list<std::pair<const char *, double>> kv, double *peakOut) {
            reset();
            set({{"Par A Feedback", 0}, {"Par B Feedback", 0}, {"Par Mix", 1}, {"Auto Kill", 0}});
            set(kv);
            runAudio(p, 20, 256, nullptr, nullptr, 0.3f, 0); // a short burst
            runAudio(p, 160, 256, nullptr, nullptr, 0.f, 0); // the echo itself (500 ms) goes by
            double r = 0;
            double pk = runAudio(p, 500, 256, nullptr, &r, 0.f, 0); // 0.96 s to 3.6 s: only the tree is left
            if (peakOut)
                *peakOut = pk;
            return r;
        };
        double pkOn = 0;
        double off = tail({{"Ancestors", 0}}, nullptr), on = tail({{"Ancestors", 1}, {"Ancestors Generations", 2}}, &pkOn);
        // "Runaway catches" can't fire with no feedback: no call, so no tree; "Lead gets louder" fires on the burst: a tree
        double calls = tail({{"Ancestors", 1}, {"Ancestors Generations", 2}, {"Ancestors Grow", 1}, {"Mutation Trigger", 2}}, nullptr);
        double called = tail({{"Ancestors", 1}, {"Ancestors Generations", 2}, {"Ancestors Grow", 1}, {"Mutation Trigger", 0}}, nullptr);
        check(called > on * 0.5, "Ancestors On Lead's calls grows on a call", fmt("tail rms %.5f", called));
        check(off < 1e-5 && on > 2e-4 && pkOn > 0.05 && pkOn <= 1.05, "Ancestors grows later echoes from each echo", fmt("tail rms off %.6f, on %.5f (peak %.3f)", off, on, pkOn));
        check(calls < on * 0.05, "Ancestors On Lead's calls waits for a call", fmt("tail rms %.6f", calls));
        double g1 = tail({{"Ancestors", 1}, {"Ancestors Generations", 0}}, nullptr);
        check(g1 < on, "more generations, more family", fmt("1 gen %.5f, 3 gens %.5f", g1, on));
    }
    // ----- v0.6: Shadow's Freeze holds a pad after the input stops; Off lets it die away
    {
        double held[2] = {0, 0};
        for (int k = 0; k < 2; ++k)
        {
            reset();
            set({{"Shadow", k ? 4.0 : 0.0}, {"Answer Chance", 1}, {"Answer After", 3}, {"Mirror Time", 1}, {"Par A Feedback", 0}, {"Par B Feedback", 0}, {"Par Mix", 1}, {"Auto Kill", 0}});
            runAudio(p, 400, 256, nullptr, nullptr, 0.3f, 0);
            runAudio(p, 200, 256, nullptr, nullptr, 0.f, 0);
            double r = 0;
            runAudio(p, 200, 256, nullptr, &r, 0.f, 0);
            held[k] = r;
        }
        check(held[0] < 1e-4 && held[1] > 1e-3, "Shadow Freeze holds A as a pad", fmt("tail rms off %.6f, freeze %.5f", held[0], held[1]));
    }
    // ----- v0.6: the preset name and the window's state are kept with the project
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        if (tb) {
        tb->send(p, "pname Wire Test");
        tb->send(p, "uistate {\"page\":\"tune\"}");
        tb->send(p, "theme eva01flip");
        Mem mem;
        clap_ostream_t os{&mem, memWrite};
        state->save(p, &os);
        tb->send(p, "pname Other");
        tb->send(p, "theme porch");
        mem.pos = 0;
        clap_istream_t is{&mem, memRead};
        state->load(p, &is);
        tb->send(p, "ready");
        std::string u = tb->poll(p);
        check(u.find("\"pname\":\"Wire Test\"") != std::string::npos && u.find("page") != std::string::npos && u.find("\"theme\":\"eva01flip\"") != std::string::npos,
              "preset name, window state and theme come back", u.substr(0, 0));
        }
    }
    // ----- v0.5 features
    {
        double r1 = 0, r0 = 0, pkv = 0;
        // Memory Noise: a hiss floor on the echoes even with nothing coming in
        reset();
        set({{"Par Mix", 1.0}, {"Crusher Place", 1}, {"Memory Noise", 0.0}});
        runAudio(p, 200, 256, nullptr, &r0, 0.f, 0);
        set({{"Memory Noise", 1.0}});
        runAudio(p, 200, 256, nullptr, &r1, 0.f, 0);
        check(r0 < 1e-5 && r1 > 1e-3, "Memory Noise adds a hiss floor", fmt("rms %.6f -> %.5f", r0, r1));
        // Memory Tilt: changes the crushed sound, stays finite
        reset();
        set({{"Crusher Bits", 6}, {"Crusher Mix", 1.0}, {"Crusher Place", 1}, {"Memory Tilt", 0.0}, {"Par Mix", 1.0}});
        runAudio(p, 300, 256, nullptr, &r0, 0.3f, 0);
        set({{"Memory Tilt", 1.0}});
        pkv = runAudio(p, 300, 256, nullptr, &r1, 0.3f, 0);
        check(pkv > 0 && std::fabs(r1 - r0) > 1e-4, "Memory Tilt changes the crush", fmt("rms %.4f vs %.4f", r0, r1));
        // Oscillate: after the input stops, the loops keep ringing (Tails) instead of dying away
        reset();
        set({{"Par A Feedback", 0.5}, {"Par B Feedback", 0.5}, {"Par Mix", 1.0}, {"Auto Kill", 0}});
        runAudio(p, 100, 256, nullptr, nullptr, 0.4f, 0);
        runAudio(p, 1600, 256, nullptr, &r0, 0.f, 0);
        reset();
        set({{"Par A Feedback", 0.5}, {"Par B Feedback", 0.5}, {"Par Mix", 1.0}, {"Auto Kill", 0}, {"Oscillate", 1}});
        runAudio(p, 100, 256, nullptr, nullptr, 0.4f, 0);
        pkv = runAudio(p, 1600, 256, nullptr, &r1, 0.f, 0);
        check(pkv > 0 && pkv <= 1.0 && r1 > 10 * r0 && r1 > 1e-3, "Oscillate keeps the loops singing", fmt("tail rms %.5f vs %.5f, peak %.3f", r1, r0, pkv));
        set({{"Oscillate Kill", 1}, {"Oscillate", 0}});
        runAudio(p, 50, 256, nullptr, nullptr, 0.f, 0);
        runAudio(p, 100, 256, nullptr, &r1, 0.f, 0);
        check(r1 < 1e-3, "Oscillate with Cut stops at once", fmt("rms %.5f", r1));
        // the Chain's Cross builds more echo than the plain chain
        reset();
        set({{"Engine 2", 1}, {"Wiring", 1}, {"Ser Mix", 1.0}, {"Ser A Feedback", 0.3}, {"Ser B Feedback", 0.3}, {"Auto Kill", 0}});
        runAudio(p, 60, 256, nullptr, nullptr, 0.4f, 0);
        runAudio(p, 600, 256, nullptr, &r0, 0.f, 0);
        reset();
        set({{"Engine 2", 1}, {"Wiring", 1}, {"Ser Mix", 1.0}, {"Ser A Feedback", 0.3}, {"Ser B Feedback", 0.3}, {"Auto Kill", 0}, {"Chain Cross", 0.8}, {"Chain Wire Drive", 0.5}, {"Chain Wire Tone", 0.5}});
        runAudio(p, 60, 256, nullptr, nullptr, 0.4f, 0);
        pkv = runAudio(p, 600, 256, nullptr, &r1, 0.f, 0);
        check(pkv > 0 && pkv <= 1.0 && r1 > 1.5 * r0, "Chain Cross feeds engine 2 back into engine 1", fmt("tail rms %.5f vs %.5f", r1, r0));
        // Varispeed in Free: an LFO swinging A's time runs clean (no clicks or blow-ups), and sounds different from no Varispeed
        reset();
        std::vector<float> recA, recB;
        set({{"Sync", 1}, {"Par A Feedback", 0.5}, {"Par Mix", 1.0}, {"LFO 1 Shape", 4}, {"LFO 1 Target", 1}, {"LFO 1 Amount", 0.4}});
        runAudio(p, 600, 256, nullptr, &r0, 0.3f, 0, &recA);
        reset();
        set({{"Sync", 1}, {"Varispeed", 1}, {"Varispeed Bend Time", std::log2(400.0)}, {"Par A Feedback", 0.5}, {"Par Mix", 1.0}, {"LFO 1 Shape", 4}, {"LFO 1 Target", 1}, {"LFO 1 Amount", 0.4}});
        pkv = runAudio(p, 600, 256, nullptr, &r1, 0.3f, 0, &recB);
        double diff = 0;
        for (size_t i = 0; i < std::min(recA.size(), recB.size()); ++i)
            diff += std::fabs(recA[i] - recB[i]);
        check(pkv > 0 && pkv <= 1.0 && r1 > 1e-3 && diff > 1.0, "Varispeed bends the time changes", fmt("rms %.4f, difference %.1f", r1, diff));
        // Hymn's Envelope: rings (finite), and differs from Free
        reset();
        set({{"Tune On", 1}, {"Tune Amount", 1.0}, {"Par Mix", 1.0}});
        runAudio(p, 300, 256, nullptr, &r0, 0.3f, 0);
        set({{"Hymn Shape", 3}, {"Hymn Attack", std::log2(200.0)}, {"Hymn Sustain", 0.3}});
        pkv = runAudio(p, 300, 256, nullptr, &r1, 0.3f, 0);
        check(pkv > 0 && pkv <= 1.0 && std::fabs(r1 - r0) > 1e-4, "Hymn's Envelope shapes the ringing", fmt("rms %.4f vs %.4f", r1, r0));
        // the new Hymn waves run clean
        bool wavesOk = true;
        for (int w = 3; w <= 6; ++w)
        {
            set({{"Hymn Shape", 0}, {"Tune Wave", (double)w}});
            double pw = runAudio(p, 120, 256, nullptr, nullptr, 0.3f, 0);
            wavesOk = wavesOk && pw > 0 && pw <= 1.0;
        }
        check(wavesOk, "Hymn's new waves (Pulse, Arp, Fall, Sparkle) run");
        // oversampled drive: heavy drive at 4x stays finite
        reset();
        set({{"Oversample", 2}, {"Par Lead Drive", 1.0}, {"Par A Feedback", 0.9}, {"Par Mix", 1.0}});
        pkv = runAudio(p, 300, 256, nullptr, &r1, 0.5f, 0);
        check(pkv > 0 && pkv <= 1.0 && r1 > 1e-3, "4x oversampled drive runs", fmt("peak %.3f", pkv));
        // the limiter: a very hot input stays under -1 dBFS
        reset();
        set({{"Par Mix", 0.5}, {"Par Output", 24.0}});
        pkv = runAudio(p, 200, 256, nullptr, nullptr, 1.0f, 0);
        check(pkv > 0 && pkv <= 0.8913, "limiter holds -1 dBFS", fmt("peak %.4f", pkv));
        // the Random mod source moves its target
        reset();
        set({{"Mod 1 Source", 1}, {"Mod 1 Target", 3}, {"Mod 1 Amount", 1.0}, {"Par Mix", 1.0}});
        pkv = runAudio(p, 600, 256, nullptr, &r1, 0.3f, 0);
        check(pkv > 0 && pkv <= 1.0, "Random mod source runs", fmt("peak %.3f", pkv));
        // a v0.4 project with 16-bit memory loads as Clean memory with the crusher in the loop
        {
            using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
            (void)sizeof(TB);
            std::string old = "GULLAY 2\n" + fmt("%u 1\n", (unsigned)idOf("DLD Memory")) + fmt("%u 12\n", (unsigned)idOf("DLD Degrade"));
            Mem m2;
            m2.data = old;
            m2.pos = 0;
            clap_istream_t is2{&m2, memRead};
            bool okl = state->load(p, &is2);
            double dm = -1, cb = 0, cm = 0;
            params->get_value(p, idOf("DLD Memory"), &dm), params->get_value(p, idOf("Crusher Bits"), &cb), params->get_value(p, idOf("Crusher Mix"), &cm);
            check(okl && dm == 0 && std::fabs(cb - 12) < 1e-3 && cm > 0.99, "16-bit memory moves to the crusher", fmt("memory %.0f bits %.1f mix %.2f", dm, cb, cm));
        }
        // MIDI profiles: save, list, load back
        {
            using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
            auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
            if (tb)
            {
                tb->send(p, fmt("learn %u 0", (unsigned)idOf("Par Mix")).c_str());
                MidiList ev;
                ev.add(0xb0, 21, 64);
                runEvents(p, &ev.in);
                tb->send(p, "mprofile save Host Test Pad");
                std::string a1 = tb->poll(p);
                tb->send(p, "mapdel -1");
                tb->poll(p);
                tb->send(p, "mprofile load Host Test Pad");
                std::string a2 = tb->poll(p);
                check(a1.find("Host Test Pad") != std::string::npos && a2.find("Par Mix") != std::string::npos, "MIDI profiles save and load", a2.substr(0, 80));
                tb->send(p, "mprofile delete Host Test Pad");
                tb->poll(p);
                tb->send(p, "mapdel -1");
                tb->poll(p);
            }
        }
        reset();
    }
    // Snapshot: writes a text file
    {
        using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
        auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
        if (tb)
        {
            tb->send(p, "snapshot host test");
            std::string out = tb->poll(p);
            size_t k = out.find("tc.snapshot(\"");
            std::string path = k == std::string::npos ? "" : out.substr(k + 13, out.find('"', k + 13) - k - 13);
            FILE *f = path.empty() ? nullptr : std::fopen(path.c_str(), "r");
            char line[256] = {0};
            if (f)
            {
                if (!std::fgets(line, sizeof(line), f))
                    line[0] = 0;
                std::fclose(f);
            }
            check(f && std::strstr(line, "Gullay"), "Snapshot file written", path);
            if (!path.empty())
                std::remove(path.c_str());
        }
    }
    // a second instance alongside (the brief: up to two)
    {
        const clap_plugin_t *p2 = factory->create_plugin(factory, &host, desc->id);
        bool ok = p2 && p2->init(p2) && p2->activate(p2, 48000, 1, 512) && p2->start_processing(p2);
        double pa = ok ? runAudio(p2, 100, 256, nullptr, nullptr, 0.3f, 0) : -1;
        double pb = runAudio(p, 100, 256, nullptr, nullptr, 0.3f, 0);
        check(ok && pa > 0 && pb > 0, "two instances run side by side");
        if (p2)
            p2->stop_processing(p2), p2->deactivate(p2), p2->destroy(p2);
    }
    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();
    std::printf("\n%s: %d failure%s\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
