#ifndef UTIL_H
#define UTIL_H
#include "CFG.h"
#include "gurobi_c.h"

void addBBToList(BBType*, BBListEntry**);
void freeBBList(BBListEntry**);

BBType* getBBListHead(BBListEntry*);
BBType* getBBListTail(BBListEntry*);

void dom_init();
void dom_free();

int isUdomV(BBType *u, BBType *v);
int isVpdomU(BBType *u, BBType *v);

int getMaxPredN(BBType* node);
int getNumSuccessors(BBType* node);
int getNCall(BBType *node);
int isNodeRT(BBType *node);

void printTerminalNodes();

BBType* getFarthestLoopTail(BBType *node);

int findTotalCodeSize();
int findMaxFuncSize();

void quit(int error, GRBenv *env);
#endif
