// Prova zip (ProvaZipA / B / C) - TEST of how many pictures the HMI loads from a plugin's <Name>_ALL_Images.zip.
// Each test plugin has only an entry in the MINI Connected list; its icon is the LAST picture of its zip:
//   A: 100 small pictures (about 25 KB)      B: 400 small pictures (about 94 KB)      C: 50 big pictures (about 2.5 MB)
// The icon shows only if the HMI loaded the whole zip. What is known before the test (13/11): a 12-picture zip of
// 10 KB loads (SportTest), an 801-picture zip of 5 MB is ignored by the HMI without any block (MINI Sport s1, twice).
// The plugin itself does NOTHING after the start: no car data, no pictures sent, no timer. It only writes 3 lines of
// log (/dev/shmem/<Name>.log and <stick>/nbt-evo-apps/<Name>.log). /opt/aa/provazip.disable switches all three off.
// Removed with: sh pkg.sh remove ProvaZipA ProvaZipB ProvaZipC
#include "../../framework/bmw_plugin.h"

namespace {

const char* const kDisable  = "/opt/aa/provazip.disable";
const char* const kLogShmem = "/dev/shmem/" NOME_PLUGIN ".log";
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };

bmw::PluginApp* g_app = 0;
void* g_vtbl[2 + kVtblSlots];

const char* nomeDirent(void* ent) {
    const char* p = (const char*)ent;
    short len20 = *(const short*)(p + 18);
    if (len20 > 0 && len20 < 64 && strlen(p + 20) == (u32)len20) return p + 20;
    short len12 = *(const short*)(p + 10);
    if (len12 > 0 && len12 < 64 && strlen(p + 12) == (u32)len12) return p + 12;
    return 0;
}

char g_logChiavetta[192] = "";

void cercaChiavetta() {
    if (g_logChiavetta[0]) return;
    void* d = opendir("/mnt");
    if (!d) return;
    void* e;
    while ((e = readdir(d)) != 0) {
        const char* n = nomeDirent(e);
        if (!n || strncmp(n, "umass", 5) != 0) continue;
        char cartella[96];
        snprintf(cartella, sizeof(cartella), "/mnt/%s/nbt-evo-apps", n);
        if (access(cartella, F_OK_Q) == 0) snprintf(g_logChiavetta, sizeof(g_logChiavetta), "%s/%s.log", cartella, NOME_PLUGIN);
        break;
    }
    closedir(d);
}

void scriviRiga(const char* path, const char* riga) {
    void* f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%s\n", riga);
    fclose(f);
}

void scriviLog(const char* fmt, ...) {
    char msg[300];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);
    int tmv[16];
    memset(tmv, 0, sizeof(tmv));
    time_t_q t = time(0);
    localtime_r(&t, tmv);
    char riga[400];
    snprintf(riga, sizeof(riga), "%02d:%02d:%02d UTC [%d] %s", tmv[2], tmv[1], tmv[0], getpid(), msg);
    scriviRiga(kLogShmem, riga);
    cercaChiavetta();
    if (g_logChiavetta[0]) scriviRiga(g_logChiavetta, riga);
}

bool azione(void*, const bmw::Parcel&) { return true; }   // the only button of the page does nothing

bool appOnInit(bmw::PluginApp* self) {
    for (u32 a = 58; a <= 59; ++a) {
        bmw::DelegatoAzione d;
        d.oggetto = (void*)(unsigned long)a;
        d.funzione = &azione;
        self->addHandler(a, d);
    }
    bool ok = self->onInit();
    scriviLog("onInit base -> %d (%s: %s)", (int)ok, NOME_PLUGIN, DESCRIZIONE);
    return ok;
}

void appOnError(bmw::PluginApp*, u32 code, const bmw::BasicString<char>& text) {
    const char* s = text.raw();
    scriviLog("onError %u: %s", code, s ? s : "(null)");
}

void appOnFinalize(bmw::PluginApp* self) {
    scriviLog("onFinalize");
    self->onFinalize();
}

bmw::PluginApp* creaApp(bmw::IDispatcher& dispatcher, const char* name, const bmw::Bundle& params, const bmw::Bundle& config) {
    scriviLog("create(%s) %s t0", name ? name : "(null)", NOME_PLUGIN);
    if (access(kDisable, F_OK_Q) == 0) { scriviLog("off: %s exists", kDisable); return 0; }
    if (g_app) { scriviLog("create: already created"); return 0; }
    void* mem = malloc(bmw::kPluginAppStorage);
    if (!mem) return 0;
    memset(mem, 0, bmw::kPluginAppStorage);
    bmw::PluginApp* app = new (mem) bmw::PluginApp(dispatcher, bmw::BasicString<char>(name ? name : NOME_PLUGIN), 1, 0,
                                                   bmw::RHMI_INTERFACE_VERSION_1, params, config);
    void** base = *reinterpret_cast<void***>(app);
    memcpy(g_vtbl, base - 2, sizeof(g_vtbl));
    g_vtbl[2 + kSlotOnInit]     = reinterpret_cast<void*>(&appOnInit);
    g_vtbl[2 + kSlotOnError]    = reinterpret_cast<void*>(&appOnError);
    g_vtbl[2 + kSlotOnFinalize] = reinterpret_cast<void*>(&appOnFinalize);
    *reinterpret_cast<void***>(app) = g_vtbl + 2;
    g_app = app;
    return app;
}

void distruggiApp(void* app) {
    scriviLog("destroy(%p)", app);
    if (!app) return;
    void** vt = *reinterpret_cast<void***>(app);
    reinterpret_cast<void (*)(void*)>(vt[1])(app);
    if (app == g_app) g_app = 0;
}

} // namespace

extern "C" {
struct InfoPlugin {
    u16 versionMajor;
    u16 versionMinor;
    const char* name;
    const char* vendor;
    u32 rhmiInterfaceVersion;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};
__attribute__((visibility("default"))) InfoPlugin pluginAppInfo = {
    1, 0, NOME_PLUGIN, "DeathWalkeR", 1, &creaApp, &distruggiApp
};
}
