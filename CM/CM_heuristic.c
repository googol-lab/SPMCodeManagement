#include "CM.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "CM_heuristic.h"
#include "DMA.h"
#include "util.h"

typedef struct _region
{
    int *func;
    int size;
} region;

region* R;

void initRegions(region** R)
{
    int r;
    int f;

    if (*R == NULL)
        *R = (region*)malloc(sizeof(region) * nFunc);

    for (r = 0; r < nFunc; r++)
    {
        ((*R)[r].func) = (int*)malloc(sizeof(int) * nFunc);
        for (f = 0; f < nFunc; f++)
            ((*R)[r].func)[f] = 0;
        (*R)[r].size = 0;
    }
}

void freeRegions(region** R)
{
    int r;
    for (r = 0; r < nFunc; r++)
        free(((*R)[r].func));

    free(*R);
}

int getRegionSize(region* R, int r)
{
    int f;
    int maxFuncSize = 0;
    for (f = 0; f < nFunc; f++)
    {
        if (R[r].func[f] == 1)
        {
            if (functions[f].size > maxFuncSize)
                maxFuncSize = functions[f].size;
        }
    }

    R[r].size = maxFuncSize;

    return maxFuncSize;
}

int getMapSize(region* R)
{
    int size = 0;

    int r;
    for (r = 0; r < nFunc; r++)
    {
        //int rSize = getRegionSize(R, r);
        //size += rSize;
        size += R[r].size;
    }

    return size;
}

int createNewRegion(region *R)
{
    int r;
    for (r = 0; r < nFunc; r++)
    {
        if (R[r].size == 0)
            break;
    }

    if (r == nFunc)
    {
        printf("can't create a new region\n");
        return -1;
    }

    return r;
}

int findRegion(region *R, int f)
{
    int r;
    int retR = -1;
    for (r = 0; r < nFunc; r++)
    {
        if (R[r].func[f] == 1)
        {
            if (retR != -1)
            {
                printf("error! %d belongs to more than one regions\n", f);
                exit(1);
            }
            retR = r;
        }
    }

    if (retR == -1)
    {
        printf("error! %d does not belong to any region\n", f);
        exit(1);
    }

    return retR;
}

int isFuncTheOnlyFunc(region *R, int f)
{
    int r = findRegion(R, f);
     
    int i;
    for (i = 0; i < nFunc; i++)
    {
        if (i == f)
            continue;
        else if (R[r].func[i] == 1)
            return 0;
    }
    return 1;
}

void moveFunctionToDifferentRegion(region* R, int f, int r1, int r2)
{
    R[r1].func[f] = 0;
    R[r2].func[f] = 1;

    getRegionSize(R, r1);
    getRegionSize(R, r2);
}

void mergeTwoRegions(region* R, int r1, int r2)
{
    int f;
    for (f = 0; f < nFunc; f++)
    {
        if (R[r2].func[f] == 1)
            R[r1].func[f] = 1;
        R[r2].func[f] = 0;
    }

    getRegionSize(R, r1);
    getRegionSize(R, r2);
}

int* getFuncMap(region *R)
{
    int* funcMap = NULL;

    funcMap = (int*)malloc(sizeof(int)*nFunc);

    int f;
    for (f = 0; f < nFunc; f++)
        funcMap[f] = 0;

    int r;
    for (r = 0; r < nFunc; r++)
    {
        if (R[r].size == 0)
            continue;
        
        int f;
        for (f = 0; f < nFunc; f++)
        {
            if (R[r].func[f] == 1)
            {
                if (funcMap[f] == 1)
                {
                    printf("funcMap error!\n");
                    exit(1);
                }
                funcMap[f] = r;
            }
        }
    }

    return funcMap;
}

void loadFuncMap(region *R, int* funcMap)
{
    int r;
    for (r = 0; r < nFunc; r++)
    {
        int f;
        for (f = 0; f < nFunc; f++)
        {
            R[r].func[f] = 0;
            if (funcMap[f] == r)
                R[r].func[f] = 1;
        }
        getRegionSize(R, r);
    }
}

BBType** tpoSortedNodes = NULL;
int tpoStartIdx;

int getWCET(int* funcMap)
{
    int i, j;
    static int bFirst = 1;
    static int* dist;
    if (bFirst == 1)
    {
        dist = (int*)malloc(sizeof(int)*nNode);
        bFirst = 0;
    }
    for (i = 0; i < nNode; i++)
        dist[i] = 0;

    //dist[rootNode->ID] = rootNode->N * rootNode->S + Cdma(rootNode->EC);
    dist[rootNode->ID] = rootNode->N * rootNode->S;

    int distMax = 0;
    BBType* distMaxNode = NULL;
    for (i = tpoStartIdx-2; i >= 0; i--)
    {
        BBType *node = tpoSortedNodes[i];

        if (node->predList == NULL)
        {
            printf("something's wrong...\n");
            exit(1);
        }
        else
        {
            int maxCost = -1;
            int maxPredID = -1;
            BBListEntry* entry = node->predList;
            while (entry)
            {
                if (dist[entry->BB->ID] > maxCost)
                {
                    maxCost = dist[entry->BB->ID];
                    maxPredID = entry->BB->ID;
                }
                entry = entry->next;
            }

            dist[node->ID] = maxCost + (node->N * node->S);
            if (node->CS==1 && node->N>0)
            {
               int bInterfere = 0;
                for (j = 0; j < nFunc; j++)
                {
                    if (j == node->EC)
                        continue;
                    if (node->IS[node->EC][j] == 1 && funcMap[node->EC] == funcMap[j])
                    {
                        bInterfere = 1;
                        break;
                    }
                }
                int Ncall = getNCall(node);
                int bRT = isNodeRT(node);
                if (bInterfere == 1) {
                    if (bRT == 1)
                        dist[node->ID] += Ncall * (Cdma(node->EC)+NUM_INSTS_AM_RETURN);
                    else
                        dist[node->ID] += Ncall * (Cdma(node->EC)+NUM_INSTS_AM_CALL);
                }
                else
                {
                    if (node->bFirst) {
                        if (Ncall > 1) {
                            if (bRT == 1)
                                dist[node->ID] += Cdma(node->EC) + (Ncall-1)*NUM_INSTS_AH_RETURN+NUM_INSTS_AM_RETURN;
                            else
                                dist[node->ID] += Cdma(node->EC) + (Ncall-1)*NUM_INSTS_AH_CALL+NUM_INSTS_AM_CALL;
                        }
                        else {
                            if (bRT == 1)
                                dist[node->ID] += Ncall*(Cdma(node->EC)+NUM_INSTS_AM_RETURN);
                            else
                                dist[node->ID] += Ncall*(Cdma(node->EC)+NUM_INSTS_AM_CALL);
                        }
                    }
                    else {
                        if (bRT == 1)
                            dist[node->ID] += Ncall * NUM_INSTS_AH_RETURN;
                        else
                            dist[node->ID] += Ncall * NUM_INSTS_AH_CALL;
                    }
                }
            }
        }

        if (dist[node->ID] >= distMax)
        {
            distMax = dist[node->ID];
            distMaxNode = node;
        }
    }

    if (distMaxNode->succList != NULL) {
        printf("distMaxNode %d is not a terminal?\n", distMaxNode->ID);
        printf("node %d : %d\n", distMaxNode->ID, dist[distMaxNode->ID]);
    }

    return dist[distMaxNode->ID];
}

int runHeuristic(int SPMSIZE)
{
    struct timeval tvBegin, tvEnd, tvDiff;

    int i;
    int bError = 0;
    for (i = 0; i < nFunc; i++) {
        if (functions[i].size > SPMSIZE) {
            printf("SPMSIZE (%d) is smaller than function %d (size %d)\n", SPMSIZE, i, functions[i].size);
            bError = 1;
        }
    }
    if (bError)
        return -1;

    initVisited();

    if (tpoSortedNodes != NULL)
        free(tpoSortedNodes);
    tpoSortedNodes = (BBType**)malloc(sizeof(BBType*) * nNode);
    tpoStartIdx = 0;

    // begin
    gettimeofday(&tvBegin, NULL);
 
    tpoStartIdx = TPOvisit(rootNode, tpoSortedNodes, 0);
    R = NULL;
    initRegions(&R);
    for (i = 0; i < nFunc; i++)
    {
        R[i].func[i] = 1;
        R[i].size = functions[i].size;
    }

    int nRegion = nFunc;
    for (nRegion = nFunc; nRegion >= 2; nRegion--)
    {
        int mapSize = getMapSize(R);
        if (mapSize <= SPMSIZE)
            break;

        int bestWCET = INT_MAX;
        int bestF1, bestF2;

        int f1, f2;
        for (f1 = 0; f1 < nFunc-1; f1++)
        {
            if (R[f1].size == 0)
                continue;

            for (f2 = f1+1; f2 < nFunc; f2++)
            {
                if (R[f2].size == 0)
                    continue;

                int* funcMapOrig = getFuncMap(R);
                mergeTwoRegions(R, f1, f2);
                int* funcMapNew = getFuncMap(R);
                int WCET = getWCET(funcMapNew);
                if (WCET < bestWCET)
                {
                    bestWCET = WCET;
                    bestF1 = f1; bestF2 = f2;
                }

                loadFuncMap(R, funcMapOrig);
                free(funcMapOrig);
                free(funcMapNew);
            }
        }

        mergeTwoRegions(R, bestF1, bestF2);
    }

    int* funcMapMerge = getFuncMap(R);
    
    initRegions(&R);
    for (i = 0; i < nFunc; i++)
        R[0].func[i] = 1;
    getRegionSize(R, 0);
    nRegion = 1;

    int* funcMapPartition = getFuncMap(R);

    while (nRegion < nFunc)
//    for (nRegion = 1; nRegion <= nFunc; nRegion++)
    {
        int f;
        int bestF, bestR;
        int origWCET, bestWCET;
        origWCET = bestWCET = getWCET(funcMapPartition);

        for (f = 0; f < nFunc; f++)
        {
            int r;
            for (r = 0; r < nRegion+1; r++)
            {
                int oldR = funcMapPartition[f];
                if (r == oldR)
                    continue;

                funcMapPartition[f] = r;
                int WCET = getWCET(funcMapPartition);
                if (WCET < bestWCET)
                {
                    loadFuncMap(R, funcMapPartition);
                    if (getMapSize(R) < SPMSIZE)
                    {
                        bestWCET = WCET;
                        bestF = f;
                        bestR = r;
                        //bestR = nRegion;
                    }
                }
                funcMapPartition[f] = oldR;
                loadFuncMap(R, funcMapPartition);
            }
        }

        //printf("%d %d\n", bestF, bestR);
        //printf("%d %d\n", origWCET, bestWCET);
        if (origWCET <= bestWCET)
            break;

        moveFunctionToDifferentRegion(R, bestF, funcMapPartition[bestF], bestR);
        funcMapPartition[bestF] = bestR;
        if (bestR == nRegion) 
            nRegion++;
    }

    int* funcMap;
    int WCETM = getWCET(funcMapMerge);
    int WCETP = getWCET(funcMapPartition); 

    if (WCETM < WCETP)
    {
        printf("merge is taken\n");
        funcMap = funcMapMerge;
    }
    else
    {
        printf("partition is taken\n");
        funcMap = funcMapPartition;
    }

    gettimeofday(&tvEnd, NULL);
    long int diff = (tvEnd.tv_usec + 1000000 * tvEnd.tv_sec) - (tvBegin.tv_usec + 1000000 * tvBegin.tv_sec);
    tvDiff.tv_sec = diff / 1000000;
    tvDiff.tv_usec = diff % 1000000;

    printf("Heuristic took %ld.%06d seconds\n", tvDiff.tv_sec, tvDiff.tv_usec);

    FILE *fp = fopen("cm_input.txt", "w");
    for (i = 0; i < nFunc; i++)
    {
        fprintf(fp, "%d\n", funcMap[i]);
    }
    fclose(fp);

    free(tpoSortedNodes);
    tpoSortedNodes = NULL;

    freeRegions(&R);
    free(funcMapMerge);
    free(funcMapPartition);

    return 0;
}


