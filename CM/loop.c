#include "CM.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "loop.h"
#include "util.h"

int nLoop = -1;
loop* loopList = NULL;

int getLB(int loopIdx)
{
    if (loopIdx == -1)
        return 1;

    if (loopList[loopIdx].lb < 0)
    {
        printf("loop bound of loop %d is less than zero??\n", loopIdx);
        exit(1);
    }

    return loopList[loopIdx].lb;
}

int isLoopAinLoopB(int a, int b)
{
    if (a == -1 || b == -1)
        return 0;

    if (loopList[a].EC == loopList[b].EC)
    {
        if (loopList[a].start >= loopList[b].start &&
            loopList[a].end < loopList[b].end)
            return 1;
    }

    return 0;
}

int isLoopsDisjoint(int a, int b)
{
    if (a == -1 || b == -1)
        return 1;

    if (loopList[a].end < loopList[b].start ||
        loopList[a].start > loopList[b].end ||
        loopList[a].EC != loopList[b].EC)
        return 1;

    return 0;
}

int isLoopAlreadyFound(long long int headAddr)
{
    int i;
    for (i = 0; i < nLoop; i++)
    {
        if (loopList[i].start == headAddr) {
            return i;
        }
    }
    return -1;
}

void adjustLoopBounds()
{
    BBType* node;

    initVisited();

    INITSTACK(nNode)

    pushBB(rootNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        BBListEntry* succEntry = node->succList;
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }

        // get the maximum iteration count 
        if (node->bLoopTail && node->loopHead != node) {
            BBListEntry* preLoopEntry = node->preLoopList;
            int maxPreLoopCount = -1;
            while (preLoopEntry) {
                if (maxPreLoopCount < preLoopEntry->BB->N) {
                    maxPreLoopCount = preLoopEntry->BB->N;
                }
                preLoopEntry = preLoopEntry->next;
            }
            if (maxPreLoopCount > 0) {
                node->N += maxPreLoopCount;
            }
        }
    }
    FREESTACK
}

//void assignIterCounts(BBType* from, BBType *to, int lbHead, int lbTail)
void assignIterCounts(BBType* from, BBType *to, int lb, int bMultiply)
{
    BBType* node;
    
    INITSTACK(nNode)

    pushBB(from, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisitedForFindingLoop == 1)
            continue;
        node->bVisitedForFindingLoop = 1;

        if (node->EC != from->EC || (node->addr >= from->addr && node->addr <= to->addr)) {
            if (bMultiply == 1) {
                node->N *= lb;
            }
            else {
                if (lb > node->N || lb == 0)
                    node->N = lb;
            }
 
            if (node == to)
               continue;
        
            BBListEntry* succEntry = node->succList;
            while (succEntry) {
                pushBB(succEntry->BB, &stack);
                succEntry = succEntry->next;
            }
        }
    }
    
    FREESTACK
    initVisitedForLoop(from, to);
}

void addEdge(BBListEntry** list, BBType* node)
{
    BBListEntry* curEntry = *list;
    while (curEntry) {
        if (curEntry->BB == node)
            return;
        curEntry = curEntry->next;
    }

    BBListEntry* newEntry = (BBListEntry*)malloc(sizeof(BBListEntry));
    newEntry->BB = node;
    newEntry->next = NULL;

    if (*list == NULL)
        *list = newEntry;
    else
    {
        BBListEntry* curEntry = *list;
        while (curEntry->next)
        {
            if (curEntry->BB == node)
                return;
            curEntry = curEntry->next;
        }

        curEntry->next = newEntry;
    }
}

BBListEntry* removeEdge(BBListEntry** list, BBType* node)
{
    if (*list == NULL)
    {
        printf("can't remove an edge from an empty list\n");
        exit(1);
    }

    BBListEntry* curEntry = *list;
    BBListEntry* prevEntry = NULL;
    while (curEntry)
    {
        int bFound = 0;
        if (curEntry->BB == node)
        {
            if (prevEntry)
                prevEntry->next = curEntry->next;
            else
                *list = curEntry->next;

            return curEntry;
        }
        
        prevEntry = curEntry;
        curEntry = curEntry->next;
    }

    return NULL;
}

void moveLoopEdges(BBType* loopHead, BBType* loopTail)
{
    BBType* node;
    
    INITSTACK(nNode)
    
    int loopEC = loopHead->EC;

    BBType* loopEntry = NULL;

    pushBB(loopTail, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisitedForFindingLoop == 1)
            continue;
        node->bVisitedForFindingLoop = 1;

        // Find loop entry edges
        if (node != loopHead)
        {
            BBListEntry* predEntry = node->predList;
            BBListEntry* predEntryPrev = NULL;
            while (predEntry)
            {
                BBType* predBB = predEntry->BB;

                if (predBB->EC != loopEC || (predBB->addr >= loopHead->addr && predBB->addr <= loopTail->addr))
                    pushBB(predBB, &stack);

                if (node->EC == loopEC && (node->addr >= loopHead->addr && node->addr <= loopTail->addr) &&
                    predBB->EC == loopEC && predBB->addr < loopHead->addr && predBB->bUnreachable == 0)
                {
                    // found a loop entry endge
                    if (loopEntry != NULL && loopEntry != node && node != loopTail)
                    {
                        printf("multiple loopEntries?\n");
                        exit(1);
                    }
                    loopEntry = node;
                    
                    // move it to loop header
                    addEdge(&(loopHead->predList), predBB);
                    addEdge(&(predBB->succList), loopHead);
                    // remove it from the current node
                    BBListEntry* tmp;
                    tmp = removeEdge(&(predBB->succList), node);
                    free(tmp);
                    tmp = removeEdge(&(node->predList), predBB);

                    predEntry = predEntry->next;
                    free(tmp);
                }
                else if (predBB->EC == loopEC && predBB->addr > loopTail->addr && predBB->bUnreachable == 0)
                {
                    printf("Loop entry edge is a back edge??? from %d to %d\n", predBB->ID, node->ID);
                    exit(1);
                }
                else
                {
                    predEntryPrev = predEntry;
                    predEntry = predEntry->next;
                }
            }
        }

        // Find loop exit edges
        if (node != loopTail)
        {
            BBListEntry* succEntry = node->succList;
            BBListEntry* succEntryPrev = NULL;
            while (succEntry)
            {
                BBType* succBB = succEntry->BB;

                if (succBB->EC == loopEC && succBB->addr > loopTail->addr)
                {
                    // found a loop exit edge
                    // move it to loop tail
                    addEdge(&(loopTail->succList), succBB);
                    addEdge(&(succBB->predList), loopTail);
                    // remove it from the current node
                    BBListEntry* tmp;
                    tmp = removeEdge(&(succBB->predList), node);
                    free(tmp);
                    tmp = removeEdge(&(node->succList), succBB);

                    succEntry = succEntry->next;
                }
                else if (succBB->EC == loopEC && succBB->addr < loopHead->addr)
                {
                    printf("Loop exit edge is a back edge??? from %d to %d\n", succBB->ID, node->ID);
                    exit(1);
                }
                else
                {
                    succEntryPrev = succEntry;
                    succEntry = succEntry->next;
                }
            }
        }
    }

    if (loopEntry != loopTail && loopEntry != NULL) {
        if (loopEntry->predList == NULL) {
            pushBB(loopEntry, &stack);
            while ((node = popBB(&stack))) {
                loopEntry->bUnreachable = 1;
                if (loopEntry->IS) {
                    int f;
                    for (f = 0; f < nFunc; f++)
                        free(loopEntry->IS[f]);
                    free(loopEntry->IS);
                    loopEntry->IS = NULL;
                }
                loopTail->S += node->S;
                loopTail->CACHE_AH += node->CACHE_AH;
                loopTail->CACHE_AM += node->CACHE_AM;
                loopTail->CACHE_FM += node->CACHE_FM;

                if (node != loopTail) {
                    BBListEntry* tmpEntry = node->succList;
                    while (tmpEntry) {
                        if (tmpEntry->BB->predList->next == NULL)
                            pushBB(tmpEntry->BB, &stack);
                        tmpEntry = tmpEntry->next;
                    }
                }
            }

            //printf("loopEntrySize:%d at node %d (loopTail is %d)\n", loopEntrySize, loopEntry->ID, loopTail->ID);
        }
    }

    FREESTACK
    initVisitedForLoop(loopHead, loopTail);
}

#if 0
void assignLoopIdx(BBType* from, BBType* to, int loopID)
{
    BBType* node;
    
    INITSTACK(nNode)

    pushBB(from, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisitedForFindingLoop == 1)
            continue;
        node->bVisitedForFindingLoop = 1;

        if (node->EC != from->EC || (node->addr >= from->addr && node->addr <= to->addr))
        {
            if (node->L < 0)
                node->L = loopID;
            else
            {
                if (isLoopAinLoopB(loopID, node->L) ||
                    isLoopsDisjoint(loopID, node->L))
                    node->L = loopID;
            }
        }
        
        if (node == to)
            continue;

        BBListEntry* succEntry = node->succList;
        while (succEntry)
        {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    
    FREESTACK
    initVisitedForLoop(from, to);
}
#endif

void findLoops()
{
    typedef struct _loopBound
    {
        long long int addr;
        //int bExit;
        int count;
    } loopBound;
    loopBound* LB = NULL;
    int lIdx;
    int defaultLB = -1;
        
    // Read loop entry addresses
    FILE *fp = fopen("loopHeads.txt", "r");
    fscanf(fp, "%d\n", &nLoop);
    fscanf(fp, "%*x\n"); // skip the START ADDR info

    LB = (loopBound*)malloc(sizeof(loopBound) * nLoop);
    for (lIdx = 0; lIdx < nLoop; lIdx++) {
        fscanf(fp, "%lld (%*llx)\n", &(LB[lIdx].addr));
    }
    fclose(fp);

    // Init loop list
    loopList = (loop*)malloc(sizeof(loop) * nLoop);
    for (lIdx = 0; lIdx < nLoop; lIdx++)
        loopList[lIdx].start = -1;

    // Get loop bounds info.
    fp = fopen("lb.out", "r");
    if (fp != NULL) {
        fscanf(fp, "%*d\n");

        // loop order may change here but it doesn't matter
        // loop addresses are rewritten anyway
        for (lIdx = 0; lIdx < nLoop; lIdx++) {
            fscanf(fp, "%lld %d\n", &LB[lIdx].addr, &LB[lIdx].count);
        }
        fclose(fp);
    }
    else {
        printf("Loop bounds not given!\n");
        printf("Each loop is assumed to be taken 10 times\n");
        //printf("Enter the default loop bound (We will assume all loops are taken the following number of times.): ");
        //scanf("%d", &defaultLB);
        defaultLB = 10;

        for (lIdx = 0; lIdx < nLoop; lIdx++) {
            LB[lIdx].count = defaultLB;
        }
    }

    initVisited();
 
    BBType* node;

    INITSTACK(nNode)

    int nLoopVisited = 0;
    // ASSIGN LOOP BOUNDS
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisited == 1)
            continue;
        
        node->bVisited = 1;
        
        BBListEntry* succEntry = node->succList;
        while (succEntry)
        {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }

        succEntry = node->succList;
        while (succEntry) {
            if (succEntry->BB->addr <= node->addr && succEntry->BB->EC == node->EC) {
                // we found a loop!
                BBType* loopHead, *loopTail;

                loopHead = succEntry->BB;
                loopTail = node;

                if (loopHead == loopTail) {
                    printf("There is a one-node loop at node %d\n", node->ID);
                    exit(1);
                }

                int lb = -1;
                for (lIdx = 0; lIdx < nLoop; lIdx++) {
                    if (LB[lIdx].addr == loopHead->addr) {
                        lb = LB[lIdx].count;
                        break;
                    }
                }

                if (lb == -1) {
                    printf("Cannot find a matching loop bound at 0x%llx\n", (long long unsigned int)loopHead->addr);
                    exit(1);
                }

                int loopIdx = isLoopAlreadyFound(loopHead->addr);
                if (loopIdx == -1) {
#ifdef DEBUG
                    printf("Loop:%d from %lld to %lld\n", nLoopVisited, loopHead->addr, loopTail->addr);
#endif
                    loopList[nLoopVisited].start = loopHead->addr;
                    loopList[nLoopVisited].end = loopTail->addr;
                    loopList[nLoopVisited].lb = lb;
                    loopList[nLoopVisited].EC = loopHead->EC;
                    nLoopVisited++;
                }
                else {
#ifdef DEBUG
                    printf("Loop:%d from %lld to %lld\n", loopIdx, loopHead->addr, loopTail->addr);
#endif
                    loopList[loopIdx].end = loopTail->addr;
                    loopList[loopIdx].lb = lb;
                }

                loopHead->bLoopHead = 1;
                loopTail->bLoopTail = 1;
                loopTail->loopHead = loopHead;
                addBBToList(loopTail, &(loopHead->loopTailList));

                moveLoopEdges(loopHead, loopTail);                
                if (defaultLB > 0)
                    assignIterCounts(loopHead, loopTail, lb, 1);
                else
                    assignIterCounts(loopHead, loopTail, lb, 0);

                //assignLoopIdx(loopHead, loopTail, nLoopVisited-1);

                succEntry = succEntry->next;

                // remove backedge
                BBListEntry* tmp;
                tmp = removeEdge(&(loopTail->succList), loopHead);
                free(tmp);
                tmp = removeEdge(&(loopHead->predList), loopTail);
                free(tmp);

                // find loop preheaders
                BBListEntry* headPredEntry = loopHead->predList;
                while (headPredEntry) {
                    if (headPredEntry->BB != loopTail && headPredEntry->BB->bUnreachable == 0) {
                        BBType* preLoop = headPredEntry->BB;

                        preLoop->bPreLoop = 1;
                        preLoop->loopHead = loopHead;
                        //addBBToList(loopTail, &(preLoop->loopTailList));

                        // add pre loops into prelooplist of the loopTail
                        addBBToList(preLoop, &(loopTail->preLoopList));
                        addBBToList(preLoop, &(loopHead->preLoopList));
                    }
                    headPredEntry = headPredEntry->next;
                }

            }
            else
                succEntry = succEntry->next;
        }

        succEntry = node->succList;
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    free(LB);
    free(loopList);
    FREESTACK
}

