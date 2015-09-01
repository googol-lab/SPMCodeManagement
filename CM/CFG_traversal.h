#ifndef CFG_TRAVERSAL_H
#define CFG_TRAVERSAL_H

#include "CM.h"

void initVisited();
void initVisitedInRange(BBType *from, BBType *to);

void initUnreachable();
void initEC();
void initReturns();
void initIS();
void initVisitedForLoop(BBType* from, BBType* to);

void findIS();

int TPOvisit(BBType* node, BBType** nodeList, int curIdx);

int isDirectlyReachable(BBType* from, BBType* to, int bStart);
void initBFirst();

int* get_rList(int f, int* rList);
void findInitialLoadingPoints();

#endif
