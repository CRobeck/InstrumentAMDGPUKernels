#include "hip/hip_runtime.h"
#include <stdint.h>
#include <stdio.h>

#define WarpSize 32

__attribute__((used)) 
__attribute__((always_inline))
__device__ void PrintKernel(uint32_t idx) {
    if(threadIdx.x == 0)
      printf("Value to send to trace stream:    %u    NumCacheLines: %u    LocationIdx: %u\n", idx,
            static_cast<uint32_t>(idx >> 26), static_cast<uint32_t>(idx & (67108864 - 1)));
}


__device__ int getNthBit(uint32_t bitArray, int nth){
  return 1 & (bitArray >> nth);
}

 __device__ bool isSharedMemPtr(const void *Ptr) {
  return __builtin_amdgcn_is_shared(
      (const __attribute__((address_space(0))) void *)Ptr);
}

__attribute__((used)) __device__ uint32_t countCacheLines(void* addressPtr, char* fileName, char* functionName,
                                char* loadOrStore, uint32_t lineNum, uint32_t columnNum,
                                uint32_t LocationIdx, uint32_t typeSize){
  uint32_t NumCacheLines = 1;
 //TODO: See if this is check is actually needed since we're already checking for addresspace 3 or 4
 //in the compiler pass before injecting this function
 if(isSharedMemPtr(addressPtr))
   return NumCacheLines;

  int activeThreadMask =__ballot(1);

  uint64_t address = reinterpret_cast<uint64_t>(addressPtr);

  uint64_t addrArray[2 * WarpSize];

  int masterThread = -1;
  for(int i = 0; i < WarpSize; i++)
    if(getNthBit(activeThreadMask, i) == 1){
      masterThread = i;
      break;
    }

  // Shuffle values from all threads into addrArray using active threads
  for(int i = 0; i < WarpSize; i++){
    if(getNthBit(activeThreadMask, i) == 0)
      addrArray[2 * i] = address;
    else{
      addrArray[2 * i] = __shfl(address, i, WarpSize);
    }
  }
  
  uint32_t LaneId = (WarpSize - 1) & threadIdx.x;
  if(masterThread == LaneId){
    NumCacheLines = 1;
    // Divide all threads by cacheLineSize (128 bytes). Every other thread represents the max address that
    // is accessed then compute (address + typeSize - 1) / cacheLineSize (128 bytes).
    for(int i = 0; i < WarpSize; i++){
      addrArray[2 * i + 1] = (addrArray[2 * i] + typeSize - 1) >> 7;
      addrArray[2 * i] >>= 7;
    }

    uint64_t baseAddr = addrArray[0];
    for(int i = 0; i < 2 * WarpSize; i++)
      if(addrArray[i] != baseAddr){
        uint64_t current = addrArray[i];
        NumCacheLines++;
        for(int j = i + 1; j < 2 * WarpSize; j++)
          if(addrArray[j] == current)
            addrArray[j] = baseAddr;
      }
  }

  return ((NumCacheLines << 26) | LocationIdx);
}