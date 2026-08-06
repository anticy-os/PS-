#ifndef BIOS_CALLS_H
#define BIOS_CALLS_H

#include <stdint.h>

const char *bios_call_name(uint32_t table_addr, uint32_t func_num);

#endif