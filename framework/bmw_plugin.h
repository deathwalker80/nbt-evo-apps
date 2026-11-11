// Declarations of the NBT EVO HMI plugin framework (libAppFramework.A.so / libCoreFramework.A.so on hu-omap),
// as used by NBT-AAD and by the iDrive Plus plugins. Only what the apps here call. No headers of the head unit
// are needed: the names below are linked against the libraries copied from your own head unit (build/link/).
#ifndef NBT_EVO_APPS_BMW_PLUGIN_H
#define NBT_EVO_APPS_BMW_PLUGIN_H

typedef unsigned int   u32;
typedef unsigned short u16;
typedef int            ssize_t_q;
typedef long           time_t_q;

extern "C" {
void* malloc(u32);
void  free(void*);
void* memset(void*, int, u32);
void* memcpy(void*, const void*, u32);
u32   strlen(const char*);
int   strncmp(const char*, const char*, u32);
int   snprintf(char*, u32, const char*, ...);
int   vsnprintf(char*, u32, const char*, __builtin_va_list);
void* fopen(const char*, const char*);
int   fclose(void*);
int   fprintf(void*, const char*, ...);
int   open(const char*, int, ...);
ssize_t_q read(int, void*, u32);
int   close(int);
int   access(const char*, int);
int   unlink(const char*);
void* opendir(const char*);
void* readdir(void*);
int   closedir(void*);
time_t_q time(time_t_q*);
void* localtime_r(const time_t_q*, void*);
int   clock_gettime(int, void*);
void* dlsym(void*, const char*);
int   getpid(void);
}

enum { O_RDONLY_Q = 0, F_OK_Q = 0, CLOCK_MONOTONIC_Q = 2 /* QNX 6 */ };

inline void* operator new(__SIZE_TYPE__, void* p) { return p; }   // placement new, without <new>

namespace bmw {
class IDispatcher;
class Bundle;
class Runnable;
enum RHMIInterfaceVersion { RHMI_INTERFACE_VERSION_1 = 1 };

template <typename T> class BasicString {
public:
    BasicString(const T* s);                       // _ZN3bmw11BasicStringIcEC1EPKc
    BasicString(const BasicString& other);
    ~BasicString();
    const T* raw() const { return m_data; }
private:
    T* m_data;
};

class BufferStream {
public:
    ~BufferStream();                               // _ZN3bmw12BufferStreamD1Ev
private:
    unsigned char m_storage[0x18];
};

class Image {
public:
    Image(const unsigned char* data, u32 size);    // _ZN3bmw5ImageC1EPKhj (copies the bytes)
private:
    unsigned char m_storage[0x20];
};

// Parcel: value of a property (setProperty) or the arguments of an action. Its real size is not known: it is
// always built in a buffer far larger than needed and destroyed with the exported base destructor (D2).
class Parcel {
public:
    Parcel(bool value);                            // _ZN3bmw6ParcelC1Eb
    Parcel(int value);                             // _ZN3bmw6ParcelC1Ei
private:
    unsigned char m_storage[128];
};

// ImageID: a picture of the plugin's own <Name>_ALL_Images.zip (<id>.png), drawn by the HMI itself
class ImageID {
public:
    ImageID(u32 id);                               // _ZN3bmw7ImageIDC1Ej
private:
    unsigned char m_storage[64];
};

// Delegate<bool, const Parcel&>: object + function (as NBT-AAD for the raAction)
struct DelegatoAzione { void* oggetto; bool (*funzione)(void* oggetto, const Parcel& argomenti); };
// Delegate<bool, const BasicString<unsigned short>&>: a CDS value as UTF-16 JSON text
struct DelegatoCds { void* oggetto; bool (*funzione)(void* oggetto, const BasicString<u16>& valore); };

const u32 kPluginAppStorage = 0x200;               // as NBT-AAD

class PluginApp {
public:
    PluginApp(IDispatcher& dispatcher, BasicString<char> name, u32 major, u32 minor,
              RHMIInterfaceVersion rhmi, const Bundle& params, const Bundle& config);
    void post(Runnable* runnable, u32 delayMs);                  // run() on the plugin thread after delayMs
    void updateModel(u32 modelId, const char* text);             // _ZN3bmw9PluginApp11updateModelEjPKc
    void updateModel(u32 modelId, const Image& image);           // _ZN3bmw9PluginApp11updateModelEjRKNS_5ImageE
    void updateModel(u32 modelId, const ImageID& image);         // _ZN3bmw9PluginApp11updateModelEjRKNS_7ImageIDE
    void setProperty(u32 componentId, u32 propertyId, const Parcel& value);   // _ZN3bmw9PluginApp11setPropertyEjjRKNS_6ParcelE
    void addHandler(u32 actionId, const DelegatoAzione& handler) __asm__("_ZN3bmw9PluginApp10addHandlerEjRKNS_8DelegateIbRKNS_6ParcelEvvvEE");
    void cdsAddPropertyChangedEventHandler(BasicString<char>& property, u32 id, const DelegatoCds& handler)
        __asm__("_ZN3bmw9PluginApp33cdsAddPropertyChangedEventHandlerERNS_11BasicStringIcEEjRKNS_8DelegateIbRKNS1_ItEEvvvEE");
    bool onInit();
    void onFinalize();
private:
    unsigned char m_storage[kPluginAppStorage];
};
} // namespace bmw

extern "C" void parcelDistruggi(void* parcel) __asm__("_ZN3bmw6ParcelD2Ev");   // exported (weak) base destructor

// RHMI component properties (the same ids as the plugin XML and IDriveConnectKit RHMIProperty)
enum { kPropVisibile = 3, kPropLarghezza = 9, kPropAltezza = 10, kPropX = 20, kPropY = 21 };

#endif
