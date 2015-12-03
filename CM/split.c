#include "CM.h"
#include "CFG.h"
#include "DFS.h"
#include "CFG_traversal.h"
#include "loop.h"
#include "split.h"
#include "util.h"

#define CUT_SESE

typedef struct _instType {
    int addr;
    int bCall;
    int bPCrel;
    int refTarget;
    int bData;
} instType;

instType* instructions = NULL;
int codeSize = -1;
int dataSize = 0;

int fToSplit = -1;

int nPartition = 0;

int nonLiteralExitPoint = -1;

extern int SPMSIZE;

void fs_readInstructions(FILE *fp)
{
    if (fp == NULL) {
        exit(1);
    }
    if (instructions) {
        free(instructions);
        instructions = NULL;
    }

    fscanf(fp, "%d\n", &codeSize);

    instructions = (instType*)malloc(sizeof(instType) * codeSize);

    int idx = 0;
    int PC = 4;
    int bData;
    unsigned int inst;

    for (idx = 0; idx < codeSize; idx++) {
        fscanf(fp, "%d %x\n", &bData, &inst);

        PC += 4;

        instructions[idx].addr = PC - 8;
        instructions[idx].bCall = 0;
        instructions[idx].bPCrel = 0;
        instructions[idx].refTarget = -1;
        instructions[idx].bData = bData;
        if (bData)
            dataSize++;

        int offset;

        // is PC-relative load/store?
        if ((inst & 0x0C000000) >> 26 == 1) {
            if ((inst & 0x000F0000) >> 16 == 0xF) {
                instructions[idx].bPCrel = 1;
                //printf("PC-relative ld/st @ %d (%x)\n", PC-8, inst); 

                if ((inst & 0x02000000) >> 25 == 1) {
                    //printf("instruction %d-0x%x: register-based offset cannot be calculated in compile-time\n", idx, inst);
                }
                else {
                    offset = ((unsigned int)inst << 20 >> 20);
                    if ((inst & 0x00800000) >> 23 == 0)
                        offset = 0 - offset;
                    //printf("offset: %d\n", offset);
                    //printf("idx %d: PC + offset = %d + %d = %d (0x%x)\n", idx, PC, offset, PC+offset, PC+offset);
                    //printf("-------------------------------\n");
                    instructions[idx].refTarget = PC+offset;
                }
            }
        }
        // pc-relative data-processing
        else if ((inst & 0x0C000000) >> 26 == 0) {
            if ((inst & 0x000F0000) >> 16 == 0xF) {
                instructions[idx].bPCrel = 1;

                if ((inst & 0x02000000) >> 25 == 1) {
                    offset = (inst & 0x000000FF) >> (2*((inst & 0x00000F00) >> 8)) | (inst & 0x000000FF) << (32-(2*((inst & 0x00000F00) >> 8)));
                    instructions[idx].refTarget = PC+offset;
                }
                else {
                    //printf("instruction %d-0x%x: register-based offset cannot be calculated in compile-time\n", idx, inst);
                }
            }
        }
        // load multiple
        else if ((inst & 0x0E000000) >> 25 == 4) {
            if ((inst & 0x000F0000) >> 16 == 0xF) {
                // do nothing because it is only accessing pc+8
                printf("pc-relative ldr/str multiple\n");
            }
        }
        // branches have no cost since the offset can be directly modified
        // however, we mark them so that we can estimate the function size after inserting management code
        else if ((inst & 0x0F000000) >> 24 == 0xb) {
            int targetAddr = ((int)inst << 8 >> 6) + (long long int)PC; 
            instructions[idx].refTarget =  targetAddr;
            if (targetAddr + functions[fToSplit].addrRange[0].startAddr > 0) {
                instructions[idx].bCall = 1;
            }
            else {
#ifdef DEBUG_SKIP
                printf("target address %lld is considered as library call, so is ignored\n", targetAddr + functions[fToSplit].addrRange[0].startAddr);
#endif
            }
        }
    }
}

// obsolete
#if 0
void fs_getExecutionCounts()
{
    if (fToSplit == -1 || instructions == NULL) {
        printf("instructions are not read yet\n");
        exit(1);
    }

    int idx;
    for (idx = 0; idx < functions[fToSplit].nOccurrence; idx++) {
        BBType* entryNode = functions[fToSplit].entryPoints[idx];
        BBType* exitNode = functions[fToSplit].exitPoints[idx];

        int baseN = entryNode->N;

        int bIdx;
        for (bIdx = entryNode->ID; bIdx <= exitNode->ID; bIdx++) {
            int relAddr = (int)(nodes[bIdx]->addr - functions[fToSplit].addrRange[0].startAddr);
            int instIdx;

            // find the instructions inside this node
            for (instIdx = relAddr/4; instIdx < relAddr/4+nodes[bIdx]->S; instIdx++) {
                if (instructions[instIdx].N < (nodes[bIdx]->N+baseN-1)/baseN) {
                    //printf("Execution count of insruction %d is updated to %d\n", instIdx, node->N);
                    instructions[instIdx].N = (nodes[bIdx]->N+baseN-1)/baseN;
                }
            }
        }
    }
}

void fs_findMatchingNodeIDs()
{
    BBType *entryNode = functions[fToSplit].entryPoints[0];
    BBType *exitNode = functions[fToSplit].exitPoints[0];

    int bIdx;
    for (bIdx = entryNode->ID; bIdx <= exitNode->ID; bIdx++) {
        BBType* node = nodes[bIdx];
        int relAddr = (int)(node->addr - functions[fToSplit].addrRange[0].startAddr);

        int iIdx;
        for (iIdx = relAddr/4; iIdx < relAddr/4+node->S; iIdx++)
            instructions[iIdx].matchingNodeID = bIdx;
    }
}
#endif //obsolete

// find all contiguous address ranges where nodes are.
int fs_findAddrRanges(BBListEntry* partitionNodeList, addrRangeType** addrRange)
{
    BBListEntry* entry = partitionNodeList;

    int nAddrRange = 0;
    if (*addrRange != NULL)
        free(*addrRange);

    *addrRange = (addrRangeType*)malloc(sizeof(addrRangeType));
    if (*addrRange == NULL) {
        printf("fs_findAddrRanges: mem alloc error\n");
        exit(1);
    }

    addrRangeType* curRange = &((*addrRange)[0]);

    curRange->startAddr = entry->BB->addr;
    curRange->size = 0;

    while (entry) {
        BBType* node = entry->BB;

        curRange->size += node->S*4;

        if (entry->next) {
            BBType* nextNode = entry->next->BB;
            // check if there's a gap between this node and the next node
            // a gap --> current address range ends, and another range starts.
            if (nextNode->addr > node->addr + node->S*4) {
                nAddrRange++;

                addrRangeType* newAddrRange = (addrRangeType*)realloc(*addrRange, sizeof(addrRangeType)*(nAddrRange+1));
                if (newAddrRange == NULL) {
                    printf("fs_findAddrRanges: mem realloc error\n");
                    exit(1); 
                }
                *addrRange = newAddrRange;

                curRange = &((*addrRange)[nAddrRange]);
                curRange->startAddr = nextNode->addr;
                curRange->size = 0;
            } 
        }
        else {
            // an end of the list means that the current address range ends.
            nAddrRange++;
        }
        
        entry = entry->next;
    }

    if (nAddrRange == 0) {
        printf("fs_findAddrRanges: no address range is found\n");
        free(*addrRange);
    }

    return nAddrRange;
}

int fs_createNewPartition(int size, long long int startAddr)
{
    int c;
    for (c= 0; c < functions[fToSplit].nChildren; c++) {
        int child = functions[fToSplit].childrenIDs[c];

        if (functions[child].addrRange[0].startAddr == startAddr)
            return child;
    }

    funcType* newFunctions = (funcType*)realloc(functions, sizeof(funcType) * (nFunc+1));
    if (newFunctions == NULL) {
        printf("mem alloc error @fs_createNewPartition\n");
        exit(1);
    }
    functions = newFunctions;
    nFunc++;

    int newFuncIdx = nFunc-1;
    printf("New partition created. ID: %d parent: %d\n", newFuncIdx, fToSplit); 

    functions[fToSplit].nChildren++;
    int* newChildrenIDs = (int*)realloc(functions[fToSplit].childrenIDs, sizeof(int) * functions[fToSplit].nChildren);
    newChildrenIDs[functions[fToSplit].nChildren - 1] = newFuncIdx;

    functions[fToSplit].childrenIDs = newChildrenIDs;

    functions[newFuncIdx].addrRange = NULL;
    functions[newFuncIdx].nAddrRange = 0;

    functions[newFuncIdx].parent = fToSplit;
    functions[newFuncIdx].size = size;
    functions[newFuncIdx].nChildren = 0;
    functions[newFuncIdx].childrenIDs = NULL;
    functions[newFuncIdx].nOccurrence = functions[newFuncIdx].nOccurrence;
    functions[newFuncIdx].entryPoints = NULL;
    functions[newFuncIdx].exitPoints = NULL;

    return newFuncIdx;
} 

int fs_findLoopBody(int startIdx)
{
    int nIdx;

    int functionStartIdx = functions[fToSplit].entryPoints[0]->ID;
    int functionEndIdx = functions[fToSplit].exitPoints[0]->ID;

    if (!(functionStartIdx <= startIdx && startIdx < functionEndIdx)) {
        printf("loop start idx %d is not within a range of function %d\n", startIdx, fToSplit);
        return -1;
    }

    BBType* loopHead = nodes[startIdx];

    if (loopHead->bLoopHead != 1) {
        return -1;
    }

    BBType* loopTail = NULL;
    BBListEntry* loopTailEntry = loopHead->loopTailList;
    while (loopTailEntry) {
        if (loopTail == NULL || loopTail->ID < loopTailEntry->BB->ID)
            loopTail = loopTailEntry->BB;

        loopTailEntry = loopTailEntry->next;
    }

    if (loopTail == NULL) {
        printf("Can't find a loop tail for a loop starting at node %d\n", loopHead->ID);
        return -1;
    }

    int loopSize = (int)((nodes[loopTail->ID]->addr + nodes[loopTail->ID]->S) - nodes[loopHead->ID]->addr);

    BBListEntry* partitionNodes = NULL;

    // find inner loops recursively
    // find the list of nodes that constitue the loop body except the nodes that belong to inner loops
    for (nIdx = loopHead->ID+1; nIdx < loopTail->ID; nIdx++) {
        BBType* node = nodes[nIdx];

        if (node->bUnreachable == 1 || node->bLiteralPool == 1)
            continue;

        if (node->bLoopHead == 1) {
            int innerLoopTailID = fs_findLoopBody(nIdx);

            printf("loopSize originally %d\n", loopSize); 
            loopSize -= ((nodes[innerLoopTailID]->addr + nodes[innerLoopTailID]->S) - node->addr);

            nIdx = innerLoopTailID + 1;
        }
    }

    printf("Loop from node %d to node %d, size: %d bytes\n", loopHead->ID, loopTail->ID, loopSize);
    return loopTail->ID;
}

void fs_cutLoopBodies()
{
    int startNodeID, endNodeID;
    int nIdx;

    startNodeID = functions[fToSplit].entryPoints[0]->ID;
    endNodeID = functions[fToSplit].exitPoints[0]->ID;

    printf("function %d from %d to %d, size: %d bytes\n", fToSplit, startNodeID, endNodeID, functions[fToSplit].size);
    for (nIdx = startNodeID; nIdx <= endNodeID; nIdx++) {
        BBType *node = nodes[nIdx];

        if (node->bUnreachable == 1 || node->bLiteralPool == 1)
            continue;

        if (node->bLoopHead == 1) {
            nIdx = fs_findLoopBody(nIdx) + 1;
        }
    }
}

// check if there's an incoming edge from another partition to the 'node' 
// partitionNodeList - the list of nodes in this partition
// returns true/false
int fs_checkCSatNode(BBListEntry* partitionNodeList, BBType* node)
{
    if (node == NULL || partitionNodeList == NULL)
        return 0;

    BBListEntry* predEntry = node->predList;
    while (predEntry) {
        BBType* predNode = predEntry->BB;

        if (isBBInNodeList(predNode, partitionNodeList) == 0)
            return 1;

        predEntry = predEntry->next;
    }

    return 0;
}

// check if there's an outgoing edge from 'node' to other partitions 
// partitionNodeList - the list of nodes in this partition
// returns true/false
int fs_checkCallToOtherPartition(BBListEntry* partitionNodeList, BBType* node)
{
    if (node == NULL || partitionNodeList == NULL)
        return 0;

    BBListEntry* succEntry = node->succList;
    while (succEntry) {
        BBType* succNode = succEntry->BB;

        if (succNode->EC == fToSplit) {
            if (isBBInNodeList(succNode, partitionNodeList) == 0)
                return 1;
        }

        succEntry = succEntry->next;
    }

    return 0;
}

int fs_getNonLiteralExitPoint(int functionID)
{
    if (functions[functionID].exitPoints == NULL)
        return -1;

    int nIdx;
    for (nIdx = functions[functionID].exitPoints[0]->ID; nIdx >= functions[functionID].entryPoints[0]->ID; nIdx--) {
        if (nodes[nIdx]->bLiteralPool == 0)
            break;
    }

    return nIdx;
}

int fs_getPartitionSize(BBListEntry* partitionNodeList)
{
    int partitionSize = 0;
    int iIdx;

    int* targetAddrList = (int*)calloc(dataSize, sizeof(int));
    int nTarget = 0;

    BBListEntry* entry = partitionNodeList;
    while (entry) {
        BBType* node = entry->BB;
        partitionSize += node->S;

        for (iIdx = 0; iIdx < node->S; iIdx++) {
            int instIdx = iIdx + (node->addr - functions[fToSplit].entryPoints[0]->addr)/4;

            // every accessesd literal pool is duplicated and attached at the end of the partition
            // find non-redundant target addresses of PC relative load/store
            if (instructions[instIdx].bPCrel == 1) {
                int refTarget = instructions[instIdx].refTarget/4;

                //printf("relative at idx %d targeting %d\n", instIdx, refTarget/4);

                int tIdx;
                int bFound = 0;
                for (tIdx = 0; tIdx < nTarget; tIdx++) {
                    if (targetAddrList[tIdx] == refTarget) {
                        bFound = 1;
                    }
                }
                if (bFound == 0) {
                    targetAddrList[nTarget++] = refTarget;
                    partitionSize++;
                }
            }
            // call needs at most 5 instructions for management
            else if (instructions[instIdx].bCall == 1) {
                partitionSize += 5;
            }
        }

        // check call to other partitions
        int nEdgeGoingOutside = fs_checkCallToOtherPartition(partitionNodeList, node);
        partitionSize += (nEdgeGoingOutside * 4);

        entry = entry->next;
    }

    free(targetAddrList);

    // management code for function return (only at the end of the original function)
    if (fToSplit != rootNode->EC) {
        if (getBBListTail(partitionNodeList)->ID == nonLiteralExitPoint)
            partitionSize += 4;
    }

    partitionSize *= 4;
    return partitionSize;
}

void fs_cutPath(BBListEntry* partitionNodeList, int partitionID, int partitionSize)
{
    // if ( ) then path A
    //        else path B
    // Here we're trying to form a new partition for one of these paths

    BBListEntry* newPartitionNodeList = NULL;
    int newPartitionID = -1;
    int newPartitionSize = 0;
    // ratio is (new partitionsize)/(the original partition size).
    // ratio is in the range of (0, 1]
    // We want to keep the sizes of two partitions as equal as possible. To do that, we use ratioDev.
    // ratioDev is deviation from 0.5, thus |ratio-0.5|.
    // ratioDev is [0.5 - 0]. 0.5 is the worst-case since the new partition size is either zero or almot the same as the original one.
    // 0 is the best case meaning that the new partition size is exactly a half of the original size.
    float bestRatioDev = 0.5;

    // A partition should be an SESE (single-entry single-exit) partition
    //  to reduce the overhead..
    // If not SESE, the overhead can grow significantly
    //  This is because, if a mapping cannot avoid reloading this partition, 
    //  the cost of reload will be added at every node that has an incoming edge from another partition

    BBListEntry* entry = partitionNodeList->next;
    while (entry) {
        // Find a node with two successors (conditional branch)
        // We assume that in the input graph, a node can have at most two successors (therefore, we do not support function pointers)
        if (entry->BB->succList == NULL || entry->BB->succList->next == NULL) {
            entry = entry->next;
            continue;
        }
        
        BBType* head; // the starting node of the new partition
        BBType* head2; // this is the starting node of the other path

        if (entry->BB->succList->BB->ID > entry->BB->succList->next->BB->ID) {
            head = entry->BB->succList->next->BB;
            head2 = entry->BB->succList->BB;
        }
        else {
            head = entry->BB->succList->BB;
            head2 = entry->BB->succList->next->BB;
        }

        // all nodes with ID [head->ID, ..., head2->ID) should be the partition
        //  check the partition is SESE
        if (isUdomV(head, nodes[head2->ID-1]) && isVpdomU(head, nodes[head2->ID-1])) {
            BBListEntry* tempNodeList = NULL;
            int nIdx;
            for (nIdx = head->ID; nIdx < head2->ID; nIdx++)
                addBBToList(nodes[nIdx], &tempNodeList);

            // check if this partition is larger than the previous one
            int tempSize = fs_getPartitionSize(tempNodeList);

            float ratio = ((float)tempSize)/partitionSize;
            float ratioDev = ((ratio - 0.5) > 0) ? ratio-0.5 : 0.5-ratio;

            printf("ratio is %f from %d to %d \n", ratio, head->ID, head2->ID-1);
            if (ratioDev < bestRatioDev) {
                freeBBList(&newPartitionNodeList);
                newPartitionNodeList = tempNodeList;
                newPartitionSize = tempSize;
                bestRatioDev = ratioDev;
            }
            else
                freeBBList(&tempNodeList);
        }

        //entry = entry->next;
        // skip the path we just tried and jump to the head2
        entry = findEntryInNodeList(head2, partitionNodeList);
    }

    if (newPartitionSize > 0) {
        printf("Partition %d is split into two.\n", partitionID);

        BBListEntry* newPartitionNodeList2 = NULL; // the rest of the nodes in the original partition
        // adjust the size of the existing partition
        entry = partitionNodeList;
        while (entry) {
            if (isBBInNodeList(entry->BB, newPartitionNodeList) == 0)
                addBBToList(entry->BB, &newPartitionNodeList2);

            entry = entry->next;
        }

        functions[partitionID].size = fs_getPartitionSize(newPartitionNodeList2);
        functions[partitionID].nAddrRange = fs_findAddrRanges(newPartitionNodeList2, &(functions[partitionID].addrRange));
        printf("\tPartition %d is from node %d to node %d, and its size is %d\n", partitionID, getBBListHead(newPartitionNodeList2)->ID, getBBListTail(newPartitionNodeList2)->ID, functions[partitionID].size);

        freeBBList(&newPartitionNodeList2);

        // make a new partition for the new partition
        nPartition++;
        newPartitionID = fs_createNewPartition(newPartitionSize, newPartitionNodeList->BB->addr); 
        functions[newPartitionID].size = newPartitionSize;
        functions[newPartitionID].nAddrRange = fs_findAddrRanges(newPartitionNodeList, &(functions[newPartitionID].addrRange));
        printf("\tPartition %d is from node %d to node %d, and its size is %d\n", newPartitionID, getBBListHead(newPartitionNodeList)->ID, getBBListTail(newPartitionNodeList)->ID, newPartitionSize);

        entry = newPartitionNodeList;
        while (entry) {
            entry->BB->EC = newPartitionID;

            if (fs_checkCSatNode(newPartitionNodeList, entry->BB) == 1)
                entry->BB->CS = 1;

            entry = entry->next;
        }
    }

    freeBBList(&newPartitionNodeList);
}

BBListEntry* fs_getSESEregion(BBListEntry* nodeList, BBType* node, float ratioGoal)
{
    if (nodeList == NULL || node == NULL)
        return NULL;

    BBListEntry* SESEregion = NULL;

    BBListEntry* entry = nodeList;
    while (entry) {
        if (entry->BB == node)
            break;
        entry = entry->next;
    }

    int totalSize = fs_getPartitionSize(nodeList);

    // if entry == NULL, node is not in the nodeList
    if (entry != NULL) {
        BBListEntry* tempList = NULL;
        float bestRatioDev = 1 - ratioGoal;
        while (entry) {
            addBBToList(entry->BB, &tempList);
            if (isUdomV(node, entry->BB) && isVpdomU(node, entry->BB)) {
                int partSize = fs_getPartitionSize(tempList);
                float ratio = ((float)partSize)/totalSize;
                float ratioDev = ((ratio - ratioGoal) > 0) ? ratio-ratioGoal : ratioGoal-ratio;
                if (ratioDev < bestRatioDev) {
                    freeBBList(&SESEregion);
                    SESEregion = duplicateBBList(tempList);
                }
            }

            entry = entry->next;
        }
    }

    return SESEregion;
}

void fs_cutSESE(BBListEntry* partitionNodeList, int partitionID)
{
    // A partition should be an SESE (single-entry single-exit) partition
    //  to reduce the overhead..
    // If not SESE, the overhead can grow significantly
    //  This is because, if a mapping cannot avoid reloading this partition, 
    //  the cost of reload will be added at every node that has an incoming edge from another partition
    
    if (partitionNodeList == NULL || partitionNodeList->BB == NULL) {
        printf("Input partitionNodeList is NULL\n");
        return;
    }

    BBType* funcTail = getBBListTail(partitionNodeList);

    // 1st SESE region (This replaces the original function, and the next SESE regions will form new partitions.)
    BBListEntry* SESEregion = fs_getSESEregion(partitionNodeList, partitionNodeList->BB, 0.5);

    BBType* partTail = getBBListTail(SESEregion);
    if (partTail == NULL) {
        printf("Cannot find an SESE region from the function head\n");
        return;
    }

    int partSize;
    if (partTail != funcTail) {
        partSize = fs_getPartitionSize(SESEregion);
        functions[partitionID].size = partSize;
        functions[partitionID].nAddrRange = fs_findAddrRanges(SESEregion, &(functions[partitionID].addrRange));

        printf("Partition %d is split.\n", partitionID);
        printf("\tPartition %d is from node %d to node %d, and its size is %d\n", partitionID, getBBListHead(SESEregion)->ID, getBBListTail(SESEregion)->ID, partSize);

        freeBBList(&SESEregion);

        // next SESE regions
        BBListEntry* entry = findEntryInNodeList(partTail, partitionNodeList);
        if (entry == NULL) {
            printf("Cannot find an entry containing BB %d in the list. Critical error\n", partTail->ID);
            exit(1);
        }
        entry = entry->next;
        while (entry) {
            SESEregion = fs_getSESEregion(partitionNodeList, entry->BB, 0.5);

            if (SESEregion != NULL) {
                // new partition
                nPartition++;
                int partSize = fs_getPartitionSize(SESEregion);
                int partID = fs_createNewPartition(partSize, SESEregion->BB->addr); 

                functions[partID].nAddrRange = fs_findAddrRanges(SESEregion, &(functions[partID].addrRange));

                printf("\tPartition %d is from node %d to node %d, and its size is %d\n", partID, getBBListHead(SESEregion)->ID, getBBListTail(SESEregion)->ID, partSize);

                BBListEntry* partitionEntry = SESEregion;
                while (partitionEntry) {
                    partitionEntry->BB->EC = partID;

                    if (fs_checkCSatNode(SESEregion, partitionEntry->BB) == 1)
                        partitionEntry->BB->CS = 1;

                    partitionEntry = partitionEntry->next;
                }

                partTail = getBBListTail(SESEregion);
                freeBBList(&SESEregion);
                entry = findEntryInNodeList(partTail, partitionNodeList);
                if (entry == NULL) {
                    printf("Cannot find an entry containing BB %d in the list. Critical error\n", partTail->ID);
                    exit(1);
                }
                entry = entry->next;
            }
            else {
                printf("Cannot find an SESE region after splitting the original function. Critical error\n");
                exit(1);
                break;
            }
        }
    }
}

#if 0 //obsolete
int fs_cutInHalf(BBListEntry* partitionNodeList, int partitionID, int partitionSize)
{
    int newPartitionID;
    int newPartitionSize;

    // A partition should be an SESE (single-entry single-exit) partition
    //  to reduce the overhead..
    // If not SESE, the overhead can grow significantly
    //  This is because, if a mapping cannot avoid reloading this partition, 
    //  the cost of reload will be added at every node that has an incoming edge from another partition

    BBType* bestCutPoint = NULL;
    // bestRatioDev is the deviation from 0.5 of the ratio of the size of the first partition size 
    //   divided by the original partition size, when a cut is made at the "bestCutPoint"
    //  thus, bestRatioDev = |firstHalfSize/partitionSize - 0.5|
    float bestRatioDev = 0.5;   

    BBListEntry* newPartitionNodeList = NULL;
    BBType* head = partitionNodeList->BB;
    addBBToList(head, &newPartitionNodeList);

    BBListEntry* entry = partitionNodeList->next;
    while (entry) {
        BBType* node = entry->BB;
    
        addBBToList(node, &newPartitionNodeList);

        if (isUdomV(head, node) && isVpdomU(head, node)) {
            int firstHalfSize = fs_getPartitionSize(newPartitionNodeList);
            float firstHalfRatio = ((float)firstHalfSize)/partitionSize;
            float ratioDev = ((firstHalfRatio - 0.5) > 0) ? firstHalfRatio-0.5 : 0.5-firstHalfRatio;

            if (ratioDev < bestRatioDev) {
                bestCutPoint = node;
                bestRatioDev = ratioDev;
            }
        }

        entry = entry->next;
    }

    freeBBList(&newPartitionNodeList);

    //if (bestRatioDev > 0.3) {
    //   return -1;
    //}


    int bAlreadyCut = 0;

    // The first half
    entry = partitionNodeList;
    while (entry) {
        BBType* node = entry->BB;

        addBBToList(node, &newPartitionNodeList);
        if (bAlreadyCut == 0) {
            if (node == bestCutPoint) {
                newPartitionSize = fs_getPartitionSize(newPartitionNodeList);

                functions[partitionID].size = newPartitionSize;
                functions[partitionID].nAddrRange = fs_findAddrRanges(newPartitionNodeList, &(functions[partitionID].addrRange));
                
                printf("Partition %d is split into two.\n", partitionID);
                printf("\tPartition %d is from node %d to node %d, and its size is %d\n", partitionID, getBBListHead(newPartitionNodeList)->ID, getBBListTail(newPartitionNodeList)->ID, newPartitionSize);
                nPartition++;

                bAlreadyCut = 1;

                BBListEntry* partitionEntry = newPartitionNodeList;
                while (partitionEntry) {
                    if (fs_checkCSatNode(newPartitionNodeList, partitionEntry->BB) == 1)
                        partitionEntry->BB->CS = 1;

                    partitionEntry = partitionEntry->next;
                }

                freeBBList(&newPartitionNodeList);
            }
        }

        entry = entry->next;
    }

    // The other half
    if (bAlreadyCut == 1) {
        newPartitionSize = fs_getPartitionSize(newPartitionNodeList);
        newPartitionID = fs_createNewPartition(newPartitionSize, nPartition);

        functions[newPartitionID].nAddrRange = fs_findAddrRanges(newPartitionNodeList, &(functions[newPartitionID].addrRange));

        printf("\tPartition %d is from node %d to node %d, and its size is %d\n", newPartitionID, getBBListHead(newPartitionNodeList)->ID, getBBListTail(newPartitionNodeList)->ID, newPartitionSize);

        BBListEntry* partitionEntry = newPartitionNodeList;
        while (partitionEntry) {
            partitionEntry->BB->EC = newPartitionID;

            if (fs_checkCSatNode(newPartitionNodeList, partitionEntry->BB) == 1)
                partitionEntry->BB->CS = 1;

            partitionEntry = partitionEntry->next;
        }
    }

    freeBBList(&newPartitionNodeList);

    return 0;
}
#endif

int fs_makeCuts(int startIdx, int endIdx, int bStart)
{
    if (nodes[startIdx]->EC != fToSplit || nodes[endIdx]->EC != fToSplit) {
        printf("range from %d to %d is not in function %d\n", startIdx, endIdx, fToSplit);
        return 0;
    }

    BBListEntry* partitionNodeList = NULL;

    int nIdx;
    for (nIdx = startIdx; nIdx <= endIdx; nIdx++) {
        BBType* node = nodes[nIdx];

        if (node->bUnreachable == 1 || node->bLiteralPool == 1)
            continue;

        if (node->bLoopHead == 1 && nIdx != startIdx) {
            int loopHeadIdx = node->ID;
            int loopTailIdx = -1;

            BBListEntry* loopTailEntry = node->loopTailList;
            while (loopTailEntry) {
                if (loopTailIdx < loopTailEntry->BB->ID)
                    loopTailIdx = loopTailEntry->BB->ID;
                loopTailEntry = loopTailEntry->next;
            }

            // if the size is less than 1k, no partition.
#ifdef LOOPSIZE_LIMIT
            if (fs_makeCuts(loopHeadIdx, loopTailIdx, 0) > 64)
#else
            if (fs_makeCuts(loopHeadIdx, loopTailIdx, 0))//) > 64)
#endif
                nIdx = loopTailIdx+1;
        }

        addBBToList(nodes[nIdx], &partitionNodeList);
    }

    int partitionSize = fs_getPartitionSize(partitionNodeList);

#ifdef LOOPSIZE_LIMIT
    if (partitionSize > 64) {
#else
    if (partitionSize) { // > 64) {
#endif
        if (bStart == 1) {
            if (partitionSize >= functions[fToSplit].size * 0.75) {
                // initiate dom-pdom analysis
                dom_init();

#ifdef CUT_SESE
                fs_cutSESE(partitionNodeList, fToSplit);
#else
                fs_cutPath(partitionNodeList, fToSplit, partitionSize);
#endif
                //if (fs_cutInHalf(partitionNodeList, fToSplit, partitionSize) == -1)
                //    fs_cutPath(partitionNodeList, fToSplit, partitionSize);

                dom_free();
            }
            else {
                // If the function is split and partitions have been created, adjust the size of the function
                if (nPartition > 1) {
                    functions[fToSplit].size = partitionSize;

                    functions[fToSplit].nAddrRange = fs_findAddrRanges(partitionNodeList, &(functions[fToSplit].addrRange));

                    printf("Function (Partition) %d's size is now shrunk to %d\n", fToSplit, partitionSize);
                }
            }
            // If the function does not have any loop
            //else {
                //fs_cutInHalf(partitionNodeList);
                //fs_makeCutsAtReturns(partitionNodeList);
            //}
        }
        else {
            nPartition++;
            
            int partitionID = fs_createNewPartition(partitionSize, partitionNodeList->BB->addr);

            functions[partitionID].nAddrRange = fs_findAddrRanges(partitionNodeList, &(functions[partitionID].addrRange));

            printf("\tPartition %d is from node %d to node %d, and its size is %d\n", partitionID, getBBListHead(partitionNodeList)->ID, getBBListTail(partitionNodeList)->ID, partitionSize);

            BBListEntry* entry = partitionNodeList;
            while (entry) {
                entry->BB->EC = partitionID;

                if (fs_checkCSatNode(partitionNodeList, entry->BB) == 1)
                    entry->BB->CS = 1;

                entry = entry->next;
            }
        }

        //if (bStart != 1 || (bStart == 1 && nPartition > 1))
        //    printf("partition size: %d (from %d to %d)\n", partitionSize, startIdx, endIdx);
    }
#ifdef LOOPSIZE_LIMIT
    else {
        if (bStart == 1) {
            nPartition++;
            if (nPartition > 1) {
                functions[fToSplit].size = partitionSize;
            }
        }
    }
#endif
    

    freeBBList(&partitionNodeList);

    return partitionSize;
}


int split(int f)
{
    if (functions[f].nOccurrence == 0) {
        return -1;
    }
    if (functions[f].nChildren > 0)
        return -1;

    char filename[32];
    sprintf(filename, "f%d", f);

    fToSplit = f;
    dataSize = 0;
    codeSize = -1;

    nonLiteralExitPoint = fs_getNonLiteralExitPoint(fToSplit);

    printf("----------------------------\n");
    //printf("Before\n");
    printf("Function %d size: %d\n", fToSplit, functions[fToSplit].size);
    //printFunctionCFG();

    FILE *fp = fopen(filename, "r");

    fs_readInstructions(fp);

    fclose(fp);

    //fs_findData();

    int oIdx;
    for (oIdx = 0; oIdx < functions[fToSplit].nOccurrence; oIdx++) {
        printf("\t%d%s instance\n", oIdx, (oIdx == 1) ? "st" : (oIdx == 2) ? "nd" : (oIdx == 3) ? "rd" : "th");
        nPartition = 0;
        fs_makeCuts(functions[fToSplit].entryPoints[oIdx]->ID, functions[fToSplit].exitPoints[oIdx]->ID, 1);    
    }

    printf("----------------------------\n");

    if (instructions) {
        free(instructions);
        instructions = NULL;
    }
    fToSplit = -1;
    codeSize = -1;
    dataSize = 0;

    nonLiteralExitPoint = -1;

    if (nPartition == 0)
        return -1;
    nPartition = 0;

    return 0;
}




