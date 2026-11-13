/*
 * uefi.h - the small slice of the UEFI ABI simple-os needs.
 *
 * The layout of every structure here is defined by the UEFI specification.
 * The static assertions at the bottom catch accidental padding changes.
 */

#ifndef UEFI_H
#define UEFI_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  BOOLEAN;
typedef uint16_t CHAR16;
typedef uint64_t UINTN;
typedef uint64_t EFI_STATUS;
typedef void    *EFI_HANDLE;
typedef void    *EFI_EVENT;

#define EFI_SUCCESS 0ULL

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    uint16_t ScanCode;
    CHAR16   UnicodeChar;
} EFI_INPUT_KEY;

typedef struct {
    uint16_t Year;
    uint8_t  Month;
    uint8_t  Day;
    uint8_t  Hour;
    uint8_t  Minute;
    uint8_t  Second;
    uint8_t  Pad1;
    uint32_t Nanosecond;
    uint16_t TimeZone;
    uint8_t  Daylight;
    uint8_t  Pad2;
} EFI_TIME;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (*Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);
    EFI_STATUS (*OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    EFI_STATUS (*ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
    EFI_STATUS (*SetCursorPosition)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN, UINTN);
    void *EnableCursor;
    void *Mode;
};

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (*ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, EFI_INPUT_KEY *);
    EFI_EVENT WaitForKey;
};

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *Reserved[28]; /* RaiseTPL ... GetNextMonotonicCount */
    EFI_STATUS (*Stall)(UINTN microseconds);
    EFI_STATUS (*SetWatchdogTimer)(UINTN, uint64_t, UINTN, CHAR16 *);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_STATUS (*GetTime)(EFI_TIME *, void *);
    void *Reserved[9]; /* SetTime ... GetNextHighMonotonicCount */
    EFI_STATUS (*ResetSystem)(UINTN, EFI_STATUS, UINTN, void *);
} EFI_RUNTIME_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

#define EFI_RESET_COLD 0

extern EFI_SYSTEM_TABLE *gST;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_RUNTIME_SERVICES *gRT;

_Static_assert(sizeof(EFI_TABLE_HEADER) == 24, "EFI_TABLE_HEADER layout");
_Static_assert(sizeof(EFI_TIME) == 16, "EFI_TIME layout");
_Static_assert(offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, OutputString) == 8,
               "EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL layout");
_Static_assert(offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, ClearScreen) == 48,
               "EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL layout");
_Static_assert(offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, SetCursorPosition) == 56,
               "EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL layout");
_Static_assert(offsetof(EFI_SIMPLE_TEXT_INPUT_PROTOCOL, ReadKeyStroke) == 8,
               "EFI_SIMPLE_TEXT_INPUT_PROTOCOL layout");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, ConIn) == 48, "EFI_SYSTEM_TABLE layout");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, ConOut) == 64, "EFI_SYSTEM_TABLE layout");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, RuntimeServices) == 88, "EFI_SYSTEM_TABLE layout");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, BootServices) == 96, "EFI_SYSTEM_TABLE layout");
_Static_assert(offsetof(EFI_BOOT_SERVICES, Stall) == 248, "EFI_BOOT_SERVICES layout");
_Static_assert(offsetof(EFI_BOOT_SERVICES, SetWatchdogTimer) == 256,
               "EFI_BOOT_SERVICES layout");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, GetTime) == 24, "EFI_RUNTIME_SERVICES layout");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, ResetSystem) == 104,
               "EFI_RUNTIME_SERVICES layout");

#endif
