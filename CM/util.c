#include "CM.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>

#include "gurobi_c.h"

int **dom = NULL;
int **pdom = NULL;

extern funcType* functions;

void addBBToList(BBType* BB, BBListEntry** list)
{
    if (*list) {
        BBListEntry* entry = *list;
        while (entry->next)
            entry = entry->next;
        
        entry->next = (BBListEntry*)malloc(sizeof(BBListEntry));
        entry->next->BB = BB;
        entry->next->next = NULL;
    }
    else {
        *list = (BBListEntry*)malloc(sizeof(BBListEntry));
        (*list)->BB = BB;
        (*list)->next = NULL;
    }
}

void freeBBList(BBListEntry** list)
{
    if (*list != NULL) {
        BBListEntry* entry = *list;
        while (entry->next) {
            BBListEntry* next = entry->next;
            free(entry);
            entry = next;
        }

        free(entry);

        *list = NULL;
    }
}

BBType* getBBListHead(BBListEntry* list)
{
    if (list != NULL)
        return list->BB;
    else
        return NULL;
}

BBType* getBBListTail(BBListEntry* list)
{
    if (list != NULL) {
        BBListEntry* entry = list;
        while (entry->next)
            entry = entry->next;

        return entry->BB;
    }
    else
        return NULL;
}

int isUdomV(BBType *u, BBType *v)
{
    if (dom[u->ID][v->ID] != -1)
        return dom[u->ID][v->ID];
/*
    static int* bVisited = NULL;
    if (bStart == 1)
    {
        if (bVisited == NULL)
            bVisited = (int*)calloc(nMaxNode, sizeof(int));
        int i;
        for (i = 0; i < nMaxNode; i++)
            bVisited[i] = 0;
    }
*/
    if (u == v)
    {
        dom[u->ID][v->ID] = 1;
        dom[v->ID][u->ID] = 1;
        return 1;
    }

/*
    if (bVisited[v->ID] == 1)
        return 0;
    bVisited[v->ID] = 1;
*/
    if (v->predList == NULL && v->bUnreachable == 0)
        return 0;

    BBListEntry* predList = v->predList;
    while (predList) {
        BBType* prevNode = predList->BB;
        if (isUdomV(u, prevNode) == 0) {
            dom[u->ID][v->ID] = 0;
            return 0;
        }

        predList = predList->next;
    }

    // After back edge is removed...
    if (v->bLoopHead) {
        BBListEntry* loopTailEntry = v->loopTailList;
        while (loopTailEntry) {
            BBType* loopTail = loopTailEntry->BB;
            if (isUdomV(u, loopTail) == 0) {
                dom[u->ID][v->ID] = 0;
                return 0;
            }

            loopTailEntry = loopTailEntry->next;
        }
    }

/*
    if (bStart == 1)
    {
        int i;
        for (i = 0; i < nMaxNode; i++)
            bVisited[i] = 0;
    }
*/
    dom[u->ID][v->ID] = 1;
    return 1;
}


int isVpdomU(BBType *u, BBType *v)
{
    if (pdom[v->ID][u->ID] != -1)
        return pdom[v->ID][u->ID];
/*
    static int* bVisited = NULL;
    if (bStart == 1)
    {
        if (bVisited == NULL)
            bVisited = (int*)calloc(nMaxNode, sizeof(int));
        int i;
        for (i = 0; i < nMaxNode; i++)
            bVisited[i] = 0;
    }
*/
    if (u == v)
    {
        pdom[v->ID][u->ID] = 1;
        pdom[u->ID][v->ID] = 1;
        return 1;
    }
/*
    if (bVisited[u->ID] == 1)
        return 0;
    bVisited[u->ID] = 1;
*/

    if (u->succList == NULL)
        return 0;

    BBListEntry* succList = u->succList;
    while (succList)
    {
        BBType* succNode = succList->BB;
        if (isVpdomU(succNode, v) == 0)
        {
            pdom[v->ID][u->ID] = 0;
            return 0;
        }

        succList = succList->next;
    }

/*
    if (bStart == 1)
    {
        int i;
        for (i = 0; i < nMaxNode; i++)
            bVisited[i] = 0;
    }
*/

    pdom[v->ID][u->ID] = 1;
    return 1;
}

void dom_init()
{
    int i, j;
    if (dom != NULL) {
        for (i = 0; i < nNode; i++)
            free(dom[i]);
        free(dom);
    }
    if (pdom != NULL) {
        for (i = 0; i < nNode; i++)
            free(pdom[i]);
        free(pdom);
    }

    dom = (int**)malloc(sizeof(int*) * nNode);
    pdom = (int**)malloc(sizeof(int*) * nNode);

    for (i = 0; i < nNode; i++) {
        dom[i] = (int*)malloc(sizeof(int) * nNode);
        pdom[i] = (int*)malloc(sizeof(int) * nNode);
        for (j = 0; j < nNode; j++) {
            dom[i][j] = -1;
            pdom[i][j] = -1;
        }
    }
}

void dom_free()
{
    int i, j;
    if (dom != NULL) {
        for (i = 0; i < nNode; i++)
            free(dom[i]);
        free(dom);
    }
    if (pdom != NULL) {
        for (i = 0; i < nNode; i++)
            free(pdom[i]);
        free(pdom);
    }

    dom = pdom = NULL;
}

int getMaxPredN(BBType* node) 
{
    int maxN = 0;
    BBListEntry* predEntry = node->predList;
    while (predEntry) {
        if (predEntry->BB->N > maxN)
            maxN = predEntry->BB->N;
        predEntry = predEntry->next;
    }

    return maxN;
}

int getNumSuccessors(BBType* node)
{
    int nSucc = 0;

    if (node == NULL)
        return -1;

    if (node->succList) {
        nSucc++;
        if (node->succList->next)
            nSucc++;
    }

    return nSucc;
}

int getNCall(BBType* node)
{
    if (node == rootNode)
        return 1;

    if (node->CS == 0)
        return -1;

    int maxN = 0;
    BBListEntry* predEntry = node->predList;
    while (predEntry) {
        if (predEntry->BB->EC != node->EC) {
            if (predEntry->BB->N > maxN)
                maxN = predEntry->BB->N;
        }
        predEntry = predEntry->next;
    }

    return maxN;
}

int isNodeRT(BBType* node)
{
    //return node->predList->BB->RT;
    if (node->CS == 0)
        return -1;

    BBListEntry* predEntry = node->predList;
    while (predEntry) {
        if (predEntry->BB->EC != node->EC) {
            if (predEntry->BB->RT == 1)
                return 1;
        }
        predEntry = predEntry->next;
    }

    return 0;
}

void printTerminalNodes()
{
    int bIdx;
    for (bIdx = 0; bIdx < nNode; bIdx++) {
        if (nodes[bIdx]->bLiteralPool == 0 && nodes[bIdx]->succList == NULL)
            printf("Node %d (EC: %d, N: %d, CS: %d) is a terminal\n", nodes[bIdx]->ID, nodes[bIdx]->EC, nodes[bIdx]->N, nodes[bIdx]->CS);
    }
}

BBType* getFarthestLoopTail(BBType *node)
{
    BBType *loopTail = NULL;
    BBListEntry *loopTailEntry = node->loopTailList;
    while (loopTailEntry) {
        if (loopTail == NULL)
            loopTail = loopTailEntry->BB;
        else {
            if (loopTail->addr < loopTailEntry->BB->addr)
                loopTail = loopTailEntry->BB;
        }

        loopTailEntry = loopTailEntry->next;
    }

    return loopTail;
}

int findTotalCodeSize()
{
    int size = 0;
    int i;
    for (i = 0; i < nFunc; i++)
        size += functions[i].size;
    
    return size;
}


int findMaxFuncSize()
{
    int maxFuncSize = 0;
    int i;
    for (i = 0; i < nFunc; i++) {
        if (functions[i].size > maxFuncSize)
            maxFuncSize = functions[i].size;
    }
    
    return maxFuncSize;
}

void quit(int error, GRBenv *env)
{
    if (error)
    {
        printf("ERROR: %s\n", GRBgeterrormsg(env));
        exit(1);
    }
}


