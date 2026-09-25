#include <stdio.h>
// CARD -> host filesystem stub
// Map to %APPDATA%/StairfaxTemperatures/save.bin
int CARDProbeEx(int c, int* m, int* s){(void)c;(void)m;(void)s; return 0;}
int CARDMount(int c, void* w, int cb){(void)c;(void)w;(void)cb; return 0;}
