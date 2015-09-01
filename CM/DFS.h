#ifndef DFS_H
#define DFS_H

#include "CFG.h"

#define FASTSTACK 1

#ifdef FASTSTACK
typedef struct _fastStack
{
    int stackPt;
    int maxSize;
    BBType **BBList;
} fastStack;

BBType* popBB(fastStack* stack);
void pushBB(BBType* BB, fastStack* stack);

#define INITSTACK(n) \
fastStack stack;\
stack.maxSize = (n);\
stack.BBList = (BBType**)malloc(sizeof(BBType*)*(n));\
stack.stackPt = 0;

#define FREESTACK \
free(stack.BBList);

#else
BBType* popBB(BBListEntry** stack);
void pushBB(BBType* BB, BBListEntry** stack);

#define INITSTACK(n) \
BBListEntry* stack = NULL;

#define FREESTACK \
free(stack);

#endif

#endif
