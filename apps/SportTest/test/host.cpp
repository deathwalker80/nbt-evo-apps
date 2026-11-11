// SportTest on the PC (Linux x86) with a fake HMI framework: CDS values in, what the plugin sends out.
// Usage: sh apps/SportTest/test/host.sh
#include "../sport_test.cpp"
extern "C" int printf(const char*, ...);
extern "C" void bsCrea(void* self, const char* s) __asm__("_ZN3bmw11BasicStringIcEC1EPKc");
extern "C" void bsCrea(void* self, const char* s) { *(const char**)self = s; }
extern "C" void bsDistruggi(void* self) __asm__("_ZN3bmw11BasicStringIcED1Ev");
extern "C" void bsDistruggi(void*) {}
extern "C" void parcelDistruggi(void*) {}
extern "C" void* dlsym(void*, const char*) { return 0; }     // QNX RTLD_DEFAULT (-2) means nothing on Linux
namespace bmw {
BufferStream::~BufferStream() {}
Image::Image(const unsigned char* d, u32 n) { memcpy(m_storage, &n, 4); memcpy(m_storage + 4, d + 1, 3); }
ImageID::ImageID(u32 id) { memcpy(m_storage, &id, 4); }
Parcel::Parcel(bool v) { m_storage[0] = 'b'; m_storage[1] = v; }
Parcel::Parcel(int v) { m_storage[0] = 'i'; memcpy(m_storage + 4, &v, 4); }
PluginApp::PluginApp(IDispatcher&, BasicString<char>, u32, u32, RHMIInterfaceVersion, const Bundle&, const Bundle&) {}
void PluginApp::post(Runnable*, u32) {}
u32 g_testi = 0, g_img = 0, g_ids = 0, g_props = 0;
bool g_verboso = false;
void PluginApp::updateModel(u32 id, const char* t) { ++g_testi; if (g_verboso) printf("  text %u \"%s\"\n", id, t); }
void PluginApp::updateModel(u32 id, const Image& im) { ++g_img; u32 n; memcpy(&n, &im, 4); if (g_verboso) printf("  image %u (%u bytes %.3s)\n", id, n, (const char*)&im + 4); }
void PluginApp::updateModel(u32 id, const ImageID& im) { ++g_ids; u32 n; memcpy(&n, &im, 4); if (g_verboso) printf("  imageID %u -> %u\n", id, n); }
void PluginApp::setProperty(u32 c, u32 p, const Parcel& v) {
    ++g_props;
    const unsigned char* s = (const unsigned char*)&v;
    int i; memcpy(&i, s + 4, 4);
    if (g_verboso) { if (s[0] == 'b') printf("  setProperty %u.%u = %s\n", c, p, s[1] ? "true" : "false"); else printf("  setProperty %u.%u = %d\n", c, p, i); }
}
bool PluginApp::onInit() { return true; }
void PluginApp::addHandler(u32 id, const DelegatoAzione&) { printf("  addHandler(%u)\n", id); }
void PluginApp::onFinalize() {}
DelegatoCds g_cds[32]; u32 g_nCds = 0;
void PluginApp::cdsAddPropertyChangedEventHandler(BasicString<char>& p, u32, const DelegatoCds& d) { g_cds[g_nCds++] = d; }
}
void cds(u32 i, const char* json) {
    static u16 buf[512]; u32 n = 0;
    while (json[n] && n < 511) { buf[n] = (unsigned char)json[n]; ++n; }
    buf[n] = 0;
    u16* p = buf;
    bmw::g_cds[i].funzione(bmw::g_cds[i].oggetto, *(const bmw::BasicString<u16>*)&p);
}
int main() {
    static unsigned char mem[0x200], disp[16], b1[16], b2[16];
    printf("== onInit (the fake framework has no vtable: creaApp is not run here)\n");
    (void)disp; (void)b1; (void)b2;
    g_app = (bmw::PluginApp*)mem;
    bmw::g_verboso = true;
    appOnInit(g_app);
    bmw::g_verboso = false;
    for (u32 t = 0; t < 40; ++t) giroRun(0);
    printf("== CDS registered: %u\n", bmw::g_nCds);
    printf("== numbers: ");
    int v;
    printf("%d ", numeroDa("{\"RPMSpeed\":3250}", "RPMSpeed", 0, &v) ? v : -1);
    printf("%d ", numeroDa("{\"acceleration\":{\"lateral\":-0.25,\"longitudinal\":1.5}}", "lateral", 0, &v) ? v : -1);
    printf("%d ", numeroDa("{\"acceleration\":{\"lateral\":-0.25,\"longitudinal\":1.5}}", "longitudinal", 0, &v) ? v : -1);
    printf("%d ", numeroDa("{\"x\":[12.5,-3]}", 0, 1, &v) ? v : -1);
    printf("%d\n", numeroDa("{\"gear\":\"N\"}", "gear", 0, &v) ? v : -1);
    printf("== car values, stage 0\n");
    cds(0, "{\"RPMSpeed\":5230}"); cds(1, "{\"speedActual\":87}"); cds(3, "{\"gear\":3}");
    cds(2, "{\"acceleration\":{\"lateral\":0.4,\"longitudinal\":-0.2}}");
    bmw::g_verboso = true; giroRun(0); bmw::g_verboso = false;
    for (u32 f = 1; f <= 4; ++f) {
        printf("== stage %u\n", f);
        static char finto[64];
        premutoFase(0, *(const bmw::Parcel*)finto);
        bmw::g_verboso = true; giroRun(0); bmw::g_verboso = false;
    }
    printf("== press: back to 0\n");
    { static char finto[64]; premutoFase(0, *(const bmw::Parcel*)finto); }
    printf("== totals: texts %u images %u imageIDs %u setProperty %u\n", bmw::g_testi, bmw::g_img, bmw::g_ids, bmw::g_props);
    return 0;
}
