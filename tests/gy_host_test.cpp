/*
 * Gullay - a tiny CLAP host that loads the built plugin the way a DAW does and
 * checks it end to end.   usage: clap_host_test <path to the plugin binary>
 */
#include <clap/clap.h>

#include <algorithm>
#include <cmath>
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
    check(lat->get(p) == 64, "latency is one Tapo block", fmt("%u samples", lat->get(p)));

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
    // each layout makes sound and stays finite
    struct Layout { const char *name; std::initializer_list<std::pair<const char *, double>> kv; };
    std::vector<std::pair<std::string, std::vector<std::pair<const char *, double>>>> layouts = {
        {"Tapo alone", {{"Slot 1", 1}}},
        {"DLD + DLD parallel", {{"Slot 2", 1}}},
        {"DLD + Tapo series", {{"Slot 2", 2}, {"Wiring", 1}}},
        {"Tapo + DLD spectral", {{"Slot 1", 1}, {"Slot 2", 1}, {"Parallel Split", 1}}},
        {"Call and Response", {{"One Engine: Part B", 1}}},
        {"Mirror", {{"One Engine: Part B", 2}}},
        {"Tapo + Mirror", {{"Slot 1", 1}, {"One Engine: Part B", 2}}},
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
        set({{"Par A Feedback", 1.2}, {"Par B Feedback", 1.2}, {"Par Lead Drive", 0.6}, {"Runaway Ceiling", -12}, {"Par Mix", 1}});
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
    // crusher, filter places, tilt, reverbs, shifts: all finite with noise
    {
        std::vector<std::pair<std::string, std::vector<std::pair<const char *, double>>>> fx = {
            {"crusher in the loop", {{"Crusher Bits", 6}, {"Crusher Mix", 1}, {"Crusher Rate", std::log2(8000.0)}}},
            {"chip mode out", {{"Chip Mode", 1}, {"Crusher Place", 1}, {"Crusher Mix", 1}}},
            {"16-bit degrade 11", {{"DLD Degrade", 11}}},
            {"filter in", {{"Filter Place", 0}, {"Filter Cutoff", 20}}},
            {"filter in the loop", {{"Filter Place", 2}, {"Filter Cutoff", 30}, {"Filter Reso", 0.8}}},
            {"tilts", {{"Tilt In", 9}, {"Tilt Out", -9}}},
            {"Plateau", {{"Reverb On", 1}, {"Reverb Send", 0}}},
            {"Cloud Seed", {{"Reverb On", 1}, {"Reverb Flavour", 1}, {"Reverb Send", 0}}},
            {"Plateau tuned to the key", {{"Reverb On", 1}, {"Reverb Tuned", 1}, {"Reverb Follows Key", 1}, {"Tune On", 1}}},
            {"shift each repeat", {{"Lead Shift", 12}, {"Lead Shift Mode", 2}, {"Tune On", 1}}},
            {"shift once on Tapo", {{"Slot 1", 1}, {"B Shift", 7}, {"B Shift Mode", 1}}},
            {"Tune into the delays", {{"Tune On", 1}, {"Tune Place", 0}}},
            {"reverse and INF", {{"Par A Reverse", 1}, {"Par B INF", 1}}},
            {"everything modulating", {{"Sway", 1}, {"Chaperone Influence", 1}, {"Mutate", 1}, {"LFO 1 Target", 40}, {"LFO 1 Amount", 1},
                                       {"LFO 2 Target", 1}, {"LFO 2 Amount", 1}, {"Mod 1 Source", 9}, {"Mod 1 Target", 7}, {"Mod 1 Amount", 1}, {"Slot 1", 1}}},
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
        set({{"Slot 1", 1}, {"MIDI Picks Patterns", 1}, {"MIDI Sets Key", 1}});
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
            // NRPN learn on Filter Cutoff
            tb->send(p, fmt("learn %u 2", idOf("Filter Cutoff")).c_str());
            MidiList nr;
            nr.add(0xb1, 99, 1), nr.add(0xb1, 98, 2), nr.add(0xb1, 6, 64), nr.add(0xb1, 38, 0);
            runEvents(p, &nr.in);
            MidiList nr2;
            nr2.add(0xb1, 99, 1), nr2.add(0xb1, 98, 2), nr2.add(0xb1, 6, 127), nr2.add(0xb1, 38, 127);
            runEvents(p, &nr2.in);
            params->get_value(p, idOf("Filter Cutoff"), &v);
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
            check(mj2.find("Crusher Mix") != std::string::npos && mj2.find("Filter Cutoff") != std::string::npos, "mappings saved with the project");
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
        for (int k = 0; k < 20 && tb; ++k)
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
        check(good == 20, "20 factory presets play", bad);
        std::string init = tb ? (tb->send(p, "ready"), tb->poll(p)) : "";
        int count = 0;
        for (const char *g : {"Front Porch", "Ring Shout", "Low Country", "Deep Water"})
            count += init.find(g) != std::string::npos;
        check(count == 4, "presets come in four groups");
    }
    // state round trip
    {
        set({{"Par A Time", 7}, {"Reverb Size", 0.33}, {"Tapo Pattern", 17}});
        Mem mem;
        clap_ostream_t os{&mem, memWrite};
        check(state->save(p, &os) && mem.data.rfind("GULLAY ", 0) == 0, "state saves");
        reset();
        mem.pos = 0;
        clap_istream_t is{&mem, memRead};
        double a = 0, b = 0, c = 0;
        bool ok = state->load(p, &is);
        params->get_value(p, idOf("Par A Time"), &a), params->get_value(p, idOf("Reverb Size"), &b), params->get_value(p, idOf("Tapo Pattern"), &c);
        check(ok && std::fabs(a - 7) < 1e-4 && std::fabs(b - 0.33) < 1e-4 && c == 17, "state loads back");
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
