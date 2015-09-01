/*
    SPM Simulator
    - Yooseong Kim 
 */

#include <iostream>
#include "base/types.hh"

#include <vector>
#include <stack>
#include "sim/spm.hh"

using namespace std;

///////////////////////////////////////
// SPM simulation
///////////////////////////////////////

SPMSim::SPMSim()
{
    C = L = M = 0;

    if (readCodeMappingInput() == false)
        mode = disable;
    else {
        mode = enable;
        readUserCodeRange();
    }
}
SPMSim::~SPMSim()
{
    if (mode == disable)
        return;

    while(!contextStack.empty()) {
        //cout << contextStack.top() << endl; 
        contextStack.pop();
    }

    printf("------------------------------------------\n");
    printf("Total Execution time: %lld (C: %lld L: %lld M: %lld)\n", C+L+M, C, L, M);
    printf("------------------------------------------\n");
}

SPMSim::AddressRange::AddressRange()
{
    nRanges = 0;
}

SPMSim::AddressRange::~AddressRange()
{
    ranges.clear();
}

void SPMSim::AddressRange::addRange(const long long int &start, const long long int &size)
{
    ranges.push_back(rangeType(start, size));

    nRanges++;
}

bool SPMSim::AddressRange::isAddrInRange(const Addr addr) const
{
    for (vector<rangeType>::const_iterator ri=ranges.begin(); ri<ranges.end(); ri++) {
        if (ri->start <= addr && addr < ri->start + ri->size) {
            return true;
        }
    }

    return false;
}

SPMSim::Partitions::Partitions()
{
    nPartitions = 0;
}

SPMSim::Partitions::~Partitions()
{
    for (int pi=0; pi < nPartitions; pi++)
        delete pList[pi].adrg;
    
    pList.clear();
}

void SPMSim::Partitions::addPartition(const int size, const int parentID, const AddressRange* adrg, const int SPMAddr)
{
    pList.push_back(partType(size, parentID, adrg, SPMAddr));

    nPartitions++;
}

int SPMSim::Partitions::findMatchingPartition(const Addr addr) const
{
    for (int pi=0; pi < nPartitions; pi++) {
        if (pList[pi].adrg->isAddrInRange(addr) == true)
            return pi;
    }
    return -1;
}

void SPMSim::Partitions::loadPartition(const int pID)
{
    try {
        if (pList.size() <= pID)
            throw pID;
    }
    catch (int pID) {
        cout << "Partition " << pID << " cannot be found\n";
    }

    // load partition pID
    pList[pID].loaded = true;

    int SPMAddr = pList[pID].SPMAddr;
    int size = pList[pID].size;

    // invalidate all partitions that share in the SPM address range
    for (int p=0; p < nPartitions; p++) {
        if (p == pID) continue;

        if (SPMAddr <= pList[p].SPMAddr && pList[p].SPMAddr < SPMAddr+size)
            pList[p].loaded = false;
    }
}

bool SPMSim::Partitions::isPartitionLoaded(const int pID) const
{
    try {
        if (pList.size() <= pID)
            throw pID;
    }
    catch (int pID) {
        cout << "Partition " << pID << " cannot be found\n";
    }

    return pList[pID].loaded;
}

int SPMSim::Partitions::getPartitionSize(const int pID) const
{
    try {
        if (pList.size() <= pID)
            throw pID;
    }
    catch (int pID) {
        cout << "Partition " << pID << " cannot be found\n";
    }   

    return pList[pID].size;
}

int SPMSim::Partitions::getPartitionParent(const int pID) const
{
    try {
        if (pList.size() <= pID)
            throw pID;
    }
    catch (int pID) {
        cout << "Partition " << pID << " cannot be found\n";
    }

    return pList[pID].parent;
}
        
bool SPMSim::readCodeMappingInput()
{
    // mapping.out
    // first line: # of partitions
    // after that, each partition is described in a line
    // <partition size> <# of address regions> <(starting address, size) ... > <mapped SPM address>

    FILE *fp = fopen("mapping.out", "r");
    if (fp == NULL) {
        printf("cannot find the mapping info file\n");
        return false;
    }

    int nP;
    fscanf(fp, "%d\n", &nP);

    int pIdx;
    for (pIdx = 0; pIdx < nP; pIdx++) {
        AddressRange* adrg = new AddressRange();

        int pno, parentID;
        fscanf(fp, "%d (%d)\n", &pno, &parentID);
        try {
            if (pno != pIdx)
                throw pIdx;
        }
        catch (int pIdx) {
            cout << "Partition " << pIdx << " cannot be found\n";
        }
        
        int pSize;
        fscanf(fp, "%d ", &pSize);

        int nRange;
        fscanf(fp, "%d ", &nRange);

        int rIdx;
        for (rIdx = 0; rIdx < nRange; rIdx++) {
            long long int start;
            long long int size;
            fscanf(fp, "(%llx %lld) ", &start, &size);

            adrg->addRange(start, size);
        }

        int SPMAddr;
        fscanf(fp, "%d\n", &SPMAddr);

        pts.addPartition(pSize, parentID, adrg, SPMAddr);
    }

    fclose(fp);
    return true;
}

void SPMSim::readUserCodeRange()
{
    // userCodeRange.txt
    // start end

    FILE *fp = fopen("userCodeRange.txt", "r");
    if (fp == NULL) {
        printf("cannot find the user code range info file\n");
        exit(1);
    }

    fscanf(fp, "%llx %llx\n", &userCodeStartAddr, &userCodeEndAddr);

    fclose(fp);
}

int Cdma(const int trSize)
{
    #define CACHE_MISS_LATENCY 50
    #define CACHE_BLOCK_SIZE 16
    #define WORD_SIZE 4

    int setup_time = CACHE_MISS_LATENCY - (CACHE_BLOCK_SIZE / WORD_SIZE);
    int transfer_size = trSize; 

    return setup_time + (transfer_size + WORD_SIZE-1)/WORD_SIZE;
}

void SPMSim::calculateSPMAccessTime(const Addr addr)
{
    if (mode == disable)
        return;

    int pID = pts.findMatchingPartition(addr);
    if (pID < 0) {
        printf("Cannot determine which partition the instruction at 0x%llx belongs to\n", addr);
        return;
    }

    //printf("Instruction at 0x%llx is in partition %d\n", addr, pID);

    int localL, localM;

    if (contextStack.empty()) {
        // initial loading of main    
        localL = Cdma(pts.getPartitionSize(pID));
        localM = 0;
        printf("Initial loading of main (%d). Cost: L %d M %d\n", pID, localL, localM);

        contextStack.push(pID);
        pts.loadPartition(pID);
    }
    else {
        if (pID == contextStack.top()) {
            localL = 0;
            localM = 0;
        }
        else {
            // check if loaded
            if (pts.isPartitionLoaded(pID)) {
                localL = 0;
                localM = 0;
            }
            else {
                localL = Cdma(pts.getPartitionSize(pID));
                localM = 4;
                printf("Loading partition %d. Cost: L %d M %d\n", pID, localL, localM);
                pts.loadPartition(pID);
            }

            // maintain context stack
            int backup = contextStack.top();
            contextStack.pop();
            if (contextStack.empty()) {
                // entering into a new partition
                contextStack.push(backup);
                contextStack.push(pID);
            }
            else if (pID == contextStack.top()) {
                // returning
                ;
            }
            //|| !(pID == contextStack.top() && pID == pts.getPartitionParent(backup))) {
        }
    }

    L += localL;
    M += localM;
}

void SPMSim::execute(const Addr addr)
{
    // consider only user code .. 
    if (userCodeStartAddr <= addr && addr < userCodeEndAddr) {
        C++;
        calculateSPMAccessTime(addr);
    }
}

///////////////////////////////////////
