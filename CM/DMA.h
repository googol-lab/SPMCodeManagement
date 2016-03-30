#ifndef DMA_H
#define DMA_H

//#define LVZERO 1

//#define CACHE_MISS_LATENCY 50

//#define CACHE_BLOCK_SIZE 16
#define CACHE_BLOCK_SIZE 32 
#define CACHE_ASSOCIATIVITY 4

#define WORD_SIZE 4

#define CASE2

///////////////////////////////
// Case 1
// Slow core + on-chip mem

#ifdef CASE1
#define SETUP_TIME 7 
#define BYTE_READ_PER_CYCLE 8 

///////////////////////////////
// Case 2
// Fast core + on-chip mem

#else 
#ifdef CASE2
#define SETUP_TIME 20 
#define BYTE_READ_PER_CYCLE 4 

///////////////////////////////
// Case 2
// Fast core + off-chip mem

#else 
#ifdef CASE3
#define SETUP_TIME 60
#define BYTE_READ_PER_CYCLE 2

#endif
#endif
#endif

#define CACHE_MISS_LATENCY (SETUP_TIME+(CACHE_BLOCK_SIZE+BYTE_READ_PER_CYCLE-1)/BYTE_READ_PER_CYCLE)
#define FETCH_LATENCY (SETUP_TIME+(WORD_SIZE+BYTE_READ_PER_CYCLE-1)/BYTE_READ_PER_CYCLE)

/////////////////////////////////////////
// What does it take to load a function (whose id is fid) to region (whose id is rid)?

// 1. Checking the region state
// load the base address of regionstate array
// load regionstate[rid] 
// cmp regionstate[rid] with fid 

// for a call, rid and fid are constants (total 3 instructions), but 
// for a return, rid and fid are passed as arguments which should be read from stack (total 3+2=5 instructions)

#define NUM_INSTS_FOR_CHECKING 3

#define NUM_INSTS_FOR_LOADING_FIDRID 2 

// Till here, at most 4 registers were used. (1 for base address, 1 for rid, 1 for fid,  and 1 for regionstate[rid] value)

// 2. Updating the region state
// str fid to regionstate[rid]

#define NUM_INSTS_FOR_STATE_UPDATE 1

// 3. Performing DMA
//      bl function_loading_service_routine (jump to the special function that handles the function loading)
// Inside function_loading_service_routine,
//      load the base address of function address array
//      load function_address[fid]
//      mcr  set the source address
//      do similar for region address (destination address) and function size (transfer size)
//      DMA initiation is another mcr instruction that sets the DMA enable bit.
//      mov pc, lr
// In total, 1 + 3*3 + 1 + 1 = 12 instructions
#define NUM_INSTS_FOR_DMA 12

// 4. Busy-waiting the completion of DMA 
// wait: mrc ~~ (load the DMA channel status register)
//       cmp (check if it is finished)
//       bne wait (if not finished go back to mrc)
// This means the completion of a DMA is checked every 3 cycle.
//  If a DMA takes W cycles, the completion is detected at (CEIL(W/3) + 1) * 3 cycles after the initiation of the DMA.
#define NUM_INSTS_FOR_BUSYWAITING 3
// This number will be used in the following DMA cost function.
int Cdma(int functionID);
int CdmaByBytes(int nBytes);

#define NUM_INSTS_IN_LOAD (NUM_INSTS_FOR_DMA-1+NUM_INSTS_FOR_BUSYWAITING)

// 5. Additional overhead
// At a call, we should pass caller_id and caller_region_id as parameters, so the callee knows who the caller is, which takes 2 additional instructions.

#define NUM_INSTS_FOR_ADDITIONAL_OVERHEAD 2

// Summary
// For a call, checking = 3, update = 1, DMA = 12, CALL = 2
// For a return, checking = 3+2 = 5, DMA = 12, CALL = 0
// To simplify a little bit,
//  for a hit, either a call or a return needs 5 instructions, and
//  for a miss, additional 15 or 13 instructions are needed to initiate a DMA

#define NUM_INSTS_AH_CALL (NUM_INSTS_FOR_CHECKING+NUM_INSTS_FOR_ADDITIONAL_OVERHEAD)
#define NUM_INSTS_AH_RETURN (NUM_INSTS_FOR_CHECKING+NUM_INSTS_FOR_LOADING_FIDRID)
#define NUM_INSTS_AM_CALL (NUM_INSTS_AH_CALL+NUM_INSTS_FOR_STATE_UPDATE+NUM_INSTS_FOR_LOADING_FIDRID+NUM_INSTS_FOR_DMA)
#define NUM_INSTS_AM_RETURN (NUM_INSTS_AH_RETURN+NUM_INSTS_FOR_STATE_UPDATE+NUM_INSTS_FOR_DMA)

///////
// Simple DMA that does not use fid or rid.
//  load source address
//   mcr 
//   load dest address
//   mcr 
//   load size
//   mcr
//   mcr enable
#define NUM_INSTS_FOR_SIMPLE_DMA 7 // (2*3 + 1)

#endif
