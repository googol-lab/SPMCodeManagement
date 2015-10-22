//
//  CFG.h
//  SPMCodeMappingForWCET
//
//  Created by Yooseong Kim on 1/30/13.
//  Copyright (c) 2013 Yooseong Kim. All rights reserved.
//

#ifndef CFG_H
#define CFG_H

extern int nNode;
extern int nEdge;
extern int nCSNode;

extern struct _BBListEntry* VcNodes; 

#define BBLEVEL_CM

typedef struct _BBType
{
    char* name;
    int ID;
    
    long long int addr;
    int EC; // execution context (ID of the function where this bb belongs)
    int CS; // 1 if execution context changes, 0 otherwise
    int S;
    int N;

    int RT;

    int L;
    int Vc;
    struct _BBType* callee;
    //int nT;
#ifdef BBLEVEL_CM
    int bLoaded;
#endif

    int bLiteralPool;
    //int caller;
    //struct _BBType* loopPreHeader;

    int bLoopHead; // 1 if the node is at the top of a loop
    int bLoopTail; // 1 if the node is at the end of a loop
    int bPreLoop;  // 1 if the node is right before a loop
    //struct _BBType* loopTail;
    struct _BBListEntry* loopTailList;
    struct _BBType* loopHead;
    struct _BBListEntry* preLoopList;

    //int bTerminal;
    //int bLoopExit;
    //struct _BBListEntry* succInLoop;

    int CACHE_AH;
    int CACHE_AM;
    int CACHE_FM;
    
    int bUnreachable;
    
    int ISsize;
    int** IS;
    int bFirst;

    int bHeuristicTried;
    
    int bVisited;
    int bVisitedForFindingLoop;
    
    struct _BBListEntry* predList;
    struct _BBListEntry* succList;
} BBType;

typedef struct _BBListEntry
{
    BBType* BB;
    struct _BBListEntry* next;
} BBListEntry;

typedef struct _addrRangeType
{
    long long int startAddr;
    long long int size;
} addrRangeType;

typedef struct _funcType
{
    addrRangeType* addrRange;
    int nAddrRange;

    int size;

    int parent;
    int nChildren;
    int *childrenIDs;

    int nOccurrence;
    BBType** entryPoints;
    BBType** exitPoints;
} funcType;

typedef struct _GCFGNode
{
    int** reachable;
    int EC;
    int nIn, nOut;
    struct _GCFGNode* in;
    struct _GCFGNode* out;
} GCFGNode;

typedef struct _GCCFGNode
{
    char name[8];
    int type;
    int size;
    struct _GCCFGNode* next;
} GCCFGNode;

typedef struct _GCCFGEdge
{
    char caller[8];
    long long int caller_addr;
    char callee[8];
    long long int callee_addr;
    int niter;
    struct _GCCFGEdge* next;
} GCCFGEdge;

#endif
