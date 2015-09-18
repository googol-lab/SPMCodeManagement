#include "CFG_traversal.h"
#include "DFS.h"

//#define IS_OLD_VER 1

void initVisited()
{
    BBType* node;

    INITSTACK(nNode)

    pushBB(rootNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 0)
            continue;
        node->bVisited = 0;
        
        BBListEntry* entry = node->succList;
        while (entry) {
            pushBB(entry->BB, &stack);
            entry = entry->next;
        }
    }
    
    FREESTACK
}

void initVisitedInRange(BBType *from, BBType *to)
{
    BBType* node;

    INITSTACK(nNode)

    pushBB(from, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 0)
            continue;
        node->bVisited = 0;
        
        if (node != to) {
            BBListEntry* entry = node->succList;
            while (entry) {
                pushBB(entry->BB, &stack);
                entry = entry->next;
            }
        }
    }
    
    FREESTACK
}

void initUnreachable()
{
    initVisited();
    
    BBType* node;

    INITSTACK(nNode)

    pushBB(rootNode, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisited == 1)
            continue;

        node->bVisited = 1;
        node->bUnreachable = 0;
        node->N = 1;
        
        BBListEntry* entry = node->succList;
        while (entry)
        {
            pushBB(entry->BB, &stack);
            entry = entry->next;
        }
    }

    FREESTACK
}

void initEC()
{
    initVisited();
    
    BBType* node;

    INITSTACK(nNode)

    int *checkEC = (int*)malloc(sizeof(int)*nFunc);
    int i;
    for (i = 0; i < nFunc; i++)
        checkEC[i] = 0;
    
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisited == 1)
            continue;
        
        //bottomNode = node;
        node->bVisited = 1;
        
        //if (checkEC[node->EC] == 0 && node->N)
        if (checkEC[node->EC] == 0)
        {
            node->bFirst = 1;
            checkEC[node->EC] = 1;
        }
        
        BBListEntry* entry = node->succList;
        while (entry)
        {
            pushBB(entry->BB, &stack);
            entry = entry->next;
        }
    }
    
    int nEmptyFunc = 0;
    for (i = 0; i < nFunc; i++)
    {
        if (checkEC[i] == 0)
            nEmptyFunc++;
        else
            checkEC[i] = i - nEmptyFunc;
    }
    
    initVisited();
    
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisited == 1)
            continue;
        
        node->bVisited = 1;
        
        if (node->EC != checkEC[node->EC])
            printf("@node%d: EC %d --- EC %d\n", node->ID, node->EC, checkEC[node->EC]);
        node->EC = checkEC[node->EC];
        
        BBListEntry* entry = node->succList;
        while (entry)
        {
            pushBB(entry->BB, &stack);
            entry = entry->next;
        }
    }
    
    //maxEC -= nEmptyFunc;
    
    free(checkEC);
    FREESTACK
}

void initIS()
{
    initVisited();
    
    BBType* node;
    int funcIdx, listIdx;

    INITSTACK(nNode)

    pushBB(rootNode, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;
        
        if (node->IS != NULL) {
            for (funcIdx = 0; funcIdx < node->ISsize; funcIdx++)
                free(node->IS[funcIdx]);
            free(node->IS);
        }
        node->IS = (int**)malloc(sizeof(int*) * nFunc);
        for (funcIdx = 0; funcIdx < nFunc; funcIdx++) 
            node->IS[funcIdx] = (int*)malloc(sizeof(int) * nFunc);

        node->ISsize = nFunc;

        for (funcIdx = 0; funcIdx < nFunc; funcIdx++) 
        {
            for (listIdx = 0; listIdx < nFunc; listIdx++)
                node->IS[funcIdx][listIdx] = 0;
        }

        BBListEntry* entry = node->succList;
        while (entry)
        {
            pushBB(entry->BB, &stack);
            entry = entry->next;
        }
    }
    
    FREESTACK
}

void initVisitedForLoop(BBType* from, BBType* to)
{
    BBType* node;
    
    INITSTACK(nNode)

    pushBB(from, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisitedForFindingLoop == 0)
            continue;
        node->bVisitedForFindingLoop = 0;
        
        if (node != to)
        {
            BBListEntry* succEntry = node->succList;
            while (succEntry)
            {
                pushBB(succEntry->BB, &stack);
                succEntry = succEntry->next;
            }
        }
    }
    
    FREESTACK
}

void findIS()
{
    if (rootNode == NULL)
    {
        printf("no rootNode!\n");
        exit(1);
    }
    
    INITSTACK(nNode)

    BBType* node;

    int funcIdx, listIdx;
    int** newReachable = (int**)calloc(nFunc, sizeof(int*));
    for (funcIdx = 0; funcIdx < nFunc; funcIdx++)
        newReachable[funcIdx] = (int*)calloc(nFunc, sizeof(int));

    BBListEntry *predEntry, *succEntry;
    BBType *predNode, *succNode;
   
    struct timeval tvBegin, tvEnd, tvDiff;

    // begin
    gettimeofday(&tvBegin, NULL);
 
    int bUpdated = 1;
    int nIter = 0;
    while (bUpdated)
    {
        nIter++;
        bUpdated = 0;
        
        initVisited();
        
        pushBB(rootNode, &stack);
        while ((node = popBB(&stack)))
        {
            if (node->bVisited)
                continue;
            node->bVisited = 1;

            for (funcIdx = 0; funcIdx < nFunc; funcIdx++) {
                for (listIdx = 0; listIdx < nFunc; listIdx++) {
                    newReachable[funcIdx][listIdx] = 0;
                }
            }

            // Visit each predecessor and merge there ISs
            predEntry = node->predList;
            while (predEntry) {
                predNode = predEntry->BB;
                predEntry = predEntry->next;
                if (predNode->bUnreachable == 1) 
                    continue;

                // predNode->EC was just executed by predNode
                // Mark the function was executed (nothing else can be in the IS[predNode->EC]
                newReachable[predNode->EC][predNode->EC] = 1;
                for (funcIdx = 0; funcIdx < nFunc; funcIdx++) {
                    if (funcIdx == predNode->EC) {
                        newReachable[predNode->EC][predNode->EC] = 1;
                    }
#ifndef IS_OLD_VER
                    else if (predNode->IS[funcIdx][funcIdx] == 1) {
                        // this means funcIdx has been executed.
#else
                    else {
#endif
                        for (listIdx = 0; listIdx < nFunc; listIdx++) {
                            if (listIdx == predNode->EC) {
                                newReachable[funcIdx][listIdx] = 1;
                            }
                            else if (predNode->IS) {
                                if (predNode->IS[funcIdx][listIdx] == 1)
                                    newReachable[funcIdx][listIdx] = 1;
                            }
                        }
                    }
                } // for (funcIdx)
            } // while (predEntry)

            // After backedges are removed, loopheads do not have
            //  edges from looptails.
            // Visit each looptail and merge there ISs
            if (node->bLoopHead) {
                BBListEntry* loopTailEntry = node->loopTailList;
                while (loopTailEntry) {
                    BBType* loopTail = loopTailEntry->BB;
                    loopTailEntry = loopTailEntry->next;
                    if (loopTail->bUnreachable == 1) 
                        continue;

                    for (funcIdx = 0; funcIdx < nFunc; funcIdx++) {
                        if (funcIdx == loopTail->EC) {
                            // loopTail->EC was just executed by loopTail
                            // Mark the function was executed
                            newReachable[loopTail->EC][loopTail->EC] = 1;
                        }
#ifndef IS_OLD_VER
                        else if (loopTail->IS[funcIdx][funcIdx] == 1) {
                            // this means funcIdx has been executed.
#else
                        else {
#endif
                            for (listIdx = 0; listIdx < nFunc; listIdx++) {
                                if (listIdx == loopTail->EC) {
                                    newReachable[funcIdx][listIdx] = 1;
                                }
                                else if (loopTail->IS) {
                                    if (loopTail->IS[funcIdx][listIdx] == 1)
                                        newReachable[funcIdx][listIdx] = 1;
                                }
                            }
                        }
                    } // for (funcIdx)
                } // while (loopTailEntry)
            } // if (node->bLoopHead)


            if (node->predList || node->loopTailList) {
                for (funcIdx = 0; funcIdx < nFunc; funcIdx++) {
                    for (listIdx = 0; listIdx <nFunc; listIdx++) {
                        if (newReachable[funcIdx][listIdx] != node->IS[funcIdx][listIdx]) {
                            bUpdated = 1;
                            node->IS[funcIdx][listIdx] = newReachable[funcIdx][listIdx];
                        }
                    }
                }
            }

            succEntry = node->succList;
            while (succEntry)
            {
                succNode = succEntry->BB;
                
                pushBB(succNode, &stack);
                succEntry = succEntry->next;
            }
        } // while pop
    }

#ifdef DEBUG
    printf("# iter for preprocessing: %d\n", nIter);
#endif
    
    gettimeofday(&tvEnd, NULL);
    long int diff = (tvEnd.tv_usec + 1000000 * tvEnd.tv_sec) - (tvBegin.tv_usec + 1000000 * tvBegin.tv_sec);
    tvDiff.tv_sec = diff / 1000000;
    tvDiff.tv_usec = diff % 1000000;
    printf("Interference Analaysis took %ld.%06d seconds\n", tvDiff.tv_sec, tvDiff.tv_usec);

    for (funcIdx = 0; funcIdx < nFunc; funcIdx++)
        free(newReachable[funcIdx]);
    free(newReachable);
    FREESTACK
}

int TPOvisit(BBType* node, BBType** nodeList, int curIdx)
{
    if (node->bVisited == 2) 
    {
        printf("There's a loop. This is not a DAG. node %d\n", node->ID);
        exit(1);
    }

    int nextIdx = curIdx;
    if (node->bVisited == 0)
    {
        node->bVisited = 2;
        
        if (node->succList == NULL)
        {
            node->bVisited = 1;
            nodeList[nextIdx++] = node;
        }
        else
        {
            BBListEntry* succEntry = node->succList;
            while (succEntry)
            {
                nextIdx = TPOvisit(succEntry->BB, nodeList, nextIdx);
                succEntry = succEntry->next;
            }

            node->bVisited = 1;
            nodeList[nextIdx++] = node;
        }
    }

    return nextIdx;
}

int isDirectlyReachable(BBType* from, BBType* to, int bStart)
{
    static int* bVisited = NULL;
    if (bStart == 1) {
        if (bVisited == NULL)
            bVisited = (int*)calloc(nNode, sizeof(int));
    }

    if (from == to)
        return 1;

    int bReachable = 0;
    if (bVisited[from->ID] == 0) {
        bVisited[from->ID] = 1;

        BBListEntry* succEntry = from->succList;
        while (succEntry) {
            // skip any backedges
            if (from->bLoopTail == 0 || (from->bLoopTail == 1 && succEntry->BB != from->loopHead)) {
                bReachable = isDirectlyReachable(succEntry->BB, to, 0);
                if (bReachable == 1)
                    break;
            }

            succEntry = succEntry->next;
        }
    }

    if (bStart == 1) {
        free(bVisited);
        bVisited = NULL;
    }
        
    return bReachable;
}

void initBFirst()
{
    BBType* node;

    int *checkEC = (int*)calloc(nFunc, sizeof(int));
    checkEC[rootNode->EC] = 1;

    // First do a topological sort
    BBType** tpoSortedNodes = (BBType**)malloc(sizeof(BBType)*nNode);
    int tpoStartIdx = 0;

    initVisited();
    tpoStartIdx = TPOvisit(rootNode, tpoSortedNodes, 0);

    int i;
    for (i = tpoStartIdx-2; i >= 0; i--) {
        node = tpoSortedNodes[i];

        if (checkEC[node->EC] == 0) {
            node->bFirst = 1;
            checkEC[node->EC] = 1;
        }
    }

    rootNode->bFirst = 0;

    free(tpoSortedNodes);
    free(checkEC);
}

// get a set of all rechable functions from f (callees, callees of a callee, ... )
int* get_rList(int f, int* rList)
{
    if (rList == NULL) {
        rList = (int*)calloc(nFunc, sizeof(int));
    }
    memset(rList, 0, sizeof(int)*nFunc);

    INITSTACK(nNode)
    initVisited();

    BBType* entryNode = functions[f].entryPoints[0];

    int bIdx;
    for (bIdx = functions[f].exitPoints[0]->ID; bIdx >= entryNode->ID; bIdx--) {
        if (nodes[bIdx]->bLiteralPool == 0)
            break;
    }
    BBType* exitNode = nodes[bIdx];

    BBType* node;
    pushBB(entryNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        rList[node->EC] = 1;

        if (node != exitNode) {
            BBListEntry *succEntry = node->succList;
            while (succEntry) {
                pushBB(succEntry->BB, &stack);
                succEntry = succEntry->next;
            }
        }
    }

    initVisitedInRange(entryNode, exitNode);

    FREESTACK

    return rList;
}

// AEX analysis
void findInitialLoadingPoints()
{
    int nIdx, fIdx;

    enum TF {True, False};
    enum TF bUpdated;

    int **AEX = (int**)malloc(sizeof(int*) * nNode);
    for (nIdx = 0; nIdx < nNode; nIdx++) {
        AEX[nIdx] = (int*)malloc(sizeof(int) * nFunc);
        for (fIdx = 0; fIdx < nFunc; fIdx++) {
            AEX[nIdx][fIdx] = 0;
        }
    }
    int *Intersect = (int*)malloc(sizeof(int) * nFunc);

    do {
        bUpdated = False;

        // visit every node
        for (nIdx = 0; nIdx < nNode; nIdx++) {
            BBType* node = nodes[nIdx];

            if (node->predList && node->bUnreachable == 0 && node->bLiteralPool == 0) {
                for (fIdx = 0; fIdx < nFunc; fIdx++)
                    Intersect[fIdx] = 1;

                // visit every predecessor
                BBListEntry *predEntry = node->predList;
                while (predEntry) {
                    BBType* predNode = predEntry->BB;

                    // Take an Intersect
                    for (fIdx = 0; fIdx < nFunc; fIdx++) {
                        if (fIdx != predNode->EC && AEX[predNode->ID][fIdx] == 0)
                            Intersect[fIdx] = 0;
                    }
                    predEntry = predEntry->next;
                }

                for (fIdx = 0; fIdx < nFunc; fIdx++) {
                    if (AEX[nIdx][fIdx] != Intersect[fIdx])
                        bUpdated = True;
                    AEX[nIdx][fIdx] = Intersect[fIdx];
                }
            }
        }
    } while (bUpdated == True);

    free(Intersect);

 //   int nAllBFirst = 1, nNewBFirst = 0;

    for (nIdx = 0; nIdx < nNode; nIdx++) {
        BBType* node = nodes[nIdx];

        if (node == rootNode || node->predList == NULL || node->bUnreachable == 1 || node->bLiteralPool == 1)
            continue;

/*
        if (node->bFirst != 1-AEX[nIdx][node->EC]) {
            //printf("node%d: node->bFirst: %d  <-------> AEX-bassed bFirst: %d\n", nIdx, node->bFirst, 1-AEX[nIdx][node->EC]);
            nNewBFirst++;
        }
*/
        if (AEX[nIdx][node->EC] == 0) {
            //printf("%d is an initial loading points of function %d\n", nIdx, node->EC);
            //nAllBFirst++;
            node->bFirst = 1;
        }
    }

    for (nIdx = 0; nIdx < nNode; nIdx++)
        free(AEX[nIdx]);
    free(AEX);
/*
    printf("-------------------------------\n");
    printf("all bFirst: %d (including the root node) new bFirst: %d\n", nAllBFirst, nNewBFirst);
    printf("-------------------------------\n");
*/
}
