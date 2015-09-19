#include "CM.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "GCCFG.h"
#include "loop.h"
#include "CM_heuristic.h"
#include "split.h"
#include "util.h"

#include "y.tab.h"
#include "gurobi_c.h"

#include "cache_analysis.h"
#include "DMA.h"

#include "CM_region_based.h"
#include "CM_region_free.h"

#include "split_wrapper.h"

void addNodeToList(BBType* node, BBListEntry** list)
{
    if (node == NULL) {
        printf("@addNodeToList: node is NULL\n");
        return;
    }

    if (*list == NULL) {
        (*list) = (BBListEntry*)malloc(sizeof(BBListEntry));
        (*list)->BB = node;
        (*list)->next = NULL;
    }
    else {
        BBListEntry* entry = *list;
        while (entry->next) {
            entry = entry->next;
        }
        entry->next = (BBListEntry*)malloc(sizeof(BBListEntry));
        entry->next->BB = node;
        entry->next->next = NULL;
    }
}

void backupCFG(int *nNodeBak, int *nFuncBak, BBType ***nodesBak, funcType **functionsBak)
{
    int bIdx, fIdx, lIdx;

    if (*nodesBak != NULL) {
        for (bIdx = 0; bIdx < *nNodeBak; bIdx++) {
            if ((*nodesBak)[bIdx]->name) {
                free((*nodesBak)[bIdx]->name);
                (*nodesBak)[bIdx]->name = NULL;
            }

            if ((*nodesBak)[bIdx]->preLoopList) {
                free((*nodesBak)[bIdx]->preLoopList);
                (*nodesBak)[bIdx]->preLoopList = NULL;
            }

            if ((*nodesBak)[bIdx]->loopTailList) {
                free((*nodesBak)[bIdx]->loopTailList);
                (*nodesBak)[bIdx]->loopTailList = NULL;
            }

            if ((*nodesBak)[bIdx]->predList) {
                free((*nodesBak)[bIdx]->predList);
                (*nodesBak)[bIdx]->predList = NULL;
            }
            if ((*nodesBak)[bIdx]->succList) {
                free((*nodesBak)[bIdx]->succList);
                (*nodesBak)[bIdx]->succList = NULL;
            }

            if ((*nodesBak)[bIdx]->IS) {
                for (fIdx = 0; fIdx < *nFuncBak; fIdx++) {
                    free((*nodesBak)[bIdx]->IS[fIdx]);
                    (*nodesBak)[bIdx]->IS[fIdx] = NULL;
                }
                free((*nodesBak)[bIdx]->IS);
                (*nodesBak)[bIdx]->IS = NULL;
            }
            free((*nodesBak)[bIdx]);
            (*nodesBak)[bIdx] = NULL;
        }
        free(*nodesBak);
        *nodesBak = NULL;
    }

    if (*functionsBak != NULL) {
        for (fIdx = 0; fIdx < *nFuncBak; fIdx++) {
            if ((*functionsBak)[fIdx].addrRange) {
                free((*functionsBak)[fIdx].addrRange);
                (*functionsBak)[fIdx].addrRange = NULL;
            }

            if ((*functionsBak)[fIdx].childrenIDs) {
                free((*functionsBak)[fIdx].childrenIDs);
                (*functionsBak)[fIdx].childrenIDs = NULL;
            }

            if ((*functionsBak)[fIdx].entryPoints) {
                free((*functionsBak)[fIdx].entryPoints);
                (*functionsBak)[fIdx].entryPoints = NULL;
            }
            if ((*functionsBak)[fIdx].exitPoints) {
                free((*functionsBak)[fIdx].exitPoints);
                (*functionsBak)[fIdx].exitPoints = NULL;
            }
        }
        free(*functionsBak);
        *functionsBak = NULL;
    }

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
            addNodeToList((*nodesBak)[preLoopEntry->BB->ID], &((*nodesBak)[bIdx]->preLoopList));
            preLoopEntry = preLoopEntry->next;
        }

        (*nodesBak)[bIdx]->predList = NULL;
        BBListEntry* predEntry = nodes[bIdx]->predList;
        while (predEntry) {
            addNodeToList((*nodesBak)[predEntry->BB->ID], &((*nodesBak)[bIdx]->predList));
            predEntry = predEntry->next;
        }

        (*nodesBak)[bIdx]->succList = NULL;
        BBListEntry* succEntry = nodes[bIdx]->succList;
        while (succEntry) {
            addNodeToList((*nodesBak)[succEntry->BB->ID], &((*nodesBak)[bIdx]->succList));
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
    int bIdx, fIdx;
    for (bIdx = 0; bIdx < nNode; bIdx++) {
        if (nodes[bIdx]->name)
            free(nodes[bIdx]->name);

        if (nodes[bIdx]->preLoopList)
            free(nodes[bIdx]->preLoopList);

        if (nodes[bIdx]->loopTailList)
            free(nodes[bIdx]->loopTailList);

        if (nodes[bIdx]->predList)
            free(nodes[bIdx]->predList);
        if (nodes[bIdx]->succList)
            free(nodes[bIdx]->succList);

        if (nodes[bIdx]->IS) {
            for (fIdx = 0; fIdx < nFunc; fIdx++) {
                free(nodes[bIdx]->IS[fIdx]);
            }
            free(nodes[bIdx]->IS);
        }
        free(nodes[bIdx]);
    }
    free(nodes);


    for (fIdx = 0; fIdx < nFunc; fIdx++) {
        if (functions[fIdx].nAddrRange)
            free(functions[fIdx].addrRange);

        if (functions[fIdx].childrenIDs)
            free(functions[fIdx].childrenIDs);

        if (functions[fIdx].entryPoints)
            free(functions[fIdx].entryPoints);
        if (functions[fIdx].exitPoints)
            free(functions[fIdx].exitPoints);
    }
    free(functions);

    nNode = nNodeBak;
    nFunc = nFuncBak;
    nodes = *nodesBak;
    functions = *functionsBak;
    rootNode = nodes[0];

    *nodesBak = NULL;
    *functionsBak = NULL;
}

void cm_fs()
{
    // sort functions in descending order of the size
    int *sortedFIdx = (int*)malloc(sizeof(int) * nFunc);
    int i, j, k;
    
    for (i = 0; i < nFunc; i++) {
        // fill in i-th item in sortedFIdx
        int maxCost = -1;
        int maxIdx = -1;

        // find max size function index that is not present in sortedFIdx[0 ... i]
        for (j = 0; j < nFunc; j++) {
            // look at j-th item in functions
            // check if j-th item is already filled in sortedFIdx as k-th item
            int bAlready = 0;
            for (k = 0; k < i; k++) {
                if (sortedFIdx[k] == j)
                    bAlready = 1;
            }
            if (bAlready)
                continue;

            if (maxCost < functions[j].size) {
                maxCost = functions[j].size;
                maxIdx = j;
            }
        }

        sortedFIdx[i] = maxIdx;
    }

    // does the largest function fit in the SPM?
    long long int prevWCET = LLONG_MAX;
    if (functions[sortedFIdx[0]].size <= SPMSIZE) {
        // run the heuristic first to get the initial WCET before function splitting
        if(runHeuristic(SPMSIZE) != -1)
            prevWCET = wcet_analysis_fixed_input();
    }

    int nNodeBak_prev;
    int nFuncBak_prev;
    BBType** nodesBak_prev = NULL;
    funcType* functionsBak_prev = NULL;

    int origNFunc = nFunc; // since nFunc may increase in the following loop
    for (i = 0; i < origNFunc; i++) {
        backupCFG(&nNodeBak_prev, &nFuncBak_prev, &nodesBak_prev, &functionsBak_prev);

        split(sortedFIdx[i]);

        initIS();
        findIS();
        findInitialLoadingPoints();

        if (runHeuristic(SPMSIZE) == -1) {
            printf("Cannot proceed due to the SPMSIZE restriction\n");
            exit(1);
        }

        long long int newWCET = wcet_analysis_fixed_input();
        if (newWCET > prevWCET) {
            printf("---------------------------\nroll back!\n---------------------------\n");
            restoreCFG(nNodeBak_prev, nFuncBak_prev, &nodesBak_prev, &functionsBak_prev);
        }
        else
            prevWCET = newWCET;
    }

    initIS();
    findIS();
    findInitialLoadingPoints();

    
    if (runHeuristic(SPMSIZE) != -1)
        wcet_analysis_fixed_input();
    printf("\n\n-----Heuristic done---------------------------------------\n\n");
    //cm_region_optimal(NULL);
    //printf("\n\n-----Region-based done------------------------------------\n\n");
    //cm_rf_optimal(NULL);
    //printf("\n\n-----Region-free done-------------------------------------\n\n");
}

#if 0
//enum CMmode mode, int nSplitStart, int nSplitEnd)
{
    long long int (*cm)(long long int*);
/
    if (mode == RegionBased)
        cm = &cm_region_optimal;
    else if (mode == RegionFree)
        cm = &cm_rf_optimal;
    else {
        printf("Unknown management mode.\n");
        return;
    }

    int f;
    int origNFunc = nFunc;
    for (f = 0; f < origNFunc; f++) {
        split(f, 2);
    }
    initIS();
    findIS();
    initBFirst();
    findInitialLoadingPoints();


    runHeuristic(SPMSIZE);
    wcet_analysis_fixed_input();
    cm_region_optimal(NULL);
    cm_rf_optimal(NULL);
    return;

    if (nSplitStart > nSplitEnd) {
        printf("The start value is smaller than the end value.\n");
        return;
    }

    if (nSplitEnd == 1) {
        cm(NULL);
        return;
    }

    //printf("# Split: %d\n", findMaxFuncSize()/(SPMSIZE/5));

    long long int WCET;
    long long int bestWCET;
    
    int f1, f2;

    long long int *fCost = (long long int*)malloc(sizeof(long long int) * nFunc);
    dbgFlag = 1;
    WCET = cm(fCost);
    //dbgFlag = 0;

    printf("-------------------------------------------------------------\n");
    printf("-------------------------------------------------------------\n");
    printf("-------------------------------------------------------------\n");

    int *sortedFList;
    if (WCET > 0) {
        // sort by fCost
        sortedFList = (int*)malloc(sizeof(int) * nFunc);
        for (f1 = 0; f1 < nFunc; f1++)
            sortedFList[f1] = f1;

        for (f1 = 0; f1 < nFunc-1; f1++) {
            for (f2 = f1+1; f2 < nFunc; f2++) {
                if (fCost[f2] > fCost[f1]) {
                    long long int tempC = fCost[f1];
                    fCost[f1] = fCost[f2];
                    fCost[f2] = tempC;
                    int tempF = sortedFList[f1];
                    sortedFList[f1] = sortedFList[f2];
                    sortedFList[f2] = tempF;
                }
            }
        }

        for (f1 = 0; f1 < nFunc; f1++)
            printf("function %d's cost: %lld\n", sortedFList[f1], fCost[f1]);
    }
    else {
        // sort by function size
        sortedFList = (int*)malloc(sizeof(int) * nFunc);
        for (f1 = 0; f1 < nFunc; f1++) {
            sortedFList[f1] = f1;
            fCost[f1] = functions[f1].size;
        }

        for (f1 = 0; f1 < nFunc-1; f1++) {
            for (f2 = f1+1; f2 < nFunc; f2++) {
                if (fCost[f2] > fCost[f1]) {
                    long long int tempC = fCost[f1];
                    fCost[f1] = fCost[f2];
                    fCost[f2] = tempC;
                    int tempF = sortedFList[f1];
                    sortedFList[f1] = sortedFList[f2];
                    sortedFList[f2] = tempF;
                }
            }
        }

        for (f1 = 0; f1 < nFunc; f1++)
            printf("function %d's size: %lld\n", sortedFList[f1], fCost[f1]);
    }

    int nNodeBak_best;
    int nFuncBak_best;
    BBType** nodesBak_best = NULL;
    funcType* functionsBak_best = NULL;

    int nNodeBak_intermediate;
    int nFuncBak_intermediate;
    BBType** nodesBak_intermediate = NULL;
    funcType* functionsBak_intermediate = NULL;

    int initNFunc = nFunc;
    backupCFG(&nNodeBak_best, &nFuncBak_best, &nodesBak_best, &functionsBak_best);

    int nSplit;
    if (WCET > 0) {
        bestWCET = WCET;

        for (nSplit = nSplitEnd; nSplit >= nSplitStart; nSplit--) {
            if (nSplit == 1)
                continue;

            for (f1 = 0; f1 < initNFunc; f1++) {
                if (fCost[f1] == 0)
                    break;
                // backup
                backupCFG(&nNodeBak_intermediate, &nFuncBak_intermediate, &nodesBak_intermediate, &functionsBak_intermediate);
                if (split(sortedFList[f1], nSplit) == 0) {
                    initIS();
                    findIS();
                    initBFirst();

                    WCET = cm(NULL);

                    if (WCET > bestWCET || WCET < 0) {
                        printf("----restoring!---\n");
                        restoreCFG(nNodeBak_intermediate, nFuncBak_intermediate, &nodesBak_intermediate, &functionsBak_intermediate);
                    }
                    else {
                        bestWCET = WCET;
                        backupCFG(&nNodeBak_best, &nFuncBak_best, &nodesBak_best, &functionsBak_best);
                    }
                }
            }
        }
    }
    else {
        bestWCET = LLONG_MAX;

        for (nSplit = nSplitEnd; nSplit >= nSplitStart; nSplit--) {
            if (nSplit == 1)
                continue;

            for (f1 = 0; f1 < initNFunc; f1++) {
                if (fCost[f1] >= SPMSIZE) {
                    if (split(sortedFList[f1], nSplit) == 0) {
                        initIS();
                        findIS();
                        initBFirst();
                    }
                }
                else
                    break;
            }

            WCET = cm(NULL);
            if (WCET == -2) {
                //printf("infeasible solution. moving on to next larger number of nSplits\n");
                //restoreCFG(nNodeBak_initial, nFuncBak_initial, &nodesBak_initial, &functionsBak_initial);
                //bestWCET = LLONG_MAX;
                //continue;
                break;
            }
            else if (WCET > 0 && WCET < bestWCET) {
            //else if (WCET > 0 && WCET < localBestWCET) {
                bestWCET = WCET;
                //localBestWCET = WCET;
                backupCFG(&nNodeBak_best, &nFuncBak_best, &nodesBak_best, &functionsBak_best);
                //backupCFG(&nNodeBak_localBest, &nFuncBak_localBest, &nodesBak_localBest, &functionsBak_localBest);
            }

            for (f1 = 0; f1 < initNFunc; f1++) {
                backupCFG(&nNodeBak_intermediate, &nFuncBak_intermediate, &nodesBak_intermediate, &functionsBak_intermediate);
                if (split(sortedFList[f1], nSplit) == 0) {
                    initIS();
                    findIS();
                    initBFirst();

                    WCET = cm(NULL);

                    if (WCET > bestWCET || WCET < 0) {
                        printf("----restoring!---\n");

                        restoreCFG(nNodeBak_intermediate, nFuncBak_intermediate, &nodesBak_intermediate, &functionsBak_intermediate);
                    }
                    else {
                        bestWCET = WCET;
                        backupCFG(&nNodeBak_best, &nFuncBak_best, &nodesBak_best, &functionsBak_best);
                    }
                }
            }
        }
    }

    printf("---------------\n");
    restoreCFG(nNodeBak_best, nFuncBak_best, &nodesBak_best, &functionsBak_best);
    dbgFlag = 1;
    cm(NULL);
    dbgFlag = 0;

    free(sortedFList);
    free(fCost);
}

#endif
