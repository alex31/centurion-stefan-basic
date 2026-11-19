#include "hardware.h"
void setup(void);
void loop(void);
extern int stb_done;
int main(void) {
    setup();
    while(!stb_done)loop();
    bas_flush();
    return 0;
}
