// App da USB on the PC (Linux x86) with a fake HMI framework: the actions of the knob in, the text of every row out.
// Usage: sh apps/AppUsb/test/host.sh
#include "../app_usb.cpp"
extern "C" int printf(const char*, ...);
extern "C" void bsCrea(void* self, const char* s) __asm__("_ZN3bmw11BasicStringIcEC1EPKc");
extern "C" void bsCrea(void* self, const char* s) { *(const char**)self = s; }
extern "C" void bsDistruggi(void* self) __asm__("_ZN3bmw11BasicStringIcED1Ev");
extern "C" void bsDistruggi(void*) {}
extern "C" void parcelDistruggi(void*) {}
extern "C" void* dlsym(void*, const char*) { return 0; }

// readdir as on QNX (name length at offset 18, name at 20), from the real one of glibc (readdir64)
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
DelegatoAzione g_az[100];
void PluginApp::addHandler(u32 id, const DelegatoAzione& d) { if (id < 100) g_az[id] = d; }
void PluginApp::onFinalize() {}
void PluginApp::cdsAddPropertyChangedEventHandler(BasicString<char>&, u32, const DelegatoCds&) {}
}

void avanti(u32 ms) { for (u32 t = 0; t < ms; t += kTickMs) { g_finto += kTickMs; giroRun(0); } }

void manopola(u32 id) {               // the selectAction of a row (focus), as the HMI sends it
    static char finto[64];
    if (!bmw::g_az[id].funzione) { printf("  (no handler for %u)\n", id); return; }
    bmw::g_az[id].funzione(bmw::g_az[id].oggetto, *(const bmw::Parcel*)finto);
    avanti(kTickMs);
}

void stato(const char* nome, u32 m0, u32 n) {
    printf("== %s\n", nome);
    for (u32 r = 0; r < n; ++r) printf("  [%u] %s\n", m0 + r, bmw::g_schermo[m0 + r]);
}

int main() {
    static unsigned char mem[0x200];
    g_app = (bmw::PluginApp*)mem;
    appOnInit(g_app);
    avanti(1000);
    stato("main", kModMain0, kRigheMain);
    manopola(22);  stato("detail of row 2", kModDett0, kRigheDett);
    manopola(24);  stato("detail of row 4", kModDett0, kRigheDett);
    manopola(25);  stato("detail of row 5 (group)", kModDett0, kRigheDett);
    manopola(40);  stato("diagnosis (state opened)", kModDiag0, kRigheDiag);
    manopola(22);  manopola(60); stato("install (t0)", kModInst0, kRigheInst);
    manopola(50);  manopola(66); stato("car saved", kModSalva0, kRigheSalva);
    manopola(54);  manopola(66); stato("phone saved", kModSalva0, kRigheSalva);
    stato("car and phone", kModMarca0, kRigheMarca);
    stato("main after", kModMain0, kRigheMain);
    printf("== texts sent %u, actions %u\n", bmw::g_nTesti, g_azioni);
    return 0;
}
