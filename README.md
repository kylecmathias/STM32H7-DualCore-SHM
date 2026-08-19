# STM32H7-DualCore-SHM

A header-only C++ abstraction for safely sharing memory between the Cortex-M7 and Cortex-M4 cores on STM32H7 microcontrollers. Tested on the STM32H755ZI. 

It wraps the Hardware Semaphore (HSEM), L1 cache maintenance, and cycle-accurate timeouts behind a single `access()` lambda to pass variables across the bus without manually managing locks, cache invalidation, or memory barriers at every call site.

## The Core Problem
Sharing variables across the H7 dual-core bus requires getting three things right simultaneously:
1. **Locking:** Enforcing mutual exclusion so only one core modifies data at a time.
2. **Cache Coherency:** The M7 has an L1 Data Cache so failing to invalidate before reading and clean after writing will result in cores reading stale cached data instead of physical RAM.
3. **Memory Ordering:** Issuing `__DSB()` barriers so the CPU doesn't reorder memory operations outside the lock boundary.

Failing any of these results in silent data corruption. `SHMBlock` guarantees all three in a single call.

## Under the Hood
Each call to `access()` executes a strict hardware sequence:
* **Acquire:** Attempts to lock HSEM Channel 24 using the 2-step read/write protocol. 
* **Sleep/Poll:** If `DONT_WAIT` is passed, the call will return instantly if it doesn't get the semaphore. If `WAIT_FOREVER` is passed, the waiting core enters a low-power `__WFE()` sleep state. If a timeout is passed, it polls the lock bounded by the DWT hardware cycle counter (accounting for the 480MHz/240MHz clock differences).
* **Invalidate:** Issues a `__DSB()` and invalidates the M7 D-Cache to fetch fresh RAM.
* **Execute:** Runs the lambda callback.
* **Flush:** Cleans the M7 D-Cache, forcing writes out to physical memory.
* **Release:** Unlocks the HSEM and pulses `__SEV()` to wake up any sleeping cores.

## Usage
```cpp
#include "stm32h7_dualcore_shm.hpp"

// 1. Define the custom payload
struct SharedData {
    uint32_t counter = 0;
    std::atomic<bool> ready{false};
};

// 2. Instantiate the template in the shared linker section (.shm)
SHM_SECTION SHMBlock<SharedData> shm;

// 3. Access safely from either core using the .data member
bool ok = shm.access([&]() {
    shm.data.counter = 1;
    shm.data.ready.store(true, std::memory_order_relaxed);
}, MS_TO_CYCLES(10)); // Give up after 10ms

if (!ok) {
    // Handle timeout contention
}

```

## API Reference

### Macros
* **`SHM_SECTION`**: Applies the GCC `__attribute__((section(".shm"))) volatile` to place the instantiated block into the shared RAM space.
* **`MS_TO_CYCLES(ms)` / `US_TO_CYCLES(us)`**: Dynamically converts milliseconds or microseconds into precise DWT CPU cycles based on the executing core's clock speed.

### `SHMBlock<T>`
The core templated wrapper. It automatically pads the user defined struct to a multiple of 32 bytes (`alignas(32)`) to prevent Cortex-M7 cache-line tearing.
* **`shm.data`**: The custom struct payload.
* **`SHMBlock::WAIT_FOREVER`**: Constant to sleep indefinitely (`__WFE`) until the lock is released.
* **`SHMBlock::DONT_WAIT`**: Constant for a non-blocking try-lock. Returns `false` immediately if the lock is held.
* **`shm.access(callback, cycles)`**: The locking method. Accepts a lambda callback and an optional timeout in cycles. Returns `true` if the lock was acquired and the callback ran, or `false` if it timed out.

### Linker Script Setup
`RAM_D3` memory must be defined and the `.shm` section in **both** the Cortex-M7 and Cortex-M4 linker scripts. The section must be marked `NOLOAD` to prevent the C runtime from zeroing it out during a single-core reboot.

**Cortex-M7 Linker Script:**
```ld
MEMORY
{
  /* ... other memory regions ... */
  RAM_D3 (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
}

SECTIONS
{
  /* ... other sections ... */
  
  .shm (NOLOAD) : 
  {
    . = ALIGN(4);
    *(.shm);
    *(.shm*);
    . = ALIGN(4);
  } >RAM_D3
}
```
**Cortex-M4 Linker Script:**
```ld
MEMORY
{
  /* ... other memory regions ... */
  RAM_D3 (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
}

SECTIONS
{
  /* ... other sections ... */
  
  .shm (NOLOAD) : 
  {
    . = ALIGN(4);
    *(.shm);
    *(.shm*);
    . = ALIGN(4);
  } >RAM_D3
}
```

### Constraints & Hardware Rules
* **Clock Configuration:** The header file assumes the microcontroller is running at maximum clock speeds (480MHz for M7, 240MHz for M4). The `SYSTEM_CORE_CLOCK_M7` and `SYSTEM_CORE_CLOCK_M4` constants at the top of the header file **must** match the clock tree speeds in STM32CubeMX, or the timeouts will be inaccurate.
* **Clock Enable:** `__HAL_RCC_HSEM_CLK_ENABLE()` must be called on both cores at startup before making any `access()` calls.
* **Cacheability:** The library assumes the shared RAM is configured in the MPU as Normal Cacheable memory. If the MPU marks it as strongly-ordered or non-cacheable, the `SCB` flush calls will safely act as harmless no-ops.
* **No Dynamic Memory:** Only supports plain, non-dynamic types inside the struct. No heap allocations, no pointers to local core memory, and no STL containers.
* **No Recursive Locking:** Calling `access()` from inside another `access()` callback on the same core will cause a permanent deadlock.
