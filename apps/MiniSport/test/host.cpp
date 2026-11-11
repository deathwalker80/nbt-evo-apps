// MINI Sport on the PC (Linux x86) with a fake HMI framework: car values in, and what the plugin sends out is kept
// as the state of the screen (picture of every model, position of every component). At the chosen moments the
// state is written to test/out/<name>.txt; preview.py draws it as the HMI would.
// Usage: sh apps/MiniSport/test/host.sh
#include "../mini_sport.cpp"
extern "C" int printf(const char*, ...);
extern "C" void bsCrea(void* self, const char* s) __asm__("_ZN3bmw11BasicStringIcEC1EPKc");
extern "C" void bsCrea(void* self, const char* s) { *(const char**)self = s; }
extern "C" void bsDistruggi(void* self) __asm__("_ZN3bmw11BasicStringIcED1Ev");
extern "C" void bsDistruggi(void*) {}
extern "C" void parcelDistruggi(void*) {}
extern "C" void* dlsym(void*, const char*) { return 0; }     // QNX RTLD_DEFAULT (-2) means nothing on Linux
extern "C" int fwrite(const void*, u32, u32, void*);

// time of the test, moved by the test itself
u32 g_finto = 100000;
extern "C" int clock_gettime(int, void* ts) {
    long* t = (long*)ts;
    t[0] = (long)(g_finto / 1000);
    t[1] = (long)(g_finto % 1000) * 1000000L;
    return 0;
}

namespace bmw {
BufferStream::~BufferStream() {}
// Image: the bytes are kept in a buffer of the test (pointer + size in the storage)
Image::Image(const unsigned char* d, u32 n) {
    unsigned char* c = (unsigned char*)malloc(n);
    memcpy(c, d, n);
    memcpy(m_storage, &c, sizeof(c));
    memcpy(m_storage + 8, &n, 4);
}
ImageID::ImageID(u32 id) { memcpy(m_storage, &id, 4); }
Parcel::Parcel(bool v) { m_storage[0] = 'b'; m_storage[1] = v; }
Parcel::Parcel(int v) { m_storage[0] = 'i'; memcpy(m_storage + 4, &v, 4); }
PluginApp::PluginApp(IDispatcher&, BasicString<char>, u32, u32, RHMIInterfaceVersion, const Bundle&, const Bundle&) {}
void PluginApp::post(Runnable*, u32) {}

const u32 kMaxId = 2000;
u32 g_idDi[kMaxId];              // model -> picture id of the zip (0 = none)
u32 g_imgDi[kMaxId];             // model -> number of the picture file sent (0 = none)
int g_x[kMaxId], g_y[kMaxId];    // component -> position set (-1000000 = as in the XML)
u32 g_nImg = 0, g_nId = 0, g_nProp = 0, g_nTesti = 0;
const char* g_dirOut = "test/out";

void PluginApp::updateModel(u32, const char*) { ++g_nTesti; }
void PluginApp::updateModel(u32 id, const Image& im) {
    unsigned char* c; u32 n;
    memcpy(&c, &im, sizeof(c));
    memcpy(&n, (const unsigned char*)&im + 8, 4);
    ++g_nImg;
    char path[160];
    snprintf(path, sizeof(path), "%s/img_%u.png", g_dirOut, g_nImg);
    void* f = fopen(path, "wb");
    if (f) { fwrite(c, 1, n, f); fclose(f); }
    free(c);
    if (id < kMaxId) g_imgDi[id] = g_nImg;
}
void PluginApp::updateModel(u32 id, const ImageID& im) { u32 n; memcpy(&n, &im, 4); if (id < kMaxId) g_idDi[id] = n; ++g_nId; }
void PluginApp::setProperty(u32 c, u32 p, const Parcel& v) {
    ++g_nProp;
    const unsigned char* s = (const unsigned char*)&v;
    int i; memcpy(&i, s + 4, 4);
    if (c >= kMaxId || s[0] != 'i') { printf("  setProperty %u.%u (unexpected)\n", c, p); return; }
    if (p == kPropX) g_x[c] = i;
    else if (p == kPropY) g_y[c] = i;
    else printf("  setProperty %u.%u = %d (unexpected)\n", c, p, i);
}
bool PluginApp::onInit() { return true; }
void PluginApp::addHandler(u32 id, const DelegatoAzione& d) { printf("  addHandler(%u)\n", id); (void)d; }
void PluginApp::onFinalize() {}
DelegatoCds g_cds[32]; u32 g_nCds = 0;
void PluginApp::cdsAddPropertyChangedEventHandler(BasicString<char>&, u32, const DelegatoCds& d) { g_cds[g_nCds++] = d; }
}

void cds(u32 i, const char* json) {
    static u16 buf[512]; u32 n = 0;
    while (json[n] && n < 511) { buf[n] = (unsigned char)json[n]; ++n; }
    buf[n] = 0;
    u16* p = buf;
    bmw::g_cds[i].funzione(bmw::g_cds[i].oggetto, *(const bmw::BasicString<u16>*)&p);
}

void avanti(u32 ms) {                       // ticks of 50 ms
    for (u32 t = 0; t < ms; t += kTickMs) { g_finto += kTickMs; giroRun(0); }
}

void scegliMenu(int i) {
    static char finto[64];
    sceltaMenu((void*)(unsigned long)i, *(const bmw::Parcel*)finto);
}

// state of the screen for the preview: page, theme, background picture of each model, picture id of each model,
// position of each component that was moved
void fotografa(const char* nome, int stato) {
    char path[160];
    snprintf(path, sizeof(path), "%s/%s.txt", bmw::g_dirOut, nome);
    void* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "state %d\ntheme %d\nmenu %d\n", stato, g_tema, g_pagina);
    for (u32 m = 0; m < bmw::kMaxId; ++m) {
        if (bmw::g_imgDi[m]) fprintf(f, "img %u %s/img_%u.png\n", m, bmw::g_dirOut, bmw::g_imgDi[m]);
        if (bmw::g_idDi[m]) fprintf(f, "id %u %u\n", m, bmw::g_idDi[m]);
        if (bmw::g_x[m] != -1000000) fprintf(f, "x %u %d\n", m, bmw::g_x[m]);
        if (bmw::g_y[m] != -1000000) fprintf(f, "y %u %d\n", m, bmw::g_y[m]);
    }
    fclose(f);
    printf("== %s: pictures %u ids %u moves %u so far\n", nome, bmw::g_nImg, bmw::g_nId, bmw::g_nProp);
}

void valoriBase(int modo) {
    char b[96];
    snprintf(b, sizeof(b), "{\"mode\":%d}", modo);
    cds(C_MODO, b);
    cds(C_TEMP, "{\"temperature\":{\"engine\":89,\"oil\":100}}");
    cds(C_GAS, "{\"acceleratorPedal\":{\"position\":64,\"ecoPosition\":0}}");
    cds(C_COPPIA, "{\"torque\":182}");
    cds(C_MARCIA, "{\"gear\":3}");
    cds(C_BATTERIA, "{\"battery\":82}");
    cds(C_STERZO, "{\"steeringWheel\":{\"angle\":-12.5,\"speed\":-0.0,\"error\":0}}");
    cds(C_FRIZIONE, "{\"clutchPedal\":{\"position\":0}}");
    cds(C_GIRI, "{\"RPMSpeed\":4230}");
    cds(C_VELOCITA, "{\"speedActual\":0}");
    cds(C_ACCEL, "{\"acceleration\":{\"lateral\":0.0,\"longitudinal\":0.0}}");
}

void velocita(int kmh) { char b[64]; snprintf(b, sizeof(b), "{\"speedActual\":%d}", kmh); cds(C_VELOCITA, b); }
void accel(const char* lat, const char* lon) {
    char b[128]; snprintf(b, sizeof(b), "{\"acceleration\":{\"lateral\":%s,\"longitudinal\":%s}}", lat, lon); cds(C_ACCEL, b);
}

int main() {
    for (u32 m = 0; m < bmw::kMaxId; ++m) bmw::g_x[m] = bmw::g_y[m] = -1000000;
    static unsigned char mem[0x200];
    g_app = (bmw::PluginApp*)mem;
    printf("== onInit (the fake framework has no vtable: creaApp is not run here)\n");
    appOnInit(g_app);
    avanti(2000);
    printf("== CDS registered: %u\n", bmw::g_nCds);
    int v;
    printf("== numbers: %d %d %d %d\n", numeroDa("{\"RPMSpeed\":3250}", "RPMSpeed", &v) ? v : -1,
           numeroDa("{\"acceleration\":{\"lateral\":-0.25,\"longitudinal\":1.5}}", "longitudinal", &v) ? v : -1,
           numeroDa("{\"steeringWheel\":{\"angle\":-8.3}}", "angle", &v) ? v : -1,
           numeroDa("{\"gear\":\"N\"}", "gear", &v) ? v : -1);
    printf("== sqrt: %d %d %d\n", radice(0), radice(430 * 430), radice(1000000 + 2));

    for (int tema = 0; tema < 2; ++tema) {
        const char* tn = tema ? "sport" : "mid";
        char nome[64];
        valoriBase(tema ? 3 : 4);
        avanti(200);
        // menu with each choice
        for (int i = 0; i < 4; ++i) { scegliMenu(i); avanti(100); snprintf(nome, sizeof(nome), "menu%d_%s", i, tn); fotografa(nome, 100); }
        // revs page
        scegliMenu(PAG_GIRI);
        avanti(300);
        snprintf(nome, sizeof(nome), "giri_%s", tn); fotografa(nome, 110);
        cds(C_GIRI, "{\"RPMSpeed\":6080}");
        avanti(100);
        snprintf(nome, sizeof(nome), "giri_alti_%s", tn); fotografa(nome, 110);
        cds(C_GIRI, "{\"RPMSpeed\":830}");
        cds(C_MARCIA, "{\"gear\":0}");
        avanti(100);
        snprintf(nome, sizeof(nome), "giri_minimo_%s", tn); fotografa(nome, 110);
        cds(C_GIRI, "{\"RPMSpeed\":4230}");
        cds(C_MARCIA, "{\"gear\":3}");
        // G-meter: a minute of curves, then a right curve with some push
        scegliMenu(PAG_G);
        static const char* const lats[6] = {"3.1", "-8.6", "1.0", "-2.0", "9.0", "0.5"};
        static const char* const lons[6] = {"1.2", "0.4", "4.0", "-9.5", "-1.0", "2.5"};
        for (int k = 0; k < 30; ++k) { accel(lats[k % 6], lons[k % 6]); avanti(2000); }
        accel("3.70", "1.96");
        avanti(500);
        snprintf(nome, sizeof(nome), "gmeter_%s", tn); fotografa(nome, 111);
        accel("0.0", "0.0");
        // timers: stop, then 0-100 in ~6.8 s, 80-120, braking 100-0
        scegliMenu(PAG_CRONO);
        velocita(0);
        avanti(1000);
        snprintf(nome, sizeof(nome), "crono_pronto_%s", tn); fotografa(nome, 112);
        for (int k = 1; k <= 74; ++k) { velocita(k); avanti(50); }
        snprintf(nome, sizeof(nome), "crono_corsa_%s", tn); fotografa(nome, 112);
        for (int k = 75; k <= 125; ++k) { velocita(k); avanti(100); }
        for (int k = 124; k >= 0; k -= 2) { velocita(k); avanti(50); }
        avanti(300);
        snprintf(nome, sizeof(nome), "crono_fatto_%s", tn); fotografa(nome, 112);
        // engine
        scegliMenu(PAG_MOTORE);
        avanti(300);
        snprintf(nome, sizeof(nome), "motore_%s", tn); fotografa(nome, 113);
    }
    // demo (engine off): the file switches it on within 5 s
    { void* f = fopen(kDemo, "w"); if (f) fclose(f); }
    scegliMenu(PAG_GIRI);
    avanti(9000);
    fotografa("demo_giri", 110);
    scegliMenu(PAG_CRONO);
    avanti(9000);
    fotografa("demo_crono", 112);
    unlink(kDemo);
    printf("== records: 0-100 %d ms, 0-50 %d ms, 80-120 %d ms, braking %d mm\n", g_record[0], g_record[1], g_record[2], g_record[3]);
    printf("== totals: pictures %u ids %u moves %u texts %u\n", bmw::g_nImg, bmw::g_nId, bmw::g_nProp, bmw::g_nTesti);
    return 0;
}
