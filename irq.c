#include "irq.h"
#include <stdint.h>
#include <stdbool.h>

uint32_t irq_read_status(IRQController *irq) {
     return irq->i_stat & 0x07FF;
}

uint32_t irq_read_mask(IRQController *irq) {
     return irq->i_mask & 0x07FF;
}

void irq_request(IRQController *irq, uint32_t irq_num) {
     irq->i_stat |= (1u << irq_num);
}