// MINI Sport - in-car sport displays for NBT EVO (ID6), hu-omap: shift light and revs, G-meter, timers
// (0-100, 0-50, 80-120, braking 100-0), engine gauges. No phone, no network: car data from the head unit (CDS),
// pictures installed with the plugin.
//
// How things move on the screen (what SportTest t1, the ProvaZip tests and the s1 test of 13/11 showed in the car):
//   - every moving part is a picture of MiniSport_ALL_Images.zip chosen by id (updateModel + ImageID, as the digits of
//     SportTest t1): LEDs and segments on/off, digits, bars in steps, labels. The zip is kept inside what loaded in the
//     car (ProvaZip B: 400 pictures, 94 KB, stored, RGBA PNG; the 801-picture 5 MB zip of s1 did not load); make.py
//     refuses to build a bigger one. The icon of the list is the LAST picture of the zip: if it shows, the HMI loaded
//     the whole zip;
//   - the G-meter dot and the centring of numbers move a picture (setProperty x/y);
//   - showing/hiding and changing the width of a picture (setProperty 3 / 9) did nothing in the car: not used;
//   - a picture or a position is sent only when it changes, at most kMaxAggiornamenti per tick (the rest goes on the
//     next tick), 10 ticks per second.
// The page backgrounds (bg_<page>_<theme>.png and bg_menu_<theme>.png, 256 colours, 30-70 KB) are sent as pictures
// at the start and when the theme changes (all of them). The moving pictures: only the page chosen last in the menu.
// Theme: MID/GREEN = blue accents, SPORT = red accents, from the car's driving mode (CDS driving.mode, seen in the
// car 13/11: 3 = MID, the mode at every start; 4 = SPORT). GREEN has no colour of its own on the head unit: MID.
// The SPORT values can be changed with /opt/aa/minisport.sport ("4" or "4 5").
//
// Files:
//   /opt/aa/minisport.disable        exists: the plugin stays off
//   /opt/aa/minisport.sport          optional: driving.mode values that mean SPORT (default 4)
//   /opt/aa/minisport.marce          optional, manual gearbox (the car sends only reverse): rpm per km/h x10 of every gear,
//                                    first gear first, e.g. "1167 625 392 281 220 183" (F60 Cooper D ALL4, measured 16/11)
//   /opt/aa/minisport.motore         optional: fuel type (6 = diesel): the shift light starts with it; the car sends its
//                                    own (engine.info) only when the ignition changes state, not at a start of the head unit
//   /dev/shmem/MiniSport.avvii       starts that did not reach 30 s; at 3 the plugin stays off (RAM: lost when the
//                                    head unit is switched off, kept when only the HMI restarts)
//   <stick>/nbt-evo-apps/MiniSport.avvii   the same count on the USB stick (kept across a full restart, when the stick
//                                    is in and mounted when the plugin starts); the higher of the two counts
//   /dev/shmem/MiniSport.demo        exists: demo values (engine off, to check the pages parked)
//   /dev/shmem/MiniSport.tema        optional "0" (MID) or "1" (SPORT): forces the theme
//   /dev/shmem/MiniSport.record      best times of the timers, kept until the head unit is switched off
//   /dev/shmem/MiniSport.log         log; also <stick>/nbt-evo-apps/MiniSport.log with a USB stick
#include "../../framework/bmw_plugin.h"
#include "layout.h"

#define NOME_PLUGIN "MiniSport"
#define VERSIONE MS_VERSIONE
#define BUILD "16/11 f: as e; diesel dial of the revs page (6000 rpm, red from 4500), numbers of the engine page centred again after a page change"   // the version is decided by the owner: this only tells the builds apart

namespace {

// ---- constants ---------------------------------------------------------------------------------------------
const char* const kDisable   = "/opt/aa/minisport.disable";
const char* const kFileSport = "/opt/aa/minisport.sport";
const char* const kFileMarce = "/opt/aa/minisport.marce";    // optional: rpm per km/h x10 of every gear, first gear first (manual gearbox)
const char* const kFileMotore = "/opt/aa/minisport.motore";   // optional: fuel type as engine.info gives it (6 = diesel), used until the car sends its own
const char* const kAvvii     = "/dev/shmem/" NOME_PLUGIN ".avvii";
const char* const kDemo      = "/dev/shmem/" NOME_PLUGIN ".demo";
const char* const kTemaForz  = "/dev/shmem/" NOME_PLUGIN ".tema";
const char* const kRecord    = "/dev/shmem/" NOME_PLUGIN ".record";
const char* const kLogShmem  = "/dev/shmem/" NOME_PLUGIN ".log";
const char* const kDirPlugin = "/opt/hmi/ID5/data/ro/Plugins/" NOME_PLUGIN;
const u32 kTickMs      = 100;          // 10 ticks per second (SportTest ran 25 asked, ~19 done)
const u32 kMaxAggiornamenti = 40;      // pictures + positions sent in one tick at most (a page needs up to ~60)
const u32 kSecondiSano = 30;
const u32 kMaxAvvii    = 3;
const u32 kOgniStatMs  = 30000;
const u32 kOgniFileMs  = 5000;         // demo / theme files checked every 5 s
const u32 kGiroCds     = 15;           // CDS registered ~1.5 s after onInit (as NBT-AAD), in ticks
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };
const u32 kAzioneScelta0 = 20;         // selectAction of the 4 menu buttons: raAction 20..23
const u32 kModTestoPulsante0 = 41;     // text models of the 4 menu buttons: a space, the names are in the background

enum { PAG_GIRI = 0, PAG_G, PAG_CRONO, PAG_MOTORE, PAG_N };

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

const char* strrchr_q(const char* s, char c) {        // last c in s (no strrchr declared in bmw_plugin.h)
    const char* r = 0;
    for (; *s; ++s) if (*s == c) r = s;
    return r;
}
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

// the start count on the stick: <stick>/nbt-evo-apps/MiniSport.avvii, only if that folder exists (false: no stick)
bool avviiSuChiavetta(char* out, u32 n) {
    cercaChiavetta();
    const char* fine = g_logChiavetta[0] ? strrchr_q(g_logChiavetta, '/') : 0;
    if (!fine) return false;
    u32 lc = (u32)(fine - g_logChiavetta);
    if (lc < 14 || strncmp(fine - 13, "/nbt-evo-apps", 13) != 0 || lc + 1 + strlen(NOME_PLUGIN) + 7 >= n) return false;
    memcpy(out, g_logChiavetta, lc);
    snprintf(out + lc, n - lc, "/%s.avvii", NOME_PLUGIN);
    return true;
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
// up to 8 numbers of a small text file; returns how many (-1: no file)
int leggiNumeri(const char* path, int* out, int max) {
    int fd = open(path, O_RDONLY_Q);
    if (fd < 0) return -1;
    char b[96];
    memset(b, 0, sizeof(b));
    read(fd, b, sizeof(b) - 1);
    close(fd);
    int n = 0;
    for (int i = 0; b[i] && n < max;) {
        bool neg = false;
        if (b[i] == '-' && b[i + 1] >= '0' && b[i + 1] <= '9') { neg = true; ++i; }
        if (b[i] >= '0' && b[i] <= '9') {
            int v = 0;
            while (b[i] >= '0' && b[i] <= '9') { if (v < 100000000) v = v * 10 + (b[i] - '0'); ++i; }
            out[n++] = neg ? -v : v;
        } else {
            ++i;
        }
    }
    return n;
}

void scriviNumeri(const char* path, const int* v, int n) {
    void* f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < n; ++i) fprintf(f, i ? " %d" : "%d", v[i]);
    fprintf(f, "\n");
    fclose(f);
}

// [u32 size][bytes] of a file in the plugin folder (pictures: at most 1 MB); every failure is written in the log
unsigned char* bloccoDaFile(const char* nome) {
    char path[192];
    snprintf(path, sizeof(path), "%s/%s", kDirPlugin, nome);
    int fd = open(path, O_RDONLY_Q);
    if (fd < 0) { scriviLog("%s: MISSING (cannot open %s)", nome, path); return 0; }
    u32 cap = 1024 * 1024, tot = 0;     // the menu backgrounds are 264-267 KB: 256 KB cut them out
    unsigned char* b = (unsigned char*)malloc(4 + cap);
    if (!b) { close(fd); scriviLog("%s: no memory for %u bytes", nome, cap); return 0; }
    for (;;) {
        if (tot == cap) { close(fd); free(b); scriviLog("%s: bigger than %u bytes, not sent", nome, cap); return 0; }
        ssize_t_q r = read(fd, b + 4 + tot, cap - tot);
        if (r <= 0) break;
        tot += (u32)r;
    }
    close(fd);
    if (!tot) { free(b); scriviLog("%s: empty file", nome); return 0; }
    memcpy(b, &tot, 4);
    return b;
}

// ---- screen ------------------------------------------------------------------------------------------------
u32 g_statImmagini = 0, g_statId = 0, g_statPos = 0;

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
    ++g_statImmagini;
}

bool mostraFile(u32 modello, const char* nome) {
    unsigned char* b = bloccoDaFile(nome);
    if (!b) return false;
    u32 n; memcpy(&n, b, 4);
    scriviLog("%s: %u bytes sent to model %u", nome, n, modello);
    mostraImmagine(modello, b);
    free(b);
    return true;
}

u32 g_aggiornamenti = 0;               // pictures + positions sent in this tick

void proprietaInt(u32 componente, u32 proprieta, int v) {
    unsigned char spazio[256] __attribute__((aligned(8)));
    memset(spazio, 0, sizeof(spazio));
    bmw::Parcel* p = new (spazio) bmw::Parcel(v);
    g_app->setProperty(componente, proprieta, *p);
    parcelDistruggi(spazio);
    ++g_statPos;
}

// what every piece shows now: picture id and position (0 / -99999 = unknown, sent again)
const int kSconosciuta = -99999;
u32 g_idMostrato[kNumPezzi];
int g_xMostrato[kNumPezzi], g_yMostrato[kNumPezzi];

u32 g_idSconosciuti = 0;

void metti(u32 pezzo, u32 id) {
    if (pezzo >= kNumPezzi || g_idMostrato[pezzo] == id) return;
    if (id < kIdPrimo || id > kIdUltimo) {
        if (g_idSconosciuti++ < 5) scriviLog("picture id %u unknown (piece %u)", id, pezzo);
        return;
    }
    if (g_aggiornamenti >= kMaxAggiornamenti) return;        // not marked as shown: sent on a next tick
    ++g_aggiornamenti;
    unsigned char spazio[128] __attribute__((aligned(8)));      // as SportTest t1, whose ImageID updates worked in the car
    memset(spazio, 0, sizeof(spazio));
    bmw::ImageID* img = new (spazio) bmw::ImageID(id);
    g_app->updateModel(kPezzi[pezzo].modello, *img);
    g_idMostrato[pezzo] = id;
    ++g_statId;
}

void spostaX(u32 pezzo, int x) {
    if (pezzo >= kNumPezzi || g_xMostrato[pezzo] == x || g_aggiornamenti >= kMaxAggiornamenti) return;
    ++g_aggiornamenti;
    proprietaInt(kPezzi[pezzo].comp, kPropX, x);
    g_xMostrato[pezzo] = x;
}

void spostaY(u32 pezzo, int y) {
    if (pezzo >= kNumPezzi || g_yMostrato[pezzo] == y || g_aggiornamenti >= kMaxAggiornamenti) return;
    ++g_aggiornamenti;
    proprietaInt(kPezzi[pezzo].comp, kPropY, y);
    g_yMostrato[pezzo] = y;
}

// first and last+1 piece of each page (the enum of layout.h is in page order)
const u32 kPrimo[PAG_N] = {P_GIRI_SEG0, P_G_PUNTO, P_CR_SEG0, P_MOT_SEG0_0};
const u32 kFine[PAG_N]  = {P_G_PUNTO, P_CR_SEG0, P_MOT_SEG0_0, kNumPezzi};

void dimenticaPagina(int pag) {
    for (u32 p = kPrimo[pag]; p < kFine[pag]; ++p) {
        g_idMostrato[p] = 0;
        g_xMostrato[p] = kSconosciuta;    // pieces moved before are put again where they must be
        g_yMostrato[p] = kSconosciuta;
    }
}

int limita(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
int assoluto(int v) { return v < 0 ? -v : v; }

// integer square root without divisions (ARMv7-A has no divide instruction and the helpers of the compiler are
// not in the HMI: every division in this file is by a constant)
int radice(u32 v) {
    u32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return (int)r;
}

// ---- car data (CDS) ----------------------------------------------------------------------------------------
const char* const kProprieta[] = {
    "engine.RPMSpeed", "driving.speedActual", "driving.acceleration", "driving.gear", "engine.torque",
    "driving.acceleratorPedal", "driving.clutchPedal", "driving.steeringWheel", "engine.temperature",
    "driving.mode", "sensors.battery", "engine.info",
};
enum { C_GIRI = 0, C_VELOCITA, C_ACCEL, C_MARCIA, C_COPPIA, C_GAS, C_FRIZIONE, C_STERZO, C_TEMP, C_MODO, C_BATTERIA, C_INFO };
const u32 kNumProprieta = sizeof(kProprieta) / sizeof(kProprieta[0]);
const u32 kMaxValore = 300;
const u32 kRawNelLog = 2;              // the first values of every property go to the log in full

void* volatile g_valori[kNumProprieta];         // last value not yet read (CDS callback -> tick)
volatile u32 g_eventi[kNumProprieta];
u32 g_eventiStat[kNumProprieta];
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

// number after "key" in a JSON text, in thousandths (12.345 -> 12345)
bool numeroDa(const char* s, const char* chiave, int* out) {
    const char* p = 0;
    u32 lc = strlen(chiave);
    for (const char* q = s; *q; ++q)
        if (*q == '"' && strncmp(q + 1, chiave, lc) == 0 && q[1 + lc] == '"') { p = q + 2 + lc; break; }
    if (!p) return false;
    while (*p && *p != '-' && (*p < '0' || *p > '9')) { if (*p == ',' || *p == '}' || *p == '{') return false; ++p; }
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

// values of the car (or of the demo); -1 / false = not received yet
struct Auto {
    int giri;            // rpm
    int velocita;        // km/h x1000
    int lat, lon;        // m/s2 x1000 (driving.acceleration; unit not confirmed in the car)
    int marcia;          // raw driving.gear
    int coppia;          // Nm
    int gas;             // accelerator pedal position, %
    int frizione;        // clutch pedal position (0/1)
    int sterzo;          // steering wheel angle, degrees x1000
    int acqua, olio;     // engine and oil temperature, °C
    int modo;            // driving.mode
    int batteria;        // sensors.battery, %, 255 = not valid
    bool haGiri, haVel, haAccel, haMarcia, haCoppia, haGas, haFrizione, haSterzo, haTemp, haModo, haBatteria;
};
Auto g_auto;
int g_carburante = -1;                 // engine.info fuelType, -1 = not received (15 = not valid); 6 = diesel on the F60 Cooper D

void leggiValori() {
    for (u32 i = 0; i < kNumProprieta; ++i) {
        char* b = (char*)__atomic_exchange_n(&g_valori[i], (void*)0, __ATOMIC_ACQ_REL);
        if (!b) continue;
        if (g_loggati[i] < kRawNelLog) { ++g_loggati[i]; scriviLog("raw %s #%u: %s", kProprieta[i], g_loggati[i], b); }
        int v, w;
        Auto& a = g_auto;
        switch (i) {
            case C_GIRI:      if (numeroDa(b, "RPMSpeed", &v)) { a.giri = v / 1000; a.haGiri = true; } break;
            case C_VELOCITA:  if (numeroDa(b, "speedActual", &v)) { a.velocita = v; a.haVel = true; } break;
            case C_ACCEL:     if (numeroDa(b, "lateral", &v) && numeroDa(b, "longitudinal", &w)) { a.lat = v; a.lon = w; a.haAccel = true; } break;
            case C_MARCIA:    if (numeroDa(b, "gear", &v)) { a.marcia = v / 1000; a.haMarcia = true; } break;
            case C_COPPIA:    if (numeroDa(b, "torque", &v)) { a.coppia = v / 1000; a.haCoppia = true; } break;
            case C_GAS:       if (numeroDa(b, "position", &v)) { a.gas = v / 1000; a.haGas = true; } break;
            case C_FRIZIONE:  if (numeroDa(b, "position", &v)) { a.frizione = v / 1000; a.haFrizione = true; } break;
            case C_STERZO:    if (numeroDa(b, "angle", &v)) { a.sterzo = v; a.haSterzo = true; } break;
            case C_TEMP:
                if (numeroDa(b, "engine", &v)) { a.acqua = v / 1000; a.haTemp = true; }
                if (numeroDa(b, "oil", &v)) { a.olio = v / 1000; a.haTemp = true; }
                break;
            case C_MODO:
                if (numeroDa(b, "mode", &v)) {
                    v /= 1000;
                    if (!a.haModo || v != a.modo) scriviLog("driving.mode %d -> %d", a.haModo ? a.modo : -1, v);
                    a.modo = v; a.haModo = true;
                }
                break;
            // battery: 255 = not valid (the car sends it for a while after the start): the last valid value is kept
            case C_BATTERIA:  if (numeroDa(b, "battery", &v) && v >= 0 && v <= 100000) { a.batteria = v / 1000; a.haBatteria = true; } break;
            case C_INFO:
                if (numeroDa(b, "fuelType", &v) && v / 1000 != 15 && v / 1000 != g_carburante) {
                    g_carburante = v / 1000;
                    scriviLog("engine.info fuelType %d -> shift light %s", g_carburante, g_carburante == 6 ? "diesel" : "petrol");
                }
                break;
            default: break;
        }
        free(b);
    }
}

// ---- demo (engine off, parked): /dev/shmem/MiniSport.demo ------------------------------------------------------
bool g_demo = false;
u32 g_demoInizio = 0;

Auto demoAuto(u32 ora) {
    Auto a = g_auto;
    u32 t = ora - g_demoInizio;
    // revs: up and down 1000..7000 in 8 s
    u32 f = t & 8191u;
    int x = (int)(f < 4096u ? f : 8192u - f);          // 0..4096
    a.giri = 1000 + ((x * 1465) >> 10);                 // 1000..~6860 (x * 6000 / 4096)
    if (x > 3900) a.giri = 6400;
    a.haGiri = true;
    // speed: 0 for 3 s, up to 130 in 10 s, down to 0 in 4 s, cycle of 18 s
    u32 c = t % 18000u;
    int v;
    if (c < 3000) v = 0;
    else if (c < 13000) v = (int)(c - 3000) * 13;       // km/h x1000: 0..130000
    else if (c < 17000) v = 130000 - (int)(c - 13000) * 32;
    else v = 0;
    a.velocita = v < 0 ? 0 : v; a.haVel = true;
    // acceleration: a circle of ~0.6 g every 6.4 s (m/s2 x1000)
    static const int cs[16] = {1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383, 0, 383, 707, 924};
    u32 k = (t >> 9) % 16u;
    a.lat = cs[k] * 6; a.lon = cs[(k + 4) % 16u] * 6; a.haAccel = true;
    a.marcia = (int)((t >> 13) % 6u) + 5; a.haMarcia = true;     // raw values 5..10 = gears 1..6
    a.coppia = (x * 230) >> 12; a.haCoppia = true;
    a.gas = (x * 100) >> 12; a.haGas = true;
    a.frizione = (t >> 11) & 1; a.haFrizione = true;
    a.sterzo = (cs[k] * 90); a.haSterzo = true;
    a.olio = 100; a.acqua = 89; a.haTemp = true;
    a.batteria = 82; a.haBatteria = true;
    return a;
}

// ---- theme ---------------------------------------------------------------------------------------------------
int g_modiSport[8] = {4};
int g_nModiSport = 1;
int g_temaForzato = -1;
int g_tema = 0;                        // 0 = MID/GREEN, 1 = SPORT

int temaDa(const Auto& a) {
    if (g_temaForzato == 0 || g_temaForzato == 1) return g_temaForzato;
    if (!a.haModo) return 0;
    for (int i = 0; i < g_nModiSport; ++i) if (a.modo == g_modiSport[i]) return 1;
    return 0;
}

// ---- backgrounds ---------------------------------------------------------------------------------------------
const char* const kNomePag[PAG_N] = {"giri", "gmeter", "crono", "motore"};
const u32 kModSfondoPag[PAG_N] = {kModSfondo110, kModSfondo111, kModSfondo112, kModSfondo113};
int g_sfondoPag[PAG_N] = {-1, -1, -1, -1};      // theme of the background sent, -1 = none
int g_sfondoMenu = -1;                           // theme of the menu background sent, -1 = none

void sfondoPagina(int pag) {
    // the revs page has a second dial for the diesel engines (6000 rpm, red from 4500): bg_giri_diesel_<theme>.png
    const bool diesel = pag == PAG_GIRI && g_carburante == 6;
    const int quale = g_tema + (diesel ? 2 : 0);
    if (g_sfondoPag[pag] == quale) return;
    char nome[40];
    snprintf(nome, sizeof(nome), "bg_%s%s_%s.png", kNomePag[pag], diesel ? "_diesel" : "", g_tema ? "sport" : "mid");
    if (mostraFile(kModSfondoPag[pag], nome)) g_sfondoPag[pag] = quale;
}

void sfondoMenu() {
    if (g_sfondoMenu == g_tema) return;
    char nome[40];
    snprintf(nome, sizeof(nome), "bg_menu_%s.png", g_tema ? "sport" : "mid");
    if (mostraFile(kModSfondo100, nome)) g_sfondoMenu = g_tema;
}

// ---- digits --------------------------------------------------------------------------------------------------
// digits of 0..9999 into c[4], 10 = empty for the leading zeros (constant divisions only)
void cifreDi(int v, int* c) {
    v = limita(v, 0, 9999);
    c[0] = v / 1000; c[1] = (v / 100) % 10; c[2] = (v / 10) % 10; c[3] = v % 10;
    if (v < 1000) c[0] = 10;
    if (v < 100) c[1] = 10;
    if (v < 10) c[2] = 10;
}

// n digits right-aligned in n pieces (the pieces are in a row in the XML); frame 10 of a digit set = empty
template <int N> void numeroDestra(u32 primo, int n, const u32 (&ids)[2][N], int v) {
    int c[4];
    cifreDi(v, c);
    if (v == 0) c[3] = 0;
    for (int k = 0; k < n; ++k) metti(primo + k, ids[g_tema][c[4 - n + k]]);
}

// ---- gear from revs and speed (manual gearbox) -------------------------------------------------------------------
int g_rapp[6];                         // rpm per km/h x10 of every gear, from /opt/aa/minisport.marce
int g_limite[5];                       // limit between gear i+1 and i+2: the geometric mean of their two values
int g_nRapp = 0;
int g_marciaCalc = 13, g_marciaCand = 13, g_marciaConta = 0;      // indexes of ID_MARCIA (11 = N, 13 = "-")

// index of ID_MARCIA, or -1 = nothing new to say (clutch pressed while moving, too slow): what is shown stays
int marciaCalcolata(const Auto& a) {
    if (g_nRapp < 2 || !a.haGiri || !a.haVel || !a.haFrizione) return 13;
    int kmh = a.velocita / 1000;
    if (kmh < 3) return a.frizione == 0 && a.giri > 300 ? 11 : 13;   // stopped, clutch released, engine running: neutral
    if (a.frizione != 0 || kmh < 8 || a.giri < 300) return -1;
    // rpm x10 against value x km/h: products instead of the division (no divide helpers in the HMI); at most 99990 and 3900 x 300
    int g = limita(a.giri, 0, 9999) * 10;
    if (kmh > 300) kmh = 300;
    if (g > g_rapp[0] * 13 / 10 * kmh || g < g_rapp[g_nRapp - 1] * 9 / 10 * kmh) return 13;
    int i = 0;
    while (i < g_nRapp - 1 && g < g_limite[i] * kmh) ++i;
    return i + 1;
}

// ---- page: revs and shift light ------------------------------------------------------------------------------
// shift light: {first LED, step between LEDs, all LEDs blink from} - petrol, diesel (engine.info fuelType 6)
const int kShift[2][3] = {{4200, 200, 6200}, {3000, 100, 4000}};
int g_giriLayout = -1;                 // 1 = four digits with the dot, 0 = three digits

void paginaGiri(const Auto& a, u32 ora) {
    const int t = g_tema;
    int giri = a.haGiri ? limita(a.giri, 0, 9999) : 0;
    // ring: 20 segments up to 7000 rpm, or up to 6000 with the colours of the diesel dial (divisions by constants only)
    const bool diesel = g_carburante == 6;
    // giri * kSegN / maximum, written with ONE constant divisor (the compiler turned two into a division by a variable,
    // which the HMI can not do): x / 6000 = x * 7 / 42000, x / 7000 = x * 6 / 42000
    int accesi = limita(giri * kSegN * ((diesel ? kGiriMax : kGiriMaxDiesel) / 1000) / (kGiriMax / 1000 * kGiriMaxDiesel), 0, kSegN);
    for (int k = 0; k < kSegN; ++k) metti(P_GIRI_SEG0 + k, (diesel ? IDG_SEGD : IDG_SEG)[k][t][k < accesi ? 0 : 1]);
    // shift light: 10 LEDs (petrol 4200 to 6000 rpm, blink from 6200; diesel 3000 to 3900, blink from 4000)
    const int* sh = kShift[g_carburante == 6 ? 1 : 0];
    bool lampo = giri >= sh[2] && ((ora / 100) & 1);
    for (int i = 0; i < 10; ++i) {
        bool on = giri >= sh[0] + i * sh[1] && !lampo;
        metti(P_GIRI_SHIFT0 + i, IDG_SHIFT[i][t][on ? 0 : 1]);
    }
    // number: "4.230" centred on the dial (pictures moved only when the length changes)
    int c[4];
    cifreDi(giri, c);
    int layout = giri >= 1000 ? 1 : 0;
    const int cc = kCella_C56, mezzo = kLarg_C56 / 2;
    {   // every tick: spostaX does nothing when the piece is in place, and a move skipped in a full tick is done on the next
        g_giriLayout = layout;
        if (layout) {
            int sx = kGiriCx - (4 * cc + kAvanzoPunto56) / 2;
            spostaX(P_GIRI_CIFRA0, sx + cc / 2 - mezzo);
            spostaX(P_GIRI_PUNTO, sx + cc + kAvanzoPunto56 / 2 - mezzo);
            for (int k = 1; k < 4; ++k) spostaX(P_GIRI_CIFRA0 + k, sx + cc + kAvanzoPunto56 + (k - 1) * cc + cc / 2 - mezzo);
        } else {
            int sx = kGiriCx - (3 * cc) / 2;
            for (int k = 1; k < 4; ++k) spostaX(P_GIRI_CIFRA0 + k, sx + (k - 1) * cc + cc / 2 - mezzo);
        }
    }
    metti(P_GIRI_CIFRA0, ID_C56[t][layout ? c[0] : 10]);
    metti(P_GIRI_PUNTO, ID_C56[t][layout ? 11 : 10]);
    for (int k = 1; k < 4; ++k) metti(P_GIRI_CIFRA0 + k, ID_C56[t][c[k]]);
    // gear: raw driving.gear as AAIdrive reads it: 0 = no data, 1 = N, 2 = R, 3 = P, 5.. = gears 1..
    // (16/11 in the car, manual gearbox: only 0 and, in reverse, 2)
    int fm = 13;                                     // "-"
    if (a.haMarcia && a.marcia != 0) {
        fm = a.marcia == 1 ? 11 : (a.marcia == 2 ? 12 : (a.marcia >= 5 && a.marcia <= 10 ? a.marcia - 4 : 13));
        g_marciaCalc = g_marciaCand = 13; g_marciaConta = 0;
    } else {
        int c = marciaCalcolata(a);                  // shown after 3 equal ticks in a row: no flicker while shifting
        if (c >= 0) {
            if (c == g_marciaCand) { if (g_marciaConta < 3) ++g_marciaConta; } else { g_marciaCand = c; g_marciaConta = 1; }
            if (g_marciaConta >= 3) g_marciaCalc = c;
        }
        fm = g_marciaCalc;
    }
    metti(P_GIRI_MARCIA, ID_MARCIA[t][fm]);
    // four values with their bars (21 steps)
    int val[4] = {a.haTemp ? a.olio : -1, a.haTemp ? a.acqua : -1, a.haGas ? a.gas : -1, a.haCoppia ? (a.coppia < 0 ? 0 : a.coppia) : -1};   // torque below zero (engine braking, idle): 0
    // steps 0..kBarraPassi (8): oil 50-150 °C, water 40-130 °C, throttle 0-100 %, torque 0-300 Nm (constant divisions)
    int passo[4] = {limita((val[0] - 50) * 2 / 25, 0, kBarraPassi), limita((val[1] - 40) * 4 / 45, 0, kBarraPassi),
                    limita(val[2] * 2 / 25, 0, kBarraPassi), limita(val[3] * 2 / 75, 0, kBarraPassi)};
    static const u32 primo[4] = {P_GIRI_VAL0_0, P_GIRI_VAL1_0, P_GIRI_VAL2_0, P_GIRI_VAL3_0};
    static const u32 barra[4] = {P_GIRI_BARRA0, P_GIRI_BARRA1, P_GIRI_BARRA2, P_GIRI_BARRA3};
    for (int i = 0; i < 4; ++i) {
        if (val[i] < 0) {
            for (int k = 0; k < 3; ++k) metti(primo[i] + k, ID_C40[t][10]);
            metti(barra[i], (i < 2 ? ID_BARRA_VERDE[t] : ID_BARRA_ACC[t])[0]);
            continue;
        }
        numeroDestra(primo[i], 3, ID_C40, limita(val[i], 0, 999));
        const u32* ids = i < 2 ? ID_BARRA_VERDE[t] : ID_BARRA_ACC[t];
        metti(barra[i], ids[passo[i]]);
    }
}

// ---- page: G-meter -------------------------------------------------------------------------------------------
int g_scia[3][2];                      // trail: dot positions 100, 200, 300 ms ago
u32 g_sciaMs = 0;
int g_gMax[4];                         // left, right, push, braking, g x1000 since the start
int g_storia[kGrafN];                  // levels of the last minute, newest last
int g_piccoIntervallo = 0;
u32 g_storiaMs = 0;

template <int N> void tre(u32 primo, const u32 (&ids)[2][N], int milli) {     // "0,43": units, tenths, hundredths of g
    milli = limita(milli, 0, 9999);
    metti(primo, ids[g_tema][limita(milli / 1000, 0, 9)]);
    metti(primo + 1, ids[g_tema][(milli / 100) % 10]);
    metti(primo + 2, ids[g_tema][(milli / 10) % 10]);
}

void calcolaG(const Auto& a, int* gl, int* gn) {
    // m/s2 x1000 -> g x1000 (9.807 m/s2 = 1 g)
    *gl = a.haAccel ? limita(a.lat, -30000, 30000) * 100 / 981 : 0;
    *gn = a.haAccel ? limita(a.lon, -30000, 30000) * 100 / 981 : 0;
}

void seguiG(const Auto& a, u32 ora) {       // maxima and history: every tick, whatever page is shown
    int gl, gn;
    calcolaG(a, &gl, &gn);
    if (-gl > g_gMax[0]) g_gMax[0] = -gl;
    if (gl > g_gMax[1]) g_gMax[1] = gl;
    if (gn > g_gMax[2]) g_gMax[2] = gn;
    if (-gn > g_gMax[3]) g_gMax[3] = -gn;
    int m = radice((u32)(gl * gl + gn * gn));
    if (m > g_piccoIntervallo) g_piccoIntervallo = m;
    if (ora - g_storiaMs >= 2000) {
        g_storiaMs = ora;
        for (int k = 0; k < kGrafN - 1; ++k) g_storia[k] = g_storia[k + 1];
        g_storia[kGrafN - 1] = limita((g_piccoIntervallo * kGrafLivelli + 500) / 1000, 0, kGrafLivelli);
        g_piccoIntervallo = 0;
    }
}

void paginaG(const Auto& a, u32 ora) {
    const int t = g_tema;
    int gl, gn;
    calcolaG(a, &gl, &gn);
    // dot: 1 g = radius of the outer ring; +lateral to the right, +longitudinal (pushing) up
    int px = limita(gl, -1150, 1150), py = limita(gn, -1150, 1150);
    const int lato = kPezzi[P_G_PUNTO].w;
    int x = kGmCx + px * kGmR / 1000 - lato / 2;
    int y = kGmCy - py * kGmR / 1000 - lato / 2;
    metti(P_G_PUNTO, ID_G_PUNTO[t][0]);
    if (assoluto(x - g_xMostrato[P_G_PUNTO]) >= 2) spostaX(P_G_PUNTO, x);
    if (assoluto(y - g_yMostrato[P_G_PUNTO]) >= 2) spostaY(P_G_PUNTO, y);
    if (ora - g_sciaMs >= 100) {
        g_sciaMs = ora;
        for (int k = 2; k > 0; --k) { g_scia[k][0] = g_scia[k - 1][0]; g_scia[k][1] = g_scia[k - 1][1]; }
        g_scia[0][0] = x + lato / 2; g_scia[0][1] = y + lato / 2;
        for (int k = 0; k < 3; ++k) {
            const u32 p = P_G_SCIA0 + k;
            const int l2 = kPezzi[p].w / 2;
            metti(p, IDG_G_SCIA[k][t][0]);
            int sx = g_scia[k][0] - l2, sy = g_scia[k][1] - l2;
            if (assoluto(sx - g_xMostrato[p]) >= 2) spostaX(p, sx);
            if (assoluto(sy - g_yMostrato[p]) >= 2) spostaY(p, sy);
        }
    }
    // now: size of the acceleration and where it pushes
    int m = radice((u32)(gl * gl + gn * gn));
    tre(P_G_ADESSO0, ID_C76, m);
    int scritta = 0;
    if (m >= 150) {
        if (assoluto(gl) >= assoluto(gn)) scritta = gl > 0 ? 1 : 2;
        else scritta = gn > 0 ? 3 : 4;
    }
    metti(P_G_TESTO, ID_G_TESTO[t][scritta]);
    for (int k = 0; k < 4; ++k) tre(P_G_MAX0_0 + 3 * k, ID_C30, g_gMax[k]);
    for (int k = 0; k < kGrafN; ++k) metti(P_G_GRAF0 + k, ID_G_BARRA[t][g_storia[k]]);
}

// ---- page: timers ------------------------------------------------------------------------------------------
enum { CR_FERMATI = 0, CR_PRONTO, CR_CORSA, CR_FATTO };
int g_cr = CR_FERMATI;
u32 g_crFermoDa = 0;                   // since when the car is stopped (0 = moving)
u32 g_crT0 = 0;                        // start of the run
int g_crTempo = -1;                    // time shown in the dial, ms (-1 = none)
bool g_cr50 = false;
int g_record[4] = {-1, -1, -1, -1};    // 0-100 ms, 0-50 ms, 80-120 ms, braking 100-0 mm
int g_ultimoRecord = -1;               // row of the last record (marked)
bool g_80armato = false, g_80corsa = false;
u32 g_80t0 = 0;
bool g_frArmata = false, g_frCorsa = false;
int g_frMm = 0, g_frPicco = 0;
u32 g_frT0 = 0, g_ultimoTick = 0;

void nuovoRecord(int riga, int v) {
    if (v <= 0) return;
    if (g_record[riga] < 0 || v < g_record[riga]) {
        g_record[riga] = v;
        g_ultimoRecord = riga;
        scriviNumeri(kRecord, g_record, 4);
        scriviLog("record %d: %d", riga, v);
    }
}

void seguiCrono(const Auto& a, u32 ora) {      // every tick, whatever page is shown
    u32 dt = g_ultimoTick ? ora - g_ultimoTick : 0;
    g_ultimoTick = ora;
    if (!a.haVel) return;
    int v = a.velocita;                         // km/h x1000
    int vk = v / 1000;
    // 0-100 and 0-50: from a stop
    if (vk == 0) {
        if (!g_crFermoDa) g_crFermoDa = ora;
        if (g_cr == CR_CORSA) { g_cr = CR_PRONTO; }            // stopped before 100: no time
        if (g_cr == CR_FERMATI || g_cr == CR_FATTO) { if (ora - g_crFermoDa >= 500) g_cr = CR_PRONTO; }
    } else {
        g_crFermoDa = 0;
        if (g_cr == CR_PRONTO) { g_cr = CR_CORSA; g_crT0 = ora; g_cr50 = false; g_ultimoRecord = -1; }
        if (g_cr == CR_CORSA) {
            g_crTempo = (int)(ora - g_crT0);
            if (!g_cr50 && vk >= 50) { g_cr50 = true; nuovoRecord(1, g_crTempo); }
            if (vk >= 100) { g_cr = CR_FATTO; nuovoRecord(0, g_crTempo); }
            if (g_crTempo > 60000) g_cr = CR_FERMATI;          // more than a minute: not a test
        }
    }
    // 80-120
    if (vk < 78) { g_80armato = true; g_80corsa = false; }
    if (g_80armato && vk >= 80) { g_80armato = false; g_80corsa = true; g_80t0 = ora; }
    if (g_80corsa && vk >= 120) { g_80corsa = false; nuovoRecord(2, (int)(ora - g_80t0)); }
    if (g_80corsa && ora - g_80t0 > 30000) g_80corsa = false;
    // braking 100-0: from below 100 km/h to a stop, within 10 s, without speeding up again
    if (vk >= 100) { g_frArmata = true; g_frCorsa = false; }
    if (g_frArmata && vk < 100) { g_frArmata = false; g_frCorsa = true; g_frMm = 0; g_frT0 = ora; g_frPicco = v; }
    if (g_frCorsa) {
        g_frMm += v / 36 * (int)dt / 100;                   // km/h x1000 * ms / 3600 = mm
        if (v > g_frPicco + 3000 || ora - g_frT0 > 10000) g_frCorsa = false;
        if (v < g_frPicco) g_frPicco = v;
        if (vk == 0 && g_frCorsa) { g_frCorsa = false; nuovoRecord(3, g_frMm); }
    }
}

// kept apart: written as "c ? v / 100 : v / 10" the compiler makes it v / (c ? 100 : 10), a division by a value
// known only at run time, which needs the division helper the HMI does not have
__attribute__((noinline)) int diviso10(int v) { return v / 10; }
__attribute__((noinline)) int diviso100(int v) { return v / 100; }

// "dd,dd" (ms) or "ddd,d" (mm -> m) in the 4 pieces of a row; "-,--" without a record
void rigaRecord(int riga) {
    const int t = g_tema;
    const u32 p = P_CR_RIGA0_0 + 5 * riga;
    int v = g_record[riga];
    if (v < 0) {
        metti(p, ID_C30[t][10]);
        metti(p + 1, ID_C30[t][11]);
        metti(p + 2, ID_C30[t][11]);
        metti(p + 3, ID_C30[t][11]);
    } else {
        int n = riga == 3 ? diviso100(v) : diviso10(v);     // decimetres or hundredths of a second
        numeroDestra(p, 4, ID_C30, limita(n, 0, 9999));
        int c[4];
        cifreDi(limita(n, 0, 9999), c);
        if (riga == 3) { if (c[2] == 10) metti(p + 2, ID_C30[t][0]); }   // "0,8 m"
        else { if (c[1] == 10) metti(p + 1, ID_C30[t][0]); if (c[2] == 10) metti(p + 2, ID_C30[t][0]); }   // "0,45 s"
    }
    metti(P_CR_SEGNO0 + 5 * riga, ID_CR_SEGNO[t][g_ultimoRecord == riga ? 0 : 1]);
}

void paginaCrono(const Auto& a) {
    const int t = g_tema;
    int vk = a.haVel ? a.velocita / 1000 : 0;
    int accesi = limita(vk * kSegN / 100, 0, kSegN);
    for (int k = 0; k < kSegN; ++k) metti(P_CR_SEG0 + k, IDG_SEG[k][t][k < accesi ? 0 : 1]);   // the pictures of the revs ring
    metti(P_CR_NOME, ID_CR_NOME[t][0]);
    // time in the dial: "6,84" / "12,35", centred
    int simboli[5], ns = 0;
    if (g_crTempo >= 0) {
        int cs = limita(g_crTempo / 10, 0, 9999);         // hundredths
        int sec = cs / 100, cent = cs % 100;
        if (sec >= 10) simboli[ns++] = sec / 10;
        simboli[ns++] = sec % 10;
        simboli[ns++] = 11;                                 // ","
        simboli[ns++] = cent / 10;
        simboli[ns++] = cent % 10;
    }
    int larg = 0;
    for (int k = 0; k < ns; ++k) larg += simboli[k] == 11 ? kAvanzoVirgola76 : kCella_C76;
    int x = kCrTempoCx - larg / 2;
    for (int k = 0; k < 5; ++k) {
        const u32 p = P_CR_TEMPO0 + k;
        if (k >= ns) { metti(p, ID_C76[t][10]); continue; }
        int w = simboli[k] == 11 ? kAvanzoVirgola76 : kCella_C76;
        spostaX(p, x + w / 2 - kLarg_C76 / 2);
        metti(p, ID_C76[t][simboli[k]]);
        x += w;
    }
    numeroDestra(P_CR_VEL0, 3, ID_C30, limita(vk, 0, 999));
    static const int statoDi[4] = {2, 0, 1, 3};           // frames: pronto, via, fermati, fatto, fermo
    static const int luciDi[4] = {1, 2, 3, 0};            // frames: spente, rosso, verde, corsa
    metti(P_CR_STATO, ID_CR_STATO[t][a.haVel ? statoDi[g_cr] : 4]);
    metti(P_CR_LUCI, ID_CR_LUCI[t][a.haVel ? luciDi[g_cr] : 0]);
    for (int i = 0; i < 4; ++i) rigaRecord(i);
}

// ---- page: engine ------------------------------------------------------------------------------------------
int g_motCifre[4] = {-1, -1, -1, -1};  // how many digits each dial number has (pictures moved when it changes)

void paginaMotore(const Auto& a) {
    const int t = g_tema;
    int val[4] = {a.haTemp ? a.olio : -1, a.haTemp ? a.acqua : -1, a.haGas ? a.gas : -1, a.haCoppia ? (a.coppia < 0 ? 0 : a.coppia) : -1};   // torque below zero (engine braking, idle): 0
    for (int i = 0; i < 4; ++i) {
        int accesi = val[i] < 0 ? 0 : limita(((val[i] - kMotMin[i]) * kMotK[i]) >> 16, 0, kMotN);
        const int schema = i < 2 ? 0 : 1;           // frames of a segment: 0 = temperature colours, 1 = throttle/torque, 2 = off
        for (int k = 0; k < kMotN; ++k) metti(P_MOT_SEG0_0 + i * kMotN + k, IDG_MSEG[k][t][k < accesi ? schema : 2]);
        const u32 p = P_MOT_VAL0_0 + 3 * i;
        if (val[i] < 0) { for (int k = 0; k < 3; ++k) metti(p + k, ID_C40[t][10]); continue; }
        int v = limita(val[i], 0, 999);
        int n = v >= 100 ? 3 : (v >= 10 ? 2 : 1);
        {   // every tick, as the revs number: a move skipped in a full tick (page just opened) left the number off centre
            g_motCifre[i] = n;
            int cx = kMotCx0 + i * kMotPasso;
            int sx = cx - n * kCella_C40 / 2;
            for (int k = 0; k < n; ++k) spostaX(p + k, sx + k * kCella_C40 + kCella_C40 / 2 - kLarg_C40 / 2);
        }
        int c[4];
        cifreDi(v, c);
        if (v == 0) c[3] = 0;
        for (int k = 0; k < 3; ++k) metti(p + k, k < n ? ID_C40[t][c[4 - n + k]] : ID_C40[t][10]);
    }
    // battery, %, right-aligned; 255 = not valid: "--"
    if (a.haBatteria && a.batteria >= 0 && a.batteria <= 100) {
        numeroDestra(P_MOT_BATT0, 3, ID_C30, a.batteria);
    } else {
        metti(P_MOT_BATT0, ID_C30[t][10]); metti(P_MOT_BATT0 + 1, ID_C30[t][11]); metti(P_MOT_BATT0 + 2, ID_C30[t][11]);
    }
    // steering wheel, degrees, right-aligned with the sign
    if (a.haSterzo) {
        int gr = a.sterzo / 1000;
        int v = limita(assoluto(gr), 0, 999);
        int c[4];
        cifreDi(v, c);
        if (v == 0) c[3] = 0;
        int primo = v >= 100 ? 1 : (v >= 10 ? 2 : 3);       // first piece with a digit
        for (int k = 0; k < 4; ++k) {
            u32 id = ID_C30[t][10];
            if (k >= primo) id = ID_C30[t][c[k]];
            else if (k == primo - 1 && gr < 0) id = ID_C30[t][11];
            metti(P_MOT_STERZO0 + k, id);
        }
    } else {
        for (int k = 0; k < 4; ++k) metti(P_MOT_STERZO0 + k, k == 3 ? ID_C30[t][11] : ID_C30[t][10]);
    }
    metti(P_MOT_FRIZIONE, ID_FRIZIONE[t][a.haFrizione && a.frizione > 0 ? 0 : 1]);
}

// ---- menu and tick ---------------------------------------------------------------------------------------------
int g_pagina = PAG_GIRI;               // page chosen last in the menu: the only one updated
u32 g_tick = 0, g_tickStat = 0, g_tickMaxMs = 0, g_inizio = 0, g_inizioStat = 0, g_ultimiFile = 0;
bool g_sano = false;

void scegli(int pag) {
    if (pag < 0 || pag >= PAG_N) return;
    if (pag != g_pagina) scriviLog("menu: %s", kNomePag[pag]);
    g_pagina = pag;
    sfondoMenu();
    sfondoPagina(pag);
    dimenticaPagina(pag);              // everything of the page is sent again (it may have been shown before)
    g_giriLayout = -1;
    for (int i = 0; i < 4; ++i) g_motCifre[i] = -1;
}

// the object of the delegate is the action id (20-23), never 0: in s1 the object of "Giri e cambiata" was 0 and its
// selectAction never reached the plugin in the car (13/11), while the other three (objects 1-3) did
bool sceltaMenu(void* oggetto, const bmw::Parcel&) {
    u32 azione = (u32)(unsigned long)oggetto;
    if (azione < kAzioneScelta0 || azione >= kAzioneScelta0 + PAG_N) { scriviLog("menu: action %u unknown", azione); return true; }
    scegli((int)(azione - kAzioneScelta0));
    return true;
}

void controllaFile(u32 ora) {
    if (ora - g_ultimiFile < kOgniFileMs && g_ultimiFile) return;
    g_ultimiFile = ora;
    bool demo = access(kDemo, F_OK_Q) == 0;
    if (demo != g_demo) { g_demo = demo; g_demoInizio = ora; scriviLog("demo %s", demo ? "ON" : "off"); }
    int v[1];
    int n = leggiNumeri(kTemaForz, v, 1);
    int f = n == 1 && (v[0] == 0 || v[0] == 1) ? v[0] : -1;
    if (f != g_temaForzato) { g_temaForzato = f; scriviLog("theme forced: %d", f); }
}

void statistiche(u32 ora, const Auto& a) {
    u32 dt = ora - g_inizioStat;
    if (dt < kOgniStatMs) return;
    char riga[700];
    u32 k = (u32)snprintf(riga, sizeof(riga), "%u ms: ticks %u (asked %u, slowest %u ms) page %s theme %d%s | pictures %u ids %u moves %u | CDS:",
                          dt, g_tickStat, dt / kTickMs, g_tickMaxMs, kNomePag[g_pagina], g_tema, g_demo ? " DEMO" : "",
                          g_statImmagini, g_statId, g_statPos);
    for (u32 i = 0; i < kNumProprieta && k < sizeof(riga) - 40; ++i) {
        u32 e = g_eventi[i];
        if (e != g_eventiStat[i]) k += (u32)snprintf(riga + k, sizeof(riga) - k, " %s=%u", kProprieta[i], e - g_eventiStat[i]);
        g_eventiStat[i] = e;
    }
    scriviLog("%s", riga);
    scriviLog("values: rpm %d km/h %d lat %d lon %d gear %d torque %d gas %d oil %d water %d mode %d battery %d steering %d clutch %d",
              a.giri, a.velocita / 1000, a.lat, a.lon, a.marcia, a.coppia, a.gas, a.olio, a.acqua, a.modo, a.batteria,
              a.sterzo / 1000, a.frizione);
    g_inizioStat = ora;
    g_tickStat = g_tickMaxMs = g_statImmagini = g_statId = g_statPos = 0;
}

int g_carburanteSfondo = -1;           // fuel type the background of the revs page was last chosen with
const u32 kMaxRapporti = 400;
u32 g_rapporti = 0, g_rapportiMs = 0;

struct Giro { void** vptr; };
Giro  g_giro;
void* g_giroVtbl[5];

void giroNoop(void*) {}

void giroRun(void*) {
    if (!g_app || g_finito) return;
    u32 ora = adessoMs();
    g_aggiornamenti = 0;
    ++g_tick; ++g_tickStat;
    if (g_tick == kGiroCds) registraCds();
    leggiValori();
    controllaFile(ora);
    if (g_carburante != g_carburanteSfondo) {       // the car said which engine it has: the dial of the revs page follows
        g_carburanteSfondo = g_carburante;
        sfondoPagina(PAG_GIRI);
    }
    // for the gear from revs and speed: one line every 2 s while driving with the clutch released (at most 400 per start)
    if (!g_demo && g_rapporti < kMaxRapporti && ora - g_rapportiMs >= 2000 && g_auto.haGiri && g_auto.haVel &&
        g_auto.haFrizione && g_auto.frizione == 0 && g_auto.velocita >= 10000) {
        g_rapportiMs = ora;
        ++g_rapporti;
        scriviLog("ratio: rpm %d km/h x10 %d torque %d gas %d", g_auto.giri, g_auto.velocita / 100, g_auto.coppia, g_auto.gas);
    }
    Auto a = g_demo ? demoAuto(ora) : g_auto;
    int tema = temaDa(a);
    if (tema != g_tema) {
        scriviLog("theme %s", tema ? "SPORT" : "MID/GREEN");
        g_tema = tema;
        sfondoMenu();
        for (int p = 0; p < PAG_N; ++p) sfondoPagina(p);   // every page: the HMI keeps what each state got last
        scegli(g_pagina);
    }
    seguiG(a, ora);
    seguiCrono(a, ora);
    switch (g_pagina) {
        case PAG_GIRI:   paginaGiri(a, ora); break;
        case PAG_G:      paginaG(a, ora); break;
        case PAG_CRONO:  paginaCrono(a); break;
        default:         paginaMotore(a); break;
    }
    statistiche(ora, a);
    if (!g_sano && ora - g_inizio > kSecondiSano * 1000) {
        g_sano = true;
        int zero = 0;
        scriviNumeri(kAvvii, &zero, 1);
        char suChiavetta[192];
        if (avviiSuChiavetta(suChiavetta, sizeof(suChiavetta))) scriviNumeri(suChiavetta, &zero, 1);
        scriviLog("%u s running: start counted as good", kSecondiSano);
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

// ---- life cycle ----------------------------------------------------------------------------------------------
bool appOnInit(bmw::PluginApp* self) {
    for (u32 i = 0; i < PAG_N; ++i) {
        bmw::DelegatoAzione d;
        d.oggetto = (void*)(unsigned long)(kAzioneScelta0 + i);
        d.funzione = &sceltaMenu;
        self->addHandler(kAzioneScelta0 + i, d);
    }
    bool ok = self->onInit();
    scriviLog("onInit base -> %d", (int)ok);
    for (u32 i = 0; i < PAG_N; ++i) self->updateModel(kModTestoPulsante0 + i, " ");
    g_inizio = g_inizioStat = adessoMs();
    int v[8];
    int n = leggiNumeri(kFileSport, v, 8);
    if (n > 0) {
        g_nModiSport = n;
        for (int i = 0; i < n; ++i) g_modiSport[i] = v[i];
        scriviLog("%s: %d driving.mode values mean SPORT (first %d)", kFileSport, n, v[0]);
    }
    if (leggiNumeri(kRecord, v, 4) == 4) { for (int i = 0; i < 4; ++i) g_record[i] = v[i]; scriviLog("records read again"); }
    for (u32 p = 0; p < kNumPezzi; ++p) { g_idMostrato[p] = 0; g_xMostrato[p] = kPezzi[p].x; g_yMostrato[p] = kPezzi[p].y; }
    for (int k = 0; k < 3; ++k) { g_scia[k][0] = -1000; g_scia[k][1] = -1000; }
    controllaFile(g_inizio);
    g_tema = temaDa(g_demo ? demoAuto(g_inizio) : g_auto);
    for (int p = 0; p < PAG_N; ++p) sfondoPagina(p);
    scegli(PAG_GIRI);
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
    scriviLog("create(%s) %s %s (build %s)", name ? name : "(null)", NOME_PLUGIN, VERSIONE, BUILD);
    if (access(kDisable, F_OK_Q) == 0) { scriviLog("off: %s exists", kDisable); return 0; }
    int avvii = 0, avviiChiavetta = 0;
    leggiNumeri(kAvvii, &avvii, 1);
    char suChiavetta[192];
    bool chiavetta = avviiSuChiavetta(suChiavetta, sizeof(suChiavetta));
    if (chiavetta) leggiNumeri(suChiavetta, &avviiChiavetta, 1);
    scriviLog("start count: RAM %d, stick %s %d", avvii, chiavetta ? suChiavetta : "(no stick)", avviiChiavetta);
    {
        int r[6];
        int n = leggiNumeri(kFileMarce, r, 6);
        bool ok = n >= 2;
        for (int i = 0; ok && i < n; ++i) if (r[i] < 50 || r[i] > 3000 || (i && r[i] >= r[i - 1])) ok = false;
        if (ok) {
            for (int i = 0; i < n; ++i) g_rapp[i] = r[i];
            for (int i = 0; i + 1 < n; ++i) g_limite[i] = radice((u32)(r[i] * r[i + 1]));
            g_nRapp = n;
        }
        if (n >= 0) scriviLog("%s: %d values, %s", kFileMarce, n, ok ? "gear from revs and speed" : "NOT used (2 to 6 falling numbers, 50..3000)");
    }
    {
        int m[1];
        if (leggiNumeri(kFileMotore, m, 1) == 1 && m[0] >= 0 && m[0] < 15) {
            g_carburante = m[0];
            scriviLog("%s: fuelType %d -> shift light %s", kFileMotore, g_carburante, g_carburante == 6 ? "diesel" : "petrol");
        }
    }
    if (avviiChiavetta > avvii) avvii = avviiChiavetta;
    if (avvii >= (int)kMaxAvvii) {
        scriviLog("off: %d starts without reaching %u s (delete %s and %s to try again)", avvii, kSecondiSano, kAvvii,
                  chiavetta ? suChiavetta : "<stick>/nbt-evo-apps/MiniSport.avvii");
        return 0;
    }
    int prossimo = avvii + 1;
    scriviNumeri(kAvvii, &prossimo, 1);
    if (chiavetta) scriviNumeri(suChiavetta, &prossimo, 1);
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
    scriviLog("create: app %p, start no. %d", (void*)app, prossimo);
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
