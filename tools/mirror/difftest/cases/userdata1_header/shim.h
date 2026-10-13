/* Harness support only: GameObject, ShopInterface, SHOP_INTERFACE and the
 * placement come from the real headers the snippet includes. Objects are
 * identified by anim.romDefNo. */
#ifndef USERDATA1_HEADER_SHIM_H
#define USERDATA1_HEADER_SHIM_H
#include "difftest.h"
#include <stdio.h>
#include <string.h>
static char gLog[512];
#define LOG(...) snprintf(gLog + strlen(gLog), sizeof gLog - strlen(gLog), __VA_ARGS__)
#endif
