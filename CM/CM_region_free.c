#include "CM.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "loop.h"
#include "util.h"

#include "gurobi_c.h"

#include "DMA.h"

#include "CM_region_free.h"

void output_mapping_result_rf(long long int *FA)
{
    // read user code starting address
    long long int userCodeStartingAddr;
    FILE *fp = fopen("userCodeRange.txt", "r");
    fscanf(fp, "%llx", &userCodeStartingAddr);
    fclose(fp);

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
        fprintf(fp, "%lld\n", FA[p]);
    }

    fclose(fp);
}

long long int cm_rf_optimal(long long int* fCost)
{
    BBType* node;

    INITSTACK(nNode)
    
    GRBenv* env = NULL;
    GRBmodel *model = NULL;
    int error;
    
    double objVal;
    int status;

    int nIdx;
    int v, f, g;
    int nCall;

    FILE* fp = fopen("codemapping.lp", "w");
    if (fp == NULL)
    {
        printf("cannot create the output file\n");
        exit(1);
    }
    
    // Objective function
    fprintf(fp, "Minimize\n");
    fprintf(fp, "W0 ");

    initVisited();
    
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        if (node->CS)
            fprintf(fp, " + BAMC%d", node->ID);

        BBListEntry* succEntry = node->succList;
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    fprintf(fp, "\n");
    
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

        /*
        if (node->N == 0) {
            succEntry = node->succList;
            while (succEntry) {
                fprintf(fp, "W%d - W%d <= 0\n", succEntry->BB->ID, node->ID);
                succEntry = succEntry->next;
            }
            continue;
        }
        */
        //printf("node%d: N %d S %d\n", node->ID, node->N, node->S);

        // Wv's
        if (node->succList == NULL)
            fprintf(fp, "W%d - L%d = %d\n", node->ID, node->ID, node->N*node->S);
        else {
            if (node->succList->next == NULL) {
                BBType* succNode = node->succList->BB;

                fprintf(fp, "W%d - W%d - L%d = %d\n", node->ID, succNode->ID, node->ID, node->N*node->S);
            } 
            else {
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
            //fprintf(fp, "L%d - LC%d - LL%d = 0\n", node->ID, node->ID, node->ID);

            // Code partition loading time
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
                        //fprintf(fp, "LC%d + %d BAMC%d = %d\n", node->ID, FM - AM, node->ID, FM);
                    } else {
                        fprintf(fp, "L%d = %d\n", node->ID, AM);
                        //fprintf(fp, "LC%d = %d\n", node->ID, AM);
                    }
                } else {
                    // if BAM == 0 then L = 0 else L = AM
                    //     L >= AM*BAM
                    fprintf(fp, "L%d - %d BAMC%d = 0\n", node->ID, AM, node->ID);
                    //fprintf(fp, "LC%d - %d BAMC%d = 0\n", node->ID, AM, node->ID);
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

    // A's (The address of each function)
    for (f = 0; f < nFunc; f++) {
        fprintf(fp, "A%d >= 0\n", f);
        fprintf(fp, "A%d <= %d\n", f, SPMSIZE-functions[f].size);
    }

    // Ordering of A's
    for (f = 0; f < nFunc; f++) {
        for (g = 0; g < nFunc; g++) {
            // -M (1-BAf_g) <= Af - Ag <= M BAf_g
            if (f == g)
                continue;
            fprintf(fp, "A%d - A%d - %d BA%d_%d >= -%d\n", f, g, INT_MAX/2, f, g, INT_MAX/2);
            fprintf(fp, "A%d - A%d - %d BA%d_%d <= 0\n", f, g, INT_MAX/2, f, g);
        }
    }
    for (f = 0; f < nFunc-1; f++) {
        for (g = f+1; g < nFunc; g++) {
            fprintf(fp, "BA%d_%d + BA%d_%d = 1\n", f, g, g, f);
        }
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
            for (f = 0; f < nFunc; f++) {
                if (f == node->EC || node->IS[node->EC][f] == 0)
                    continue;

                bInterference = 1;
                // If Af <= AnodeEC ==> Af + functions[f].size <= AnodeEC   ==> BAMC = 0
                // Af + functions[f].size <= An + M BAf_n + M BAMC
                // Af - An - M BAf_n - M BAMC <= -functions[f].size
                fprintf(fp, "A%d - A%d - %d BA%d_%d - %d BAMC%d <= %d\n", 
                        f, 
                        node->EC, 
                        INT_MAX/2,
                        f,
                        node->EC,
                        INT_MAX/2,
                        node->ID,
                        -functions[f].size);

                // M (1 - BAMC) + Af + functions[f].size >= An + 1 + M BAf_n + M BAMC
                // Af - An - M BAf_n - M BAMC >= 1 - functions[f].size - M
/*
                fprintf(fp, "A%d - A%d - %d BA%d_%d - %d BAMC%d >= %d\n",
                        f,
                        node->EC,
                        INT_MAX/2,
                        f,
                        node->EC,
                        INT_MAX/2,
                        node->ID,
                        1-functions[f].size-INT_MAX/2);
*/
                // An + functions[n].size <= Af + M BAn_f + M BAMC
                // An - Af - M BAn_f - M BAMC <= - functions[node->EC].size 
                fprintf(fp, "A%d - A%d - %d BA%d_%d - %d BAMC%d <= %d\n", 
                        node->EC,
                        f,
                        //SPMSIZE*2,
                        INT_MAX/2,
                        node->EC,
                        f,
                        INT_MAX/2,
                        node->ID,
                        -functions[node->EC].size);

                // M (1 - BAMC) + An + functions[n].size >= Af + 1 + M BAn_f + M BAMC
                // An - Af - M BAn_f - M BAMC >= 1 - M - functions[n].size
/*
                fprintf(fp, "A%d - A%d - %d BA%d_%d - %d BAMC%d >= %d\n",
                        node->EC,
                        f,
                        INT_MAX/2,
                        node->EC,
                        f,
                        INT_MAX/2,
                        node->ID,
                        1-functions[f].size-INT_MAX/2);
*/
            }
            if (bInterference == 0)
                fprintf(fp, "BAMC%d = 0\n", node->ID);
        }
        BBListEntry* succEntry = node->succList;
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    
    fprintf(fp, "General\n");

    for (f = 0; f < nFunc; f++)
        fprintf(fp, "A%d\n", f);
    
    initVisited();
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

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

    for (f = 0; f < nFunc; f++) {
        for (g = 0; g < nFunc; g++) {
            if (g != f)
                fprintf(fp, "BA%d_%d\n", f,g);
        }
    }

    initVisited();
    
    pushBB(rootNode, &stack);
    while ((node = popBB(&stack))) {
        if (node->bVisited == 1)
            continue;
        node->bVisited = 1;

        if (node->CS) {
            fprintf(fp, "BAMC%d\n", node->ID);
            //fprintf(fp, "BAML%d\n", node->ID);
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
        while (succEntry) {
            pushBB(succEntry->BB, &stack);
            succEntry = succEntry->next;
        }
    }
    
    fprintf(fp, "End\n");
    fclose(fp);

    ///////// SOLVER
    error = GRBloadenv(&env, "codemapping.log");
    if (error || env == NULL) {
        fprintf(stderr, "Error: could not create environment\n");
        exit(1);
    }
    GRBsetintparam(env, GRB_INT_PAR_OUTPUTFLAG, 1);
    //GRBsetintparam(env, GRB_INT_PAR_MIPFOCUS, 1);
    //GRBsetintparam(env, GRB_INT_PAR_PRESOLVE, 2);
/*
    GRBsetintparam(env, GRB_INT_PAR_PREDEPROW, 1);
    GRBsetintparam(env, GRB_INT_PAR_PRESPARSIFY, 1);
    GRBsetintparam(env, GRB_INT_PAR_PREPASSES, 30);
    GRBsetintparam(env, GRB_INT_PAR_VARBRANCH, 2);
    GRBsetintparam(env, GRB_INT_PAR_SCALEFLAG, 2);
    GRBsetdblparam(env, GRB_DBL_PAR_IMPROVESTARTTIME, 300);
    GRBsetdblparam(env, GRB_DBL_PAR_OBJSCALE, 1000);
    GRBsetintparam(env, GRB_INT_PAR_NUMERICFOCUS, 1);
*/
    //GRBsetintparam(env, GRB_INT_PAR_THREADS, 8);
    //GRBsetdblparam(env, GRB_DBL_PAR_TIMELIMIT, 14400);
    GRBsetdblparam(env, GRB_DBL_PAR_TIMELIMIT, 600);

    error = GRBreadmodel(env, "codemapping.lp", &model);
    if (error)  quit(error, env);

    error = GRBoptimize(model);
    if (error)
         quit(error, env);
    
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
            GRBfreemodel(model);
            GRBfreeenv(env);

            FREESTACK
            return -1;
        } 

        long long int *A = (long long int*)malloc(sizeof(long long int) * nFunc); // address mapping
        for (f = 0; f < nFunc; f++) {
            A[f] = -1;
        
            static char varName[32];
            memset(varName, 0, 32);
            sprintf(varName, "A%d", f);
                
            error = GRBgetvarbyname(model, varName, &varIdx);
            if (error)  quit(error, env);
                
            double vVal;
            error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdx, &vVal);
                            
            A[f] = (long long int)vVal;

            if (functions[f].parent == -1)
                printf("Function %d (size %d) is mapped to address %lld. <%lld ... %lld>\n", f, functions[f].size, A[f], A[f], A[f]+functions[f].size-1);
            else
                printf("Function %d (size %d) is a child of function %d and mapped to address %lld. <%lld ... %lld>\n", f, functions[f].size, functions[f].parent, A[f], A[f], A[f]+functions[f].size-1);
        }
        printf("---------------------------------------\n");

        output_mapping_result_rf(A);

/////////////
        int* dist = (int*)calloc(nNode, sizeof(int));
        BBType** tpoSortedNodes = (BBType**)malloc(sizeof(BBType)*nNode);
        int tpoStartIdx = 0;

        initVisited();

        tpoStartIdx = TPOvisit(rootNode, tpoSortedNodes, 0);

        int maxDist = 0;
        BBType* maxDistNode;
        dist[rootNode->ID] = rootNode->N * rootNode->S + Cdma(rootNode->EC);

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
            if (node->CS) {
                int nCall = getNCall(node);
                int bAM = 0;
                // CODE
                for (f = 0; f < nFunc; f++) {
                    if (f == node->EC)
                        continue;

                    if (node->IS[node->EC][f]==1) {
                        // Do addresses overlap?
                        if (A[f] < A[node->EC]) {
                            if (A[f]+functions[f].size > A[node->EC])
                                bAM = 1;
                        }
                        else {
                            if (A[node->EC]+functions[node->EC].size > A[f])
                                bAM = 1;
                        }
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

            //sprintf(varNameNode, "W%d", maxDistPredNode->ID);
            //error = GRBgetvarbyname(model, varNameNode, &varIdxNode);
            //if (error)  quit(error, env);

            //double wPredNode;
            //error = GRBgetdblattrelement(model, GRB_DBL_ATTR_X, varIdxNode, &wPredNode);

            CC = node->N*node->S;

            WCETwithoutSPM += CC;
            if (dbgFlag)
                printf("node %d: EC: %d, N = %d, S= %d\n", node->ID, node->EC, node->N, node->S);

            WC = 0;
            MC = 0;
#ifndef LVZERO
            if (node->CS) {
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
                        // Do addresses overlap?
                        if (A[f] < A[node->EC]) {
                            if (A[f]+functions[f].size > A[node->EC])
                                bAM = 1;
                        }
                        else {
                            if (A[node->EC]+functions[node->EC].size > A[f])
                                bAM = 1;
                        }
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
 
        if ((long long int)objVal != WCETwithoutSPM+totalSPMCost+totalMGMTCost) {
            printf("Costs do not match. %lld vs %d\n", (long long int)objVal, WCETwithoutSPM+totalSPMCost+totalMGMTCost);
        }
        printf("WCET: %lld (C: %lld (%.2f%%), W: %lld (%.2f%%), M: %lld (%.2f%%))\n", (long long int)objVal, (long long int)WCETwithoutSPM, WCETwithoutSPM/objVal*100, (long long int)totalSPMCost, totalSPMCost/objVal*100, (long long int)totalMGMTCost, totalMGMTCost/objVal*100);
        free(tpoSortedNodes);
        free(dist);
        free(A);
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

    FREESTACK

    return (long long int)objVal;
}

