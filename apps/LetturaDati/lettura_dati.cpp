// Lettura dati (LetturaDati) - TEST plugin, READ ONLY: it listens to the car data (CDS) and writes what it receives
// to its log, and says which graphics libraries (EGL, OpenGL ES 2, screen, LayerManager) the HMI process already has
// loaded. It changes nothing in the car: no command, no file outside its log and its start counter.
// What it is for: which car data the head unit gives to a plugin (name, how often, what values look like) for the
// next apps, and whether drawing with the GPU from a plugin is worth a try (the libraries it would need).
//
// Car data: every property of IDriveConnectKit CDSProperty.kt (MIT) of the groups climate, comfort, controls,
// driving, engine, navigation, sensors, vehicle; NOT communication (phone calls, contacts), entertainment, the VIN,
// and the entries that are requests (search, journeys, URLs, onlineStartUseCase, tcProxy) rather than data.
// They are registered 20 at a time, one group per second, and every group is in the log BEFORE it is
// registered: if a name upsets the HMI, the last line of the log says which group. Car data are read ONLY with the stick in: before every group the
// plugin writes <stick>/nbt-evo-apps/LetturaDati.stato ("group N"), and a later start that finds a group there
// registers nothing - even after a restart of the whole head unit, which empties /dev/shmem (see controllaStato).
// The CDS callback does no malloc and no log: it counts, and copies one value into a fixed buffer when the plugin
// thread has taken the previous one. The plugin thread writes the log, in blocks (one open of the file per block).
//
// Log: /dev/shmem/LetturaDati.log (RAM: stops at 1.5 MB) and <stick>/nbt-evo-apps/LetturaDati.log (stops at 30 MB):
//   - start, the graphics libraries (symbol, address, library that has it);
//   - the first 3 values of every property as they arrive;
//   - every 30 s: how many values every property sent in those 30 s;
//   - every 2 minutes: the last value of every property, and the list of the properties that never sent anything.
// Screen: one page in the MINI Connected list, "Lettura dati", with counters (updated every second).
// Safety as the other plugins: /opt/aa/letturadati.disable switches it off; after 3 starts that do not reach 30 s
// it stays off (delete /dev/shmem/LetturaDati.avvii). Removed with: sh pkg.sh remove LetturaDati
#include "../../framework/bmw_plugin.h"

#define NOME_PLUGIN "LetturaDati"
#define VERSIONE "t0"

namespace {

// ---- constants ---------------------------------------------------------------------------------------------
const char* const kDisable  = "/opt/aa/letturadati.disable";
const char* const kAvvii    = "/dev/shmem/" NOME_PLUGIN ".avvii";
const char* const kLogShmem = "/dev/shmem/" NOME_PLUGIN ".log";
const u32 kTickMs        = 250;
const u32 kSecondiSano   = 30;
const u32 kMaxAvvii      = 3;
const u32 kPrimoGruppoMs = 1500;        // first CDS group 1.5 s after onInit (as MiniSport and NBT-AAD)
const u32 kOgniGruppoMs  = 1000;
const u32 kPerGruppo     = 20;
const u32 kOgniContiMs   = 30000;
const u32 kOgniValoriMs  = 120000;
const u32 kOgniSchermoMs = 1000;
const u32 kMaxShmem      = 1536 * 1024;
const u32 kMaxChiavetta  = 30u * 1024 * 1024;
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };

// models and actions (LetturaDati.xml, made by apps/LetturaDati/make.py)
const u32 kModRiga0 = 41, kRighe = 7;
const u32 kSel0 = 20, kNoOp = 59;

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
u32 g_scrittiShmem = 0, g_scrittiChiavetta = 0;
bool g_pienoShmem = false, g_pienoChiavetta = false;

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

// appends a text (one or more whole lines) to a log file, if it is still under its size limit
void aggiungi(const char* path, const char* testo, u32 n, u32* scritti, bool* pieno, u32 max) {
    if (*pieno || !path[0]) return;
    void* f = fopen(path, "a");
    if (!f) return;
    if (*scritti + n > max) {
        fprintf(f, "(log stopped here: %u bytes, the limit of this file)\n", *scritti);
        *pieno = true;
    } else {
        fprintf(f, "%s", testo);
        *scritti += n;
    }
    fclose(f);
}

// the log is written in blocks: lines collect in g_blocco and go to the files with one open per file
char g_blocco[48 * 1024];
u32 g_nBlocco = 0;

void svuota() {
    if (!g_nBlocco) return;
    g_blocco[g_nBlocco] = 0;
    aggiungi(kLogShmem, g_blocco, g_nBlocco, &g_scrittiShmem, &g_pienoShmem, kMaxShmem);
    cercaChiavetta();
    aggiungi(g_logChiavetta, g_blocco, g_nBlocco, &g_scrittiChiavetta, &g_pienoChiavetta, kMaxChiavetta);
    g_nBlocco = 0;
}

void accoda(const char* fmt, ...) {
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
    int n = snprintf(riga, sizeof(riga), "%02d:%02d:%02d UTC [%d] %s\n", tmv[2], tmv[1], tmv[0], getpid(), msg);
    if (n <= 0) return;
    if ((u32)n >= sizeof(riga)) n = sizeof(riga) - 1;
    if (g_nBlocco + (u32)n >= sizeof(g_blocco)) svuota();
    memcpy(g_blocco + g_nBlocco, riga, (u32)n);
    g_nBlocco += (u32)n;
}

void scriviLog(const char* fmt, ...) {          // one line, written at once
    char msg[900];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);
    accoda("%s", msg);
    svuota();
}

// ---- small files -------------------------------------------------------------------------------------------
int numeroDaFile(const char* path) {
    int fd = open(path, O_RDONLY_Q);
    if (fd < 0) return 0;
    char b[16];
    ssize_t_q r = read(fd, b, sizeof(b) - 1);
    close(fd);
    if (r <= 0) return 0;
    b[r] = 0;
    int n = 0;
    for (int i = 0; b[i] >= '0' && b[i] <= '9'; ++i) n = n * 10 + (b[i] - '0');
    return n;
}

bool scriviFile(const char* path, const char* testo) {
    void* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "%s\n", testo);
    fclose(f);
    return true;
}

// ---- screen: the text of the rows ------------------------------------------------------------------------
char g_testo[kRighe][120];

void riga(u32 r, const char* fmt, ...) {
    char t[120];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(t, sizeof(t), fmt, ap);
    __builtin_va_end(ap);
    if (!t[0]) { t[0] = ' '; t[1] = 0; }
    if (r >= kRighe || strncmp(g_testo[r], t, sizeof(t)) == 0) return;
    memcpy(g_testo[r], t, sizeof(t));
    g_app->updateModel(kModRiga0 + r, t);
}

// ---- graphics libraries of the HMI process (lookups only, nothing is called) -----------------------------
struct InfoDl { const char* fname; void* fbase; const char* sname; void* saddr; };   // Dl_info
typedef int (*FnDladdr)(void*, InfoDl*);
void* const kGlobale = (void*)-2;      // RTLD_DEFAULT on QNX

struct GruppoGfx { const char* nome; const char* simboli[6]; };
const GruppoGfx kGfx[] = {
    {"control (HMI framework)", {"_ZTVN3bmw8RunnableE", "_ZN3bmw9PluginApp11updateModelEjPKc", 0}},
    {"EGL", {"eglGetDisplay", "eglGetCurrentContext", "eglCreateWindowSurface", "eglCreatePixmapSurface", "eglQueryString", 0}},
    {"GLES2", {"glDrawArrays", "glTexImage2D", "glCreateShader", "glReadPixels", "glGetString", 0}},
    {"screen", {"screen_create_context", "screen_create_window", "screen_create_window_buffers", "screen_post_window", "screen_get_window_property_iv", 0}},
    {"ilm", {"ilm_init", "ilm_getLayerIDs", "ilm_surfaceCreate", "ilm_layerAddSurface", "ilm_commitChanges", 0}},
    {"KD", {"kdCreateWindow", "kdRealizeWindow", 0}},
};
const u32 kNumGfx = sizeof(kGfx) / sizeof(kGfx[0]);
u32 g_gfxTrovati[kNumGfx], g_gfxCercati[kNumGfx];

void controllaGrafica() {
    FnDladdr dladdr_ = (FnDladdr)dlsym(kGlobale, "dladdr");
    accoda("graphics: lookups in the HMI process (dlsym, global scope; dladdr %s)", dladdr_ ? "available" : "NOT found");
    for (u32 g = 0; g < kNumGfx; ++g) {
        g_gfxTrovati[g] = g_gfxCercati[g] = 0;
        for (u32 s = 0; s < 6 && kGfx[g].simboli[s]; ++s) {
            ++g_gfxCercati[g];
            void* a = dlsym(kGlobale, kGfx[g].simboli[s]);
            if (!a) { accoda("  %s: %s NOT found", kGfx[g].nome, kGfx[g].simboli[s]); continue; }
            ++g_gfxTrovati[g];
            InfoDl info;
            memset(&info, 0, sizeof(info));
            if (dladdr_ && dladdr_(a, &info) && info.fname)
                accoda("  %s: %s at %p in %s (base %p)", kGfx[g].nome, kGfx[g].simboli[s], a, info.fname, info.fbase);
            else
                accoda("  %s: %s at %p", kGfx[g].nome, kGfx[g].simboli[s], a);
        }
    }
    accoda("graphics: a library loaded only for itself (RTLD_LOCAL) is not seen here: the full list of the libraries of "
           "the process is in verifica.log (pidin mem)");
    svuota();
}

// ---- car data (CDS) ----------------------------------------------------------------------------------------
const char* const kProprieta[] = {
    "climate.ACCompressor", "climate.ACMode", "climate.ACSystemTemperatures", "climate.driverSettings",
    "climate.passengerSettings", "climate.residualHeat", "climate.seatHeatDriver", "climate.seatHeatPassenger",
    "controls.convertibleTop", "controls.cruiseControl", "controls.defrostRear", "controls.headlights",
    "controls.lights", "controls.startStopStatus", "controls.sunroof", "controls.turnSignal",
    "controls.windowDriverFront", "controls.windowDriverRear", "controls.windowPassengerFront",
    "controls.windowPassengerRear", "controls.windshieldWiper", "driving.acceleration", "driving.acceleratorPedal",
    "driving.averageConsumption", "driving.averageSpeed", "driving.brakeContact", "driving.clutchPedal",
    "driving.DSCActive", "driving.ecoTip", "driving.gear", "driving.keyPosition", "driving.odometer",
    "driving.parkingBrake", "driving.shiftIndicator", "driving.speedActual", "driving.speedDisplayed",
    "driving.steeringWheel", "driving.mode", "engine.consumption", "engine.info", "engine.RPMSpeed", "engine.status",
    "engine.temperature", "engine.torque", "navigation.currentPositionDetailedInfo", "navigation.finalDestination",
    "navigation.finalDestinationDetailedInfo", "navigation.GPSExtendedInfo", "navigation.GPSPosition",
    "navigation.guidanceStatus", "navigation.infoToNextDestination", "navigation.nextDestination",
    "navigation.nextDestinationDetailedInfo", "sensors.battery", "sensors.doors", "sensors.fuel",
    "sensors.PDCRangeFront", "sensors.PDCRangeRear", "sensors.PDCStatus", "sensors.seatOccupiedPassenger",
    "sensors.seatbelt", "sensors.temperatureExterior", "sensors.temperatureInterior", "sensors.trunk",
    "vehicle.country", "vehicle.language", "vehicle.type", "vehicle.unitSpeed", "vehicle.units", "engine.rangeCalc",
    "engine.electricVehicleMode", "driving.SOCHoldState", "driving.electricalPowerDistribution",
    "driving.displayRangeElectricVehicle", "sensors.SOCBatteryHybrid", "sensors.batteryTemp", "driving.ecoRangeWon",
    "climate.airConditionerCompressor", "controls.startStopLEDs", "driving.ecoRange", "driving.FDRControl",
    "driving.keyNumber", "navigation.infoToFinalDestination", "navigation.units", "sensors.lid",
    "sensors.seatOccupiedDriver", "sensors.seatOccupiedRearLeft", "sensors.seatOccupiedRearRight",
    "vehicle.systemTime", "vehicle.time", "driving.drivingStyle", "navigation.routeElapsedInfo",
    "sensors.PDCRangeFront2", "sensors.PDCRangeRear2", "climate.seatHeatDRR", "climate.seatHeatPSR",
    "climate.seatClimate", "climate.seatClimatePassenger", "climate.seatClimateDRR", "climate.seatClimatePSR",
    "climate.fondSettings", "climate.ventilation", "climate.seatAutoClimateDR", "climate.seatAutoClimatePS",
    "climate.seatAutoClimateDRR", "climate.seatAutoClimatePSR", "climate.armRestHeating", "comfort.seatControlPS",
    "comfort.seatControlDRR", "comfort.seatControlPSR", "comfort.seatMassageRL", "comfort.seatControlRR",
    "comfort.seatLumbarRL", "comfort.seatLumbarRR", "comfort.scentGenerator", "comfort.ambientLight",
    "comfort.ambientColour", "comfort.bodyTraining", "comfort.bodyTrainingStatus", "comfort.seatMemory",
    "controls.sunroof2", "controls.shadowingRoof", "controls.sunblind", "driving.mode35up",
    "driving.trafficSignInfo1", "driving.trafficSignInfo2", "driving.trafficSignInfo3", "driving.trafficSignInfo4",
    "driving.trafficSignInfoVisibility", "driving.rsConfigStatus", "driving.orNPITextMessage", "driving.orInfo",
    "driving.sliTextMessage", "driving.sliNextSpeedLimitRecognition", "driving.sliActualSpeedLimitInfo",
    "driving.laneBoundariesLoFrq", "driving.laneBoundaries", "driving.laneChange",
    "driving.signRecognitionFrontCamera", "driving.envLearning", "navigation.currentPositionCountry",
    "navigation.naviguidanceinfo", "sensors.tirePressureWarning", "sensors.childSafetyLock",
    "vehicle.dayNightChangeOver", "vehicle.interactionDetection", "vehicle.powerStateHeadUnit", "vehicle.statePWF",
    "vehicle.userTimeZone", "vehicle.fasStatus", "vehicle.partType", "vehicle.fastCharging",
};
const u32 kNumProprieta = sizeof(kProprieta) / sizeof(kProprieta[0]);
const u32 kMaxValore = 300;
const u32 kPrimiNelLog = 3;

// callback -> plugin thread: state 0 = buffer free, 1 = being written by the callback, 2 = value ready
volatile u32 g_eventi[kNumProprieta];
volatile u32 g_stato[kNumProprieta];
char g_buf[kNumProprieta][kMaxValore];
// plugin thread only
char g_ultimo[kNumProprieta][kMaxValore];
u32 g_eventiPrima[kNumProprieta];       // g_eventi at the last 30 s count
u32 g_loggati[kNumProprieta];
u32 g_registrate = 0;
u32 eventi(u32 i) { return __atomic_load_n(&g_eventi[i], __ATOMIC_RELAXED); }
int g_ultimaCambiata[3] = {-1, -1, -1};

bool cambioCds(void* oggetto, const bmw::BasicString<u16>& valore) {
    u32 i = (u32)(unsigned long)oggetto;
    if (i >= kNumProprieta) return true;
    __atomic_add_fetch(&g_eventi[i], 1u, __ATOMIC_RELAXED);
    u32 libero = 0;
    if (!__atomic_compare_exchange_n(&g_stato[i], &libero, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return true;
    const u16* p = valore.raw();
    u32 n = 0;
    while (p && p[n] && n < kMaxValore - 1) { g_buf[i][n] = p[n] >= 0x20 && p[n] < 0x7f ? (char)p[n] : '?'; ++n; }
    g_buf[i][n] = 0;
    __atomic_store_n(&g_stato[i], 2u, __ATOMIC_RELEASE);
    return true;
}

// ---- marker on the stick: survives a restart of the HMI AND of the whole head unit (/dev/shmem does not) ------
// <stick>/nbt-evo-apps/LetturaDati.stato: "group N" while group N is being registered, "ok" once all of them are
// registered and the plugin ran 30 s. A start that finds "group N" knows the previous one stopped there: it does not
// register anything (log and screen say which group); delete the file to try again.
typedef void* (*FnFileno)(void*);
typedef int (*FnInt1)(int);
typedef int (*FnPtr1)(void*);
char g_fileStato[200] = "";
enum { CDS_ATTESA = 0, CDS_VIA, CDS_FERMA } ;
int g_cds = CDS_ATTESA;
char g_motivoFermo[120] = "";

bool scriviStato(const char* testo) {          // written and pushed to the stick (fflush + fsync if available)
    void* f = fopen(g_fileStato, "w");
    if (!f) return false;
    fprintf(f, "%s\n", testo);
    FnPtr1 fflush_ = (FnPtr1)dlsym(kGlobale, "fflush");
    FnPtr1 fileno_ = (FnPtr1)dlsym(kGlobale, "fileno");
    FnInt1 fsync_ = (FnInt1)dlsym(kGlobale, "fsync");
    int r = -9;
    if (fflush_ && fileno_ && fsync_) { fflush_(f); r = fsync_(fileno_(f)); }
    fclose(f);
    static bool detto = false;
    if (!detto) { detto = true; accoda("marker %s: fsync %s", g_fileStato, r == 0 ? "ok" : (r == -9 ? "not available (fclose only)" : "failed")); }
    return true;
}

// waits for the stick, then reads the marker: CDS_VIA (register), CDS_FERMA (never), CDS_ATTESA (no stick yet)
void controllaStato() {
    cercaChiavetta();
    if (!g_logChiavetta[0]) return;
    u32 n = strlen(g_logChiavetta);
    if (n < 5 || n + 3 > sizeof(g_fileStato)) { g_cds = CDS_FERMA; snprintf(g_motivoFermo, sizeof(g_motivoFermo), "percorso chiavetta strano"); return; }
    memcpy(g_fileStato, g_logChiavetta, n - 4);
    memcpy(g_fileStato + n - 4, ".stato", 7);
    char b[64];
    b[0] = 0;
    int fd = open(g_fileStato, O_RDONLY_Q);
    if (fd >= 0) {
        ssize_t_q r = read(fd, b, sizeof(b) - 1);
        close(fd);
        b[r > 0 ? r : 0] = 0;
        for (u32 k = 0; b[k]; ++k) if (b[k] == '\n' || b[k] == '\r') { b[k] = 0; break; }
    }
    if (strncmp(b, "group ", 6) == 0) {
        g_cds = CDS_FERMA;
        snprintf(g_motivoFermo, sizeof(g_motivoFermo), "l'avvio prima si e' fermato nel %s", b);
        scriviLog("CDS OFF: %s says '%s' - the previous start stopped while registering that group (or the car was "
                  "switched off just then). Nothing is registered. Delete that file to try again.", g_fileStato, b);
        return;
    }
    if (!scriviStato("group 0")) {
        g_cds = CDS_FERMA;
        snprintf(g_motivoFermo, sizeof(g_motivoFermo), "non riesco a scrivere sulla chiavetta");
        scriviLog("CDS OFF: cannot write %s", g_fileStato);
        return;
    }
    g_cds = CDS_VIA;
    scriviLog("marker %s was '%s': CDS registration starts", g_fileStato, b[0] ? b : "(missing)");
}

void registraGruppo() {
    if (g_registrate >= kNumProprieta) return;
    u32 fine = g_registrate + kPerGruppo;
    if (fine > kNumProprieta) fine = kNumProprieta;
    char st[24];
    snprintf(st, sizeof(st), "group %u", g_registrate / kPerGruppo + 1);
    scriviStato(st);
    char nomi[800];
    u32 k = 0;
    nomi[0] = 0;
    for (u32 i = g_registrate; i < fine && k < sizeof(nomi) - 1; ++i) {
        int n = snprintf(nomi + k, sizeof(nomi) - k, "%s%s", i > g_registrate ? " " : "", kProprieta[i]);
        if (n > 0) k += (u32)n;
    }
    scriviLog("CDS: registering %u-%u: %s", g_registrate + 1, fine, nomi);   // in the files BEFORE registering
    for (u32 i = g_registrate; i < fine; ++i) {
        bmw::BasicString<char> nome(kProprieta[i]);
        bmw::DelegatoCds d;
        d.oggetto = (void*)(unsigned long)i;
        d.funzione = &cambioCds;
        g_app->cdsAddPropertyChangedEventHandler(nome, 0, d);
    }
    g_registrate = fine;
    scriviLog("CDS: %u of %u registered", g_registrate, kNumProprieta);
}

// takes the values ready in the buffers (plugin thread)
void prendiValori() {
    for (u32 i = 0; i < g_registrate; ++i) {
        if (__atomic_load_n(&g_stato[i], __ATOMIC_ACQUIRE) != 2u) continue;
        if (strncmp(g_ultimo[i], g_buf[i], kMaxValore) != 0) {
            memcpy(g_ultimo[i], g_buf[i], kMaxValore);
            if (g_ultimaCambiata[0] != (int)i) {
                g_ultimaCambiata[2] = g_ultimaCambiata[1];
                g_ultimaCambiata[1] = g_ultimaCambiata[0];
                g_ultimaCambiata[0] = (int)i;
            }
        }
        if (g_loggati[i] < kPrimiNelLog) {
            ++g_loggati[i];
            accoda("value %u of %s: %s", g_loggati[i], kProprieta[i], g_ultimo[i]);
        }
        __atomic_store_n(&g_stato[i], 0u, __ATOMIC_RELEASE);
    }
    svuota();
}

u32 contaConDati() {
    u32 n = 0;
    for (u32 i = 0; i < g_registrate; ++i) if (eventi(i)) ++n;
    return n;
}

u32 g_eventi30 = 0, g_eventiTot = 0;

void logConti(u32 trascorsiMs) {
    char r[860];
    u32 k = 0, tot = 0, quante = 0;
    r[0] = 0;
    accoda("== count of the last %u ms (properties with values: %u of %u registered)", trascorsiMs, contaConDati(), g_registrate);
    for (u32 i = 0; i < g_registrate; ++i) {
        u32 e = eventi(i);
        u32 d = e - g_eventiPrima[i];
        g_eventiPrima[i] = e;
        if (!d) continue;
        tot += d;
        ++quante;
        int n = snprintf(r + k, sizeof(r) - k, "%s%s %u", k ? ", " : "", kProprieta[i], d);
        if (n < 0 || k + (u32)n >= sizeof(r) - 1) { accoda("  %s", r); k = 0; r[0] = 0; n = snprintf(r, sizeof(r), "%s %u", kProprieta[i], d); }
        if (n > 0) k += (u32)n;
    }
    if (k) accoda("  %s", r);
    g_eventi30 = tot;
    g_eventiTot += tot;
    accoda("== %u values from %u properties in %u ms", tot, quante, trascorsiMs);
    svuota();
}

void logValori() {
    accoda("== last value of every property");
    char r[860];
    u32 k = 0;
    r[0] = 0;
    for (u32 i = 0; i < g_registrate; ++i) {
        u32 e = eventi(i);
        if (e) { accoda("  %s (%u values) = %s", kProprieta[i], e, g_ultimo[i]); continue; }
        int n = snprintf(r + k, sizeof(r) - k, "%s%s", k ? " " : "", kProprieta[i]);
        if (n < 0 || k + (u32)n >= sizeof(r) - 1) { accoda("  never sent: %s", r); k = 0; r[0] = 0; n = snprintf(r, sizeof(r), "%s", kProprieta[i]); }
        if (n > 0) k += (u32)n;
    }
    if (k) accoda("  never sent: %s", r);
    accoda("== end of the values");
    svuota();
}

// ---- screen ------------------------------------------------------------------------------------------------
void valoreBreve(int i, char* out, u32 n) {
    if (i < 0) { snprintf(out, n, "-"); return; }
    snprintf(out, n, "%s = %s", kProprieta[i], g_ultimo[i]);
}

void aggiornaSchermo() {
    riga(0, "Lettura dati %s: solo lettura, scrive solo il suo log", VERSIONE);
    if (g_cds == CDS_FERMA) riga(1, "Dati auto: FERMO, %s", g_motivoFermo);
    else if (g_cds == CDS_ATTESA) riga(1, "Dati auto: aspetto la chiavetta (senza chiavetta non legge)");
    else riga(1, "Dati auto: %u di %u registrati, %u hanno mandato valori", g_registrate, kNumProprieta, contaConDati());
    riga(2, "Valori: %u negli ultimi 30 s, %u in tutto%s", g_eventi30, g_eventiTot,
         g_pienoChiavetta ? " (log chiavetta pieno)" : (g_logChiavetta[0] ? "" : " (nessuna chiavetta)"));
    char v[110];
    for (u32 r = 0; r < 3; ++r) {
        valoreBreve(g_ultimaCambiata[r], v, sizeof(v));
        riga(3 + r, "%s", v);
    }
    riga(6, "GPU (librerie dell'HMI): EGL %u/%u, GLES2 %u/%u, screen %u/%u, LayerManager %u/%u",
         g_gfxTrovati[1], g_gfxCercati[1], g_gfxTrovati[2], g_gfxCercati[2], g_gfxTrovati[3], g_gfxCercati[3],
         g_gfxTrovati[4], g_gfxCercati[4]);
}

// ---- actions (rows of the page: only logged) ----------------------------------------------------------------
bool azione(void* oggetto, const bmw::Parcel&) {
    scriviLog("action %u", (u32)(unsigned long)oggetto);
    return true;
}

// ---- the plugin thread tick ---------------------------------------------------------------------------------
struct Giro { void** vptr; };
Giro  g_giro;
void* g_giroVtbl[5];
u32 g_inizio = 0, g_ultimoGruppo = 0, g_ultimiConti = 0, g_ultimiValori = 0, g_ultimoSchermo = 0;
bool g_sano = false, g_graficaFatta = false, g_statoOk = false;

void giroNoop(void*) {}

void giroRun(void*) {
    if (!g_app || g_finito) return;
    u32 ora = adessoMs();
    if (!g_graficaFatta) { g_graficaFatta = true; controllaGrafica(); }
    if (g_cds == CDS_ATTESA && ora - g_inizio >= kPrimoGruppoMs) controllaStato();
    if (g_cds == CDS_VIA && g_registrate < kNumProprieta && ora - g_ultimoGruppo >= kOgniGruppoMs) {
        g_ultimoGruppo = ora;
        registraGruppo();
        if (g_registrate == kNumProprieta) scriviStato("group all (registered, waiting 30 s of running)");
    }
    if (g_cds == CDS_VIA && g_registrate == kNumProprieta && !g_statoOk && g_sano) { g_statoOk = true; scriviStato("ok"); }
    prendiValori();
    if (ora - g_ultimiConti >= kOgniContiMs) { logConti(ora - g_ultimiConti); g_ultimiConti = ora; }
    if (ora - g_ultimiValori >= kOgniValoriMs) { g_ultimiValori = ora; logValori(); }
    if (ora - g_ultimoSchermo >= kOgniSchermoMs) { g_ultimoSchermo = ora; aggiornaSchermo(); }
    if (!g_sano && ora - g_inizio > kSecondiSano * 1000) {
        g_sano = true;
        scriviFile(kAvvii, "0");
        scriviLog("%u s running: start counted as good", kSecondiSano);
    }
    g_app->post(reinterpret_cast<bmw::Runnable*>(&g_giro), kTickMs);
}

void avviaGiro(bmw::PluginApp* app) {
    void** base = reinterpret_cast<void**>(dlsym(kGlobale, "_ZTVN3bmw8RunnableE"));
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
    for (u32 a = kSel0; a < kSel0 + kRighe; ++a) {
        bmw::DelegatoAzione d;
        d.oggetto = (void*)(unsigned long)a;
        d.funzione = &azione;
        self->addHandler(a, d);
    }
    bmw::DelegatoAzione d;
    d.oggetto = (void*)(unsigned long)kNoOp;
    d.funzione = &azione;
    self->addHandler(kNoOp, d);
    bool ok = self->onInit();
    scriviLog("onInit base -> %d (%u car data properties to register, %u at a time)", (int)ok, kNumProprieta, kPerGruppo);
    g_inizio = adessoMs();
    g_ultimoGruppo = g_ultimiConti = g_ultimiValori = g_ultimoSchermo = g_inizio;
    for (u32 r = 0; r < kRighe; ++r) g_testo[r][0] = 0;
    aggiornaSchermo();
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
    cercaChiavetta();
    scriviLog("create(%s) %s %s", name ? name : "(null)", NOME_PLUGIN, VERSIONE);
    if (access(kDisable, F_OK_Q) == 0) { scriviLog("off: %s exists", kDisable); return 0; }
    int avvii = numeroDaFile(kAvvii);
    if (avvii >= (int)kMaxAvvii) { scriviLog("off: %d starts without reaching %u s (delete %s to try again)", avvii, kSecondiSano, kAvvii); return 0; }
    char n[16];
    snprintf(n, sizeof(n), "%d", avvii + 1);
    scriviFile(kAvvii, n);
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
    scriviLog("create: app %p, start no. %d", (void*)app, avvii + 1);
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
