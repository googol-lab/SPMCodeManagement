#include "CM.H"
#include "DMA.h"

int Cdma(int functionID)
{
    int setup_time = CACHE_MISS_LATENCY - (CACHE_BLOCK_SIZE / WORD_SIZE);
    int transfer_size = functions[functionID].size; 

    return setup_time + (transfer_size + WORD_SIZE-1)/WORD_SIZE;
}

int CdmaByBytes(int nBytes)
{
    int setup_time = CACHE_MISS_LATENCY - (CACHE_BLOCK_SIZE / WORD_SIZE);
    int transfer_size = nBytes; 

    return setup_time + (transfer_size + WORD_SIZE-1)/WORD_SIZE;
}
