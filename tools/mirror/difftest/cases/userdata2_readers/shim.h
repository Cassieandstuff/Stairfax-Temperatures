/* Harness support only: GameObject and the object states come from the real
 * headers the snippet includes. Objects are identified by anim.romDefNo. */
#ifndef USERDATA2_SHIM_H
#define USERDATA2_SHIM_H
#include "difftest.h"
#include <stdio.h>
#include <string.h>
static char gLog[512];
#define LOG(...) snprintf(gLog + strlen(gLog), sizeof gLog - strlen(gLog), __VA_ARGS__)
#endif
