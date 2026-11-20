#include <stdint.h>

extern uint16_t mmu_present(void);

/* This executable fits the ordinary CENTOS map before BASIC is loaded. */
int main(void)
{
    return mmu_present() == 0x1000u ? 0 : 250;
}
