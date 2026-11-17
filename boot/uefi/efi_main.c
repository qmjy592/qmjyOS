#include "qmjy/boot.h"
#include <stddef.h>

/* UEFI x64 uses the Microsoft x64 calling convention. */
typedef unsigned long long EFI_UINTN;
typedef unsigned long long EFI_STATUS;
typedef unsigned long long EFI_PHYSICAL_ADDRESS;
typedef unsigned short EFI_CHAR16;
typedef void *EFI_HANDLE;
#define EFIAPI __attribute__((ms_abi))
#define EFI_ERROR(Status) (((long long)(Status)) < 0)
#define EFI_SUCCESS 0ULL
#define EFI_LOAD_ERROR 0x8000000000000001ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_INVALID_PARAMETER 2ULL
#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001U
#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_ALLOCATE_MAX_ADDRESS 1U
#define EFI_ALLOCATE_ADDRESS 2U
#define EFI_LOADER_CODE 1U
#define EFI_LOADER_DATA 2U
#define EFI_PAGE_SIZE 4096ULL
#define EFI_MAX_PHYS 0x3FFFFFFFULL
#define EFI_MEMORY_MAP_PAGES 256ULL /* 1 MiB reserved before GetMemoryMap */
#define EFI_MEMORY_MAP_EXTRA_PAGES 16ULL
#define EFI_MEMORY_MAP_MAX_GROWTH_ATTEMPTS 8U
#define EFI_EXIT_BOOT_SERVICES_MAX_ATTEMPTS 5U
#define EFI_PAGE_TABLE_PAGES 68U
_Static_assert(EFI_PAGE_TABLE_PAGES == QM_BOOT_UEFI_PAGE_TABLE_PAGES,
               "UEFI bootstrap page-table allocation size");
#define EFI_MAX_KERNEL_FILE (32ULL * 1024ULL * 1024ULL)
#define EFI_MAX_KERNEL_SPAN (128ULL * 1024ULL * 1024ULL)

static void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void)
{
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}

static void serial_putc(char c)
{
    unsigned int timeout = 1000000U;
    while (!(inb(0x3FD) & 0x20U) && --timeout)
        __asm__ volatile ("pause");
    if (timeout)
        outb(0x3F8, (unsigned char)c);
}

static void serial_puts(const char *text)
{
    while (*text)
        serial_putc(*text++);
}

static EFI_UINTN read_cr4(void)
{
    EFI_UINTN value;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

static void serial_put_hex64(EFI_UINTN value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    serial_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xFU]);
}

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt" ::: "memory");
}

static void log_status(const char *message, EFI_STATUS status)
{
    serial_puts(message);
    serial_puts(" status=");
    serial_put_hex64(status);
    serial_puts("\r\n");
}

typedef struct {
    unsigned int Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} EFI_GUID;

typedef struct {
    EFI_UINTN Signature;
    unsigned int Revision;
    unsigned int HeaderSize;
    unsigned int CRC32;
    unsigned int Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, unsigned char);
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, EFI_CHAR16 *);
};

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(unsigned int, unsigned int,
                                                 EFI_UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS,
                                             EFI_UINTN);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(EFI_UINTN *, void *, EFI_UINTN *,
                                                 EFI_UINTN *, unsigned int *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(unsigned int, EFI_UINTN, void **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER)(EFI_UINTN, EFI_UINTN,
                                                     EFI_UINTN, EFI_CHAR16 *);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, EFI_UINTN);
typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **,
                                                 EFI_HANDLE, EFI_HANDLE, unsigned int);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    void *HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    void *Stall;
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    EFI_OPEN_PROTOCOL OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_CHAR16 *FirmwareVendor;
    unsigned int FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    EFI_UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    unsigned int Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    unsigned int LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    EFI_UINTN ImageSize;
    unsigned int ImageCodeType;
    unsigned int ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    EFI_UINTN Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **,
                               EFI_CHAR16 *, EFI_UINTN, EFI_UINTN);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL *);
    void *Delete;
    EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL *, EFI_UINTN *, void *);
    void *Write;
    void *GetPosition;
    void *SetPosition;
    EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE_PROTOCOL *, EFI_GUID *, EFI_UINTN *, void *);
    void *SetInfo;
    void *Flush;
    void *OpenEx;
    void *ReadEx;
    void *WriteEx;
    void *FlushEx;
};

typedef struct {
    EFI_UINTN Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(void *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    unsigned short Year;
    unsigned char Month, Day, Hour, Minute, Second, Pad1;
    unsigned int Nanosecond;
    short TimeZone;
    unsigned char Daylight, Pad2;
} EFI_TIME;

typedef struct {
    EFI_UINTN Size;
    EFI_UINTN FileSize;
    EFI_UINTN PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    EFI_UINTN Attribute;
    EFI_CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct {
    unsigned int Version;
    unsigned int HorizontalResolution;
    unsigned int VerticalResolution;
    unsigned int PixelFormat;
    unsigned int PixelInformation[4];
    unsigned int PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    unsigned int MaxMode;
    unsigned int Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    EFI_UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    EFI_UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct {
    unsigned int Type;
    unsigned int Reserved;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_PHYSICAL_ADDRESS VirtualStart;
    EFI_UINTN NumberOfPages;
    EFI_UINTN Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* Guard the UEFI ABI layouts that this freestanding application dereferences. */
_Static_assert(sizeof(EFI_TABLE_HEADER) == 24U, "UEFI table header size");
_Static_assert(offsetof(EFI_BOOT_SERVICES, AllocatePages) == 40U,
               "UEFI AllocatePages offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, FreePages) == 48U,
               "UEFI FreePages offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, GetMemoryMap) == 56U,
               "UEFI GetMemoryMap offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, ExitBootServices) == 232U,
               "UEFI ExitBootServices offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, OpenProtocol) == 280U,
               "UEFI OpenProtocol offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, LocateProtocol) == 320U,
               "UEFI LocateProtocol offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, SetWatchdogTimer) == 256U,
               "UEFI SetWatchdogTimer offset");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, BootServices) == 96U,
               "UEFI SystemTable BootServices offset");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, ConfigurationTable) == 112U,
               "UEFI SystemTable ConfigurationTable offset");
_Static_assert(offsetof(EFI_FILE_PROTOCOL, GetInfo) == 64U,
               "UEFI FileProtocol GetInfo offset");
_Static_assert(sizeof(EFI_MEMORY_DESCRIPTOR) == 40U,
               "UEFI memory descriptor prefix size");

/* The x64 UEFI descriptor prefix is 40 bytes; firmware may use a larger stride. */
typedef struct {
    unsigned char bytes[40];
} EFI_MEMORY_DESCRIPTOR_PREFIX;

typedef struct {
    unsigned char bytes[16];
} EFI_GUID_SIZE_CHECK;

static EFI_GUID g_loaded_image_guid = {
    0x5B1B31A1U, 0x9562U, 0x11D2U,
    {0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}
};
static EFI_GUID g_simple_fs_guid = {
    0x964E5B22U, 0x6459U, 0x11D2U,
    {0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}
};
static EFI_GUID g_file_info_guid = {
    0x09576E92U, 0x6D3FU, 0x11D2U,
    {0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}
};
static EFI_GUID g_gop_guid = {
    0x9042A9DEU, 0x23DCU, 0x4A38U,
    {0x96,0xFB,0x7A,0xDE,0xD0,0x80,0x51,0x6A}
};
static EFI_GUID g_acpi20_guid = {
    0x8868E871U, 0xE4F1U, 0x11D3U,
    {0xBC,0x22,0x00,0x80,0xC7,0x3C,0x88,0x81}
};
static EFI_GUID g_acpi10_guid = {
    0xEB9D2D30U, 0x2D88U, 0x11D3U,
    {0x9A,0x16,0x00,0x90,0x27,0x3F,0xC1,0x4D}
};

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    unsigned int i;
    for (i = 0; i < 16U; ++i)
        if (x[i] != y[i]) return 0;
    return 1;
}

/* No UEFI CRT is linked. The compiler may lower aggregate copies to memcpy. */
void *memcpy(void *destination, const void *source, EFI_UINTN size)
{
    volatile unsigned char *d = (volatile unsigned char *)destination;
    const volatile unsigned char *s = (const volatile unsigned char *)source;
    EFI_UINTN i;
    for (i = 0; i < size; ++i) d[i] = s[i];
    return destination;
}

static void *zero_bytes(void *pointer, EFI_UINTN size)
{
    volatile unsigned char *p = (volatile unsigned char *)pointer;
    EFI_UINTN i;
    for (i = 0; i < size; ++i) p[i] = 0;
    return pointer;
}

static void copy_bytes(void *destination, const void *source, EFI_UINTN size)
{
    unsigned char *d = (unsigned char *)destination;
    const unsigned char *s = (const unsigned char *)source;
    EFI_UINTN i;
    for (i = 0; i < size; ++i) d[i] = s[i];
}

static int add_u64(EFI_UINTN a, EFI_UINTN b, EFI_UINTN *out)
{
    if (a > ~0ULL - b) return 0;
    *out = a + b;
    return 1;
}

static int is_power_of_two(EFI_UINTN value)
{
    return value != 0 && (value & (value - 1ULL)) == 0;
}

typedef struct __attribute__((packed)) {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    EFI_UINTN entry;
    EFI_UINTN phoff;
    EFI_UINTN shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} ELF64_HEADER;

typedef struct __attribute__((packed)) {
    unsigned int type;
    unsigned int flags;
    EFI_UINTN offset;
    EFI_UINTN vaddr;
    EFI_UINTN paddr;
    EFI_UINTN filesz;
    EFI_UINTN memsz;
    EFI_UINTN align;
} ELF64_PROGRAM_HEADER;

typedef struct {
    EFI_UINTN physical_start;
    EFI_UINTN memory_size;
    unsigned int flags;
} EFI_KERNEL_SEGMENT;

static EFI_KERNEL_SEGMENT g_kernel_segments[128];
static EFI_UINTN g_kernel_segment_count;

static int ranges_overlap(EFI_UINTN a, EFI_UINTN alen,
                          EFI_UINTN b, EFI_UINTN blen)
{
    EFI_UINTN ae, be;
    if (!alen || !blen || !add_u64(a, alen, &ae) || !add_u64(b, blen, &be))
        return 1;
    return a < be && b < ae;
}

static EFI_STATUS read_kernel_file(EFI_BOOT_SERVICES *bs,
                                   EFI_HANDLE image_handle,
                                   unsigned char **out_data,
                                   EFI_UINTN *out_size)
{
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *file = 0;
    EFI_FILE_INFO *file_info = 0;
    EFI_UINTN info_size = 0;
    EFI_UINTN file_size;
    EFI_UINTN offset;
    unsigned char *buffer = 0;
    EFI_STATUS status;
    static EFI_CHAR16 path[] = {
        '\\','E','F','I','\\','Q','M','J','Y','O','S','\\',
        'K','E','R','N','E','L','6','4','-','U','E','F','I','.','E','L','F',0
    };

    status = bs->OpenProtocol(image_handle, &g_loaded_image_guid,
                              (void **)&loaded, image_handle, 0,
                              EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status) || loaded == 0)
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;

    status = bs->OpenProtocol(loaded->DeviceHandle, &g_simple_fs_guid,
                              (void **)&fs, image_handle, 0,
                              EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status) || fs == 0)
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || root == 0)
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;

    status = root->Open(root, &file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || file == 0) {
        root->Close(root);
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;
    }

    status = file->GetInfo(file, &g_file_info_guid, &info_size, 0);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < sizeof(EFI_FILE_INFO)) {
        file->Close(file); root->Close(root);
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;
    }
    status = bs->AllocatePool(EFI_LOADER_DATA, info_size, (void **)&file_info);
    if (EFI_ERROR(status) || file_info == 0) {
        file->Close(file); root->Close(root);
        return EFI_ERROR(status) ? status : 0x8000000000000009ULL;
    }
    status = file->GetInfo(file, &g_file_info_guid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        bs->FreePool(file_info); file->Close(file); root->Close(root);
        return status;
    }
    file_size = file_info->FileSize;
    bs->FreePool(file_info);
    if (file_size == 0 || file_size > EFI_MAX_KERNEL_FILE) {
        file->Close(file); root->Close(root);
        return 0x8000000000000002ULL;
    }
    status = bs->AllocatePool(EFI_LOADER_DATA, file_size, (void **)&buffer);
    if (EFI_ERROR(status) || buffer == 0) {
        file->Close(file); root->Close(root);
        return EFI_ERROR(status) ? status : 0x8000000000000009ULL;
    }

    offset = 0;
    while (offset < file_size) {
        EFI_UINTN amount = file_size - offset;
        status = file->Read(file, &amount, buffer + offset);
        if (EFI_ERROR(status) || amount == 0) {
            bs->FreePool(buffer); file->Close(file); root->Close(root);
            return EFI_ERROR(status) ? status : 0x8000000000000007ULL;
        }
        offset += amount;
    }
    file->Close(file);
    root->Close(root);

    *out_data = buffer;
    *out_size = file_size;
    return EFI_SUCCESS;
}

static EFI_STATUS load_kernel_elf(EFI_BOOT_SERVICES *bs,
                                  const unsigned char *file,
                                  EFI_UINTN file_size,
                                  EFI_UINTN *kernel_base_out,
                                  EFI_UINTN *kernel_size_out,
                                  EFI_UINTN *entry_out)
{
    EFI_UINTN rollback_count = 0;
    EFI_PHYSICAL_ADDRESS rollback_addresses[128];
    EFI_UINTN rollback_pages[128];
    EFI_STATUS rollback_status;

    ELF64_HEADER header;
    EFI_UINTN table_bytes, table_end;
    EFI_UINTN minimum = ~0ULL;
    EFI_UINTN maximum = 0;
    EFI_UINTN kernel_base;
    EFI_UINTN entry = 0;
    unsigned int load_count = 0;
    int entry_executable = 0;
    unsigned int i, j;
    EFI_STATUS status;

    g_kernel_segment_count = 0;
    if (file == 0 || file_size < sizeof(header))
        return 0x8000000000000002ULL;
    copy_bytes(&header, file, sizeof(header));
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' ||
        header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != 2 || header.ident[5] != 1 ||
        header.ident[6] != 1 || header.type != 2 || header.machine != 62 ||
        header.version != 1 || header.ehsize < sizeof(header) ||
        header.phoff < header.ehsize ||
        header.phentsize < sizeof(ELF64_PROGRAM_HEADER) ||
        header.phnum == 0 || header.phnum > 128)
        return 0x8000000000000002ULL;
    if (header.phnum > (~0ULL / header.phentsize))
        return 0x8000000000000002ULL;
    table_bytes = (EFI_UINTN)header.phentsize * header.phnum;
    if (!add_u64(header.phoff, table_bytes, &table_end) || table_end > file_size)
        return 0x8000000000000002ULL;

    for (i = 0; i < header.phnum; ++i) {
        ELF64_PROGRAM_HEADER ph;
        EFI_UINTN position = header.phoff + (EFI_UINTN)i * header.phentsize;
        EFI_UINTN file_end, memory_end;
        copy_bytes(&ph, file + position, sizeof(ph));
        if (ph.type == 2U || ph.type == 3U || ph.type == 7U)
            return 0x8000000000000002ULL; /* PT_DYNAMIC/INTERP/TLS */
        if (ph.type != 1) continue; /* PT_LOAD only */
        if (ph.memsz == 0 || ph.filesz > ph.memsz ||
            !add_u64(ph.offset, ph.filesz, &file_end) || file_end > file_size ||
            !add_u64(ph.paddr, ph.memsz, &memory_end) || ph.vaddr != ph.paddr ||
            ph.paddr < 0x200000ULL || memory_end > 0x40000000ULL ||
            (ph.paddr & (EFI_PAGE_SIZE - 1ULL)) != 0 ||
            (ph.flags & ~7U) != 0 || (ph.flags & 4U) == 0 ||
            (ph.flags & 3U) == 3U)
            return 0x8000000000000002ULL;
        if (ph.align < EFI_PAGE_SIZE || !is_power_of_two(ph.align) ||
            ((ph.vaddr - ph.offset) & (ph.align - 1ULL)) != 0 ||
            ((ph.paddr - ph.offset) & (ph.align - 1ULL)) != 0)
            return 0x8000000000000002ULL;
        {
            EFI_UINTN rounded_memsz;
            if (!add_u64(ph.memsz, EFI_PAGE_SIZE - 1ULL, &rounded_memsz))
                return 0x8000000000000002ULL;
            rounded_memsz &= ~(EFI_PAGE_SIZE - 1ULL);
            if (rounded_memsz > 0x40000000ULL - ph.paddr)
                return 0x8000000000000002ULL;
        }
        /* The entry must point to bytes present in the ELF file, not zero-filled BSS. */
        if ((ph.flags & 1U) && header.entry >= ph.vaddr &&
            header.entry - ph.vaddr < ph.filesz)
            entry_executable = 1;
        for (j = 0; j < i; ++j) {
            ELF64_PROGRAM_HEADER previous;
            EFI_UINTN prev_pos = header.phoff + (EFI_UINTN)j * header.phentsize;
            EFI_UINTN prev_rounded;
            EFI_UINTN this_rounded;
            copy_bytes(&previous, file + prev_pos, sizeof(previous));
            if (previous.type != 1) continue;
            if (!add_u64(previous.memsz, EFI_PAGE_SIZE - 1ULL, &prev_rounded) ||
                !add_u64(ph.memsz, EFI_PAGE_SIZE - 1ULL, &this_rounded))
                return 0x8000000000000002ULL;
            prev_rounded &= ~(EFI_PAGE_SIZE - 1ULL);
            this_rounded &= ~(EFI_PAGE_SIZE - 1ULL);
            if (ranges_overlap(ph.paddr, this_rounded,
                               previous.paddr, prev_rounded))
                return 0x8000000000000002ULL;
        }
        if (g_kernel_segment_count >= 128ULL)
            return 0x8000000000000002ULL;
        g_kernel_segments[g_kernel_segment_count].physical_start = ph.paddr;
        g_kernel_segments[g_kernel_segment_count].memory_size = ph.memsz;
        g_kernel_segments[g_kernel_segment_count].flags = ph.flags;
        ++g_kernel_segment_count;
        if (ph.paddr < minimum) minimum = ph.paddr;
        if (memory_end > maximum) maximum = memory_end;
        ++load_count;
    }
    if (load_count == 0 || !entry_executable || maximum <= minimum)
        return 0x8000000000000002ULL;
    if (maximum - minimum > EFI_MAX_KERNEL_SPAN)
        return 0x8000000000000002ULL;

    kernel_base = minimum;
    if (maximum <= kernel_base || maximum > 0x40000000ULL)
        return 0x8000000000000002ULL;

    /* Each PT_LOAD owns complete pages: code is executable; data is writable.
     * linker64.ld page-aligns output segment boundaries to prevent W^X aliasing. */
    for (i = 0; i < header.phnum; ++i) {
        ELF64_PROGRAM_HEADER ph;
        EFI_UINTN position = header.phoff + (EFI_UINTN)i * header.phentsize;
        EFI_UINTN rounded_size;
        EFI_UINTN pages;
        EFI_PHYSICAL_ADDRESS requested;
        copy_bytes(&ph, file + position, sizeof(ph));
        if (ph.type != 1) continue;
        if (!add_u64(ph.memsz, EFI_PAGE_SIZE - 1ULL, &rounded_size))
            return 0x8000000000000002ULL;
        rounded_size &= ~(EFI_PAGE_SIZE - 1ULL);
        pages = rounded_size / EFI_PAGE_SIZE;
        /* Load all segments as writable LoaderData. The kernel replaces
         * firmware page tables after ExitBootServices and applies its own
         * executable policy, avoiding firmware code-page RO faults on copy. */
        requested = ph.paddr;
        status = bs->AllocatePages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
                                   pages, &requested);
        if (EFI_ERROR(status)) {
            while (rollback_count != 0) {
                --rollback_count;
                rollback_status = bs->FreePages(
                    rollback_addresses[rollback_count],
                    rollback_pages[rollback_count]);
                if (EFI_ERROR(rollback_status))
                    return rollback_status;
            }
            return status;
        }

        if (requested != ph.paddr) {
            rollback_status = bs->FreePages(requested, pages);
            if (EFI_ERROR(rollback_status))
                return rollback_status;

            while (rollback_count != 0) {
                --rollback_count;
                rollback_status = bs->FreePages(
                    rollback_addresses[rollback_count],
                    rollback_pages[rollback_count]);
                if (EFI_ERROR(rollback_status))
                    return rollback_status;
            }
            return 0x8000000000000002ULL;
        }

        if (rollback_count >= 128) {
            rollback_status = bs->FreePages(requested, pages);
            if (EFI_ERROR(rollback_status))
                return rollback_status;
            while (rollback_count != 0) {
                --rollback_count;
                rollback_status = bs->FreePages(
                    rollback_addresses[rollback_count],
                    rollback_pages[rollback_count]);
                if (EFI_ERROR(rollback_status))
                    return rollback_status;
            }
            return EFI_LOAD_ERROR;
        }

        rollback_addresses[rollback_count] = requested;
        rollback_pages[rollback_count] = pages;
        ++rollback_count;

        zero_bytes((void *)(unsigned long long)ph.paddr, rounded_size);
        if (ph.filesz != 0)
            copy_bytes((void *)(unsigned long long)ph.paddr,
                       file + ph.offset, ph.filesz);
    }

    entry = header.entry;
    *kernel_base_out = kernel_base;
    *kernel_size_out = maximum - kernel_base;
    *entry_out = entry;
    serial_puts("[QBOOT UEFI] ELF64 PT_LOAD image loaded and validated\r\n");
    return EFI_SUCCESS;
}

static int cpu_supports_nx(void)
{
    EFI_UINTN max_leaf = 0x80000000U;
    EFI_UINTN ebx = 0, ecx = 0, edx = 0;

    __asm__ volatile("cpuid"
                     : "+a"(max_leaf), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     :
                     : "memory");
    if (max_leaf < 0x80000001U)
        return 0;

    max_leaf = 0x80000001U;
    __asm__ volatile("cpuid"
                     : "+a"(max_leaf), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     :
                     : "memory");
    return (edx & (1U << 20)) != 0;
}

/*
 * Build a supervisor-only identity map for the first GiB.
 * Non-kernel memory remains RW+NX using 2 MiB pages. Every 2 MiB
 * region touched by the kernel is split into 4 KiB PTEs; ELF segment
 * flags stored by load_kernel_elf() are then applied page by page.
 * This function deliberately does not reference the loader's file buffer:
 * it may already have been released after PT_LOAD segments were copied.
 *
 * Three pages hold PML4/PDPT/PD. A 128 MiB span can intersect at most
 * 65 PDEs, so 68 contiguous pages are sufficient in the worst case.
 */
static EFI_STATUS build_kernel_identity_page_tables(
    EFI_PHYSICAL_ADDRESS root_physical,
    EFI_UINTN kernel_base,
    EFI_UINTN kernel_size)
{
    volatile EFI_UINTN *pml4;
    volatile EFI_UINTN *pdpt;
    volatile EFI_UINTN *pd;
    EFI_UINTN kernel_end;
    EFI_UINTN first_pde;
    EFI_UINTN last_pde;
    EFI_UINTN table_count;
    EFI_UINTN next_table = 3ULL;
    EFI_UINTN i;
    int nx_available;

    if (root_physical == 0 ||
        (root_physical & (EFI_PAGE_SIZE - 1ULL)) != 0 ||
        root_physical >= 0x40000000ULL ||
        EFI_PAGE_TABLE_PAGES > (0x40000000ULL - root_physical) / EFI_PAGE_SIZE ||
        g_kernel_segment_count == 0 || g_kernel_segment_count > 128ULL ||
        kernel_size == 0 || kernel_base < 0x200000ULL ||
        kernel_base >= 0x40000000ULL ||
        kernel_size > EFI_MAX_KERNEL_SPAN ||
        kernel_size > 0x40000000ULL - kernel_base)
        return (EFI_STATUS)0x8000000000000002ULL;

    kernel_end = kernel_base + kernel_size;
    first_pde = kernel_base >> 21;
    last_pde = (kernel_end - 1ULL) >> 21;
    table_count = last_pde - first_pde + 1ULL;
    if (table_count == 0 || table_count > 65ULL ||
        table_count + 3ULL > EFI_PAGE_TABLE_PAGES)
        return (EFI_STATUS)0x8000000000000002ULL;

    nx_available = cpu_supports_nx();
    if (!nx_available) {
        serial_puts("[QBOOT UEFI] CPU lacks NX; refusing a kernel mapping without W^X.\r\n");
        return (EFI_STATUS)0x8000000000000003ULL;
    }

    zero_bytes((void *)(unsigned long long)root_physical,
               EFI_PAGE_TABLE_PAGES * EFI_PAGE_SIZE);
    pml4 = (volatile EFI_UINTN *)(unsigned long long)root_physical;
    pdpt = (volatile EFI_UINTN *)(unsigned long long)(root_physical + EFI_PAGE_SIZE);
    pd = (volatile EFI_UINTN *)(unsigned long long)(root_physical + 2ULL * EFI_PAGE_SIZE);

    pml4[0] = (root_physical + EFI_PAGE_SIZE) | 0x3ULL;
    pdpt[0] = (root_physical + 2ULL * EFI_PAGE_SIZE) | 0x3ULL;

    /* Map every low 1 GiB page supervisor-only and non-executable. */
    for (i = 0; i < 512ULL; ++i) {
        EFI_UINTN physical = i * 0x200000ULL;
        EFI_UINTN nx = 1ULL << 63;
        if (i >= first_pde && i <= last_pde) {
            EFI_UINTN pt_physical = root_physical + next_table * EFI_PAGE_SIZE;
            volatile EFI_UINTN *pt =
                (volatile EFI_UINTN *)(unsigned long long)pt_physical;
            EFI_UINTN page;
            for (page = 0; page < 512ULL; ++page)
                pt[page] = (physical + page * EFI_PAGE_SIZE) | 0x3ULL | nx;
            pd[i] = pt_physical | 0x3ULL;
            ++next_table;
        } else {
            pd[i] = physical | 0x83ULL | nx;
        }
    }

    /* Override kernel leaves with the validated ELF segment permissions. */
    for (i = 0; i < g_kernel_segment_count; ++i) {
        const EFI_KERNEL_SEGMENT *segment = &g_kernel_segments[i];
        EFI_UINTN rounded;
        EFI_UINTN pages;
        EFI_UINTN page_index;
        EFI_UINTN pte_flags = 0x1ULL;
        EFI_UINTN paddr = segment->physical_start;
        EFI_UINTN memsz = segment->memory_size;
        unsigned int flags = segment->flags;

        if (memsz == 0 || paddr < kernel_base || paddr >= kernel_end ||
            memsz > kernel_end - paddr || (flags & ~7U) != 0 ||
            (flags & 4U) == 0 || (flags & 3U) == 3U ||
            (paddr & (EFI_PAGE_SIZE - 1ULL)) != 0 ||
            !add_u64(memsz, EFI_PAGE_SIZE - 1ULL, &rounded))
            return (EFI_STATUS)0x8000000000000002ULL;
        rounded &= ~(EFI_PAGE_SIZE - 1ULL);
        pages = rounded / EFI_PAGE_SIZE;
        if (flags & 2U)
            pte_flags |= 0x2ULL;
        if (!(flags & 1U))
            pte_flags |= (1ULL << 63);

        for (page_index = 0; page_index < pages; ++page_index) {
            EFI_UINTN physical = paddr + page_index * EFI_PAGE_SIZE;
            EFI_UINTN pde_index = physical >> 21;
            EFI_UINTN pte_index = (physical >> 12) & 0x1FFULL;
            EFI_UINTN pt_slot = 3ULL;
            EFI_UINTN n;
            volatile EFI_UINTN *pt;

            if (pde_index < first_pde || pde_index > last_pde)
                return (EFI_STATUS)0x8000000000000002ULL;
            for (n = first_pde; n < pde_index; ++n)
                ++pt_slot;
            pt = (volatile EFI_UINTN *)(unsigned long long)
                (root_physical + pt_slot * EFI_PAGE_SIZE);
            if ((pd[pde_index] & 0x000FFFFFFFFFF000ULL) !=
                (root_physical + pt_slot * EFI_PAGE_SIZE))
                return (EFI_STATUS)0x8000000000000002ULL;
            pt[pte_index] = physical | pte_flags;
        }
    }

    if (next_table != table_count + 3ULL)
        return (EFI_STATUS)0x8000000000000002ULL;

    serial_puts("[QBOOT UEFI] 4 KiB kernel page tables built with W^X permissions\r\n");
    return EFI_SUCCESS;
}
static EFI_STATUS allocate_low_pages(EFI_BOOT_SERVICES *bs, EFI_UINTN pages,
                                     EFI_PHYSICAL_ADDRESS *address)
{
    EFI_STATUS status;
    EFI_STATUS free_status;
    EFI_PHYSICAL_ADDRESS maximum = EFI_MAX_PHYS;
    uint64_t allocation_bytes;

    if (bs == NULL || address == NULL || pages == 0)
        return EFI_LOAD_ERROR;

    *address = 0;

    /* Check multiplication before requesting pages. */
    if ((uint64_t)pages > UINT64_MAX / EFI_PAGE_SIZE)
        return EFI_LOAD_ERROR;

    allocation_bytes = (uint64_t)pages * EFI_PAGE_SIZE;

    status = bs->AllocatePages(
        EFI_ALLOCATE_MAX_ADDRESS,
        EFI_LOADER_DATA,
        pages,
        &maximum
    );
    if (EFI_ERROR(status))
        return status;

    /* The complete allocation must remain inside the 1 GiB identity map. */
    if (maximum == 0 ||
        maximum > EFI_MAX_PHYS ||
        maximum >= 0x40000000ULL ||
        allocation_bytes > 0x40000000ULL - maximum) {

        free_status = bs->FreePages(maximum, pages);
        if (EFI_ERROR(free_status))
            return free_status;

        return EFI_LOAD_ERROR;
    }

    *address = maximum;
    return EFI_SUCCESS;
}

static EFI_UINTN find_acpi_rsdp(EFI_SYSTEM_TABLE *st)
{
    EFI_UINTN i;
    if (st == 0 || st->ConfigurationTable == 0) return 0;
    for (i = 0; i < st->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE *entry = &st->ConfigurationTable[i];
        const unsigned char *p;
        unsigned int j;
        unsigned int sum = 0;
        if (guid_equal(&entry->VendorGuid, &g_acpi20_guid) ||
            guid_equal(&entry->VendorGuid, &g_acpi10_guid)) {
            p = (const unsigned char *)entry->VendorTable;
            if (p == 0 || p[0] != 'R' || p[1] != 'S' || p[2] != 'D' ||
                p[3] != ' ' || p[4] != 'P' || p[5] != 'T' ||
                p[6] != 'R' || p[7] != ' ')
                continue;
            for (j = 0; j < 20U; ++j) sum += p[j];
            if ((sum & 0xFFU) != 0) continue;
            if (p[15] >= 2U) {
                unsigned int length = (unsigned int)p[20] |
                    ((unsigned int)p[21] << 8) |
                    ((unsigned int)p[22] << 16) |
                    ((unsigned int)p[23] << 24);
                if (length < 36U || length > 4096U) continue;
                sum = 0;
                for (j = 0; j < length; ++j) sum += p[j];
                if ((sum & 0xFFU) != 0) continue;
            }
            return (EFI_UINTN)entry->VendorTable;
        }
    }
    return 0;
}

static void collect_framebuffer(EFI_BOOT_SERVICES *bs, qm_boot_framebuffer_t *fb)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status;
    EFI_GUID *guid = &g_gop_guid;
    (void)zero_bytes(fb, sizeof(*fb));
    status = bs->LocateProtocol(guid, 0, (void **)&gop);
    if (EFI_ERROR(status) || gop == 0 || gop->Mode == 0 || gop->Mode->Info == 0)
        return;
    if (gop->Mode->FrameBufferBase == 0 || gop->Mode->FrameBufferSize == 0 ||
        gop->Mode->Info->HorizontalResolution == 0 ||
        gop->Mode->Info->VerticalResolution == 0 ||
        gop->Mode->Info->PixelsPerScanLine == 0 ||
        gop->Mode->Info->PixelFormat > 1U)
        return;
    if ((EFI_UINTN)gop->Mode->Info->PixelsPerScanLine > 0xFFFFFFFFULL / 4ULL)
        return;
    fb->physical_address = gop->Mode->FrameBufferBase;
    fb->size = gop->Mode->FrameBufferSize;
    fb->width = gop->Mode->Info->HorizontalResolution;
    fb->height = gop->Mode->Info->VerticalResolution;
    fb->pitch = gop->Mode->Info->PixelsPerScanLine * 4U;
    fb->bits_per_pixel = 32U;
    fb->pixel_format = (qm_u16_t)gop->Mode->Info->PixelFormat;
}

__attribute__((ms_abi))
EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    EFI_UINTN map_pages = EFI_MEMORY_MAP_PAGES;
    EFI_UINTN required_map_pages;
    EFI_UINTN new_map_pages;
    EFI_PHYSICAL_ADDRESS new_map_buffer_pa;
    EFI_STATUS map_free_status;
    EFI_STATUS map_cleanup_status;

    EFI_BOOT_SERVICES *bs;
    unsigned char *kernel_file = 0;
    EFI_UINTN kernel_file_size = 0;
    EFI_UINTN kernel_base = 0, kernel_size = 0, kernel_entry = 0;
    EFI_PHYSICAL_ADDRESS boot_page = 0, map_buffer_pa = 0;
    EFI_PHYSICAL_ADDRESS page_table_root = 0;
    void *map_buffer;
    EFI_UINTN map_capacity = EFI_MEMORY_MAP_PAGES * EFI_PAGE_SIZE;
    EFI_UINTN map_size = 0, map_key = 0, descriptor_size = 0;
    unsigned int descriptor_version = 0;
    EFI_STATUS status;
    unsigned int exit_attempt;
    unsigned int map_growth_attempt = 0;
    int exited = 0;
    qm_boot_info_t *boot_info;
    qm_boot_memory_region_t *regions;
    qm_boot_framebuffer_t framebuffer;
    EFI_UINTN rsdp;
    EFI_UINTN regions_offset = (sizeof(qm_boot_info_t) + 7ULL) & ~7ULL;
    typedef void (__attribute__((sysv_abi)) *QM_KERNEL_ENTRY)(qm_boot_info_t *);

    serial_init();
    /* This bootstrap constructs four-level paging only. Fail closed on LA57. */
    if ((read_cr4() & (1ULL << 12)) != 0) {
        serial_puts("[QBOOT UEFI] LA57 five-level paging is unsupported by this kernel bootstrap.\r\n");
        return (EFI_STATUS)0x8000000000000003ULL; /* EFI_UNSUPPORTED */
    }
    serial_puts("QBOOT_EFI_SERIAL_START\r\n");
    serial_puts("[QBOOT UEFI] x86_64 firmware application entered\r\n");
    if (system_table == 0 || system_table->BootServices == 0)
        halt_forever();
    bs = system_table->BootServices;

    if (read_cr4() & (1ULL << 12)) {
        serial_puts("[QBOOT UEFI] 5-level paging (LA57) is not supported by this kernel yet\r\n");
        halt_forever();
    }

    status = read_kernel_file(bs, image_handle, &kernel_file, &kernel_file_size);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] kernel file read failed", status);
        halt_forever();
    }
    serial_puts("[QBOOT UEFI] kernel ELF file read\r\n");

    status = load_kernel_elf(bs, kernel_file, kernel_file_size,
                             &kernel_base, &kernel_size, &kernel_entry);
    bs->FreePool(kernel_file);
    kernel_file = 0;
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] kernel ELF load failed", status);
        halt_forever();
    }

    status = allocate_low_pages(bs, 1, &boot_page);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] boot-info page allocation failed", status);
        halt_forever();
    }
    status = allocate_low_pages(bs, EFI_MEMORY_MAP_PAGES, &map_buffer_pa);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] memory-map buffer allocation failed", status);
        halt_forever();
    }
    status = allocate_low_pages(bs, EFI_PAGE_TABLE_PAGES, &page_table_root);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] page-table allocation failed", status);
        halt_forever();
    }
    status = build_kernel_identity_page_tables(page_table_root, kernel_base, kernel_size);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] page-table construction failed", status);
        halt_forever();
    }
    serial_puts("[QBOOT UEFI] kernel identity page tables prepared\r\n");

    boot_info = (qm_boot_info_t *)(unsigned long long)boot_page;
    regions = (qm_boot_memory_region_t *)
        ((unsigned char *)(unsigned long long)boot_page + regions_offset);
    if (regions_offset + QM_BOOT_MAX_MEMORY_REGIONS * sizeof(*regions) > EFI_PAGE_SIZE) {
        serial_puts("[QBOOT UEFI] boot-info page layout overflow\r\n");
        halt_forever();
    }
    map_buffer = (void *)(unsigned long long)map_buffer_pa;

    collect_framebuffer(bs, &framebuffer);
    rsdp = find_acpi_rsdp(system_table);
    serial_puts("[QBOOT UEFI] GOP and ACPI discovery complete\r\n");

    /* Disable the firmware watchdog before leaving Boot Services so a long-
     * running kernel is not reset by a still-armed loader watchdog. */
    status = bs->SetWatchdogTimer(0, 0, 0, 0);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] watchdog disable failed", status);
        halt_forever();
    }

    /*
     * GetMemoryMap/ExitBootServices is retried with a fresh MapKey each time.
     * Firmware may partially shut down services after the first EBS call;
     * therefore, after that point this path uses only GetMemoryMap,
     * ExitBootServices, and the UEFI Memory Allocation Services used for
     * buffer growth. All protocol discovery and watchdog work is complete.
     *
     * Buffer growth and ExitBootServices attempts have separate limits:
     * growth must not consume the number of actual EBS attempts.
     */
    for (exit_attempt = 0;
         exit_attempt < EFI_EXIT_BOOT_SERVICES_MAX_ATTEMPTS;) {
        map_size = map_capacity;
        status = bs->GetMemoryMap(&map_size, map_buffer, &map_key,
                                  &descriptor_size, &descriptor_version);

        if (status == EFI_BUFFER_TOO_SMALL) {
            if (map_growth_attempt >= EFI_MEMORY_MAP_MAX_GROWTH_ATTEMPTS) {
                serial_puts("[QBOOT UEFI] memory-map growth limit reached\r\n");
                halt_forever();
            }
            ++map_growth_attempt;

            /* EFI returns the required byte count in map_size. */
            if (map_size == 0 ||
                map_size > UINT64_MAX - (EFI_PAGE_SIZE - 1ULL)) {
                serial_puts("[QBOOT UEFI] invalid required memory-map size\r\n");
                halt_forever();
            }

            required_map_pages =
                (map_size + EFI_PAGE_SIZE - 1ULL) / EFI_PAGE_SIZE;

            /* Reserve headroom: allocating the replacement can itself add
             * descriptors. Keep the full allocation below the 1-GiB map. */
            if (required_map_pages == 0 ||
                required_map_pages >
                    (0x40000000ULL / EFI_PAGE_SIZE) -
                    EFI_MEMORY_MAP_EXTRA_PAGES) {
                serial_puts("[QBOOT UEFI] required memory-map buffer too large\r\n");
                halt_forever();
            }

            new_map_pages =
                required_map_pages + EFI_MEMORY_MAP_EXTRA_PAGES;
            if (new_map_pages > UINT64_MAX / EFI_PAGE_SIZE) {
                serial_puts("[QBOOT UEFI] memory-map allocation overflow\r\n");
                halt_forever();
            }

            status = allocate_low_pages(
                bs, new_map_pages, &new_map_buffer_pa);
            if (EFI_ERROR(status)) {
                log_status("[QBOOT UEFI] memory-map growth allocation failed",
                           status);
                halt_forever();
            }

            /* Allocate replacement first; only then release the old block.
             * FreePages changes the map, so always fetch a new MapKey next. */
            map_free_status = bs->FreePages(map_buffer_pa, map_pages);
            if (EFI_ERROR(map_free_status)) {
                map_cleanup_status =
                    bs->FreePages(new_map_buffer_pa, new_map_pages);
                if (EFI_ERROR(map_cleanup_status)) {
                    log_status("[QBOOT UEFI] replacement buffer cleanup failed",
                               map_cleanup_status);
                }
                log_status("[QBOOT UEFI] old memory-map buffer release failed",
                           map_free_status);
                halt_forever();
            }

            map_buffer_pa = new_map_buffer_pa;
            map_buffer = (void *)(unsigned long long)map_buffer_pa;
            map_pages = new_map_pages;
            map_capacity = map_pages * EFI_PAGE_SIZE;

            serial_puts("[QBOOT UEFI] memory-map buffer expanded; retrying\r\n");
            continue;
        }

        if (EFI_ERROR(status)) {
            log_status("[QBOOT UEFI] GetMemoryMap failed", status);
            halt_forever();
        }

        /* DescriptorSize is a firmware-provided stride, not necessarily the
         * size of the prefix struct. Reject partial trailing descriptors. */
        if (descriptor_version != 1U ||
            descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
            map_size == 0 || map_size > map_capacity ||
            (map_size % descriptor_size) != 0) {
            serial_puts("[QBOOT UEFI] invalid firmware memory-map layout\r\n");
            halt_forever();
        }

        /* No allocations or unrelated Boot Services occur between this
         * successful GetMemoryMap and ExitBootServices. */
        ++exit_attempt;
        status = bs->ExitBootServices(image_handle, map_key);
        if (!EFI_ERROR(status)) {
            exited = 1;
            __asm__ volatile ("cli" ::: "memory");
            break;
        }

        if ((status & 0xFFFFULL) != EFI_INVALID_PARAMETER) {
            log_status("[QBOOT UEFI] ExitBootServices failed", status);
            halt_forever();
        }
        /* EFI_INVALID_PARAMETER: loop, refresh the map, and retry. */
    }
    if (!exited) {
        serial_puts("[QBOOT UEFI] ExitBootServices retry limit reached\r\n");
        halt_forever();
    }

    /* No Boot Services or protocol methods are called after this point. */
    if (exit_attempt > 1U)
        serial_puts("[QBOOT UEFI] ExitBootServices MapKey retry succeeded\r\n");
    serial_puts("[QBOOT UEFI] ExitBootServices PASS\r\n");
    status = qm_boot_info_from_uefi(
        map_buffer, map_size, descriptor_size,
        regions, (qm_u64_t)((unsigned long long)boot_page + regions_offset),
        QM_BOOT_MAX_MEMORY_REGIONS,
        kernel_base, kernel_size, kernel_base, kernel_entry,
        &framebuffer, rsdp, (qm_u64_t)(unsigned long long)system_table,
        (qm_u64_t)(unsigned long long)page_table_root, boot_info);
    if (status != QM_OK) {
        serial_puts("[QBOOT UEFI] boot-info normalization failed code=");
        serial_put_hex64((EFI_UINTN)(unsigned int)(-status));
        serial_puts("\r\n");
        halt_forever();
    }
    serial_puts("[QBOOT UEFI] qm_boot_info_t validated\r\n");
    serial_puts("[QBOOT UEFI] kernel handoff via RDI\r\n");

    ((QM_KERNEL_ENTRY)(unsigned long long)kernel_entry)(boot_info);
    serial_puts("[QBOOT UEFI] kernel entry unexpectedly returned\r\n");
    halt_forever();
    return EFI_SUCCESS;
}
