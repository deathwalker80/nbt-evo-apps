// Lettura dati on the PC (Linux x86) with a fake HMI framework: CDS values sent from another thread (as the HMI
// does with asyncCallbacks), the clock moved by hand, the rows of the page and the files written checked.
// Usage: sh apps/LetturaDati/test/host.sh      (mode as argument: normale | ferma3 | senza)
#define dlsym dlsymFinto       // the plugin's global lookups go to the fake below (the real dlsym stays for the
                                // sanitizer runtime)
#include "../lettura_dati.cpp"
extern "C" int printf(const char*, ...);
extern "C" int atoi(const char*);
extern "C" int fflush(void*);
extern "C" int fileno(void*);
extern "C" int fsync(int);
extern "C" int dladdr(const void*, void*);
extern "C" void exit(int);
extern "C" int usleep(unsigned);
extern "C" void bsCrea(void* self, const char* s) __asm__("_ZN3bmw11BasicStringIcEC1EPKc");
extern "C" void bsCrea(void* self, const char* s) { *(const char**)self = s; }
extern "C" void bsDistruggi(void* self) __asm__("_ZN3bmw11BasicStringIcED1Ev");
extern "C" void bsDistruggi(void*) {}
extern "C" void parcelDistruggi(void*) {}
void finta() {}
extern "C" void* dlsymFinto(void* h, const char* s) {     // the global lookups of the plugin (h = -2 as on QNX)
    if (h != (void*)-2) return 0;
    if (!strncmp(s, "fflush", 9)) return (void*)&fflush;
    if (!strncmp(s, "fileno", 9)) return (void*)&fileno;
    if (!strncmp(s, "fsync", 9)) return (void*)&fsync;
    if (!strncmp(s, "dladdr", 9)) return (void*)&dladdr;
    if (!strncmp(s, "eglGetDisplay", 20)) return (void*)&printf;      // a symbol "found", to see the dladdr line
    if (!strncmp(s, "glDrawArrays", 20)) return (void*)&finta;
    return 0;
}

struct DirentLinux { unsigned long long ino; long long off; unsigned short reclen; unsigned char type; char name[256]; };
extern "C" DirentLinux* readdir64(void*);
extern "C" void* readdir(void* d) {
    static unsigned char q[300];
    DirentLinux* e = readdir64(d);
    if (!e) return 0;
    memset(q, 0, sizeof(q));
    short n = (short)strlen(e->name);
    memcpy(q + 18, &n, 2);
    memcpy(q + 20, e->name, (u32)n + 1);
    return q;
}

u32 g_finto = 100000;
extern "C" int clock_gettime(int, void* ts) {
    long* t = (long*)ts;
    t[0] = (long)(g_finto / 1000);
    t[1] = (long)(g_finto % 1000) * 1000000L;
    return 0;
}

namespace bmw {
BufferStream::~BufferStream() {}
Image::Image(const unsigned char*, u32) {}
ImageID::ImageID(u32) {}
Parcel::Parcel(bool) {}
Parcel::Parcel(int) {}
PluginApp::PluginApp(IDispatcher&, BasicString<char>, u32, u32, RHMIInterfaceVersion, const Bundle&, const Bundle&) {}
void PluginApp::post(Runnable*, u32) {}
char g_schermo[200][120];
u32 g_nTesti = 0;
void PluginApp::updateModel(u32 id, const char* t) { ++g_nTesti; if (id < 200) snprintf(g_schermo[id], 120, "%s", t); }
void PluginApp::updateModel(u32, const Image&) {}
void PluginApp::updateModel(u32, const ImageID&) {}
void PluginApp::setProperty(u32, u32, const Parcel&) {}
bool PluginApp::onInit() { return true; }
void PluginApp::addHandler(u32, const DelegatoAzione&) {}
void PluginApp::onFinalize() {}
DelegatoCds g_cdsD[400];
const char* g_cdsN[400];
volatile u32 g_nCds = 0;
void PluginApp::cdsAddPropertyChangedEventHandler(BasicString<char>& n, u32, const DelegatoCds& d) {
    u32 k = g_nCds;
    g_cdsD[k] = d;
    g_cdsN[k] = n.raw();
    __atomic_store_n(&g_nCds, k + 1, __ATOMIC_RELEASE);
}
}

// the "HMI" thread: values of the first properties of every group (as JSON text, UTF-16) as fast as it can
volatile bool g_basta = false;
u32 g_mandati = 0;
void* hmi(void*) {
    u16 testo[200];
    u32 giro = 0;
    while (!__atomic_load_n(&g_basta, __ATOMIC_ACQUIRE)) {
        u32 n = __atomic_load_n(&bmw::g_nCds, __ATOMIC_ACQUIRE);
        for (u32 k = 0; k < n; ++k) {
            if (k % 7) continue;                      // only some properties send values (the others: "never sent")
            char a[160];
            snprintf(a, sizeof(a), "{\"%s\":%u}", bmw::g_cdsN[k], giro);
            u32 i = 0;
            for (; a[i]; ++i) testo[i] = (u16)a[i];
            testo[i] = 0;
            u16* p = testo;
            bmw::g_cdsD[k].funzione(bmw::g_cdsD[k].oggetto, *(const bmw::BasicString<u16>*)&p);
            ++g_mandati;
        }
        ++giro;
        usleep(200);
    }
    return 0;
}

extern "C" int pthread_create(void*, const void*, void* (*)(void*), void*);
extern "C" int pthread_join(unsigned long, void**);

void avanti(u32 ms) { for (u32 t = 0; t < ms; t += kTickMs) { g_finto += kTickMs; giroRun(0); usleep(300); } }

void schermo(const char* quando) {
    printf("== screen %s\n", quando);
    for (u32 r = 0; r < kRighe; ++r) printf("  [%u] %s\n", kModRiga0 + r, bmw::g_schermo[kModRiga0 + r]);
}

int main(int argc, char** argv) {
    const char* modo = argc > 1 ? argv[1] : "normale";
    static unsigned char mem[0x200];
    g_app = (bmw::PluginApp*)mem;
    appOnInit(g_app);
    unsigned long th = 0;
    pthread_create(&th, 0, hmi, 0);
    avanti(1000);
    schermo("after 1 s");
    if (!strncmp(modo, "ferma3", 9)) {               // the HMI "dies" while registering group 3
        avanti(1250 + 2000);
        __atomic_store_n(&g_basta, true, __ATOMIC_RELEASE); pthread_join(th, 0);
        printf("== stopped with %u properties registered\n", g_registrate);
        exit(0);
    }
    avanti(14000);
    schermo("after 15 s");
    avanti(130000);
    __atomic_store_n(&g_basta, true, __ATOMIC_RELEASE);
    pthread_join(th, 0);
    schermo("after 131 s");
    u32 somma = 0;
    for (u32 i = 0; i < kNumProprieta; ++i) somma += g_eventi[i];
    printf("== registered %u (fake HMI %u), values sent %u, counted by the plugin %u: %s\n", g_registrate, bmw::g_nCds,
           g_mandati, somma, somma == g_mandati ? "SAME" : "DIFFERENT");
    return 0;
}
