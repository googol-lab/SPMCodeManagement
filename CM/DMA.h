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
//  1. call(caller_fid, callee_fid);
//   -->
//    push cr_id (caller_fid) to stack
//    push cl_id (callee_fid) to stack
//    push all arguments to callee to stack  --- this is not overhead. it's there even without code management
//    bl call                                
//  void call()
//    ld r0, cl_id
//    ld r1, rid of cl_id 
//    ld r2, base address region_state
//    ld r2, region_state[r1]
//    cmp r2, r0

#define NUM_INSTS_FOR_CHECKING_CALL 8

//  2. if the function has to be loaded
//    (1) update the region state
//    store r0, region_state[r1]  // update the region state
//    bl load
//  void load()
//    (1) dma
//    ld r2, base address of function_address
//    ld r2, function_address[r0]
//    mcr -- set the source address
//    ld r2, base address of region_address
//    ld r2, region_address[r1]
//    mcr -- set the target address
//    ld r2, base address of function_size
//    ld r2, function_size[r0]
//    mcr -- set the transfer size
//    mcr -- init the DMA
//    (2) busy-waiting the completion of DMA
//    wait: mrc (load the DMA channel status register)
//          cmp
//          bne (repeat if not completed)
//    (3) return
//    bl

#define NUM_INSTS_FOR_BUSYWAITING 3
// This number will be used in the following DMA cost function.
int Cdma(int functionID);
int CdmaByBytes(int nBytes);

#define NUM_INSTS_IN_LOAD 14

#define SIZE_OF_LOAD_FUNCTION (NUM_INSTS_IN_LOAD+3) // 3 base addresses (function_address, region_address, function_size)

// 3. call the callee
//    bl callee
// 4. check and load for caller
//    ld r0, cr_id
//    ld r1, rid of cr_id
//    ld r2, base address of region_state
//    ld r2, region_state[r1]
//    cmp r2, r0
//    store r0, region_state[r1]  // update the region state
//    bl load
// 5. return
//    bl

#define NUM_INSTS_FOR_CHECKING_RETURN 6

#define SIZE_OF_CALL_FUNCTIONA (16 + 2)   // 16 instructions + 2 base addresses (region_state, region_id)

// Summary
// For a call, checking = 3, update = 1, DMA = 12, CALL = 2
// For a return, checking = 3+2 = 5, DMA = 12, CALL = 0
// To simplify a little bit,
//  for a hit, either a call or a return needs 5 instructions, and
//  for a miss, additional 15 or 13 instructions are needed to initiate a DMA

#define NUM_INSTS_AH_CALL NUM_INSTS_FOR_CHECKING_CALL
#define NUM_INSTS_AH_RETURN NUM_INSTS_FOR_CHECKING_RETURN

#define NUM_INSTS_AM_CALL (NUM_INSTS_FOR_CHECKING_CALL+2+NUM_INSTS_IN_LOAD) // 1 for state update and 1 for calling load
// 8+1+15 = 24
#define NUM_INSTS_AM_RETURN (NUM_INSTS_FOR_CHECKING_RETURN+2+NUM_INSTS_IN_LOAD+1) // 1 for state update and 1 for calling load and 1 for bl at the end of CALL  
// 7+1+15+1 = 24

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
