#include "CM.h"
#include "CFG_traversal.h"
#include "util.h"
#include "DMA.h"
#include "DFS.h"
#include "CM_bblevel.h"
#include "cache_analysis.h"

extern enum sizeMode {NOCACHE, NOSPM, C1S3, C3S1, C1S1, DOUBLE} SIZEMODE;
int SPMSIZE_IN_BBLEVEL;

// when 1, assume non-cached main memory accesses
//#define NO_CACHE_MODE 1

// number of basic blocking considered to be loaded in each iteration 
//#define NUM_BB_LOADED_PER_ITER 10
extern int NUM_BB_LOADED_PER_ITER;

// load range type
typedef struct _lrType {
    int startBBID;  // this should be the ID of bb, not addr, because addresses of bb's change during the execution of the algorithm
    int endBBID;
    struct _lrType *next;
} lrType;

// reload point type
typedef struct _rpType {
    BBType* loopHead;
    int lb;     // loop bound
    int remainingSPMsize;
    int nLR;    // # of load ranges
    lrType* listLR;
    struct _rpType *next;
} rpType;

BBType** listNodeTPSorted = NULL;
int tpTraversalStartIdx;;

// return the literal pool size
int lpSize;
int getLiteralPoolSize()
{
    int sz = 0;
    int i;
    for (i = 0; i < nNode; i++) {
        if (nodes[i]->bLiteralPool) 
            sz += nodes[i]->S;
    }

    return sz;
}

// Iterate through all loops and return the reload points (the loop headers of the loops that are selected to be considered for loading to the SPM) - It is not preheaders because a loop can have multiple preheaders. 

// Reload points should be the outer-most loop...
rpType* findReloadPoints()
{
    rpType* rpList = NULL;

    int i, j;
    for (i = 0; i < nNode; i++) {
        // found a loop
        if (nodes[i]->bLoopHead) {
            BBType* loopHead = nodes[i];
#if 0
            BBListEntry* loopTailEntry = loopHead->loopTailList;
            BBType* loopTail = loopHead->loopTailList->BB;
            while (loopTailEntry) {
                if (loopTail->ID < loopTailEntry->BB->ID)
                    loopTail = loopTailEntry->BB;

                loopTailEntry = loopTailEntry->next;
            }

            int WCET_offchip = 0, WCET_onchip = 0;

            int nLR = 0;
            lrType* ldRange = NULL;
            lrType* lastLDRange = NULL;
            long long int lastAddr;

            for (j = loopHead->ID; j <= loopTail->ID; j++) {
                WCET_offchip += (nodes[j]->S*4) * CACHE_MISS_LATENCY; 
                // post dominators of the loop header are the most frequently executed basic blocks
                // thus, these are to be loaded
                if (isVpdomU(loopHead, nodes[j])) {
                    WCET_onchip += (nodes[j]->S*4) * 1; // SPM access latency = 1

                    // find contiguous load ranges
                    if (nLR != 0) {
                        // if this basic block is in the contiguous address range with the previous one
                        if (lastAddr == nodes[j]->addr) {
                            // these can be loaded at the same time
                            lastLDRange->sz += nodes[j]->S*4;
                        }
                        else {
                            lrType* newLDRange = (lrType*)malloc(sizeof(lrType));
                            if (newLDRange == NULL) {
                                printf("mem alloc error in findLoadCandidates\n");
                                exit(1);
                            }
                            newLDRange->next = NULL;
                            nLR++;
                            lastLDRange->next = newLDRange;
                            lastLDRange = newLDRange;
                        }
                    }
                    else {
                        ldRange = (lrType*)malloc(sizeof(lrType));
                        if (ldRange == NULL) {
                            printf("mem alloc error in findLoadCandidates\n");
                            exit(1);
                        }
                        ldRange->next = NULL;
                        nLR++;
                        lastLDRange = ldRange;
                    }
                    lastLDRange->sz = nodes[j]->S * 4;
                    lastAddr = nodes[j]->addr + 4*nodes[j]->S;
                }
                else
                    WCET_onchip += (nodes[j]->S*4) * CACHE_MISS_LATENCY;
            }

            // calculate the DMA cost for loading all load ranges
            lrType* ldRangeEntry = ldRange;
            int dmaCost = 0;
            while (ldRangeEntry) {
                dmaCost += CdmaByBytes(ldRangeEntry->sz);
                ldRangeEntry = ldRangeEntry->next;
            }

            // for all loop pre-headers, add the loading cost 
            BBListEntry* loopPreHeader = loopTail->preLoopList;
            while (loopPreHeader) {
                WCET_onchip += loopPreHeader->BB->N * dmaCost;
                loopPreHeader = loopPreHeader->next;
            }

            // if there's benefit, add this loop to the load candidate list
            if (WCET_offchip > WCET_onchip) {
                rpType *newRP = (rpType*)malloc(sizeof(rpType));
                newRP->loopHead = loopHead;
                newRP->lb = loopHead->N;
                newRP->nLR = nLR;
                newRP->listLR = ldRange;
                newRP->next = NULL;

                if (rpList == NULL) {
                    rpList = newRP;
                }
                else {
                    rpType* rpListLast = rpList;
                    while (rpListLast->next)
                        rpListLast = rpListLast->next;

                    rpListLast->next = newRP;
                }
            }
#else
                rpType *newRP = (rpType*)malloc(sizeof(rpType));
                newRP->loopHead = loopHead;
                newRP->lb = loopHead->N;
                newRP->nLR = 0;
                newRP->listLR = NULL;
                newRP->next = NULL;

                if (rpList == NULL) {
                    rpList = newRP;
                }
                else {
                    rpType* rpListLast = rpList;
                    while (rpListLast->next)
                        rpListLast = rpListLast->next;

                    rpListLast->next = newRP;
                }
#endif
        }
    }

    return rpList;
}

void freeLoadRanges(rpType* listRP)
{
    rpType* rpEntry = listRP;
    while (rpEntry) {
        lrType* lrEntry = rpEntry->listLR;
        while (lrEntry) {
            lrType* next = lrEntry->next;
            free(lrEntry);
            lrEntry = next;
        }
        rpEntry->listLR = NULL;
        rpEntry->nLR = 0;

        rpEntry = rpEntry->next;
    }
}

void initReloadPoints(rpType* listRP)
{
    freeLoadRanges(listRP);
    rpType* rpEntry = listRP;
    while (rpEntry) {
        // the literal pool should be allocated in the code SPM, so its size should be considered here
        rpEntry->remainingSPMsize = SPMSIZE_IN_BBLEVEL/4 - lpSize;
        rpEntry = rpEntry->next;
    }
}

void freeReloadPoints(rpType** listRP)
{
    freeLoadRanges(*listRP);
    rpType* rpEntry = *listRP;
    while (rpEntry) {
        rpType* next = rpEntry->next;
        free(rpEntry);
        rpEntry = next;
    }
    *listRP = NULL;
}

// return 1 only if bb is in the loop
int isInLoop(BBType* loopHead, BBType* loopTail, BBType* bb)
{
    //initVisitedInRange(loopHead, loopTail);
    initVisited();
    
    BBType* node;

    INITSTACK(nNode)

    int bFound = 0;
    pushBB(loopHead, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;

        node->bVisited = 1;

        if (node == bb) {
            bFound = 1;
            break;
        }

        if (isBBInNodeList(node, loopHead->loopTailList) == 0) {
            BBListEntry* succEntry = node->succList;
            while (succEntry) {
                if (succEntry->BB->EC != loopHead->EC || (succEntry->BB->ID >= loopHead->ID && succEntry->BB->ID <= loopTail->ID))
                    pushBB(succEntry->BB, &stack);
                succEntry = succEntry->next;
            }
        }
    }
 
    FREESTACK
    return bFound;
}

// return the reload point for a given basic block 
//  hierarchically farthest 
rpType* getReloadPoint(rpType* listRP, BBType* bb)
{
    if (listRP == NULL || bb == NULL)
        return NULL;

    rpType* retRP = NULL;
    rpType* rpEntry = listRP;
    while (rpEntry) {
        BBType* loopHead = rpEntry->loopHead;
        BBListEntry* loopTailEntry = loopHead->loopTailList;
        BBType* loopTail = loopHead->loopTailList->BB;
        while (loopTailEntry) {
            if (loopTail->ID < loopTailEntry->BB->ID)
                loopTail = loopTailEntry->BB;

            loopTailEntry = loopTailEntry->next;
        }

        // check if bb lies in this reload point 
        if (loopHead->N <= bb->N && isInLoop(loopHead, loopTail, bb)) {
            // found!
            if (retRP != NULL) {
                // get outer loop
                if (loopHead->ID < retRP->loopHead->ID) {
                    retRP = rpEntry;
                }
            }
            else
                retRP = rpEntry;
        }

        rpEntry = rpEntry->next;
    }

    return retRP;
}

int addBBForLoading(rpType* listRP, BBType* bb)
{
    if (listRP == NULL || bb == NULL)
        return -1;

    rpType* rp = getReloadPoint(listRP, bb);
    if (rp == NULL)
        return -1;

    if (rp->remainingSPMsize < bb->S) 
        return 0;

    // find the corresponding LR
    int bFound = 0;
    lrType* lrEntry = rp->listLR;
    while (lrEntry) {
        // there shouldn't be any overlap
        if (lrEntry->startBBID == bb->ID+1) {
            //printf("reload point %d: load range starting from %d to %d is now from %d to %d\n", rp->loopHead->ID, lrEntry->startBBID, lrEntry->endBBID, bb->ID, lrEntry->endBBID);
            lrEntry->startBBID = bb->ID;
            bFound = 1;
            break;
        }
        else if (lrEntry->endBBID+1 == bb->ID) {
            //printf("reload point %d: load range starting from %d to %d is now from %d to %d\n", rp->loopHead->ID, lrEntry->startBBID, lrEntry->endBBID, lrEntry->startBBID, bb->ID);
            lrEntry->endBBID = bb->ID;
            bFound = 1;
            break;
        }

        lrEntry = lrEntry->next;
    }   

    if (bFound == 0) {
        lrType* newLR = (lrType*)malloc(sizeof(lrType));
        newLR->startBBID = bb->ID;
        newLR->endBBID = bb->ID;

        //printf("reload point %d: creating new load range from %d to %d\n", rp->loopHead->ID, newLR->startBBID, newLR->endBBID);

        // make a sorted list according to the start address
        if (rp->listLR) {
            if (rp->listLR->startBBID > newLR->startBBID) {
                newLR->next = rp->listLR;
                rp->listLR = newLR;
            }
            else {
                lrEntry = rp->listLR;
                while (lrEntry->next) {
                    if (lrEntry->next->startBBID > newLR->startBBID) {
                        newLR->next = lrEntry->next;
                        lrEntry->next = newLR;
                        break;
                    }
                    lrEntry = lrEntry->next;
                }
                if (lrEntry->next == NULL) {
                    lrEntry->next = newLR;
                    newLR->next = NULL;
                }
            }
        }
        else {
            rp->listLR = newLR;
            newLR->next = NULL;
        }
        
        rp->nLR++;

        // For all loop preheaders...
        BBListEntry* loopPreHeader = rp->loopHead->preLoopList;
        while (loopPreHeader) {
            BBType* reloadPoint = loopPreHeader->BB;

            // adjust node size for added reload instructions
            reloadPoint->S += NUM_INSTS_FOR_SIMPLE_DMA;

            // adjust addresses for added reload instructions
            int n;
            for (n = 0; n < nNode; n++) {
                if (nodes[n]->addr > reloadPoint->addr)
                    nodes[n]->addr += 4 * NUM_INSTS_FOR_SIMPLE_DMA;
            }

            loopPreHeader = loopPreHeader->next;
        }
    }
    else {
        // existing load ranges have been expanded. let's see if they are touching each other..
        // merge
        lrEntry = rp->listLR;
        while (lrEntry->next) {
            //printf("load range from %d to %d is seeing load range from %d to %d\n", lrEntry->startBBID, lrEntry->endBBID, lrEntry->next->startBBID, lrEntry->next->endBBID);
            if (lrEntry->endBBID+1 == lrEntry->next->startBBID) {
                //printf("reload point %d: load range from %d to %d and from %d to %d are merged and become one from %d to %d\n", rp->loopHead->ID, lrEntry->startBBID, lrEntry->endBBID, lrEntry->next->startBBID, lrEntry->next->endBBID, lrEntry->startBBID, lrEntry->next->endBBID);
                lrEntry->endBBID = lrEntry->next->endBBID;
                lrType* next = lrEntry->next->next;
                free(lrEntry->next);
                lrEntry->next = next;                
                rp->nLR--;

                // For all loop preheaders...
                BBListEntry* loopPreHeader = rp->loopHead->preLoopList;
                while (loopPreHeader) {
                    BBType* reloadPoint = loopPreHeader->BB;

                    // adjust node size for added reload instructions
                    reloadPoint->S -= NUM_INSTS_FOR_SIMPLE_DMA;

                    // adjust addresses for added reload instructions
                    int n;
                    for (n = 0; n < nNode; n++) {
                        if (nodes[n]->addr > reloadPoint->addr)
                            nodes[n]->addr -= 4 * NUM_INSTS_FOR_SIMPLE_DMA;
                    }

                    loopPreHeader = loopPreHeader->next;
                }
                break;
            }
            lrEntry = lrEntry->next;
        }
    }

    bb->bLoaded = 1;

    // consider BB's with the same addresses that should be marked as loaded by this
    // ex.
    // main { 
    //      // instance 1 
    //      f(); 
    //      while () { 
    //          // instance 2
    //          f(); 
    //      } 
    //      // instance 3
    //      f(); 
    //  }
    //  In this example, f() is called three times. In an inlined CFG, there will be three instances of the CFG of f().
    //  If we load a basic block in f() in instance 2 (in the while loop),
    //   the corresponding basic block with the same address that appear after the while loop in instance 3 should be 
    //   considered to have been loaded as well. Note that the one in instance 1 may not be loaded depending on the 
    //   location of the reload point..

    if (functions[bb->EC].nOccurrence > 1) {
        int o;
        for (o = 0; o < functions[bb->EC].nOccurrence; o++) {
            int entryPointID = functions[bb->EC].entryPoints[o]->ID;
            int exitPointID = functions[bb->EC].exitPoints[o]->ID;

            int n;
            for (n = entryPointID; n <= exitPointID; n++) {
                // find the corresponding node with the same address
                if (nodes[n] != bb && nodes[n]->addr == bb->addr) {
                    rpType* rpOther = getReloadPoint(listRP, nodes[n]);

                    if (rpOther == rp) {
                        printf("node %d is also loaded as node %d is being loaded\n", n, bb->ID);
                        nodes[n]->bLoaded = 1;
                    }
                }
            }
        }
    }

    rp->remainingSPMsize -= bb->S;

    return 1;
}

int rmBBForLoading(rpType* listRP, BBType* bb)
{
    if (listRP == NULL || bb == NULL)
        return -1;

    rpType* rp = getReloadPoint(listRP, bb);
    if (rp == NULL)
        return -1;

    // find the corresponding LR
    lrType* lrEntry = rp->listLR;
    lrType* prevEntry = NULL;
    while (lrEntry) {
        if (lrEntry->startBBID <= bb->ID && bb->ID <= lrEntry->endBBID) {
            break;
        }

        prevEntry = lrEntry;
        lrEntry = lrEntry->next;
    }   

    if (lrEntry) {
        if (lrEntry->startBBID == lrEntry->endBBID) {
            //printf("reload point %d: load range covers only one bb and it's being removed..\n", rp->loopHead->ID);
            // this load range used to cover only this bb.. then remove this load range
            if (prevEntry) {
                // this load range is in the middle of the load range list
                prevEntry->next = lrEntry->next;
                free(lrEntry);
            }
            // this load range is the head of the load range list
            else {
                rp->listLR = lrEntry->next;
                free(lrEntry);
            }
            rp->nLR--;

            // For all loop preheaders...
            BBListEntry* loopPreHeader = rp->loopHead->preLoopList;
            while (loopPreHeader) {
                BBType* reloadPoint = loopPreHeader->BB;

                // adjust node size for added reload instructions
                reloadPoint->S -= NUM_INSTS_FOR_SIMPLE_DMA;

                // adjust addresses for added reload instructions
                int n;
                for (n = 0; n < nNode; n++) {
                    if (nodes[n]->addr > reloadPoint->addr)
                        nodes[n]->addr -= 4 * NUM_INSTS_FOR_SIMPLE_DMA;
                }

                loopPreHeader = loopPreHeader->next;
            }
        }
        else {
            // there are more bb's that are loaded by this load range.. then just reduce the load size and adjust starting address
            if (lrEntry->startBBID == bb->ID) {
                // at the beginning
                //printf("reload point %d: load range from %d to %d is now from %d to %d..\n", rp->loopHead->ID, lrEntry->startBBID, lrEntry->endBBID, lrEntry->startBBID+1, lrEntry->endBBID);
                lrEntry->startBBID += 1;
            }
            else if (lrEntry->endBBID == bb->ID) {
                // at the end
                //printf("reload point %d: load range from %d to %d is now from %d to %d..\n", rp->loopHead->ID, lrEntry->startBBID, lrEntry->endBBID, lrEntry->startBBID, lrEntry->endBBID-1);
                lrEntry->endBBID -= 1;
            }
            else {
                // in the middle
                // split the load range into two
                // make the second one first -- create a new one 
                //printf("reload point %d: load range from %d to %d is now split into two....", rp->loopHead->ID, lrEntry->startBBID, lrEntry->endBBID);
                lrType* newLR = (lrType*)malloc(sizeof(lrType));
                newLR->startBBID = bb->ID+1;
                newLR->endBBID = lrEntry->endBBID;
                newLR->next = lrEntry->next;
                // then the first one -- just adjust the size
                lrEntry->endBBID = bb->ID-1;
                lrEntry->next = newLR;

                //printf("from %d to %d and from %d and to %d\n", lrEntry->startBBID, lrEntry->endBBID, lrEntry->next->startBBID, lrEntry->next->endBBID);

                rp->nLR++;

                // For all loop preheaders...
                BBListEntry* loopPreHeader = rp->loopHead->preLoopList;
                while (loopPreHeader) {
                    BBType* reloadPoint = loopPreHeader->BB;

                    // adjust node size for added reload instructions
                    reloadPoint->S += NUM_INSTS_FOR_SIMPLE_DMA;

                    // adjust addresses for added reload instructions
                    int n;
                    for (n = 0; n < nNode; n++) {
                        if (nodes[n]->addr > reloadPoint->addr)
                            nodes[n]->addr += 4 * NUM_INSTS_FOR_SIMPLE_DMA;
                    }

                    loopPreHeader = loopPreHeader->next;
                }
            }
        }
    }
    else {
        // wait.. cound't find a matching load range...?
        printf("can't find a mathing load range for bb %d at a reload point (loopHead ID %d) @rmBBForLoading\n", bb->ID, rp->loopHead->ID);
    }

    rp->remainingSPMsize += bb->S;

    bb->bLoaded = 0;

    if (functions[bb->EC].nOccurrence > 1) {
        int o;
        for (o = 0; o < functions[bb->EC].nOccurrence; o++) {
            int entryPointID = functions[bb->EC].entryPoints[o]->ID;
            int exitPointID = functions[bb->EC].exitPoints[o]->ID;

            int n;
            for (n = entryPointID; n <= exitPointID; n++) {
                // find the corresponding node with the same address
                if (nodes[n] != bb && nodes[n]->addr == bb->addr) {
                    rpType* rpOther = getReloadPoint(listRP, nodes[n]);

                    if (rpOther == rp) {
                        nodes[n]->bLoaded = 0;
                    }
                }
            }
        }
    }


    return 1;
}

#if 0
lcType* getMostBenficialLoop(lcType* loadCandidates)
{
    lcType* mostBeneficialLoop = loadCandidates;
    lcType* lcEntry = loadCandidates;
    while (lcEntry) {
        if (lcEntry->lb > mostBeneficialLoop->lb)
            mostBeneficialLoop = lcEntry;

        lcEntry = lcEntry->next;
    }

    return mostBeneficialLoop;
}

lcType* detachLoopFromLoadCandidates(lcType* loop, lcType** loadCandidates)
{
    if (loop == NULL || *loadCandidates == NULL)
        return;

    lcType* lcEntry = *loadCandidates;
    lcType* lcEntryPrev = NULL;

    int bRemoved = 0;
    while (lcEntry) {
        if (lcEntry == loop) {
            if (lcEntryPrev)
                lcEntryPrev->next = lcEntry->next;
            else    // first one -> modify loadcandidiate itself 
                *loadCandidates = lcEntry->next;

            lcEntry->next = NULL;
            bRemoved = 1;
            break;
        }

        lcEntryPrev = lcEntry;
        lcEntry = lcEntry->next;
    }

    if (bRemoved == 0) {
        printf("there is no corresponding loop with loop head ID %d in the given load candidates\n", loop->loopHead->ID);
    }

    return loop;
}

void sortLoadCandidates(lcType** loadCandidates)
{
    lcType* newLoadCandidates = NULL;
    lcType* lastEntry = NULL;

    lcType* mbloop;
    while ((mbloop  = getMostBeneficialLoop(*loadCandidates))) {
        mbloop = detachLoopFromLoadCndidates(mbloop, loadCandidates);

        if (newLoadCandidates == NULL)
            newLoadCandidates = mbloop;
        else
            lastEntry->next = mbloop;
        lastEntry = mbloop;
    }
    
    *loadCandidates = newLoadCandidates;
}
#endif

// calculate loading cost at reloading points
unsigned int getReloadCost(BBType* node, rpType* listRP)
{
    unsigned int dmaCost = 0;
    if (node->bPreLoop) {
        BBType* loopHead = node->loopHead;
        rpType* rpEntry = listRP;
        while (rpEntry) {
            if (rpEntry->loopHead == loopHead) {
                if (rpEntry->nLR > 0) {
                    //printf("reload point %d: \n", loopHead->ID);
                    lrType* lrEntry = rpEntry->listLR;
                    while (lrEntry) {
                        //printf("\tfrom %d to %d\n", lrEntry->startBBID, lrEntry->endBBID);
                        int sz = 0;
                        int n;
                        for (n = lrEntry->startBBID; n <= lrEntry->endBBID; n++)
                            sz += nodes[n]->S*4;

                        dmaCost += CdmaByBytes(sz);
                        lrEntry = lrEntry->next;
                    }
                }
            }
            rpEntry = rpEntry->next;
        }
    }
    
    return node->N * dmaCost; 
}

int cache_cost(BBType* node)
{
    if (node->bLoaded == 1)
        return 0;

#ifndef NO_CACHE_MODE
    if (SIZEMODE != NOCACHE) {
        if (node->N > 1)
            return (node->N * node->CACHE_AM * CACHE_MISS_LATENCY) + (CACHE_MISS_LATENCY * node->CACHE_FM + (node->N-1) * node->CACHE_FM);
        else
            return (node->N * node->CACHE_AM * CACHE_MISS_LATENCY) + (CACHE_MISS_LATENCY * node->CACHE_FM * node->N);
    }
    else
        return node->N * node->S * FETCH_LATENCY;
#else
    return node->N * node->S * FETCH_LATENCY;
#endif
}

int comp_cost(BBType* node)
{
    if (node->bLoaded == 1)
        return node->N * node->S;

#ifndef NO_CACHE_MODE
    if (SIZEMODE != NOCACHE)
        return node->N * node->CACHE_AH;
    else
        return 0;
#else
    return 0;
#endif    
}

// branching between SPM and main memory may take longer since it may need a long jump.
// In ARM, a long jump is a branch with 32-bit constant address, which needs one literal pool access before branch
void add_long_jumps()
{
    int i;
    for (i = 0; i < nNode; i++) {
        BBType* node = nodes[i];

        BBListEntry* succEntry = node->succList;
        while (succEntry) {
            BBType* succNode = succEntry->BB;

            // if mapped on different memory
            if ((node->bLoaded == 1 && succNode->bLoaded != 1) ||
                (node->bLoaded != 1 && succNode->bLoaded == 1)) {
                // check if fall-through edge. Fall through becomes an explicit long jump
                int n_additional_insts = 0;
                if (node->addr + 4*node->S == succNode->addr) 
                    n_additional_insts = 2;
                else
                    n_additional_insts = 1;

                node->S += n_additional_insts;

                // adjust addresses for added long jump instructions
                int n;
                for (n = 0; n < nNode; n++) {
                    if (nodes[n]->addr > node->addr)
                        nodes[n]->addr += 4*n_additional_insts;
                }
            }
            succEntry = succEntry->next;
        }
    }
}

// returns the WCET and WCEP as a BBListEntry
unsigned int evaluate_WCET(BBListEntry** WCEP, rpType* listRP, int bPrint)
{
    int i, j;

#ifndef NO_CACHE_MODE
    if (SIZEMODE != NOCACHE)
        cache_analysis();
#endif

    unsigned int* dist = (unsigned int*)calloc(nNode, sizeof(unsigned int));
    unsigned int* dmaCost = (unsigned int*)calloc(nNode, sizeof(unsigned int));
    if (dist == NULL || dmaCost == NULL) {
        printf("memory allocation error at evalulate_WCET\n");
        exit(1);
    }

    if (bPrint == 0)
        freeBBList(WCEP);
    dmaCost[rootNode->ID] = getReloadCost(rootNode, listRP);
    dist[rootNode->ID] = comp_cost(rootNode) + cache_cost(rootNode) + dmaCost[rootNode->ID];
    
    unsigned int distMax = 0;
    BBType* distMaxNode = NULL;
    for (i = tpTraversalStartIdx-2; i >= 0; i--) { // tpTraversalStartIdx-1 is rootNode
        BBType* node = listNodeTPSorted[i];

        // find max pred node
        unsigned int maxCost = 0;
        int maxPredID;
        BBListEntry* predEntry = node->predList;
        while (predEntry) {
            if (dist[predEntry->BB->ID] >= maxCost) {
                maxCost = dist[predEntry->BB->ID];
                maxPredID = predEntry->BB->ID;
            }
            predEntry = predEntry->next;
        }

        dmaCost[node->ID] = getReloadCost(node, listRP);
        dist[node->ID] = maxCost + comp_cost(node) + cache_cost(node) + dmaCost[node->ID];

        // keep a record of the max cost node
        if (dist[node->ID] > distMax) {
            distMax = dist[node->ID];
            distMaxNode = node;
        }
    }
    
    if (distMaxNode->succList != NULL) {
        printf("distMaxNode %d is not a terminal? @ evalulate_WCET\n", distMaxNode->ID);
    }

    int total_reload_cost = 0;
    int total_always_miss_cost = 0;
    int total_first_miss_cost = 0;

    // backtrack and find the WCEP
    BBType* node = distMaxNode;
    while (node) {
        if (bPrint == 0)
            addBBToList(node, WCEP);

        if (bPrint) {
            printf("Node %d (size %d, N %d, addr %lld): ", node->ID, node->S, node->N, node->addr);
            if (node->bLoaded == 1) 
                printf(" loaded in SPM -> latency = %d ", comp_cost(node));
            else {
#ifndef NO_CACHE_MODE
                if (SIZEMODE != NOCACHE)                 
                    printf("               -> latency = %d (AH %d AM %d FM %d) ", comp_cost(node)+cache_cost(node), node->CACHE_AH, node->CACHE_AM, node->CACHE_FM);
                else
                    printf("               -> latency = %d (AM %d) ", comp_cost(node)+cache_cost(node), node->S);
        
#else
                printf("               -> latency = %d ", cache_cost(node));
#endif
            }

            if (node->bPreLoop) {
                printf("Reload cost: %u (Total latency: %u)", dmaCost[node->ID], dmaCost[node->ID] + comp_cost(node) + cache_cost(node));
            }
            printf("\n");

            total_reload_cost += dmaCost[node->ID];
            if (node->bLoaded != 1) {
                if (SIZEMODE != NOCACHE) {
                    total_first_miss_cost += node->CACHE_FM*CACHE_MISS_LATENCY;
                    total_always_miss_cost += node->N*node->CACHE_AM*CACHE_MISS_LATENCY;
                }
                else {
                    total_first_miss_cost += 0;
                    total_always_miss_cost += cache_cost(node);
                }
            }
        }

        // check all predecessor and find the max
        if (node->predList) {
            BBListEntry* predEntry = node->predList;
            BBType* maxPredNode = predEntry->BB;
            while (predEntry) {
                if (dist[predEntry->BB->ID] > dist[maxPredNode->ID])
                    maxPredNode = predEntry->BB;
                predEntry = predEntry->next;
            }

            node = maxPredNode;
        }
        else
            node = NULL;
    }

    if (bPrint) {
        printf("Total computation cost: %.2f %%, Total cache miss handling cost: %d (AM %d + FM %d, %.2f %%), Total SPM reload cost: %d (%.2f %%)\n",(double)(distMax-total_always_miss_cost-total_first_miss_cost-total_reload_cost)/distMax*100, total_always_miss_cost+total_first_miss_cost, total_always_miss_cost, total_first_miss_cost, (double)(total_always_miss_cost+total_first_miss_cost)/distMax*100, total_reload_cost, (double)total_reload_cost/distMax*100);
    }

    free(dist);
    free(dmaCost);

    if (bPrint) 
        printf("WCET: %u\n", distMax);

    return distMax;
}

// Return the most frequently executed N basic blocks that lie on WCEP.
BBListEntry* selectMostBeneficialBBs(BBListEntry* WCEP, int N)
{
    if (WCEP == NULL || N < 1)
        return NULL;

    int i;

    BBListEntry* listBB = NULL;
    int nSelected = 0;
    
    // while N basic blocks are selected
    while (nSelected < N) {
        BBType *mostBeneficialBB = NULL;
        
        // Iterate through all basic blocks and find the most frequently executed basic block, not selected yet
        for (i = 0; i < nNode; i++) {
            if (nodes[i]->bLoaded == 0 && nodes[i]->N > 0) {
                if (isBBInNodeList(nodes[i], listBB) == 0) {
                    if (isBBInNodeList(nodes[i], WCEP)) {
                        if (mostBeneficialBB != NULL) {
                            if (mostBeneficialBB->N < nodes[i]->N)
                                mostBeneficialBB = nodes[i];
                        }
                        else
                            mostBeneficialBB = nodes[i];
                    }
                }
            }
        }

        if (mostBeneficialBB == NULL) break;

        addBBToList(mostBeneficialBB, &listBB);
        nSelected++;
    }

    return listBB;
}

void cm_bblevel() 
{
    //lpSize = getLiteralPoolSize();
    lpSize = 0; // considering literal pools is another problem.. let's assume those are handled by something else and not loaded into the instruction SPM 

    int n;
    for (n = 0; n < nNode; n++)
        nodes[n]->bLoaded = 0;

#ifndef NO_CACHE_MODE
    // init cache analysis
    switch (SIZEMODE) {
    case NOCACHE:
        SPMSIZE_IN_BBLEVEL = SPMSIZE;
        init_cache_analysis(0, CACHE_BLOCK_SIZE, CACHE_ASSOCIATIVITY);
        break;
    case NOSPM:
        SPMSIZE_IN_BBLEVEL = 0;
        init_cache_analysis(SPMSIZE, CACHE_BLOCK_SIZE, CACHE_ASSOCIATIVITY);
        break;
    case C3S1:
        SPMSIZE_IN_BBLEVEL = SPMSIZE/4;
        init_cache_analysis(SPMSIZE/4*3, CACHE_BLOCK_SIZE, CACHE_ASSOCIATIVITY);
        break;
    case C1S3:
        SPMSIZE_IN_BBLEVEL = SPMSIZE/4*3;
        init_cache_analysis(SPMSIZE/4, CACHE_BLOCK_SIZE, CACHE_ASSOCIATIVITY);
        break;
    case C1S1:
        SPMSIZE_IN_BBLEVEL = SPMSIZE/2;
        init_cache_analysis(SPMSIZE/2, CACHE_BLOCK_SIZE, CACHE_ASSOCIATIVITY);
        break;
    case DOUBLE:
        SPMSIZE_IN_BBLEVEL = SPMSIZE;
        init_cache_analysis(SPMSIZE, CACHE_BLOCK_SIZE, CACHE_ASSOCIATIVITY);
        break;
    }
#else 
    SPMSIZE_IN_BBLEVEL = SPMSIZE;
#endif

    // get repload points 
    dom_init();
    rpType *listRP = findReloadPoints();
    dom_free();
    initReloadPoints(listRP);

    // do topological sort for WCET estimation
    if (listNodeTPSorted != NULL)
        free(listNodeTPSorted);    
    listNodeTPSorted = (BBType**)malloc(sizeof(BBType*) * nNode);
    tpTraversalStartIdx = 0;

    initVisited();
    tpTraversalStartIdx = TPOvisit(rootNode, listNodeTPSorted, 0);

    BBListEntry* WCEP = NULL;
    unsigned int WCET;
    if (SIZEMODE == NOSPM)
        goto EXIT_CMBBLEVEL;

    WCET = evaluate_WCET(&WCEP, listRP, 0);
    printf("Initial WCET: %u\n", WCET);

    int nUp = 0;
    BBListEntry* listBB = selectMostBeneficialBBs(WCEP, NUM_BB_LOADED_PER_ITER);
    while (listBB) {
        // load these basic blocks until SPM gets full 
        BBListEntry* lbEntry = listBB;
        while (lbEntry) {
            BBType* node = lbEntry->BB;
            if (node->bLoaded == 0) {
                if (addBBForLoading(listRP, node) != 1)
                    node->bLoaded = -1; // marking this node to be removed from further consideration
            }

            lbEntry = lbEntry->next;
        }

        unsigned int newWCET = evaluate_WCET(&WCEP, listRP, 0);
        if (newWCET > WCET) { 
            if (nUp < 0) {
                printf("WCET went up to %u.\n", newWCET);
                nUp++;
            }
            else {
            printf("WCET went up to %u. Roll back the changes and finish.\n", newWCET);
            // roll back the changes in this iteration
            lbEntry = listBB;
            while (lbEntry) {
                BBType* node = lbEntry->BB;
                if (node->bLoaded == 1) {
                    rmBBForLoading(listRP, node);
                }

                lbEntry = lbEntry->next;
            }
            break;
            }
        }
        WCET = newWCET;
        printf("WCET went down to %u\n", WCET);

        freeBBList(&listBB);
        listBB = selectMostBeneficialBBs(WCEP, NUM_BB_LOADED_PER_ITER);
    }

EXIT_CMBBLEVEL:
    add_long_jumps();
    evaluate_WCET(&WCEP, listRP, 1);
    freeBBList(&WCEP);

    // free rp
    freeLoadRanges(listRP);
    freeReloadPoints(&listRP);
    free(listNodeTPSorted);    
}
