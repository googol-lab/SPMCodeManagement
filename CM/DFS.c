#include <stdio.h>
#include <stdlib.h>
#include "DFS.h"

#ifdef FASTSTACK
BBType* popBB(fastStack* stack)
{
    if (stack->stackPt == 0)
        return NULL;
    
    BBType* BB = stack->BBList[stack->stackPt-1];
    stack->BBList[stack->stackPt-1] = NULL;
    stack->stackPt--;
    
    return BB;
}

void pushBB(BBType* BB, fastStack* stack)
{
    if (stack->stackPt == stack->maxSize)
    {
        printf("@pushBB: stack size overage (size:%d)\n", stack->maxSize);
        exit(1);
    }
    
    stack->BBList[stack->stackPt++] = BB;
}
#else
BBType* popBB(BBListEntry** stack)
{
    if (*stack == NULL)
        return NULL;
    
    BBType* BB = (*stack)->BB;
    *stack = (*stack)->next;
    
    return BB;
}

void pushBB(BBType* BB, BBListEntry** stack)
{
    BBListEntry* newEntry = (BBListEntry*)malloc(sizeof(BBListEntry));
    newEntry->BB = BB;
    nnewEntry->next = *stack;
    *stack = newEntry;
}
#endif

