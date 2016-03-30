#include "CM.H"
#include "DMA.h"
#include <math.h>

int Cdma(int functionID)
{
    //int setup_time = CACHE_MISS_LATENCY - (CACHE_BLOCK_SIZE / BYTE_READ_PER_CYCLE);
    int transfer_size = functions[functionID].size; 

    int DMATIME = SETUP_TIME + (int)ceil(transfer_size/BYTE_READ_PER_CYCLE);

    return ((DMATIME + NUM_INSTS_FOR_BUSYWAITING-1)/NUM_INSTS_FOR_BUSYWAITING + 1) * NUM_INSTS_FOR_BUSYWAITING;
}

int CdmaByBytes(int nBytes)
{
    //int setup_time = CACHE_MISS_LATENCY - (CACHE_BLOCK_SIZE / BYTE_READ_PER_CYCLE);
    int transfer_size = nBytes; 

    //int DMATIME = SETUP_TIME + (transfer_size + BYTE_READ_PER_CYCLE-1)/BYTE_READ_PER_CYCLE;
    int DMATIME = SETUP_TIME + (int)ceil(transfer_size/BYTE_READ_PER_CYCLE);

    return ((DMATIME + NUM_INSTS_FOR_BUSYWAITING-1)/NUM_INSTS_FOR_BUSYWAITING + 1) * NUM_INSTS_FOR_BUSYWAITING;
}
