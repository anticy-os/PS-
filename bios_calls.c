#include "bios_calls.h"
#include <stddef.h>
#include <stdint.h>

static const char *a0_names[256] = {
    [0x00]="FileOpen", [0x01]="FileSeek", [0x02]="FileRead", [0x03]="FileWrite",
    [0x04]="FileClose", [0x05]="FileIoctl", [0x06]="exit", [0x07]="FileGetDeviceFlag",
    [0x08]="FileGetc", [0x09]="FilePutc", [0x0A]="todigit", [0x0B]="atof",
    [0x0C]="strtoul", [0x0D]="strtol", [0x0E]="abs", [0x0F]="labs",
    [0x10]="atoi", [0x11]="atol", [0x12]="atob", [0x13]="SaveState",
    [0x14]="RestoreState", [0x15]="strcat", [0x16]="strncat", [0x17]="strcmp",
    [0x18]="strncmp", [0x19]="strcpy", [0x1A]="strncpy", [0x1B]="strlen",
    [0x1C]="index", [0x1D]="rindex", [0x1E]="strchr", [0x1F]="strrchr",
    [0x20]="strpbrk", [0x21]="strspn", [0x22]="strcspn", [0x23]="strtok",
    [0x24]="strstr", [0x25]="toupper", [0x26]="tolower", [0x27]="bcopy",
    [0x28]="bzero", [0x29]="bcmp", [0x2A]="memcpy", [0x2B]="memset",
    [0x2C]="memmove", [0x2D]="memcmp", [0x2E]="memchr", [0x2F]="rand",
    [0x30]="srand", [0x31]="qsort", [0x32]="strtod", [0x33]="malloc",
    [0x34]="free", [0x35]="lsearch", [0x36]="bsearch", [0x37]="calloc",
    [0x38]="realloc", [0x39]="InitHeap", [0x3A]="SystemErrorExit", [0x3B]="std_in_getchar",
    [0x3C]="std_out_putchar", [0x3D]="std_in_gets", [0x3E]="std_out_puts", [0x3F]="printf",
    [0x40]="SystemErrorUnresolvedException", [0x41]="LoadExeHeader", [0x42]="LoadExeFile",
    [0x43]="DoExecute", [0x44]="FlushCache", [0x45]="init_a0_b0_c0_vectors",
    [0x46]="GPU_dw", [0x47]="gpu_send_dma", [0x48]="SendGP1Command", [0x49]="GPU_cw",
    [0x4A]="GPU_cwp", [0x4B]="send_gpu_linked_list", [0x4C]="gpu_abort_dma",
    [0x4D]="GetGPUStatus", [0x4E]="gpu_sync", [0x51]="LoadAndExecute",
    [0x54]="CdInit", [0x55]="_bu_init", [0x56]="CdRemove",
    [0x5B]="dev_tty_init", [0x5C]="dev_tty_open", [0x5D]="dev_tty_in_out",
    [0x5E]="dev_tty_ioctl", [0x5F]="dev_cd_open", [0x60]="dev_cd_read",
    [0x61]="dev_cd_close", [0x62]="dev_cd_firstfile", [0x63]="dev_cd_nextfile",
    [0x64]="dev_cd_chdir", [0x65]="dev_card_open", [0x66]="dev_card_read",
    [0x67]="dev_card_write", [0x68]="dev_card_close", [0x69]="dev_card_firstfile",
    [0x6A]="dev_card_nextfile", [0x6B]="dev_card_erase", [0x6C]="dev_card_undelete",
    [0x6D]="dev_card_format", [0x6E]="dev_card_rename", [0x70]="_bu_init",
    [0x71]="CdAsyncSeekL", [0x78]="CdAsyncGetStatus", [0x7C]="CdAsyncReadSector",
    [0x7E]="CdGetLastSector", [0x96]="AddCDROMDevice", [0x97]="AddMemCardDevice",
    [0x98]="AddDuartTtyDevice", [0x99]="add_nullcon_driver", [0xA3]="DeliverEvent",
    [0xA6]="InstallExceptionHandlers", [0xA7]="SysInitMemory", [0xA8]="SysInitKernelVariables",
    [0xA9]="ChangeClearPAD", [0xAA]="InitPAD2", [0xAB]="StartPAD2", [0xAC]="_card_info_subfunc",
};

static const char *b0_names[256] = {
    [0x00]="alloc_kernel_memory", [0x01]="free_kernel_memory", [0x02]="init_timer",
    [0x03]="get_timer", [0x04]="enable_timer_irq", [0x05]="disable_timer_irq",
    [0x06]="restart_timer", [0x07]="DeliverEvent", [0x08]="OpenEvent",
    [0x09]="CloseEvent", [0x0A]="WaitEvent", [0x0B]="TestEvent",
    [0x0C]="EnableEvent", [0x0D]="DisableEvent", [0x0E]="OpenThread",
    [0x0F]="CloseThread", [0x10]="ChangeThread", [0x12]="InitPad",
    [0x13]="StartPad", [0x14]="StopPad", [0x15]="OutdatedPadInitAndStart",
    [0x16]="OutdatedPadGetButtons", [0x17]="ReturnFromException", [0x18]="SetDefaultExitFromException",
    [0x19]="SetCustomExitFromException", [0x20]="UnDeliverEvent", [0x32]="FileOpen",
    [0x33]="FileSeek", [0x34]="FileRead", [0x35]="FileWrite", [0x36]="FileClose",
    [0x37]="FileIoctl", [0x38]="exit", [0x39]="FileGetDeviceFlag", [0x3A]="FileGetc",
    [0x3B]="FilePutc", [0x3C]="std_in_getchar", [0x3D]="std_out_putchar",
    [0x3E]="std_in_gets", [0x3F]="std_out_puts", [0x40]="chdir", [0x41]="FormatDevice",
    [0x42]="firstfile", [0x43]="nextfile", [0x44]="FileRename", [0x45]="FileDelete",
    [0x46]="FileUndelete", [0x47]="AddDevice", [0x48]="RemoveDevice", [0x49]="PrintInstalledDevices",
    [0x4A]="InitCard", [0x4B]="StartCard", [0x4C]="StopCard", [0x4D]="_card_info",
    [0x4E]="_card_async_load_directory", [0x4F]="set_card_auto_format", [0x50]="_card_write_test",
    [0x56]="CdRemove", [0x57]="CdUnremove", [0x5B]="CdEject", [0x5C]="CdRetry",
    [0x5D]="CdStatus", [0x5E]="CdApplyVolume", [0x5F]="CdParseFilename",
    [0x66]="GetC0Table", [0x67]="GetB0Table", [0x69]="GetSystemInfo",
};

static const char *c0_names[256] = {
    [0x00]="InitRCnt", [0x01]="InitException", [0x02]="SysEnqIntRP",
    [0x03]="SysDeqIntRP", [0x04]="get_free_EvCB_slot", [0x05]="get_free_TCB_slot",
    [0x06]="ExceptionHandler", [0x07]="InstallExceptionHandler", [0x08]="SysInitMemory",
    [0x09]="SysInitKernelVariables", [0x0A]="ChangeClearRCnt", [0x0C]="InitDefInt",
    [0x0D]="SetIrqAutoAck", [0x12]="InstallDevices", [0x13]="FlushStdInOutPut",
    [0x1C]="AdjustA0Table",
};

const char *bios_call_name(uint32_t table_addr, uint32_t func_num) {
    if (func_num > 0xFF) return NULL;
    const char *name = NULL;
    switch (table_addr) {
        case 0xA0: name = a0_names[func_num]; break;
        case 0xB0: name = b0_names[func_num]; break;
        case 0xC0: name = c0_names[func_num]; break;
        default: return NULL;
    }
    return name;
}