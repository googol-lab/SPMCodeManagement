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

#include "util.h"
#include "split_wrapper.h"

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

    int nNodeBak_prev;
    int nFuncBak_prev;
    BBType** nodesBak_prev = NULL;
    funcType* functionsBak_prev = NULL;

    backupCFG(&nNodeBak_prev, &nFuncBak_prev, &nodesBak_prev, &functionsBak_prev);

    int startIdx;
    int origNFunc = nFunc; // since nFunc may increase in the following loop

    // does the largest function fit in the SPM?
    long long int prevWCET = LLONG_MAX;
    long long int newWCET;
    if (functions[sortedFIdx[0]].size <= SPMSIZE) {
        // run the heuristic first to get the initial WCET before function splitting
        if(runHeuristic(SPMSIZE) != -1)
            prevWCET = wcet_analysis_fixed_input(SILENT);

        startIdx = 0;
    }
    else {
        backupCFG(&nNodeBak_prev, &nFuncBak_prev, &nodesBak_prev, &functionsBak_prev);
        // if not, split all functions that are larger than the SPM size
        for (i = 0; i < origNFunc; i++) {
            if (functions[sortedFIdx[i]].size <= SPMSIZE)
                break;

            if (split(sortedFIdx[i]) == -1) {
                printf("splitting function %d failed\n", i);
                goto FS_EXIT;
            }
        }
        
        initIS();
        findIS();
        findInitialLoadingPoints();

        if (runHeuristic(SPMSIZE) == -1) {
            printf("Cannot proceed due to the SPMSIZE restriction\n");
            exit(1);
        }

        newWCET = wcet_analysis_fixed_input(SILENT);
        if (newWCET > prevWCET) {
            printf("WCET did not decrease. Try a larger SPM size\n");
            exit(1);
        }
        else
            prevWCET = newWCET;

        backupCFG(&nNodeBak_prev, &nFuncBak_prev, &nodesBak_prev, &functionsBak_prev);

        startIdx = i;
    }

    for (i = startIdx; i < origNFunc; i++) {
        backupCFG(&nNodeBak_prev, &nFuncBak_prev, &nodesBak_prev, &functionsBak_prev);

        if (split(sortedFIdx[i]) == -1) {
            printf("splitting function %d failed\n", i);
            continue;
        }

        initIS();
        findIS();
        findInitialLoadingPoints();

        if (runHeuristic(SPMSIZE) == -1) {
            printf("Cannot proceed due to the SPMSIZE restriction\n");
            exit(1);
        }

        long long int newWCET = wcet_analysis_fixed_input(SILENT);
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
        wcet_analysis_fixed_input(VERBOSE);
    printf("\n\n-----Heuristic done---------------------------------------\n\n");
    //cm_region_optimal(NULL);
    //printf("\n\n-----Region-based done------------------------------------\n\n");
    //cm_rf_optimal(NULL);
    //printf("\n\n-----Region-free done-------------------------------------\n\n");

FS_EXIT:
    free(sortedFIdx);
    freeCFG(nNodeBak_prev, nFuncBak_prev, &nodesBak_prev, &functionsBak_prev);
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
