#include "hardware.h"
void setup(void);
void loop(void);
extern int stb_done;
#ifdef STBASIC_COMMANDS
void ib_reset(void);
#endif
int main(void) {
    setup();
#ifdef STBASIC_COMMANDS
    ib_reset();
#endif
    while(!stb_done)loop();
    bas_flush();
    return 0;
}
