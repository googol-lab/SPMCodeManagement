#ifndef CM_H
#define CM_H

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <limits.h>
#include <sys/time.h>
#include "CFG.h"

extern BBType* rootNode;
extern int nFunc;
extern BBType** nodes;
extern funcType* functions;

extern int SPMSIZE;

extern int dbgFlag;

#endif
