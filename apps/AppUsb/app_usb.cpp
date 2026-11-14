// App da USB (AppUsb) - the packages of the nbt-evo-apps USB stick from the iDrive menu, with the knob.
//
// t0 = DIAGNOSIS ONLY: nothing is installed. It answers, in the car, the two things the real installer needs:
//   1. can the plugin, running inside the HMI, change the system? (user of the HMI, /opt/aa and the plugin folder
//      writable, a command can be started) - with the full output of id, mount, ls, df in the log;
//   2. does the "press" reach the plugin? An action on the press (raAction) never arrived in SportTest t1; the
//      selectAction (focus on a button) works in iDrive Plus. So a press only opens another state and the first
//      button of that state, which gets the focus as it opens, sends the selectAction that starts the work.
// It also shows the packages on the stick (and what is installed, from /opt/aa/nbt-evo-apps/installed, written by
// pkg.sh) and lets you choose the car (MINI / BMW / Supra) and the phone app of iDrive Plus: the only thing it
// writes is that choice, /opt/aa/nbt-evo-apps/brand and phone, the same files as pkg.sh.
// Every action received is in the log with its time: /dev/shmem/AppUsb.log and <stick>/nbt-evo-apps/AppUsb.log.
// Safety as the other plugins: /opt/aa/appusb.disable switches it off; after 3 starts that do not reach 30 s it
// stays off (delete /dev/shmem/AppUsb.avvii).
#include "../../framework/bmw_plugin.h"

#define NOME_PLUGIN "AppUsb"
#define VERSIONE "t0"

extern "C" {
int system(const char*);
int getuid(void);
int geteuid(void);
}

namespace {

// ---- constants ---------------------------------------------------------------------------------------------
const char* const kDisable  = "/opt/aa/appusb.disable";
const char* const kAvvii    = "/dev/shmem/" NOME_PLUGIN ".avvii";
const char* const kLogShmem = "/dev/shmem/" NOME_PLUGIN ".log";
const char* const kDiagFile = "/dev/shmem/" NOME_PLUGIN ".diag";
const char* const kPlugins  = "/opt/hmi/ID5/data/ro/Plugins";
const char* const kDati     = "/opt/aa/nbt-evo-apps";
const u32 kTickMs          = 250;
const u32 kSecondiSano     = 30;
const u32 kMaxAvvii        = 3;
const u32 kOgniChiavettaMs = 5000;
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };
enum { W_OK_Q = 2 };

// models (AppUsb.xml, made by apps/AppUsb/make.py)
const u32 kModMain0 = 441, kModDett0 = 451, kModDiag0 = 461, kModInst0 = 471, kModMarca0 = 481, kModSalva0 = 491;   // make.py STATI
const u32 kRigheMain = 7, kRigheDett = 7, kRigheDiag = 7, kRigheInst = 4, kRigheMarca = 5, kRigheSalva = 2;
// selectActions (the focus arriving on a row)
const u32 kSelMain0 = 20, kSelDiag = 40, kSelMarca0 = 50, kSelInst0 = 60, kSelSalva0 = 66;   // 30-36 detail, 59 nothing
const u32 kVoci = 5;                    // packages or groups in the main list (rows 2-6)

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

char g_chiavetta[128] = "";             // /mnt/umassN/nbt-evo-apps ("" = no stick)
char g_logChiavetta[192] = "";

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
    if (g_logChiavetta[0]) scriviRiga(g_logChiavetta, riga);
}

// ---- small files -------------------------------------------------------------------------------------------
// a whole small file into b (at most n-1 bytes, then 0); false if missing
bool leggiFile(const char* path, char* b, u32 n) {
    int fd = open(path, O_RDONLY_Q);
    if (fd < 0) return false;
    u32 tot = 0;
    while (tot < n - 1) {
        ssize_t_q r = read(fd, b + tot, n - 1 - tot);
        if (r <= 0) break;
        tot += (u32)r;
    }
    close(fd);
    b[tot] = 0;
    return true;
}

// first line of a small file, without the newline ("" if missing)
void primaRiga(const char* path, char* out, u32 n) {
    char b[64];
    out[0] = 0;
    if (!leggiFile(path, b, sizeof(b))) return;
    u32 k = 0;
    while (b[k] && b[k] != '\n' && b[k] != '\r' && k < n - 1) { out[k] = b[k]; ++k; }
    out[k] = 0;
}

// value of "key=" in a package.txt / installed record, "" if missing
void campo(const char* testo, const char* chiave, char* out, u32 n) {
    out[0] = 0;
    u32 lc = strlen(chiave);
    const char* p = testo;
    while (*p) {
        if (strncmp(p, chiave, lc) == 0 && p[lc] == '=') {
            p += lc + 1;
            u32 k = 0;
            while (*p && *p != '\n' && *p != '\r' && k < n - 1) out[k++] = *p++;
            out[k] = 0;
            return;
        }
        while (*p && *p != '\n') ++p;
        if (*p) ++p;
    }
}

int numeroDaFile(const char* path) {
    char b[16];
    if (!leggiFile(path, b, sizeof(b))) return 0;
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

bool uguali(const char* a, const char* b) { return strncmp(a, b, 64) == 0; }

// ---- screen: the text of the rows ------------------------------------------------------------------------
const u32 kPrimoModello = 441, kNumModelli = 60;     // text models 441-497 (make.py STATI)
char g_testo[kNumModelli][120];         // text shown in every model (only changes are sent)

void riga(u32 modello, const char* fmt, ...) {
    char t[120];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(t, sizeof(t), fmt, ap);
    __builtin_va_end(ap);
    if (!t[0]) { t[0] = ' '; t[1] = 0; }        // an empty row: one space, as in the XML
    if (modello < kPrimoModello || modello - kPrimoModello >= kNumModelli) return;
    char* prima = g_testo[modello - kPrimoModello];
    if (strncmp(prima, t, sizeof(t)) == 0) return;
    memcpy(prima, t, sizeof(t));
    g_app->updateModel(modello, t);
}

// ---- the stick, its packages and what is installed --------------------------------------------------------
const u32 kMaxPacchetti = 16;
struct Pacchetto { char nome[24], titolo[48], versione[16], gruppo[24], marche[32], installata[16]; };
Pacchetto g_pac[kMaxPacchetti];
u32 g_nPac = 0;
char g_marca[16] = "", g_telefono[16] = "nbt";

// an entry of the main list: an app, or a group (iDrive Plus) of several packages
struct Voce { char titolo[48]; char gruppo[24]; int pacchetto; };
Voce g_voci[kVoci];
u32 g_nVoci = 0;

void cercaChiavetta() {
    char trovata[128] = "";
    void* d = opendir("/mnt");
    if (d) {
        void* e;
        while ((e = readdir(d)) != 0) {
            const char* n = nomeDirent(e);
            if (!n || strncmp(n, "umass", 5) != 0) continue;
            char c[128];
            snprintf(c, sizeof(c), "/mnt/%s/nbt-evo-apps", n);
            if (access(c, F_OK_Q) == 0) { memcpy(trovata, c, sizeof(c)); break; }
        }
        closedir(d);
    }
    if (strncmp(trovata, g_chiavetta, sizeof(trovata)) == 0) return;
    memcpy(g_chiavetta, trovata, sizeof(trovata));
    if (g_chiavetta[0]) snprintf(g_logChiavetta, sizeof(g_logChiavetta), "%s/%s.log", g_chiavetta, NOME_PLUGIN);
    else g_logChiavetta[0] = 0;
    scriviLog("stick: %s", g_chiavetta[0] ? g_chiavetta : "none");
}

void leggiImpostazioni() {
    char p[96];
    snprintf(p, sizeof(p), "%s/brand", kDati);
    primaRiga(p, g_marca, sizeof(g_marca));
    snprintf(p, sizeof(p), "%s/phone", kDati);
    primaRiga(p, g_telefono, sizeof(g_telefono));
    if (!g_telefono[0]) memcpy(g_telefono, "nbt", 4);
}

void leggiPacchetti() {
    g_nPac = 0;
    if (!g_chiavetta[0]) return;
    char dir[160];
    snprintf(dir, sizeof(dir), "%s/pacchetti", g_chiavetta);
    void* d = opendir(dir);
    if (!d) return;
    char b[700];
    void* e;
    while ((e = readdir(d)) != 0 && g_nPac < kMaxPacchetti) {
        const char* n = nomeDirent(e);
        if (!n || n[0] == '.') continue;
        char pt[256];
        snprintf(pt, sizeof(pt), "%s/%s/package.txt", dir, n);
        if (!leggiFile(pt, b, sizeof(b))) continue;
        Pacchetto& p = g_pac[g_nPac];
        memset(&p, 0, sizeof(p));
        campo(b, "name", p.nome, sizeof(p.nome));
        campo(b, "title", p.titolo, sizeof(p.titolo));
        campo(b, "version", p.versione, sizeof(p.versione));
        campo(b, "group", p.gruppo, sizeof(p.gruppo));
        campo(b, "brands", p.marche, sizeof(p.marche));
        if (!p.nome[0]) continue;
        char ip[160];
        snprintf(ip, sizeof(ip), "%s/installed/%s.txt", kDati, p.nome);
        if (leggiFile(ip, b, sizeof(b))) campo(b, "version", p.installata, sizeof(p.installata));
        ++g_nPac;
    }
    closedir(d);
    for (u32 i = 1; i < g_nPac; ++i)            // always the same order: by name
        for (u32 j = i; j > 0 && strncmp(g_pac[j].nome, g_pac[j - 1].nome, sizeof(g_pac[j].nome)) < 0; --j) {
            Pacchetto t = g_pac[j]; g_pac[j] = g_pac[j - 1]; g_pac[j - 1] = t;
        }
}

bool perQuestaAuto(const Pacchetto& p) {
    if (!p.marche[0] || uguali(p.marche, "all")) return true;
    if (!g_marca[0]) return false;
    u32 lm = strlen(g_marca);
    for (const char* q = p.marche; *q;) {
        if (strncmp(q, g_marca, lm) == 0 && (q[lm] == 0 || q[lm] == ' ')) return true;
        while (*q && *q != ' ') ++q;
        while (*q == ' ') ++q;
    }
    return false;
}

void costruisciVoci() {
    g_nVoci = 0;
    for (u32 i = 0; i < g_nPac && g_nVoci < kVoci; ++i) {          // the apps of nbt-evo-apps one by one
        if (!uguali(g_pac[i].gruppo, "nbt-evo-apps")) continue;
        Voce& v = g_voci[g_nVoci++];
        memcpy(v.titolo, g_pac[i].titolo, sizeof(v.titolo));
        v.gruppo[0] = 0;
        v.pacchetto = (int)i;
    }
    for (u32 i = 0; i < g_nPac && g_nVoci < kVoci; ++i) {          // then every other group as one entry
        if (!g_pac[i].gruppo[0] || uguali(g_pac[i].gruppo, "nbt-evo-apps")) continue;
        bool gia = false;
        for (u32 k = 0; k < g_nVoci; ++k) if (g_voci[k].gruppo[0] && uguali(g_voci[k].gruppo, g_pac[i].gruppo)) gia = true;
        if (gia) continue;
        Voce& v = g_voci[g_nVoci++];
        memcpy(v.titolo, g_pac[i].gruppo, sizeof(v.gruppo));
        v.titolo[sizeof(v.gruppo) - 1] = 0;
        memcpy(v.gruppo, g_pac[i].gruppo, sizeof(v.gruppo));
        v.pacchetto = -1;
    }
}

const char* nomeMarca(const char* m) {
    if (uguali(m, "mini")) return "MINI";
    if (uguali(m, "bmw")) return "BMW";
    if (uguali(m, "supra")) return "Toyota Supra";
    return "non scelta";
}

const char* nomeTelefono() {
    if (uguali(g_telefono, "brand")) {
        if (uguali(g_marca, "bmw")) return "BMW Plus";
        if (uguali(g_marca, "supra")) return "Supra Plus";
        return "MINI Plus";
    }
    return "NBT Plus";
}

// ---- texts of the states -------------------------------------------------------------------------------------
void scriviMain() {
    riga(kModMain0, "Diagnosi (non cambia nulla)");
    riga(kModMain0 + 1, "Auto: %s   -   iDrive Plus: app %s", nomeMarca(g_marca), nomeTelefono());
    for (u32 r = 0; r < kVoci; ++r) {
        u32 m = kModMain0 + 2 + r;
        if (!g_chiavetta[0]) { riga(m, r == 0 ? "Chiavetta con la cartella nbt-evo-apps non trovata" : ""); continue; }
        if (r >= g_nVoci) { riga(m, r == 0 ? "Nessun pacchetto in nbt-evo-apps/pacchetti" : ""); continue; }
        const Voce& v = g_voci[r];
        if (v.pacchetto >= 0) {
            const Pacchetto& p = g_pac[v.pacchetto];
            riga(m, "%s  %s   (installata: %s)%s", p.titolo, p.versione, p.installata[0] ? p.installata : "no",
                 perQuestaAuto(p) ? "" : "   - non per questa auto");
        } else {
            u32 n = 0, inst = 0;
            char ver[16] = "";
            for (u32 i = 0; i < g_nPac; ++i)
                if (uguali(g_pac[i].gruppo, v.gruppo)) { ++n; if (g_pac[i].installata[0]) ++inst; memcpy(ver, g_pac[i].versione, sizeof(ver)); }
            riga(m, "%s  %s   (%u plugin, installati %u)", v.titolo, ver, n, inst);
        }
    }
}

int g_voceScelta = -1;                  // entry of the main list with the focus (its detail is ready)

void scriviDettaglio(int voce) {
    for (u32 r = 0; r < kRigheDett; ++r) riga(kModDett0 + r, "");
    if (voce < 0 || voce >= (int)g_nVoci) { riga(kModDett0, "Nessun pacchetto"); return; }
    const Voce& v = g_voci[voce];
    if (v.pacchetto >= 0) {
        const Pacchetto& p = g_pac[v.pacchetto];
        riga(kModDett0, "%s", p.titolo);
        riga(kModDett0 + 1, "Sulla chiavetta: %s   -   installata: %s", p.versione, p.installata[0] ? p.installata : "no");
        if (!p.marche[0] || uguali(p.marche, "all")) riga(kModDett0 + 2, "Per tutte le auto");
        else riga(kModDett0 + 2, "Per: %s   -   la tua auto: %s", p.marche, nomeMarca(g_marca));
        riga(kModDett0 + 3, "Dopo l'installazione: riavvio, tasto power premuto 30 secondi");
        riga(kModDett0 + 6, "Installa (prova t0: non installa niente)");
        return;
    }
    riga(kModDett0, "%s   -   app del telefono: %s", v.titolo, nomeTelefono());
    u32 r = 1;
    char lista[120] = "";
    u32 nl = 0, inst = 0, n = 0;
    for (u32 i = 0; i < g_nPac; ++i) {
        if (!uguali(g_pac[i].gruppo, v.gruppo)) continue;
        ++n;
        if (g_pac[i].installata[0]) ++inst;
        int k = snprintf(lista + nl, sizeof(lista) - nl, "%s%s %s", nl ? ",  " : "", g_pac[i].nome, g_pac[i].versione);
        nl = (k < 0 || nl + (u32)k >= sizeof(lista)) ? sizeof(lista) - 1 : nl + (u32)k;
        if (nl > 70 && r < 5) { riga(kModDett0 + r++, "%s", lista); nl = 0; lista[0] = 0; }
    }
    if (nl && r < 5) riga(kModDett0 + r++, "%s", lista);
    riga(kModDett0 + 5, "Installati %u su %u", inst, n);
    riga(kModDett0 + 6, "Installa tutto il gruppo (prova t0: non installa niente)");
}

void scriviMarca() {
    riga(kModMarca0, "Auto: MINI%s", uguali(g_marca, "mini") ? "   (scelta)" : "");
    riga(kModMarca0 + 1, "Auto: BMW%s", uguali(g_marca, "bmw") ? "   (scelta)" : "");
    riga(kModMarca0 + 2, "Auto: Toyota Supra%s", uguali(g_marca, "supra") ? "   (scelta)" : "");
    riga(kModMarca0 + 3, "iDrive Plus, app del telefono: NBT Plus%s", uguali(g_telefono, "nbt") ? "   (scelta)" : "");
    riga(kModMarca0 + 4, "iDrive Plus, app del telefono: MINI / BMW / Supra Plus%s", uguali(g_telefono, "brand") ? "   (scelta)" : "");
}

// ---- what the actions ask (set by the handlers, done by the tick) -----------------------------------------
volatile u32 g_chiestaDiag = 0, g_chiestaInst = 0, g_chiestoSalva = 0;
volatile int g_fuocoMain = -1, g_fuocoMarca = -1;
u32 g_azioni = 0;
u32 g_ultimaDiag = 0;

bool azione(void* oggetto, const bmw::Parcel&) {
    u32 id = (u32)(unsigned long)oggetto;
    ++g_azioni;
    scriviLog("action %u (no. %u)", id, g_azioni);
    if (id >= kSelMain0 && id < kSelMain0 + kRigheMain) g_fuocoMain = (int)(id - kSelMain0);
    else if (id == kSelDiag) g_chiestaDiag = 1;
    else if (id >= kSelMarca0 && id < kSelMarca0 + kRigheMarca) g_fuocoMarca = (int)(id - kSelMarca0);
    else if (id == kSelInst0) g_chiestaInst = 1;
    else if (id == kSelSalva0) g_chiestoSalva = 1;
    return true;
}

// ---- diagnosis -------------------------------------------------------------------------------------------
void diagnosi() {
    int uid = getuid(), euid = geteuid();
    bool aa = access("/opt/aa", W_OK_Q) == 0;
    bool pl = access(kPlugins, W_OK_Q) == 0;
    unlink(kDiagFile);
    char cmd[400];
    snprintf(cmd, sizeof(cmd),
             "( echo '== id'; id; echo '== mount'; mount; echo '== ls'; ls -ld %s /opt/aa %s; echo '== df'; df -k %s /opt/aa ) > %s 2>&1",
             kPlugins, kDati, kPlugins, kDiagFile);
    int rs = system(cmd);
    static char out[8000];
    bool comandi = leggiFile(kDiagFile, out, sizeof(out)) && out[0];
    scriviLog("diagnosis: uid %d euid %d, /opt/aa writable %d, %s writable %d, system() -> %d, output %s",
              uid, euid, (int)aa, kPlugins, (int)pl, rs, comandi ? "yes" : "NO");
    if (comandi) {                      // the whole output, line by line
        char* p = out;
        while (*p) {
            char* q = p;
            while (*q && *q != '\n') ++q;
            char c = *q;
            *q = 0;
            scriviLog("  %s", p);
            if (!c) break;
            p = q + 1;
        }
    }
    riga(kModDiag0, "Utente dell'HMI: uid %d, euid %d%s", uid, euid, euid == 0 ? "   (root)" : "");
    riga(kModDiag0 + 1, "Scrittura in /opt/aa: %s", aa ? "si" : "no");
    riga(kModDiag0 + 2, "Scrittura nei plugin: %s%s", pl ? "si" : "no", pl ? "" : "   (normale senza il remount)");
    riga(kModDiag0 + 3, "Avvio di comandi: %s", comandi ? "si" : "no");
    riga(kModDiag0 + 4, "Chiavetta: %s   -   %u pacchetti", g_chiavetta[0] ? g_chiavetta : "non trovata", g_nPac);
    riga(kModDiag0 + 5, "Azioni arrivate finora: %u (la pressione funziona se sei qui)", g_azioni);
    riga(kModDiag0 + 6, "Tutto nel log: nbt-evo-apps/AppUsb.log");
}

void installaProva() {
    int v = g_voceScelta;
    const char* nome = (v >= 0 && v < (int)g_nVoci) ? g_voci[v].titolo : "?";
    scriviLog("install asked for '%s' - t0: nothing is installed", nome);
    riga(kModInst0, "Prova t0: qui partirebbe l'installazione di %s", nome);
    riga(kModInst0 + 1, "Nessun file e' stato toccato");
    riga(kModInst0 + 2, "La pressione e' arrivata al plugin: il sistema funziona");
    riga(kModInst0 + 3, "Torna indietro con il tasto BACK");
}

void salvaScelta() {
    int f = g_fuocoMarca;
    const char* chiave = f <= 2 ? "brand" : "phone";
    const char* valore = f == 0 ? "mini" : f == 1 ? "bmw" : f == 2 ? "supra" : f == 3 ? "nbt" : "brand";
    if (f < 0 || f > 4) { riga(kModSalva0, "Nessuna scelta: torna indietro e scegli una riga"); return; }
    char p[96];
    snprintf(p, sizeof(p), "%s/%s", kDati, chiave);
    bool ok = scriviFile(p, valore);
    scriviLog("save %s=%s -> %s", chiave, valore, ok ? "ok" : "FAILED");
    if (ok) {
        leggiImpostazioni();
        riga(kModSalva0, "Salvato: %s", f <= 2 ? nomeMarca(valore) : (f == 3 ? "app NBT Plus" : "app MINI / BMW / Supra Plus"));
        riga(kModSalva0 + 1, "Torna indietro con il tasto BACK");
    } else {
        riga(kModSalva0, "Non salvato: %s non scrivibile", kDati);
        riga(kModSalva0 + 1, "Serve la cartella (la crea pkg.sh) e il remount: vedi il log");
    }
    scriviMarca();
    scriviMain();
}

// ---- tick ----------------------------------------------------------------------------------------------------
u32 g_inizio = 0, g_ultimaChiavetta = 0;
bool g_sano = false;

void aggiorna() {
    cercaChiavetta();
    leggiImpostazioni();
    leggiPacchetti();
    costruisciVoci();
    scriviMain();
    scriviMarca();
}

struct Giro { void** vptr; };
Giro  g_giro;
void* g_giroVtbl[5];

void giroNoop(void*) {}

void giroRun(void*) {
    if (!g_app || g_finito) return;
    u32 ora = adessoMs();
    if (ora - g_ultimaChiavetta >= kOgniChiavettaMs) { g_ultimaChiavetta = ora; aggiorna(); }
    int fm = g_fuocoMain;
    if (fm >= 2 && fm - 2 != g_voceScelta) { g_voceScelta = fm - 2; scriviDettaglio(g_voceScelta); }
    if (g_chiestaDiag) { g_chiestaDiag = 0; if (!g_ultimaDiag || ora - g_ultimaDiag > 3000) { g_ultimaDiag = ora; diagnosi(); } }
    if (g_chiestaInst) { g_chiestaInst = 0; installaProva(); }
    if (g_chiestoSalva) { g_chiestoSalva = 0; salvaScelta(); }
    if (!g_sano && ora - g_inizio > kSecondiSano * 1000) {
        g_sano = true;
        scriviFile(kAvvii, "0");
        scriviLog("%u s running: start counted as good", kSecondiSano);
    }
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
const u32 kAzioni[] = {20, 21, 22, 23, 24, 25, 26, 30, 31, 32, 33, 34, 35, 36, 40, 50, 51, 52, 53, 54, 59, 60, 61, 62, 63, 66, 67};

bool appOnInit(bmw::PluginApp* self) {
    for (u32 i = 0; i < sizeof(kAzioni) / sizeof(kAzioni[0]); ++i) {
        bmw::DelegatoAzione d;
        d.oggetto = (void*)(unsigned long)kAzioni[i];
        d.funzione = &azione;
        self->addHandler(kAzioni[i], d);
    }
    bool ok = self->onInit();
    scriviLog("onInit base -> %d", (int)ok);
    g_inizio = adessoMs();
    g_ultimaChiavetta = g_inizio;
    aggiorna();
    scriviDettaglio(-1);
    for (u32 r = 0; r < kRigheDiag; ++r) riga(kModDiag0 + r, r == 0 ? "Diagnosi in corso..." : "");
    for (u32 r = 0; r < kRigheInst; ++r) riga(kModInst0 + r, r == 0 ? "..." : "");
    for (u32 r = 0; r < kRigheSalva; ++r) riga(kModSalva0 + r, r == 0 ? "..." : "");
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
