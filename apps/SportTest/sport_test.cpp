// SportTest - test plugin for the in-car apps on NBT EVO (ID6), hu-omap. No phone, no network: car data from the
// head unit itself (CDS) and pictures installed with the plugin, loaded once at start.
//
// It answers, in one session in the car, what the real apps (shift light + rev counter, G-meter, timers, engine
// gauges) need to know:
//   1. how often the car data arrives (CDS events per second, per property, with the raw values in the log);
//   2. how fast small pictures can be swapped (the rev digits: updateModel with pictures already in memory);
//   3. whether setProperty works to show/hide a picture (shift LEDs), change its width (rev bar) and move it
//      (G-meter dot): then the real apps never send a new picture for a moving part;
//   4. whether the HMI shows a 256-colour PNG with transparency like a full-colour one (the two samples);
//   5. whether a picture of the plugin's own SportTest_ALL_Images.zip can be switched by its id (updateModel with
//      an ImageID): then the pictures stay in the zip and the HMI draws them itself (second row of digits).
// Stages, with the "Stage" button (or from the shell: echo N > /dev/shmem/SportTest.vai, N = 0..4):
//   0 data + digits + text   1 + LEDs (visibility)   2 + rev bar (width)   3 + G-meter dot (position)
//   4 digits from the zip (ImageID)
// setProperty and ImageID updates are new here: before the first call of each stage the stage is written to
// /dev/shmem/SportTest.fase; if the HMI restarts during a stage (crash), at the next start the plugin finds it and
// leaves that kind of call off (1-3: setProperty, 4: ImageID) until the head unit is switched off (/dev/shmem is RAM).
// Log: /dev/shmem/SportTest.log and, with a USB stick, <stick>/nbt-evo-apps/SportTest.log (or the stick root).
// Safety as the iDrive Plus plugins: /opt/aa/sporttest.disable switches it off; after 3 starts that do not reach
// 30 s it stays off (delete /dev/shmem/SportTest.avvii).
#include "../../framework/bmw_plugin.h"

#define NOME_PLUGIN "SportTest"
#define VERSIONE "t1"

namespace {

// ---- constants ---------------------------------------------------------------------------------------------
const char* const kDisable    = "/opt/aa/sporttest.disable";
const char* const kAvvii      = "/dev/shmem/" NOME_PLUGIN ".avvii";
const char* const kFase       = "/dev/shmem/" NOME_PLUGIN ".fase";
const char* const kVai        = "/dev/shmem/" NOME_PLUGIN ".vai";
const char* const kLogShmem   = "/dev/shmem/" NOME_PLUGIN ".log";
const char* const kDirPlugin  = "/opt/hmi/ID5/data/ro/Plugins/" NOME_PLUGIN;
const u32 kTickMs       = 40;          // asked rate of the screen updates: 25 per second
const u32 kSecondiSano  = 30;
const u32 kMaxAvvii     = 3;
const u32 kOgniStatMs   = 5000;
const u32 kGiroCds      = 38;          // CDS registered ~1.5 s after onInit (as NBT-AAD), in ticks
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };

// models and components (SportTest.xml)
const u32 kModFase = 41, kModDati = 42;
const u32 kModSfondo = 71, kMod256 = 72, kModRgba = 73, kModBarra = 74, kModPunto = 75;
const u32 kModLed0 = 80, kModCifra0 = 90, kModCifraZip0 = 96;
const u32 kIdCifra0 = 64100;           // digit<c>.png in SportTest_ALL_Images.zip: 64100..64109, 64110 = empty
const u32 kCompBarra = 310, kCompPunto = 311, kCompLed0 = 320;
const u32 kAzioneFase = 30, kAzioneDati = 31;
const u32 kLed = 10, kCifre = 4;

// screen geometry (the same numbers as tools/make_assets.py and the XML)
const int kBarraW = 1100;          // rev bar at x 20, y 40, 1100 x 40
const int kCerchioX = 980, kCerchioY = 300, kRaggio = 130, kPunto = 36;
const int kGiriMax = 7000, kGiriLed = 4000, kGiriPassoLed = 250;

bmw::PluginApp* g_app = 0;
void* g_vtbl[2 + kVtblSlots];
volatile bool g_finito = false;

// ---- time and log --------------------------------------------------------------------------------------------
u32 adessoMs() {
    long ts[2] = {0, 0};
    if (clock_gettime(CLOCK_MONOTONIC_Q, ts) == 0) return (u32)ts[0] * 1000u + (u32)(ts[1] / 1000000);
    return (u32)time(0) * 1000u;
}

const char* nomeDirent(void* ent) {
    const char* p = (const char*)ent;
    short len20 = *(const short*)(p + 18);
    if (len20 > 0 && len20 < 64 && strlen(p + 20) == (u32)len20) return p + 20;
    short len12 = *(const short*)(p + 10);
    if (len12 > 0 && len12 < 64 && strlen(p + 12) == (u32)len12) return p + 12;
    return 0;
}

char g_logChiavetta[192] = "";
time_t_q g_ultimaRicerca = 0;

void cercaChiavetta() {
    time_t_q ora = time(0);
    if (g_logChiavetta[0] || (g_ultimaRicerca && ora - g_ultimaRicerca < 20)) return;
    g_ultimaRicerca = ora;
    void* d = opendir("/mnt");
    if (!d) return;
    void* e;
    while ((e = readdir(d)) != 0) {
        const char* n = nomeDirent(e);
        if (!n || strncmp(n, "umass", 5) != 0) continue;
        char cartella[96];
        snprintf(cartella, sizeof(cartella), "/mnt/%s/nbt-evo-apps", n);
        if (access(cartella, F_OK_Q) == 0) snprintf(g_logChiavetta, sizeof(g_logChiavetta), "%s/%s.log", cartella, NOME_PLUGIN);
        else snprintf(g_logChiavetta, sizeof(g_logChiavetta), "/mnt/%s/%s.log", n, NOME_PLUGIN);
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
    char msg[900];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);
    int tmv[16];
    memset(tmv, 0, sizeof(tmv));
    time_t_q t = time(0);
    localtime_r(&t, tmv);
    char riga[1000];
    snprintf(riga, sizeof(riga), "%02d:%02d:%02d UTC [%d] %s", tmv[2], tmv[1], tmv[0], getpid(), msg);
    scriviRiga(kLogShmem, riga);
    cercaChiavetta();
    if (g_logChiavetta[0]) scriviRiga(g_logChiavetta, riga);
}

// ---- small files -------------------------------------------------------------------------------------------
u32 leggiNumeroFile(const char* path, bool* esiste) {
    int fd = open(path, O_RDONLY_Q);
    if (esiste) *esiste = fd >= 0;
    if (fd < 0) return 0;
    char b[16]; memset(b, 0, sizeof(b));
    read(fd, b, sizeof(b) - 1);
    close(fd);
    u32 n = 0;
    for (int i = 0; b[i] >= '0' && b[i] <= '9'; ++i) n = n * 10 + (u32)(b[i] - '0');
    return n;
}

void scriviNumeroFile(const char* path, u32 n) {
    void* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%u\n", n);
    fclose(f);
}

// [u32 size][bytes] of a file in the plugin folder (pictures: at most 256 KB)
unsigned char* bloccoDaFile(const char* nome) {
    char path[192];
    snprintf(path, sizeof(path), "%s/%s", kDirPlugin, nome);
    int fd = open(path, O_RDONLY_Q);
    if (fd < 0) return 0;
    u32 cap = 256 * 1024, tot = 0;
    unsigned char* b = (unsigned char*)malloc(4 + cap);
    if (!b) { close(fd); return 0; }
    for (;;) {
        if (tot == cap) { tot = 0; break; }
        ssize_t_q r = read(fd, b + 4 + tot, cap - tot);
        if (r <= 0) break;
        tot += (u32)r;
    }
    close(fd);
    if (!tot) { free(b); return 0; }
    memcpy(b, &tot, 4);
    return b;
}

// ---- screen ------------------------------------------------------------------------------------------------
void mostraImmagine(u32 modello, const unsigned char* blocco) {
    if (!blocco) return;
    u32 n; memcpy(&n, blocco, 4);
    unsigned char spazio[64] __attribute__((aligned(8)));
    memset(spazio, 0, sizeof(spazio));
    bmw::Image* img = new (spazio) bmw::Image(blocco + 4, n);
    g_app->updateModel(modello, *img);
    // bmw::Image exports no destructor: its two members are destroyed by hand
    reinterpret_cast<bmw::BufferStream*>(spazio + 4)->~BufferStream();
    reinterpret_cast<bmw::BasicString<char>*>(spazio)->~BasicString();
}

u32 g_chiamateProp = 0;

void proprietaBool(u32 componente, u32 proprieta, bool v) {
    unsigned char spazio[256] __attribute__((aligned(8)));
    memset(spazio, 0, sizeof(spazio));
    bmw::Parcel* p = new (spazio) bmw::Parcel(v);
    g_app->setProperty(componente, proprieta, *p);
    parcelDistruggi(spazio);
    ++g_chiamateProp;
}

void proprietaInt(u32 componente, u32 proprieta, int v) {
    unsigned char spazio[256] __attribute__((aligned(8)));
    memset(spazio, 0, sizeof(spazio));
    bmw::Parcel* p = new (spazio) bmw::Parcel(v);
    g_app->setProperty(componente, proprieta, *p);
    parcelDistruggi(spazio);
    ++g_chiamateProp;
}

// pictures loaded once
unsigned char* g_cifra[11];            // 0..9 and 10 = empty
unsigned char* g_led = 0;

// ---- car data (CDS) ----------------------------------------------------------------------------------------
// names from IDriveConnectKit (CDSProperty.kt), the same table as libhmiremoting.so of the head unit
const char* const kProprieta[] = {
    "engine.RPMSpeed", "driving.speedActual", "driving.acceleration", "driving.gear", "engine.torque",
    "driving.acceleratorPedal", "driving.brakeContact", "driving.clutchPedal", "driving.steeringWheel",
    "engine.temperature", "driving.mode", "driving.shiftIndicator", "driving.speedDisplayed", "engine.info",
    "sensors.battery", "navigation.GPSExtendedInfo",
};
enum { P_GIRI = 0, P_VELOCITA, P_ACCEL, P_MARCIA };
const u32 kNumProprieta = sizeof(kProprieta) / sizeof(kProprieta[0]);
const u32 kMaxValore = 400;
const u32 kRawNelLog = 3;              // the first values of every property go to the log in full

void* volatile g_valori[kNumProprieta];         // last value not yet read (CDS callback -> tick)
volatile u32 g_eventi[kNumProprieta];            // CDS events since the start
char g_ultimo[kNumProprieta][kMaxValore];        // last value read (tick only)
u32 g_loggati[kNumProprieta];

bool cambioCds(void* oggetto, const bmw::BasicString<u16>& valore) {
    u32 i = (u32)(unsigned long)oggetto;
    if (i >= kNumProprieta) return true;
    __atomic_add_fetch(&g_eventi[i], 1u, __ATOMIC_RELAXED);
    const u16* p = valore.raw();
    u32 n = 0;
    while (p && p[n] && n < kMaxValore - 1) ++n;
    unsigned char* b = (unsigned char*)malloc(n + 1);
    if (!b) return true;
    for (u32 k = 0; k < n; ++k) b[k] = p[k] < 0x80 ? (unsigned char)p[k] : '?';
    b[n] = 0;
    void* vecchio = __atomic_exchange_n(&g_valori[i], (void*)b, __ATOMIC_ACQ_REL);
    if (vecchio) free(vecchio);
    return true;
}

void registraCds() {
    for (u32 i = 0; i < kNumProprieta; ++i) {
        bmw::BasicString<char> nome(kProprieta[i]);
        bmw::DelegatoCds d;
        d.oggetto = (void*)(unsigned long)i;
        d.funzione = &cambioCds;
        g_app->cdsAddPropertyChangedEventHandler(nome, 0, d);
    }
    scriviLog("CDS: %u properties registered", kNumProprieta);
}

// number after "key": in a JSON text, in thousandths (12.345 -> 12345); the n-th number if the key is missing
bool numeroDa(const char* s, const char* chiave, u32 ennesimo, int* out) {
    const char* p = 0;
    if (chiave) {
        u32 lc = strlen(chiave);
        for (const char* q = s; *q; ++q)
            if (*q == '"' && strncmp(q + 1, chiave, lc) == 0 && q[1 + lc] == '"') { p = q + 2 + lc; break; }
        if (p) { while (*p && *p != '-' && (*p < '0' || *p > '9')) { if (*p == ',' || *p == '}') return false; ++p; } }
    }
    if (!p) {
        p = s;
        for (u32 k = 0;; ++k) {
            while (*p && *p != '-' && (*p < '0' || *p > '9')) ++p;
            if (!*p) return false;
            if (k == ennesimo) break;
            while (*p == '-' || *p == '.' || (*p >= '0' && *p <= '9')) ++p;
        }
    }
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    if (*p < '0' || *p > '9') return false;
    int v = 0;                                   // 32-bit only (no 64-bit division helpers in the HMI)
    while (*p >= '0' && *p <= '9') { if (v < 2000000) v = v * 10 + (*p - '0'); ++p; }
    v *= 1000;
    if (*p == '.') {
        ++p;
        static const int pesi[3] = {100, 10, 1};
        for (u32 k = 0; *p >= '0' && *p <= '9'; ++k, ++p) if (k < 3) v += (*p - '0') * pesi[k];
    }
    *out = neg ? -v : v;
    return true;
}

// ---- state of the test -------------------------------------------------------------------------------------
u32 g_fase = 0;
bool g_propVietata = false;      // the HMI restarted during a setProperty stage: no setProperty at all
bool g_idVietato = false;        // the HMI restarted during the ImageID stage: no ImageID updates
int g_cifraZip[4] = {-1, -1, -1, -1};
bool g_primaChiamata[5];         // "first call done" logged once per stage
int g_giri = 0, g_velocita = 0, g_marcia = -1, g_lat = 0, g_lon = 0;   // rpm, km/h x1000, g x1000
bool g_haGiri = false, g_haAccel = false;
int g_cifraMostrata[kCifre];
int g_ledAccesi = -1, g_barraW = -1, g_puntoX = -9999, g_puntoY = -9999;
char g_testoMostrato[160] = "";
u32 g_tick = 0, g_tickStat = 0, g_cifreStat = 0, g_testiStat = 0, g_propStat = 0, g_tickMaxMs = 0;
u32 g_eventiStat[kNumProprieta];
u32 g_inizioStat = 0, g_inizio = 0;
bool g_sano = false;

const char* testoFase(u32 f) {
    switch (f) {
        case 0: return "Fase 0: dati e cifre - premi per la fase 1";
        case 1: return "Fase 1: + LED (visibilita') - premi per la 2";
        case 2: return "Fase 2: + barra giri (larghezza) - premi per la 3";
        case 3: return "Fase 3: + punto G-meter (posizione) - premi per la 4";
        default: return "Fase 4: cifre dallo zip (ImageID) - premi: 0";
    }
}

void impostaFase(u32 f) {
    if (f > 4) f = 0;
    if (f >= 1 && f <= 3 && g_propVietata) { scriviLog("stage %u skipped: setProperty is off", f); f = 4; }
    if (f == 4 && g_idVietato) { scriviLog("stage 4 skipped: ImageID updates are off"); f = 0; }
    g_fase = f;
    if (f > 0) scriviNumeroFile(kFase, f);       // before the first setProperty / ImageID call of the stage
    else unlink(kFase);                          // back to 0: a later restart of the HMI is not blamed on a stage
    char t[96];
    snprintf(t, sizeof(t), "%s%s%s", testoFase(f), g_propVietata ? " (setProperty spento)" : "", g_idVietato ? " (ImageID spento)" : "");
    g_app->updateModel(kModFase, t);
    scriviLog("stage %u", f);
}

bool premutoDati(void*, const bmw::Parcel&) { return true; }   // the data line: nothing to do

bool premutoFase(void*, const bmw::Parcel&) {
    impostaFase(g_fase + 1);
    return true;
}

// ---- one tick ------------------------------------------------------------------------------------------------
void leggiValori() {
    for (u32 i = 0; i < kNumProprieta; ++i) {
        char* b = (char*)__atomic_exchange_n(&g_valori[i], (void*)0, __ATOMIC_ACQ_REL);
        if (!b) continue;
        u32 n = strlen(b);
        if (n >= kMaxValore) n = kMaxValore - 1;
        memcpy(g_ultimo[i], b, n);
        g_ultimo[i][n] = 0;
        free(b);
        if (g_loggati[i] < kRawNelLog) { ++g_loggati[i]; scriviLog("raw %s #%u: %s", kProprieta[i], g_loggati[i], g_ultimo[i]); }
        int v;
        if (i == P_GIRI && numeroDa(g_ultimo[i], "RPMSpeed", 0, &v)) { g_giri = v / 1000; g_haGiri = true; }
        if (i == P_VELOCITA && numeroDa(g_ultimo[i], "speedActual", 0, &v)) g_velocita = v;
        if (i == P_MARCIA && numeroDa(g_ultimo[i], "gear", 0, &v)) g_marcia = v / 1000;
        if (i == P_ACCEL) {
            int a, b2;
            bool okA = numeroDa(g_ultimo[i], "lateral", 0, &a) || numeroDa(g_ultimo[i], 0, 0, &a);
            bool okB = numeroDa(g_ultimo[i], "longitudinal", 0, &b2) || numeroDa(g_ultimo[i], 0, 1, &b2);
            if (okA && okB) { g_lat = a; g_lon = b2; g_haAccel = true; }
        }
    }
}

int limita(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

// the four digits of 0..9999, 10 = blank for the leading zeros (by constant divisions only: ARMv7-A has no
// divide instruction and the helpers of the compiler are not in the HMI)
void cifreDi(int v, int* c) {
    c[0] = v / 1000; c[1] = (v / 100) % 10; c[2] = (v / 10) % 10; c[3] = v % 10;
    if (v < 1000) c[0] = 10;
    if (v < 100) c[1] = 10;
    if (v < 10) c[2] = 10;
}

void aggiornaSchermo(u32 ora) {
    // demo movement when the car sends nothing (engine off, parked): every part can be checked anyway
    int giri = g_giri;
    if (!g_haGiri || giri == 0) { int t = (int)(ora % 4000); giri = t < 2000 ? t * kGiriMax / 2000 : (4000 - t) * kGiriMax / 2000; }
    // 1) digits: a picture already in memory only where the digit changed
    int v = limita(giri, 0, 9999);
    int cifra[kCifre];
    cifreDi(v, cifra);
    for (u32 k = 0; k < kCifre; ++k) {
        int c = cifra[k];
        if (c != g_cifraMostrata[k]) { mostraImmagine(kModCifra0 + k, g_cifra[c]); g_cifraMostrata[k] = c; ++g_cifreStat; }
    }
    // 2) text line
    char t[160];
    int sp = g_velocita / 1000;
    snprintf(t, sizeof(t), "%s%d giri  %d km/h  marcia %d  lat %d  lon %d", (!g_haGiri || g_giri == 0) ? "DEMO " : "",
             giri, sp, g_marcia, g_lat, g_lon);
    if (strncmp(t, g_testoMostrato, sizeof(t)) != 0) {
        memcpy(g_testoMostrato, t, sizeof(t));
        g_app->updateModel(kModDati, t);
        ++g_testiStat;
    }
    if (g_fase == 4 && !g_idVietato) {
        // 2b) the same digits, as pictures of the zip switched by id
        bool prima = !g_primaChiamata[4];
        for (u32 k = 0; k < kCifre; ++k) {
            int c = cifra[k];
            if (c == g_cifraZip[k]) continue;
            unsigned char spazio[128] __attribute__((aligned(8)));
            memset(spazio, 0, sizeof(spazio));
            bmw::ImageID* id = new (spazio) bmw::ImageID(kIdCifra0 + (u32)c);
            g_app->updateModel(kModCifraZip0 + k, *id);
            g_cifraZip[k] = c;
            ++g_cifreStat;
        }
        if (prima) { g_primaChiamata[4] = true; scriviLog("stage 4: first ImageID updates returned"); }
        return;
    }
    if (g_propVietata || g_fase == 0) return;
    u32 prima = g_chiamateProp;
    // 3) LEDs: visibility
    int accesi = limita((giri - kGiriLed) / kGiriPassoLed + 1, 0, (int)kLed);
    if (giri < kGiriLed) accesi = 0;
    if (accesi != g_ledAccesi) {
        for (u32 i = 0; i < kLed; ++i) {
            bool on = (int)i < accesi, eraOn = (int)i < g_ledAccesi;
            if (g_ledAccesi < 0 || on != eraOn) proprietaBool(kCompLed0 + i, kPropVisibile, on);
        }
        g_ledAccesi = accesi;
    }
    // 4) rev bar: width
    if (g_fase >= 2) {
        int w = limita(giri * kBarraW / kGiriMax, 4, kBarraW);
        if (g_barraW < 0 || w - g_barraW >= 4 || g_barraW - w >= 4) { proprietaInt(kCompBarra, kPropLarghezza, w); g_barraW = w; }
    }
    // 5) G-meter dot: position (1 g = radius); demo circle without data
    if (g_fase >= 3) {
        int lat = g_lat, lon = g_lon;
        if (!g_haAccel || (lat < 50 && lat > -50 && lon < 50 && lon > -50)) {
            static const int c[16] = {1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383, 0, 383, 707, 924};
            u32 k = (ora / 125) % 16;
            lat = c[k] * 6 / 10; lon = c[(k + 4) % 16] * 6 / 10;
        }
        int x = kCerchioX - kPunto / 2 + limita(lat, -1000, 1000) * kRaggio / 1000;
        int y = kCerchioY - kPunto / 2 - limita(lon, -1000, 1000) * kRaggio / 1000;
        if (x - g_puntoX >= 2 || g_puntoX - x >= 2) { proprietaInt(kCompPunto, kPropX, x); g_puntoX = x; }
        if (y - g_puntoY >= 2 || g_puntoY - y >= 2) { proprietaInt(kCompPunto, kPropY, y); g_puntoY = y; }
    }
    if (g_chiamateProp != prima) {
        g_propStat += g_chiamateProp - prima;
        if (!g_primaChiamata[g_fase]) { g_primaChiamata[g_fase] = true; scriviLog("stage %u: first setProperty calls returned", g_fase); }
    }
}

void statistiche(u32 ora) {
    u32 dt = ora - g_inizioStat;
    if (dt < kOgniStatMs) return;
    char riga[700];
    u32 k = (u32)snprintf(riga, sizeof(riga), "%u ms: ticks %u (asked %u, slowest %u ms) digits %u text %u setProperty %u | CDS:",
                          dt, g_tickStat, dt / kTickMs, g_tickMaxMs, g_cifreStat, g_testiStat, g_propStat);
    for (u32 i = 0; i < kNumProprieta && k < sizeof(riga) - 40; ++i) {
        u32 e = g_eventi[i];
        if (e != g_eventiStat[i]) k += (u32)snprintf(riga + k, sizeof(riga) - k, " %s=%u", kProprieta[i], e - g_eventiStat[i]);
        g_eventiStat[i] = e;
    }
    scriviLog("%s", riga);
    // the last raw value of every property, every 30 s
    if ((ora - g_inizio) / 30000 != (g_inizioStat - g_inizio) / 30000)
        for (u32 i = 0; i < kNumProprieta; ++i) if (g_ultimo[i][0]) scriviLog("last %s: %s", kProprieta[i], g_ultimo[i]);
    g_inizioStat = ora;
    g_tickStat = g_cifreStat = g_testiStat = g_propStat = g_tickMaxMs = 0;
    bool c = false;
    u32 vai = leggiNumeroFile(kVai, &c);
    if (c) { unlink(kVai); scriviLog("%s: stage %u asked from the shell", kVai, vai); if (vai <= 4) impostaFase(vai); }
}

struct Giro { void** vptr; };
Giro  g_giro;
void* g_giroVtbl[5];

void giroNoop(void*) {}

void giroRun(void*) {
    if (!g_app || g_finito) return;
    u32 ora = adessoMs();
    ++g_tick; ++g_tickStat;
    if (g_tick == kGiroCds) registraCds();
    leggiValori();
    aggiornaSchermo(ora);
    statistiche(ora);
    if (!g_sano && ora - g_inizio > kSecondiSano * 1000) {
        g_sano = true; scriviNumeroFile(kAvvii, 0); scriviLog("%u s running: start counted as good", kSecondiSano);
    }
    u32 durata = adessoMs() - ora;
    if (durata > g_tickMaxMs) g_tickMaxMs = durata;
    g_app->post(reinterpret_cast<bmw::Runnable*>(&g_giro), kTickMs);
}

void avviaGiro(bmw::PluginApp* app) {
    void** base = reinterpret_cast<void**>(dlsym((void*)-2 /* RTLD_DEFAULT on QNX */, "_ZTVN3bmw8RunnableE"));
    if (base) memcpy(g_giroVtbl, base, sizeof(g_giroVtbl));
    else { g_giroVtbl[0] = 0; g_giroVtbl[1] = 0; }
    g_giroVtbl[2] = reinterpret_cast<void*>(&giroNoop);
    g_giroVtbl[3] = reinterpret_cast<void*>(&giroNoop);
    g_giroVtbl[4] = reinterpret_cast<void*>(&giroRun);
    g_giro.vptr = g_giroVtbl + 2;
    app->post(reinterpret_cast<bmw::Runnable*>(&g_giro), kTickMs);
}

void caricaImmagini() {
    static const char* const fissi[] = {"background.png", "sample256.png", "samplergba.png", "bar.png", "dot.png"};
    static const u32 modelli[] = {kModSfondo, kMod256, kModRgba, kModBarra, kModPunto};
    for (u32 i = 0; i < 5; ++i) {
        unsigned char* b = bloccoDaFile(fissi[i]);
        if (b) { u32 n; memcpy(&n, b, 4); mostraImmagine(modelli[i], b); free(b); scriviLog("%s: %u bytes", fissi[i], n); }
        else scriviLog("%s: MISSING", fissi[i]);
    }
    g_led = bloccoDaFile("led.png");
    for (u32 i = 0; i < kLed; ++i) mostraImmagine(kModLed0 + i, g_led);
    u32 tot = 0;
    for (u32 c = 0; c <= 10; ++c) {
        char nome[24];
        snprintf(nome, sizeof(nome), c < 10 ? "digit%u.png" : "digit_.png", c);
        g_cifra[c] = bloccoDaFile(nome);
        if (g_cifra[c]) { u32 n; memcpy(&n, g_cifra[c], 4); tot += n; } else scriviLog("%s: MISSING", nome);
    }
    scriviLog("led.png %s, digits in memory: %u bytes", g_led ? "ok" : "MISSING", tot);
    for (u32 k = 0; k < kCifre; ++k) g_cifraMostrata[k] = -1;
}

// ---- life cycle ----------------------------------------------------------------------------------------------
bool appOnInit(bmw::PluginApp* self) {
    bmw::DelegatoAzione d;
    d.oggetto = 0;
    d.funzione = &premutoFase;
    self->addHandler(kAzioneFase, d);
    bmw::DelegatoAzione nulla;
    nulla.oggetto = 0;
    nulla.funzione = &premutoDati;
    self->addHandler(kAzioneDati, nulla);
    bool ok = self->onInit();
    scriviLog("onInit base -> %d", (int)ok);
    { long ts[2] = {0, 0}; int r = clock_gettime(CLOCK_MONOTONIC_Q, ts); scriviLog("monotonic clock: %s", r == 0 ? "ok" : "NOT available (seconds only)"); }
    g_inizio = g_inizioStat = adessoMs();
    bool c = false;
    u32 f = leggiNumeroFile(kFase, &c);
    if (c && f >= 1 && f <= 3) {
        g_propVietata = true;
        scriviLog("%s says stage %u: the HMI restarted during that stage - setProperty stays OFF until the head unit is switched off", kFase, f);
    }
    if (c && f == 4) {
        g_idVietato = true;
        scriviLog("%s says stage 4: the HMI restarted during that stage - ImageID updates stay OFF until the head unit is switched off", kFase);
    }
    caricaImmagini();
    impostaFase(0);
    avviaGiro(self);
    return ok;
}

void appOnError(bmw::PluginApp*, u32 code, const bmw::BasicString<char>& text) {
    const char* s = text.raw();
    scriviLog("onError %u: %s", code, s ? s : "(null)");
}

void appOnFinalize(bmw::PluginApp* self) {
    scriviLog("onFinalize");
    g_finito = true;
    self->onFinalize();
}

bmw::PluginApp* creaApp(bmw::IDispatcher& dispatcher, const char* name, const bmw::Bundle& params, const bmw::Bundle& config) {
    scriviLog("create(%s) %s %s", name ? name : "(null)", NOME_PLUGIN, VERSIONE);
    if (access(kDisable, F_OK_Q) == 0) { scriviLog("off: %s exists", kDisable); return 0; }
    u32 avvii = leggiNumeroFile(kAvvii, 0);
    if (avvii >= kMaxAvvii) { scriviLog("off: %u starts without reaching %u s (delete %s to try again)", avvii, kSecondiSano, kAvvii); return 0; }
    scriviNumeroFile(kAvvii, avvii + 1);
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
    scriviLog("create: app %p, start no. %u", (void*)app, avvii + 1);
    return app;
}

void distruggiApp(void* app) {
    scriviLog("destroy(%p)", app);
    if (!app) return;
    g_finito = true;
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
