#ifndef IRQ_H
#define IRQ_H

#define I_VBLANK 0
#define I_GPU    1
#define I_CDROM  2
#define I_DMA    3
#define I_TIMER0 4
#define I_TIMER1 5
#define I_TIMER2 6
#define I_CONTROLLER 7
#define I_SIO    8
#define I_SPU    9
#define I_PIO    10

#include <stdint.h>
#include <stdbool.h>

typedef struct {
     uint32_t i_mask;
     uint32_t i_stat;
} IRQController;

uint32_t irq_read_status(IRQController *irq);
uint32_t irq_read_mask(IRQController *irq);
void irq_request(IRQController *irq, uint32_t irq_num);

#endif