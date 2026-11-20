#include <cstdlib>
// loads the plugin, plays noise, and writes what the window would be sent: the init, then updates
#include <clap/clap.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
static const void *hostExt(const clap_host_t *, const char *) { return nullptr; }
static void hostNop(const clap_host_t *) {}
static const clap_host_t host = {CLAP_VERSION_INIT, nullptr, "gui dump", "", "", "1", hostExt, hostNop, hostNop, hostNop};
static bool pushNop(const clap_output_events_t *, const clap_event_header_t *) { return true; }
static const clap_output_events_t outEvents = {nullptr, pushNop};
static uint32_t evSize(const clap_input_events_t *) { return 0; }
static const clap_event_header_t *evGet(const clap_input_events_t *, uint32_t) { return nullptr; }
static const clap_input_events_t noEvents = {nullptr, evSize, evGet};
int main(int argc, char **argv)
{
    void *lib = dlopen(argv[1], RTLD_NOW);
    auto *entry = (const clap_plugin_entry_t *)dlsym(lib, "clap_entry");
    entry->init(argv[1]);
    auto *f = (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    auto *p = f->create_plugin(f, &host, f->get_plugin_descriptor(f, 0)->id);
    p->init(p);
    p->activate(p, 48000, 1, 512);
    p->start_processing(p);
    using TB = struct { void (*send)(const clap_plugin_t *, const char *); const char *(*poll)(const clap_plugin_t *); };
    auto *tb = (const TB *)p->get_extension(p, "com.otiskanno.gullay/test-bridge");
    if (argc > 3)
        tb->send(p, (std::string("preset ") + argv[3]).c_str());
    tb->send(p, "ready");
    tb->send(p, "pats");
    if (const char *demo = std::getenv("GY_DEMO")) // extra window commands, separated by ;
    {
        std::string all = demo, one;
        for (size_t i = 0; i <= all.size(); ++i)
            if (i == all.size() || all[i] == ';') { if (!one.empty()) tb->send(p, one.c_str()); one.clear(); }
            else one += all[i];
    }
    FILE *o = std::fopen(argv[2], "w");
    std::vector<float> l(256), r(256), ol(256), orr(256);
    float *ins[2] = {l.data(), r.data()}, *outs[2] = {ol.data(), orr.data()};
    clap_audio_buffer_t ib{ins, nullptr, 2, 0, 0}, ob{outs, nullptr, 2, 0, 0};
    clap_event_transport_t tr{};
    tr.header.size = sizeof(tr), tr.header.type = CLAP_EVENT_TRANSPORT;
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    tr.tempo = 120;
    uint32_t seed = 1;
    std::fprintf(o, "window.__msgs = [\n");
    for (int k = 0; k < 120; ++k)
    {
        for (int b = 0; b < 6; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                float n = ((seed >> 9) / 4194304.f - 1.f) * 0.3f;
                float env = ((k * 6 + b) % 24) < 3 ? 1.f : 0.05f;
                l[i] = r[i] = n * env;
            }
            clap_process_t pr{};
            pr.frames_count = 256, pr.audio_inputs = &ib, pr.audio_outputs = &ob, pr.audio_inputs_count = pr.audio_outputs_count = 1;
            pr.in_events = &noEvents, pr.out_events = &outEvents;
            tr.song_pos_beats = (clap_beattime)std::llround((k * 6 + b) * 256.0 / 24000.0 * CLAP_BEATTIME_FACTOR);
            pr.transport = &tr;
            p->process(p, &pr);
        }
        if (k == 60)
            tb->send(p, "view 1");
        std::string js = tb->poll(p);
        std::string esc;
        for (char c : js)
        {
            if (c == '\\' || c == '`' || c == '$')
                esc += '\\';
            esc += c;
        }
        std::fprintf(o, "`%s`,\n", esc.c_str());
    }
    std::fprintf(o, "];\n");
    std::fclose(o);
    return 0;
}
