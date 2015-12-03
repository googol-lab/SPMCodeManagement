#ifndef UTIL_H
#define UTIL_H
#include "CFG.h"
#include "gurobi_c.h"

void addBBToList(BBType*, BBListEntry**);
void freeBBList(BBListEntry**);
BBListEntry* duplicateBBList(BBListEntry*);

BBType* getBBListHead(BBListEntry*);
BBType* getBBListTail(BBListEntry*);

BBListEntry* findEntryInNodeList(BBType* node, BBListEntry* nodeList);
int isBBInNodeList(BBType* node, BBListEntry* nodeList);

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

void takeOutLiteralPools();

void quit(int error, GRBenv *env);

void backupCFG(int *nNodeBak, int *nFuncBak, BBType ***nodesBak, funcType **functionsBak);
void freeCFG(int _nNode, int _nFunc, BBType ***_nodes, funcType **_functions);
void restoreCFG(int nNodeBak, int nFuncBak, BBType ***nodesBak, funcType **functionsBak);
#endif
