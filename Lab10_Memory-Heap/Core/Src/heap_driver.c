#include "heap_driver.h"

#define HEAP_START_ADDR 0x20001000   // heap starts here in SRAM
#define HEAP_SIZE       4096         // 4 KB total
#define BLOCK_SIZE      16           // each block is 16 bytes
#define BLOCK_COUNT     256          // 4096 / 16

// One entry per block: 0 = free, 1 = start of an allocation, 2 = continuation
static uint8_t block_map[BLOCK_COUNT];

// ---------- heap_init: mark every block as free ----------
void heap_init(void)
{
    for (int i = 0; i < BLOCK_COUNT; i++)
    {
        block_map[i] = 0;
    }
}

// ---------- heap_alloc: find free blocks in a row ----------
void *heap_alloc(size_t size)
{
    if (size == 0) return NULL;

    // Step 1: how many blocks do we need? (round up)
    size_t needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Step 2: scan the block map for 'needed' free blocks in a row
    for (size_t i = 0; i + needed <= BLOCK_COUNT; i++)
    {
        size_t count = 0;
        while (count < needed && block_map[i + count] == 0)
        {
            count++;
        }

        // Step 3: found enough free blocks
        if (count == needed)
        {
            block_map[i] = 1;                    // first block
            for (size_t j = 1; j < needed; j++)
            {
                block_map[i + j] = 2;            // rest of the blocks
            }
            // Address = heap start + (block number * block size)
            return (void *)(HEAP_START_ADDR + i * BLOCK_SIZE);
        }
    }

    // Step 4: no space left
    return NULL;
}

// ---------- heap_free: mark the blocks free again ----------
void heap_free(void *ptr)
{
    uintptr_t addr = (uintptr_t)ptr;

    // Step 1: address must be inside the heap
    if (addr < HEAP_START_ADDR || addr >= HEAP_START_ADDR + HEAP_SIZE) return;

    // Step 2: address must be at the start of a block
    if ((addr - HEAP_START_ADDR) % BLOCK_SIZE != 0) return;

    size_t i = (addr - HEAP_START_ADDR) / BLOCK_SIZE;

    // Step 3: must be the start of an allocation
    if (block_map[i] != 1) return;

    // Step 4: free the first block, then every continuation block after it
    block_map[i] = 0;
    i++;
    while (i < BLOCK_COUNT && block_map[i] == 2)
    {
        block_map[i] = 0;
        i++;
    }
}