#include "CM.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "loop.h"
#include "util.h"
#include <math.h>

#include "gurobi_c.h"

#include "DMA.h"

#include "CM_region_based.h"

void output_mapping_result(int *M, int numRegion, int *RS)
{
    // read user code starting address
    long long int userCodeStartingAddr;
    FILE *fp = fopen("userCodeRange.txt", "r");
    fscanf(fp, "%llx", &userCodeStartingAddr);
    fclose(fp);

    // calculate region starting addresses
    long long int *RA = (long long int*)malloc(sizeof(long long int)*numRegion);
    int i;
    RA[0] = 0;
    for (i = 1; i < numRegion; i++) {
        RA[i] = (long long int)RA[i-1] + RS[i-1];
    }

    // output 
    fp = fopen("mapping.out", "w");
    
    // # of partitions
    fprintf(fp, "%d\n", nFunc);
    int p;
    for (p = 0; p < nFunc; p++) {
        // partition ID (parent ID)
        fprintf(fp, "%d (%d)\n", p, functions[p].parent);

        // partition size
        fprintf(fp, "%d ", functions[p].size);
        
        // # of address ranges that the partition lies in
        fprintf(fp, "%d ", functions[p].nAddrRange);

        // address rangess (starting address and size)
        int nIdx;
        for (nIdx = 0; nIdx < functions[p].nAddrRange; nIdx++) {
            fprintf(fp, "(%llx %lld) ", userCodeStartingAddr + functions[p].addrRange[nIdx].startAddr, functions[p].addrRange[nIdx].size);
        }
        
        // mapped address
        fprintf(fp, "%lld\n", RA[M[p]]);
    }

    free(RA);
    fclose(fp);
}

long long int cm_region_optimal(long long int* fCost)
{
    BBType* node;

    INITSTACK(nNode)
    
    GRBenv* env = NULL;
    GRBmodel *model = NULL;
    int error;
    
    double objVal;
    int status;
    int* M = (int*)malloc(sizeof(int) * nFunc); // mapping
    int* RS = (int*)malloc(sizeof(int) * nFunc); // region size
    int* SFR = (int*)malloc(sizeof(int) * nFunc); // smallest function in a region

    int nIdx;
    int v, f, g, r;
    int nCall;

    FILE* fp = fopen("codemapping.lp", "w");
    if (fp == NULL)
    {
        printf("cannot create the output file\n");
        exit(1);
    }
    
    // Objective function
    fprintf(fp, "Minimize\n");
    fprintf(fp, "W0\n");
    
    fprintf(fp, "Subject to\n");

    initVisited();
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

        if (node->N == 0) {
            succEntry = node->succList;
            while (succEntry) {
                fprintf(fp, "W%d - W%d <= 0\n", succEntry->BB->ID, node->ID);
                succEntry = succEntry->next;
            }
            continue;
        }
        //printf("node%d: N %d S %d\n", node->ID, node->N, node->S);

        // Wv's
        if (node->succList == NULL)
            fprintf(fp, "W%d - L%d = %d\n", node->ID, node->ID, node->N*node->S);
        else {
            if (node->succList->next == NULL) {
                BBType* succNode = node->succList->BB;

                fprintf(fp, "W%d - W%d - L%d = %d\n", node->ID, succNode->ID, node->ID, node->N*node->S);
            } else {
                fprintf(fp, "W%d - MX%d - L%d = %d\n", node->ID, node->ID, node->ID, node->N*node->S);
                BBListEntry* succEntry = node->succList;
                while (succEntry) {
                    fprintf(fp, "MX%d - W%d >= 0\n", node->ID, succEntry->BB->ID);
                    fprintf(fp, "W%d - MX%d - %d B%d_%d >= -%d\n", succEntry->BB->ID, node->ID, INT_MAX/2, succEntry->BB->ID, node->ID, INT_MAX/2);
                    succEntry = succEntry->next;
                }
                succEntry = node->succList;
                while (succEntry->next) {
                    fprintf(fp, "B%d_%d + ", succEntry->BB->ID, node->ID);
                    succEntry = succEntry->next;
                }
                fprintf(fp, "B%d_%d = 1\n", succEntry->BB->ID, node->ID);
            } // else (node->succList->next != NULL)
        } // else (node->SuccList != NULL)

        // Lv's
        if (node->CS) {
#ifdef LVZERO
            fprintf(fp, "L%d = 0\n", node->ID);
#else
            /////////////////////////////////////
            int N = getNCall(node);
            if (N > 0) {
                int AM, FM;
                if (isNodeRT(node) == 1) {
                    AM = N*(Cdma(node->EC) + 4);
                    FM = Cdma(node->EC) + 4*(N-1) + 11;
                }
                else {
                    AM = N*(Cdma(node->EC) + 5);
                    FM = Cdma(node->EC) + 4*(N-1) + 12;
                }

                // Instead of node->N, we should use getNCall(node)
                //  The latter represents the actual number of times it is called
                // The former can be greater than the latter in case the latter is 
                //  at a loop tail.
                if (node->bFirst) {
                    // let say AM = N*(Cdma+MO) and FM = Cdma + 2*(N-1) + FMO ... 
                    // MO (MangementOverhead) is 10 for return and 4 for call
                    // FMO (FirstMiss MO) is 14 for return and 8 for call
                    if (N > 1) {
                        // if BAM == 0 then L = FM else AM
                        //  L >= AM*BAM + FM*(1-BAM) ==> L + (FM-AM)*BAM >= FM
                        fprintf(fp, "L%d + %d BAMC%d = %d\n", node->ID, FM - AM, node->ID, FM);
                    } else {
                        fprintf(fp, "L%d = %d\n", node->ID, AM);
                    }
                } else {
                    // if BAM == 0 then L = 0 else L = AM
                    //     L >= AM*BAM
                    fprintf(fp, "L%d - %d BAMC%d = 0\n", node->ID, AM, node->ID);
                }
            }
            else
                fprintf(fp, "L%d = 0\n", node->ID);
#endif
        }
        else if (node == rootNode) {
#ifndef LVZERO
            int Lmain = Cdma(node->EC);
            fprintf(fp, "L%d = %d\n", node->ID, Lmain);
#else
            fprintf(fp, "L%d = 0\n", node->ID);
#endif
        }
        else
            fprintf(fp, "L%d = 0\n", node->ID);
    } // while
    
    // M's
    for (f = 0; f < nFunc-1; f++)
    {
        // cartessian product, F x F
        for (g = f+1; g < nFunc; g++)
        {
            // for each region
            for (r = 0; r < nFunc; r++)
            {
                fprintf(fp, "-2 M%d_%d_%d + M%d_%d + M%d_%d >=  0\n", f, g, r, f, r, g, r);
                fprintf(fp, "-2 M%d_%d_%d + M%d_%d + M%d_%d <=  1\n", f, g, r, f, r, g, r);
            }
        }
    }
    
    for (f = 0; f < nFunc; f++)
    {
        for (r = 0; r < nFunc-1; r++)
            fprintf(fp, "M%d_%d + ", f, r);
        fprintf(fp, "M%d_%d = 1\n", f, r);
    }

    // BAM's
    initVisited();
    
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;
        
        if (node->CS == 1) {
            //////BAMC --- for code
            int bInterference = 0;
            for (f = 0; f < node->EC; f++) {
                if (node->IS[node->EC][f] == 0)
                    continue;

                bInterference = 1;
                for (r = 0; r < nFunc; r++)
                    fprintf(fp, "BAMC%d - M%d_%d_%d >= 0\n", node->ID, f, node->EC, r);
            }
            for (f = node->EC+1; f < nFunc; f++) {
                if (node->IS[node->EC][f] == 0)
                    continue;

                bInterference = 1;
                for (r = 0; r < nFunc; r++)
                    fprintf(fp, "BAMC%d - M%d_%d_%d >= 0\n", node->ID, node->EC, f, r);
            }
            if (bInterference == 0)
                fprintf(fp, "BAMC%d = 0\n", node->ID);

            else {
                fprintf(fp, "BAMC%d", node->ID);
                for (f = 0; f < node->EC; f++) {
                    if (node->IS[node->EC][f] == 0)
                        continue;
                    for (r = 0; r < nFunc; r++)
                        fprintf(fp, " - M%d_%d_%d", f, node->EC, r);
                }
                for (f = node->EC+1; f < nFunc; f++) {
                    if (node->IS[node->EC][f] == 0)
                        continue;
                    for (r = 0; r < nFunc; r++)
                        fprintf(fp, " - M%d_%d_%d", node->EC, f, r);
                }
                fprintf(fp, " <= 0\n");
            }
        }
        BBListEntry* succEntry = node->succList;
        while (succEntry)
        {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    
    // Size
    for (r = 0; r < nFunc; r++)
    {
        for (f = 0; f < nFunc; f++)
        {
            fprintf(fp, "%d M%d_%d - S%d <=  0\n", functions[f].size, f, r, r);
        }
    }
    
    for (r = 0; r < nFunc-1; r++)
        fprintf(fp, "S%d + ", r);
    fprintf(fp, "S%d <= %d\n", r, SPMSIZE);
    
    fprintf(fp, "Bounds\n");
    for (r = 0; r < nFunc; r++)
    {
        fprintf(fp, "S%d >=  0\n", r);
    }
    
    fprintf(fp, "General\n");
    
    initVisited();
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        fprintf(fp, "W%d\n", node->ID);
        if (node->N > 0) {
            if (node->succList) {
                if (node->succList->next != NULL)
                    fprintf(fp, "MX%d\n", node->ID);
            }
            fprintf(fp, "L%d\n", node->ID);
        }
 
        BBListEntry* succEntry = node->succList;
        while (succEntry)
        {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    for (r = 0; r < nFunc; r++)
        fprintf(fp, "S%d\n", r);
    
    fprintf(fp, "Binary\n");
    for (f = 0; f < nFunc; f++)
    {
        for (g = 0; g < nFunc; g++)
        {
            fprintf(fp, "M%d_%d\n", f, g);
        }
    }

    for (f = 0; f < nFunc-1; f++)
    {
        for (g = f+1; g < nFunc; g++)
        {
            for (r = 0; r < nFunc; r++)
            {
                fprintf(fp, "M%d_%d_%d\n", f, g, r);
            }
        }
    }

    initVisited();
    
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        if (node->CS) {
            fprintf(fp, "BAMC%d\n", node->ID);
        }

        if (node->N > 0) {
            if (node->succList) {
                if (node->succList->next != NULL) {
                    BBListEntry* succEntry = node->succList;
                    while (succEntry) {
                        fprintf(fp, "B%d_%d\n", succEntry->BB->ID, node->ID);
                        succEntry = succEntry->next;
                    }
                }
            }
        } 
        BBListEntry* succEntry = node->succList;
        while (succEntry)
        {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    
    fprintf(fp, "End\n");
    fclose(fp);

    ///////// SOLVER
    error = GRBloadenv(&env, "codemapping.log");
    if (error || env == NULL)
    {
        fprintf(stderr, "Error: could not create environment\n");
        exit(1);
    }
    GRBsetintparam(env, GRB_INT_PAR_OUTPUTFLAG, 1);
    //GRBsetintparam(env, GRB_INT_PAR_METHOD, 1);
    //GRBsetintparam(env, GRB_INT_PAR_NORMADJUST, 1);
/*
    GRBsetintparam(env, GRB_INT_PAR_MIPFOCUS, 1);
    //GRBsetintparam(env, GRB_INT_PAR_PRESOLVE, 2);
    GRBsetintparam(env, GRB_INT_PAR_PREDEPROW, 1);
    GRBsetintparam(env, GRB_INT_PAR_PRESPARSIFY, 1);
    GRBsetintparam(env, GRB_INT_PAR_PREPASSES, 30);
    GRBsetintparam(env, GRB_INT_PAR_VARBRANCH, 2);
    GRBsetintparam(env, GRB_INT_PAR_SCALEFLAG, 2);
    GRBsetdblparam(env, GRB_DBL_PAR_OBJSCALE, 100000);
    GRBsetintparam(env, GRB_INT_PAR_THREADS, 8);
    GRBsetintparam(env, GRB_INT_PAR_NUMERICFOCUS, 1);
    GRBsetdblparam(env, GRB_DBL_PAR_IMPROVESTARTTIME, 300);
*/
    GRBsetdblparam(env, GRB_DBL_PAR_TIMELIMIT, 600);

    error = GRBreadmodel(env, "codemapping.lp", &model);
    if (error)  quit(error, env);

    error = GRBoptimize(model);
    if (error) quit(error, env);
    
    error = GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status);
    if (error)  quit(error, env);
    
    if (fCost != NULL) {
        for (f = 0; f < nFunc; f++)
            fCost[f] = 0;
    }

    objVal = 0;
    if (status == GRB_INFEASIBLE) {
        GRBsetintparam(env, GRB_INT_PAR_MIPFOCUS, 1);
    	
        error = GRBoptimize(model);
        if (error) quit(error, env);
    
        error = GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status);
        if (error)  quit(error, env);
    } 

    if (status == GRB_OPTIMAL || status == GRB_TIME_LIMIT) {
        int varIdx;
        static char *W0 = "W0";
        error = GRBgetvarbyname(model, W0, &varIdx);
        if (error)  quit(error, env);
        error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdx, &objVal);
 
        if (objVal == 0) {
            /*
            GRBfreemodel(model);
            GRBfreeenv(env);

            FREESTACK
            return -1;
            */
            GRBsetintparam(env, GRB_INT_PAR_MIPFOCUS, 1);
    	
            error = GRBoptimize(model);
            if (error) quit(error, env);
    
            error = GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status);
            if (error)  quit(error, env);

            if (! (status == GRB_OPTIMAL || status == GRB_TIME_LIMIT)) {
                GRBfreemodel(model);
                GRBfreeenv(env);
    
                FREESTACK
                return -1;
            }
            error = GRBgetvarbyname(model, W0, &varIdx);
            if (error)  quit(error, env);
            error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdx, &objVal);
        }

        //error = GRBgetdblattr(model, GRB_DBL_ATTR_OBJVAL, &objVal);
        //if (error)  quit(error, env);
        
        int memreq = 0;
        for (r = 0; r < nFunc; r++) {
            RS[r] = 0;  // region size
            SFR[r] = INT_MAX;   // smallest function in region
        }
        
        // Region numbers from ILP results do not always start from zero.
        // For example, let us say nFunc is 10, but only two regions are used.
        // Then, ILP may map functions to region 4 and 9.
        // This should actually be region 0 and region 1, for our convenience.
        // MM --> translates the region numbers from ILP to human-friendly region numbers, sorted from zero.
        int* MM = (int*)malloc(sizeof(int) * nFunc);
        for (r = 0; r < nFunc; r++)
            MM[r] = -1;

        int numRegion = 0;
        for (f = 0; f < nFunc; f++) {
            for (r = 0; r < nFunc; r++) {
                int varIdx;
                static char varName[32];
                memset(varName, 0, 32);
                sprintf(varName, "M%d_%d", f, r);
                
                error = GRBgetvarbyname(model, varName, &varIdx);
                if (error)  quit(error, env);
                
                double vVal;
                error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdx, &vVal);
                            
                if (vVal > 0) {
                    if (MM[r] == -1) {
                        MM[r] = numRegion++;
                    }

                    if (functions[f].parent == -1)
                        printf("Function %d (size %d) is mapped to Region %d\n", f, functions[f].size, MM[r]);
                    else
                        printf("Function %d (size %d) is a child of function %d and mapped to Region %d\n", f, functions[f].size, functions[f].parent, MM[r]);
 
                    M[f] = MM[r];
                    if (RS[MM[r]] < functions[f].size)
                        RS[MM[r]] = functions[f].size;

                    if (functions[f].size < SFR[MM[r]]) 
                        SFR[MM[r]] = functions[f].size;
                }
            }
        }
        free(MM);
        printf("-----------------\n");
        printf("# regions: %d\n", numRegion);
        double WF = 0.0, AF = 0.0;
        for (r = 0; r < numRegion; r++) {
            memreq += RS[r];
            printf("Region %3d's size: %8d\t", r, RS[r]);

            double F = ((double)RS[r])/SFR[r];
            if (F > WF)
                WF = F;
            AF += F;

            printf("Fragmentation factor (region size/smallest function size)): %.4f\n", F);
        }
        //free(RS);
        free(SFR);
        AF /= numRegion;
        printf("Worst fragmentation: %.4f\t Average fragmentation: %.4f\n", WF, AF);
        printf("memreq: %d\n", memreq);

        output_mapping_result(M, numRegion, RS);

/////////////
        int* dist = (int*)calloc(nNode, sizeof(int));
        BBType** tpoSortedNodes = (BBType**)malloc(sizeof(BBType)*nNode);
        int tpoStartIdx = 0;

        initVisited();

        tpoStartIdx = TPOvisit(rootNode, tpoSortedNodes, 0);

        int maxDist = 0;
        BBType* maxDistNode;
#ifndef LVZERO
        dist[rootNode->ID] = rootNode->N * rootNode->S + Cdma(rootNode->EC);
#endif

        int i;
        for (i = tpoStartIdx-2; i >= 0; i--) {
            node = tpoSortedNodes[i];

            int maxCost = -1;

            BBListEntry* predEntry = node->predList;
            while (predEntry) {
                BBType* predNode = predEntry->BB;
                if (dist[predNode->ID] > maxCost) {
                    maxCost = dist[predNode->ID];
                }
                predEntry = predEntry->next;
            }

            dist[node->ID] = maxCost + (node->N * node->S);
#ifndef LVZERO
            if (node->CS==1 && node->N>0) {
                int nCall = getNCall(node);
                int bAM = 0;
                // CODE 
                for (f = 0; f < nFunc; f++) {
                    if (f == node->EC)
                        continue;

                    if (node->IS[node->EC][f]==1) {
                        // Are they mapped to the same region?
                        if (M[f] == M[node->EC])
                            bAM = 1;
                    }
                }
                if (bAM == 1 && nCall > 0) {
                    if (isNodeRT(node) == 1)
                        dist[node->ID] += nCall * (Cdma(node->EC) + 10);
                    else
                        dist[node->ID] += nCall * (Cdma(node->EC) + 4);
                }
                else {
                    if (node->bFirst) {
                        if (nCall == 1) {
                            if (isNodeRT(node) == 1)
                                dist[node->ID] += nCall * (Cdma(node->EC) + 10);
                            else
                                dist[node->ID] += nCall * (Cdma(node->EC) + 4);
                        }
                        else if (nCall > 1) {
                            if (isNodeRT(node) == 1)
                                dist[node->ID] += Cdma(node->EC) + (nCall-1) * 2 + 14;
                            else
                                dist[node->ID] += Cdma(node->EC) + (nCall-1) * 2 + 8;
                        }
                    }
                }
            }
#endif
            if (dist[node->ID] > maxDist) {
                maxDist = dist[node->ID];
                maxDistNode = node;
            }
        }

        initVisited();

        int nSPMAM = 0, nSPMFM = 0;
        int totalSPMCost = 0;
        
        if (fCost != NULL)
#ifndef LVZERO
            fCost[rootNode->EC] = Cdma(rootNode->EC);
#else
            fCost[rootNode->EC] = 0;
#endif

        int totalMGMTCost = 0;
        int WCETwithoutSPM = 0;
        

        float* fCostExFM = (float*)malloc(sizeof(float) * nFunc);
        for (i = 0; i < nFunc; i++)
            fCostExFM[i] = 0;

        int CostExFM = 0;

        int CC, WC, MC; // computation cost, waiting cost, management cost
        pushBB(maxDistNode, &stack);
        while ((node = popBB(&stack))) {
            if (node->bVisited == 1)
                continue;
            node->bVisited = 1;
        
            int distMax = -1;
            BBType *maxDistPredNode;
            BBListEntry* predEntry = node->predList;
            while (predEntry) {
                BBType *predNode = predEntry->BB;
                if (dist[predNode->ID] >= distMax) {
                    maxDistPredNode = predNode;
                    distMax = dist[predNode->ID];
                }

                predEntry = predEntry->next;
            }

            int varIdxNode;
            static char varNameNode[32];
            memset(varNameNode, 0, 32);
            sprintf(varNameNode, "W%d", node->ID);
            error = GRBgetvarbyname(model, varNameNode, &varIdxNode);
            if (error)  quit(error, env);
                
            double wNode;
            error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdxNode, &wNode);

            sprintf(varNameNode, "L%d", node->ID);
            error = GRBgetvarbyname(model, varNameNode, &varIdxNode);
               
            double lNode;
            error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdxNode, &lNode);
            if (fCost != NULL)
#ifndef LVZERO
                fCost[node->EC] += (long long int)(lNode+0.5);
#else
                fCost[node->EC] += 0;
#endif
            CC = node->N*node->S;

            WCETwithoutSPM += CC;
            if (dbgFlag)
                printf("node %d: EC: %d, N = %d, S= %d\n", node->ID, node->EC, node->N, node->S);

            WC = 0;
            MC = 0;
#ifndef LVZERO
            if (node->CS==1 && node->N>0) {
                nCall = getNCall(node);
                int bAM = 0;
                if (dbgFlag)
                    printf("CODE: [");
                for (f = 0; f < nFunc; f++) {
                    if (dbgFlag) {
                        if (node->IS[node->EC][f] == 1)
                            printf(" %d", f);
                    }
                    if (f == node->EC)
                        continue;
 
                    if (node->IS[node->EC][f]==1) {
                        // Are they mapped to the same region?
                        if (M[f] == M[node->EC])
                            bAM = 1;
                    }
                }
                if (dbgFlag)
                    printf(" ]\n");
                if (bAM == 1 && nCall > 0) {
                    nSPMAM++;
                    if (isNodeRT(node) == 1) {
                        WC = nCall * Cdma(node->EC);
                        MC = nCall * 4;
                    }
                    else {
                        WC = nCall * Cdma(node->EC);
                        MC = nCall * 5;
                    }

                    int max = 0;
                    for (f = 0; f < nFunc; f++) {
                        if (f == node->EC)
                            continue;
                        if (node->IS[node->EC][f] == 1 && functions[f].size > max)
                            max = functions[f].size;
                    }
                    if (max+functions[node->EC].size <= SPMSIZE) {
                        fCostExFM[node->EC] += WC+MC;
                        CostExFM += WC+MC;
                    }
                }
                else {
                    if (node->bFirst) {
                        if (nCall == 1) {
                            nSPMAM++;
                            if (isNodeRT(node) == 1) {
                                WC = nCall * Cdma(node->EC);
                                MC = nCall * 4;
                            }
                            else {
                                WC = nCall * Cdma(node->EC);
                                MC = nCall * 5;
                            }
                        }
                        else if (nCall > 1) {
                            nSPMFM++;
                            if (isNodeRT(node) == 1) {
                                WC = Cdma(node->EC);
                                MC = (nCall-1) * 4 + 11;
                            }
                            else {
                                WC = Cdma(node->EC);
                                MC = (nCall-1) * 4 + 12;
                            }
                        }
                    }
                }
            }
            else if (node == rootNode) {
                WC = Cdma(rootNode->EC);
            }
#endif
            if (dbgFlag) {
                if (WC > 0 || MC > 0)
                    printf("   WC: %d, MC: %d\n", WC, MC);
            }
            totalSPMCost += WC;
            totalMGMTCost += MC;

            if ((long long int)(lNode+0.5) != (WC+MC)) {
                printf("Loading cost doesn't match at node %d %s %d from %d, %d times. Estimated : %d vs. Calculated: %lld\n", node->ID, (isNodeRT(node) == 1) ? "returning to":"calling", node->EC, node->predList->BB->EC, nCall, (WC+MC), (long long int)(lNode+0.5));
                printf("W:%d, M: %d\n", WC, MC);
            }
 
            pushBB(maxDistPredNode, &stack);
        }

        float avgSkewCost = 0;
        int nCaseAdded = 0;
        float maxSkewCost = 0;
        if (CostExFM > 0) {
            printf("CostExFM: %d\n", CostExFM);
            for (i = 0; i < nFunc; i++) {
                if (fCostExFM[i] > 0) {
                    printf("function%d costExFM: %f sizeSkew: %d/%d = %f skewCost: %f\n", i, fCostExFM[i], RS[M[i]], functions[i].size, ((float)RS[M[i]])/functions[i].size, fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size);
                    avgSkewCost += fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size;
                    if (maxSkewCost < (fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size))
                        maxSkewCost = fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size;
                    nCaseAdded++;
                }
            }
        }
        avgSkewCost /= nCaseAdded;

        float stdDev = 0;
        if (CostExFM > 0) {
            for (i = 0; i < nFunc; i++) {
                if (fCostExFM[i] > 0) {
                    float diff = avgSkewCost - (fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size);
                    diff *= diff;
                    stdDev += diff;
                }
            }

            stdDev /= nCaseAdded;
            stdDev = sqrt(stdDev);

            printf("\n\n");
            printf("maxSkewCost: %f\n", maxSkewCost);
            printf("avgSkewCost: %f\n", avgSkewCost);
            printf("stdDev: %f\n\n", stdDev);
        }
        free(RS);
        free(fCostExFM);
 
        if ((long long int)objVal != WCETwithoutSPM+totalSPMCost+totalMGMTCost) {
            printf("Costs do not match. %lld vs %d\n", (long long int)objVal, WCETwithoutSPM+totalSPMCost+totalMGMTCost);
        }
        printf("WCET: %lld (C: %lld (%.2f%%), W: %lld (%.2f%%), M: %lld (%.2f%%))\n", (long long int)objVal, (long long int)WCETwithoutSPM, WCETwithoutSPM/objVal*100, (long long int)totalSPMCost, totalSPMCost/objVal*100, (long long int)totalMGMTCost, totalMGMTCost/objVal*100);
        free(tpoSortedNodes);
        free(dist);
    }
    else {
        GRBfreemodel(model);
        GRBfreeenv(env);

        FREESTACK

        return -2;
    }

    if (error)
    {
        printf("ERROR: %s\n", GRBgeterrormsg(env));
        exit(1);
    }

    GRBfreemodel(model);
    GRBfreeenv(env);

    free(M);
    FREESTACK

    return (long long int)objVal;
}

void wcet_analysis_fixed_input()
{
    BBType* node;

    INITSTACK(nNode)
    
    GRBenv* env = NULL;
    GRBmodel *model = NULL;
    int error;
    
    double objVal;
    int status;
    int nIdx;
    int v, f, g, r;
    int nCall;

    int *M = (int*)malloc(sizeof(int) * nFunc);

    // read mapping input
    FILE *fp = fopen("cm_input.txt", "r");
    if (fp == NULL)
    {
        printf("code mapping input not found\n");
        exit(1);
    }
    for (f = 0; f < nFunc; f++) {
        fscanf(fp, "%d\n", &M[f]);
        printf("Function %d (size %d) is mapped to Region %d\n", f, functions[f].size, M[f]);
    }
    fclose(fp);

    int i;
    int numRegion = 1;
    int *RS = (int*)malloc(sizeof(int)*nFunc);
    int *SFR = (int*)malloc(sizeof(int)*nFunc);
    for (i = 0; i < nFunc; i++) {
        RS[i] = 0;          // region size
        SFR[i] = INT_MAX;   // smallest function size in a region
    }
    for (i = 0; i < nFunc; i++) {
        if (M[i] > 0 && M[i]+1 > numRegion) {
            numRegion = M[i]+1;
        }
    }
    for (i = 0; i < nFunc; i++) {
        if (functions[i].size > RS[M[i]])
            RS[M[i]] = functions[i].size;
        if (functions[i].size < SFR[M[i]]) 
            SFR[M[i]] = functions[i].size;
    }
    int memreq = 0;
    for (i = 0; i < nFunc; i++) {
        memreq += RS[i];
    }
    printf("memreq: %d\n", memreq);
    printf("-----------------\n");
    printf("# regions: %d\n", numRegion);
    double WF = 0.0, AF = 0.0;
    for (i = 0; i < numRegion; i++) {
        printf("Region %3d's size: %8d\t", i, RS[i]);

        double F = ((double)RS[i])/SFR[i];
        if (F > WF)
            WF = F;
        AF += F;

        printf("Fragmentation factor (region size/smallest function size)): %.4f\n", F);
    }
    //free(RS);
    free(SFR);
    AF /= numRegion;
    printf("Worst fragmentation: %.4f\t Average fragmentation: %.4f\n", WF, AF);

    output_mapping_result(M, numRegion, RS);

    fp = fopen("wcet_fixed.lp", "w");
    if (fp == NULL)
    {
        printf("cannot create the output file\n");
        exit(1);
    }
    
    // Objective function
    fprintf(fp, "Minimize\n");
    fprintf(fp, "W0\n");
    
    fprintf(fp, "Subject to\n");

    initVisited();
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

        if (node->N == 0) {
            succEntry = node->succList;
            while (succEntry) {
                fprintf(fp, "W%d - W%d <= 0\n", succEntry->BB->ID, node->ID);
                succEntry = succEntry->next;
            }
            continue;
        }

        // Wv's
        if (node->succList == NULL)
            fprintf(fp, "W%d - L%d = %d\n", node->ID, node->ID, node->N*node->S);
        else {
            if (node->succList->next == NULL) {
                BBType* succNode = node->succList->BB;

                if (node == rootNode) {
                    fprintf(fp, "W%d - W%d - L%d = %d\n", node->ID, succNode->ID, node->ID, (node->N*node->S));
                } else {
                    fprintf(fp, "W%d - W%d - L%d = %d\n", node->ID, succNode->ID, node->ID, node->N*node->S);
                }
            } else {
                if (node == rootNode) {
                    fprintf(fp, "W%d - MX%d - L%d = %d\n", node->ID, node->ID, node->ID, (node->N*node->S));
                } else {
                    fprintf(fp, "W%d - MX%d - L%d = %d\n", node->ID, node->ID, node->ID, node->N*node->S);
                }
                BBListEntry* succEntry = node->succList;
                while (succEntry) {
                    fprintf(fp, "MX%d - W%d >= 0\n", node->ID, succEntry->BB->ID);
                    fprintf(fp, "W%d - MX%d - %d B%d_%d >= -%d\n", succEntry->BB->ID, node->ID, INT_MAX/2, succEntry->BB->ID, node->ID, INT_MAX/2);
                    succEntry = succEntry->next;
                }
                succEntry = node->succList;
                while (succEntry->next) {
                    fprintf(fp, "B%d_%d + ", succEntry->BB->ID, node->ID);
                    succEntry = succEntry->next;
                }
                fprintf(fp, "B%d_%d = 1\n", succEntry->BB->ID, node->ID);
            } // else (node->succList->next != NULL)
        } // else (node->SuccList != NULL)

        // Lv's
        if (node->CS) {
#ifdef LVZERO
            fprintf(fp, "L%d = 0\n", node->ID);
#else
            // Loading time
            int N = getNCall(node);
            if (N > 0) {
                int L = 0;
                int bAM = 0;

                for (f = 0; f < nFunc; f++) {
                    if (node->IS[node->EC][f] == 1 && f != node->EC) {
                        if (M[node->EC] == M[f]) {
                            bAM = 1;
                            break;
                        }
                    }
                }

                if (isNodeRT(node) == 1) {
                    if (bAM == 1 || (node->bFirst == 1 && N == 1)) {
                        L = N*(Cdma(node->EC) + 4);
                    }
                    else {
                        if (node->bFirst) {
                            L = Cdma(node->EC) + 4*(N-1) + 11;
                        }
                    }
                }
                else {
                    if (bAM == 1 || (node->bFirst == 1 && N == 1)) {
                        L = N*(Cdma(node->EC) + 5);
                    }
                    else {
                        if (node->bFirst) {
                            L = Cdma(node->EC) + 4*(N-1) + 12;
                        }
                    }
                }
                fprintf(fp, "L%d = %d\n", node->ID, L);
            }
            else
                fprintf(fp, "L%d = 0\n", node->ID);
#endif
        }
        else if (node == rootNode) {
#ifndef LVZERO
            int Lmain = Cdma(node->EC);
            fprintf(fp, "L%d = %d\n", node->ID, Lmain);
#else
            fprintf(fp, "L%d = 0\n", node->ID);
#endif
        }
        else
            fprintf(fp, "L%d = 0\n", node->ID);
    } // while
    
    fprintf(fp, "General\n");
    
    initVisited();
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        fprintf(fp, "W%d\n", node->ID);
        if (node->N > 0) {
            if (node->succList) {
                if (node->succList->next != NULL)
                    fprintf(fp, "MX%d\n", node->ID);
            }
            fprintf(fp, "L%d\n", node->ID);
       }
 
        BBListEntry* succEntry = node->succList;
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    
    fprintf(fp, "Binary\n");
    initVisited();
    
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack)))
    {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        if (node->N > 0) {
            if (node->succList) {
                if (node->succList->next != NULL) {
                    BBListEntry* succEntry = node->succList;
                    while (succEntry) {
                        fprintf(fp, "B%d_%d\n", succEntry->BB->ID, node->ID);
                        succEntry = succEntry->next;
                    }
                }
            }
        } 
        BBListEntry* succEntry = node->succList;
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    
    fprintf(fp, "End\n");
    fclose(fp);

    ///////// SOLVER
    error = GRBloadenv(&env, "wcet_fixed.log");
    if (error || env == NULL)
    {
        fprintf(stderr, "Error: could not create environment\n");
        exit(1);
    }
    GRBsetintparam(env, GRB_INT_PAR_OUTPUTFLAG, 1);

    error = GRBreadmodel(env, "wcet_fixed.lp", &model);
    if (error)  quit(error, env);

    error = GRBoptimize(model);
    if (error)
         quit(error, env);
    
    error = GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status);
    if (error)  quit(error, env);
    
    objVal = 0;
    if (status == GRB_OPTIMAL || status == GRB_TIME_LIMIT) {
        int varIdx;
        static char *W0 = "W0";
        error = GRBgetvarbyname(model, W0, &varIdx);
        if (error)  quit(error, env);
        error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdx, &objVal);
 
        if (objVal == 0) {
            GRBfreemodel(model);
            GRBfreeenv(env);

            FREESTACK
        }

/////////////
        int* dist = (int*)calloc(nNode, sizeof(int));
        BBType** tpoSortedNodes = (BBType**)malloc(sizeof(BBType)*nNode);
        int tpoStartIdx = 0;

        initVisited();

        tpoStartIdx = TPOvisit(rootNode, tpoSortedNodes, 0);

        int maxDist = 0;
        BBType* maxDistNode;
#ifndef LVZERO
        dist[rootNode->ID] = rootNode->N * rootNode->S + Cdma(rootNode->EC);
#endif

        int i;
        for (i = tpoStartIdx-2; i >= 0; i--) {
            node = tpoSortedNodes[i];

            int maxCost = -1;

            BBListEntry* predEntry = node->predList;
            while (predEntry) {
                BBType* predNode = predEntry->BB;
                if (dist[predNode->ID] > maxCost) {
                    maxCost = dist[predNode->ID];
                }
                predEntry = predEntry->next;
            }

            dist[node->ID] = maxCost + (node->N * node->S);
#ifndef LVZERO
            if (node->CS==1 && node->N>0) {
                int nCall = getNCall(node);
                int bAM = 0;
                // CODE 
                for (f = 0; f < nFunc; f++) {
                    if (f == node->EC)
                        continue;

                    if (node->IS[node->EC][f]==1) {
                        // Are they mapped to the same region?
                        if (M[f] == M[node->EC])
                            bAM = 1;
                    }
                }
                if (bAM == 1 && nCall > 0) {
                    if (isNodeRT(node) == 1)
                        dist[node->ID] += nCall * (Cdma(node->EC) + 10);
                    else
                        dist[node->ID] += nCall * (Cdma(node->EC) + 4);
                }
                else {
                    if (node->bFirst) {
                        if (nCall == 1) {
                            if (isNodeRT(node) == 1)
                                dist[node->ID] += nCall * (Cdma(node->EC) + 10);
                            else
                                dist[node->ID] += nCall * (Cdma(node->EC) + 4);
                        }
                        else if (nCall > 1) {
                            if (isNodeRT(node) == 1)
                                dist[node->ID] += Cdma(node->EC) + (nCall-1) * 2 + 14;
                            else
                                dist[node->ID] += Cdma(node->EC) + (nCall-1) * 2 + 8;
                        }
                    }
                }
            }
#endif
            if (dist[node->ID] > maxDist) {
                maxDist = dist[node->ID];
                maxDistNode = node;
            }
        }

        initVisited();

        int nSPMAM = 0, nSPMFM = 0;
        int totalSPMCost = 0;
        
        int totalMGMTCost = 0;
        int WCETwithoutSPM = 0;


        float* fCostExFM = (float*)malloc(sizeof(float) * nFunc);
        for (i = 0; i < nFunc; i++)
            fCostExFM[i] = 0;

        int CostExFM = 0;
        
        int CC, WC, MC; // computation cost, waiting cost, management cost
        pushBB(maxDistNode, &stack);
        while ((node = popBB(&stack))) {
            if (node->bVisited == 1)
                continue;
            node->bVisited = 1;
        
            int distMax = -1;
            BBType *maxDistPredNode;
            BBListEntry* predEntry = node->predList;
            while (predEntry) {
                BBType *predNode = predEntry->BB;
                if (dist[predNode->ID] >= distMax) {
                    maxDistPredNode = predNode;
                    distMax = dist[predNode->ID];
                }

                predEntry = predEntry->next;
            }

            int varIdxNode;
            static char varNameNode[32];
            memset(varNameNode, 0, 32);
            sprintf(varNameNode, "W%d", node->ID);
            error = GRBgetvarbyname(model, varNameNode, &varIdxNode);
            if (error)  quit(error, env);
                
            double wNode;
            error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdxNode, &wNode);

            sprintf(varNameNode, "L%d", node->ID);
            error = GRBgetvarbyname(model, varNameNode, &varIdxNode);
               
            double lNode;
            error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdxNode, &lNode);

            CC = node->N*node->S;

            WCETwithoutSPM += CC;
            if (dbgFlag)
                printf("node %d: EC: %d, N = %d, S= %d\n", node->ID, node->EC, node->N, node->S);

            WC = 0;
            MC = 0;
#ifndef LVZERO
            if (node->CS==1 && node->N>0) {
                nCall = getNCall(node);
                int bAM = 0;
                if (dbgFlag)
                    printf("CODE: [");
                for (f = 0; f < nFunc; f++) {
                    if (dbgFlag) {
                        if (node->IS[node->EC][f] == 1)
                            printf(" %d", f);
                    }
                    if (f == node->EC)
                        continue;
 
                    if (node->IS[node->EC][f]==1) {
                        // Are they mapped to the same region?
                        if (M[f] == M[node->EC])
                            bAM = 1;
                    }
                }
                if (dbgFlag)
                    printf(" ]\n");
                if (bAM == 1 && nCall > 0) {
                    nSPMAM++;
                    if (isNodeRT(node) == 1) {
                        WC = nCall * Cdma(node->EC);
                        MC = nCall * 4;
                    }
                    else {
                        WC = nCall * Cdma(node->EC);
                        MC = nCall * 5;
                    }

                    int max = 0;
                    for (f = 0; f < nFunc; f++) {
                        if (f == node->EC)
                            continue;
                        if (node->IS[node->EC][f] == 1 && functions[f].size > max)
                            max = functions[f].size;
                    }
                    if (max+functions[node->EC].size <= SPMSIZE) {
                        fCostExFM[node->EC] += WC+MC;
                        CostExFM += WC+MC;
                    }
                }
                else {
                    if (node->bFirst) {
                        if (nCall == 1) {
                            nSPMAM++;
                            if (isNodeRT(node) == 1) {
                                WC = nCall * Cdma(node->EC);
                                MC = nCall * 4;
                            }
                            else {
                                WC = nCall * Cdma(node->EC);
                                MC = nCall * 5;
                            }
                        }
                        else if (nCall > 1) {
                            nSPMFM++;
                            if (isNodeRT(node) == 1) {
                                WC = Cdma(node->EC);
                                MC = (nCall-1) * 4 + 11;
                            }
                            else {
                                WC = Cdma(node->EC);
                                MC = (nCall-1) * 4 + 12;
                            }
                        }
                    }
                }
            }
            else if (node == rootNode) {
                WC = Cdma(rootNode->EC);
            }
#endif
            if (dbgFlag) {
                if (WC > 0 || MC > 0)
                    printf("   WC: %d, MC: %d\n", WC, MC);
            }
            totalSPMCost += WC;
            totalMGMTCost += MC;

            if ((long long int)(lNode+0.5) != (WC+MC)) {
                printf("Loading cost doesn't match at node %d %s %d from %d, %d times. Estimated : %d vs. Calculated: %lld\n", node->ID, (isNodeRT(node) == 1) ? "returning to":"calling", node->EC, node->predList->BB->EC, nCall, (WC+MC), (long long int)(lNode+0.5));
                printf("W:%d, M: %d\n", WC, MC);
            }
 
            pushBB(maxDistPredNode, &stack);
        }

        float avgSkewCost = 0;
        int nCaseAdded = 0;
        float maxSkewCost = 0;
        if (CostExFM > 0) {
            printf("CostExFM: %d\n", CostExFM);
            for (i = 0; i < nFunc; i++) {
                if (fCostExFM[i] > 0) {
                    printf("function%d costExFM: %f sizeSkew: %d/%d = %f skewCost: %f\n", i, fCostExFM[i], RS[M[i]], functions[i].size, ((float)RS[M[i]])/functions[i].size, fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size);
                    avgSkewCost += fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size;
                    if (maxSkewCost < (fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size))
                        maxSkewCost = fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size;
                    nCaseAdded++;
                }
            }
        }
        avgSkewCost /= nCaseAdded;

        float stdDev = 0;
        if (CostExFM > 0) {
            for (i = 0; i < nFunc; i++) {
                if (fCostExFM[i] > 0) {
                    float diff = avgSkewCost - (fCostExFM[i]/CostExFM * ((float)RS[M[i]])/functions[i].size);
                    diff *= diff;
                    stdDev += diff;
                }
            }

            stdDev /= nCaseAdded;
            stdDev = sqrt(stdDev);

            printf("\n\n");
            printf("maxSkewCost: %f\n", maxSkewCost);
            printf("avgSkewCost: %f\n", avgSkewCost);
            printf("stdDev: %f\n\n", stdDev);
        }
        free(RS);
        free(fCostExFM);

        if ((long long int)objVal != WCETwithoutSPM+totalSPMCost+totalMGMTCost) {
            printf("Costs do not match. %lld vs %d\n", (long long int)objVal, WCETwithoutSPM+totalSPMCost+totalMGMTCost);
        }
        printf("WCET: %lld (C: %lld (%.2f%%), W: %lld (%.2f%%), M: %lld (%.2f%%))\n", (long long int)objVal, (long long int)WCETwithoutSPM, WCETwithoutSPM/objVal*100, (long long int)totalSPMCost, totalSPMCost/objVal*100, (long long int)totalMGMTCost, totalMGMTCost/objVal*100);
        free(tpoSortedNodes);
        free(dist);
    }

    if (error)
    {
        printf("ERROR: %s\n", GRBgeterrormsg(env));
        exit(1);
    }

    GRBfreemodel(model);
    GRBfreeenv(env);

    free(M);
    FREESTACK
}

