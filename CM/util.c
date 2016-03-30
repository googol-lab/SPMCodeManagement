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

BBListEntry* duplicateBBList(BBListEntry* list)
{
    BBListEntry* newList = NULL;
    BBListEntry* entry = list;
    while (entry) {
        addBBToList(entry->BB, &newList);
        entry = entry->next;
    }

    return newList;
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


BBListEntry* findEntryInNodeList(BBType* node, BBListEntry* nodeList)
{
    BBListEntry* entry = nodeList;
    while (entry) {
        if (entry->BB == node)
            return entry;
        entry = entry->next;
    }

    return NULL;
}

int isBBInNodeList(BBType* node, BBListEntry* nodeList)
{
    BBListEntry* entry = nodeList;
    while (entry) {
        if (entry->BB == node) 
            return 1;

        entry = entry->next;
    } 

    return 0;
}

int isUdomV(BBType *u, BBType *v)
{
    if (dom[u->ID][v->ID] != -1)
        return dom[u->ID][v->ID];

    if (u == v)
    {
        dom[u->ID][v->ID] = 1;
        dom[v->ID][u->ID] = 1;
        return 1;
    }

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

/*
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
*/
    dom[u->ID][v->ID] = 1;
    return 1;
}


int isVpdomU(BBType *u, BBType *v)
{
    if (pdom[v->ID][u->ID] != -1)
        return pdom[v->ID][u->ID];

    if (u == v)
    {
        pdom[v->ID][u->ID] = 1;
        pdom[u->ID][v->ID] = 1;
        return 1;
    }

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

int findOriginalCodeSize()
{
    int size = 0;
    int i, n;
    for (i = 0; i < nFunc; i++) {
        if (functions[i].nOccurrence < 1) {
            size += functions[i].size;
            continue;
        }

        int sourceIdx = functions[i].entryPoints[0]->ID;
        int sinkIdx = functions[i].exitPoints[0]->ID;

        for (n = sourceIdx; n <= sinkIdx; n++) {
            size += (nodes[n]->S * 4);
        }
    }
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

void takeOutLiteralPools()
{
    int n;
    for (n = 0; n < nNode; n++) {
        if (nodes[n]->bLiteralPool)
            functions[nodes[n]->EC].size -= nodes[n]->S * 4;
    }
}

void quit(int error, GRBenv *env)
{
    if (error)
    {
        printf("ERROR: %s\n", GRBgeterrormsg(env));
        exit(1);
    }
}

void freeCFG(int _nNode, int _nFunc, BBType ***_nodes, funcType **_functions)
{
    int bIdx, fIdx;
    if (*_nodes) {
        for (bIdx = 0; bIdx < _nNode; bIdx++) {
            if ((*_nodes)[bIdx]->name)
                free((*_nodes)[bIdx]->name);

            if ((*_nodes)[bIdx]->preLoopList)
                freeBBList(&((*_nodes)[bIdx]->preLoopList));

            if ((*_nodes)[bIdx]->loopTailList)
                freeBBList(&((*_nodes)[bIdx]->loopTailList));

            if ((*_nodes)[bIdx]->predList)
                freeBBList(&((*_nodes)[bIdx]->predList));
            if ((*_nodes)[bIdx]->succList)
                freeBBList(&((*_nodes)[bIdx]->succList));

            if ((*_nodes)[bIdx]->IS) {
                for (fIdx = 0; fIdx < _nFunc; fIdx++)
                    free((*_nodes)[bIdx]->IS[fIdx]);
                free((*_nodes)[bIdx]->IS);
            }
            free((*_nodes)[bIdx]);
        }
        free((*_nodes));
        *_nodes = NULL;
    }

    if (*_functions) {
        for (fIdx = 0; fIdx < _nFunc; fIdx++) {
            if ((*_functions)[fIdx].nAddrRange)
                free((*_functions)[fIdx].addrRange);

            if ((*_functions)[fIdx].childrenIDs)
                free((*_functions)[fIdx].childrenIDs);

            if ((*_functions)[fIdx].entryPoints)
                free((*_functions)[fIdx].entryPoints);
            if ((*_functions)[fIdx].exitPoints)
                free((*_functions)[fIdx].exitPoints);
        }
        free((*_functions));
        *_functions = NULL;
    }
}

void backupCFG(int *nNodeBak, int *nFuncBak, BBType ***nodesBak, funcType **functionsBak)
{
    freeCFG(*nNodeBak, *nFuncBak, nodesBak, functionsBak);

    int bIdx, fIdx, lIdx;

    *nNodeBak = nNode;
    *nodesBak = (BBType**)malloc(sizeof(BBType*) * *nNodeBak);
    for (bIdx = 0; bIdx < *nNodeBak; bIdx++)
        (*nodesBak)[bIdx] = (BBType*)malloc(sizeof(BBType));

    for (bIdx = 0; bIdx < *nNodeBak; bIdx++) {
        memcpy((*nodesBak)[bIdx], nodes[bIdx], sizeof(BBType));

        if (nodes[bIdx]->callee)
            (*nodesBak)[bIdx]->callee = (*nodesBak)[nodes[bIdx]->callee->ID];

        (*nodesBak)[bIdx]->loopTailList = NULL;
        if (nodes[bIdx]->loopTailList) {
            BBListEntry *loopTailEntry = nodes[bIdx]->loopTailList;
            while (loopTailEntry) {
                addBBToList((*nodesBak)[loopTailEntry->BB->ID], &((*nodesBak)[bIdx]->loopTailList));
                loopTailEntry = loopTailEntry->next;
            }
        }
        if (nodes[bIdx]->loopHead)
            (*nodesBak)[bIdx]->loopHead = (*nodesBak)[nodes[bIdx]->loopHead->ID];

        (*nodesBak)[bIdx]->preLoopList = NULL;
        BBListEntry* preLoopEntry = nodes[bIdx]->preLoopList;
        while (preLoopEntry) {
            addBBToList((*nodesBak)[preLoopEntry->BB->ID], &((*nodesBak)[bIdx]->preLoopList));
            preLoopEntry = preLoopEntry->next;
        }

        (*nodesBak)[bIdx]->predList = NULL;
        BBListEntry* predEntry = nodes[bIdx]->predList;
        while (predEntry) {
            addBBToList((*nodesBak)[predEntry->BB->ID], &((*nodesBak)[bIdx]->predList));
            predEntry = predEntry->next;
        }

        (*nodesBak)[bIdx]->succList = NULL;
        BBListEntry* succEntry = nodes[bIdx]->succList;
        while (succEntry) {
            addBBToList((*nodesBak)[succEntry->BB->ID], &((*nodesBak)[bIdx]->succList));
            succEntry = succEntry->next;
        }

        if (nodes[bIdx]->IS != NULL) {
            (*nodesBak)[bIdx]->IS = (int**)malloc(sizeof(int*) * nFunc);
            for (fIdx = 0; fIdx < nFunc; fIdx++) {
                (*nodesBak)[bIdx]->IS[fIdx] = (int*)malloc(sizeof(int) * nFunc);
                for (lIdx = 0; lIdx < nFunc; lIdx++) {
                    (*nodesBak)[bIdx]->IS[fIdx][lIdx] = nodes[bIdx]->IS[fIdx][lIdx];
                }
            }
        }
        else
            (*nodesBak)[bIdx]->IS = NULL;
    }

    *nFuncBak = nFunc;
    *functionsBak = (funcType*)malloc(sizeof(funcType) * nFunc);
    for (fIdx = 0; fIdx < nFunc; fIdx++) {
        memcpy(&((*functionsBak)[fIdx]), &(functions[fIdx]), sizeof(funcType));

        if (functions[fIdx].nAddrRange > 0) {
            (*functionsBak)[fIdx].addrRange = (addrRangeType*)malloc(sizeof(addrRangeType) * functions[fIdx].nAddrRange);
            int rIdx;
            for (rIdx = 0; rIdx < functions[fIdx].nAddrRange; rIdx++) {
                (*functionsBak)[fIdx].addrRange[rIdx].startAddr = functions[fIdx].addrRange[rIdx].startAddr;
                (*functionsBak)[fIdx].addrRange[rIdx].size = functions[fIdx].addrRange[rIdx].size;
            }
        }

        if (functions[fIdx].nChildren > 0) {
            (*functionsBak)[fIdx].childrenIDs = (int*)malloc(sizeof(int) * functions[fIdx].nChildren);
            int cIdx;
            for (cIdx = 0; cIdx < functions[fIdx].nChildren; cIdx++) {
                (*functionsBak)[fIdx].childrenIDs[cIdx] = functions[fIdx].childrenIDs[cIdx];
            }
        }

        if (functions[fIdx].entryPoints) {
            (*functionsBak)[fIdx].entryPoints = (BBType**)malloc(sizeof(BBType*) * functions[fIdx].nOccurrence);
            int pIdx;
            for (pIdx = 0; pIdx < functions[fIdx].nOccurrence; pIdx++)
                (*functionsBak)[fIdx].entryPoints[pIdx] = (*nodesBak)[functions[fIdx].entryPoints[pIdx]->ID]; 
        }
        if (functions[fIdx].exitPoints) {
            (*functionsBak)[fIdx].exitPoints = (BBType**)malloc(sizeof(BBType*) * functions[fIdx].nOccurrence);
            int pIdx;
            for (pIdx = 0; pIdx < functions[fIdx].nOccurrence; pIdx++)
                (*functionsBak)[fIdx].exitPoints[pIdx] = (*nodesBak)[functions[fIdx].exitPoints[pIdx]->ID];
        }
    }
}

void restoreCFG(int nNodeBak, int nFuncBak, BBType ***nodesBak, funcType **functionsBak)
{
    freeCFG(nNode, nFunc, &nodes, &functions);

    nNode = nNodeBak;
    nFunc = nFuncBak;
    nodes = *nodesBak;
    functions = *functionsBak;
    rootNode = nodes[0];

    *nodesBak = NULL;
    *functionsBak = NULL;
}

