/* Shared by the pre-XNU touch diagnostic and verified UMI boost handoff. */
#pragma once
#include <stdbool.h>
static void delay(unsigned us)
{
    uint64_t end = now() + (rsr("cntfrq_el0") * us + 999999) / 1000000;
    while (now() < end) { }
}
static void tp_write(uintptr_t p, uint32_t v)
{
    wr32(p, v);
    __asm__ volatile("dsb sy" ::: "memory");
}
