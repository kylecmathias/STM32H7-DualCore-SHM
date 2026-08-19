#pragma once

#include "stm32h755xx.h"
#include "stm32h7xx_hal_hsem.h"


#ifndef SHM_SECTION
#define SHM_SECTION __attribute__((section(".shm"))) volatile
#endif /* #ifndef SHM_BLOCK_OPEN */

inline constexpr uint32_t SYSTEM_CORE_CLOCK_M7 = 480000000; //480mhz
inline constexpr uint32_t SYSTEM_CORE_CLOCK_M4 = 240000000; //240mhz
inline constexpr uint32_t M7_CID = 3;
inline constexpr uint32_t M4_CID = 1;

#if defined(__CORTEX_M) && (__CORTEX_M == 7)
    #define MS_TO_CYCLES(ms)   (static_cast<int32_t>((static_cast<uint64_t>(ms) * SYSTEM_CORE_CLOCK_M7) / 1000))
    #define US_TO_CYCLES(us)   (static_cast<int32_t>((static_cast<uint64_t>(us) * SYSTEM_CORE_CLOCK_M7) / 1000000))
#elif defined(__CORTEX_M) && (__CORTEX_M == 4)
    #define MS_TO_CYCLES(ms)   (static_cast<int32_t>((static_cast<uint64_t>(ms) * SYSTEM_CORE_CLOCK_M4) / 1000))
    #define US_TO_CYCLES(us)   (static_cast<int32_t>((static_cast<uint64_t>(us) * SYSTEM_CORE_CLOCK_M4) / 1000000))
#else
    #error "Unknown core"
#endif /* #if defined(__CORTEX_M) && (__CORTEX_M == 7) */


inline constexpr uint32_t SHM_HSEM = 24; //hardware semaphore channel 24


/*
@brief Class for accessing shared memory in RAM_D3
*/
template <typename T>
class alignas(32) SHMBlock {    
    public:
        T data;

        static constexpr int32_t WAIT_FOREVER = -1;
        static constexpr int32_t DONT_WAIT = 0;

        SHMBlock() = default;

        /*
        @brief Access shared memory stored in the RAM_D3.

        @code
        struct SharedData {
            uint32_t counter = 0;
            bool flag = false;
        };

        SHM_SECTION SHMBlock<SharedData> shm;

        bool ok = shm.access([&]() {
            shm.data.counter = 1;
            shm.data.flag = true;
        }, MS_TO_CYCLES(10));
        @endcode

        @param cb Anonymous function for the critical section to execute
        @param cycles Number of cycles to try and access, MS_TO_CYCLES(ms) and US_TO_CYCLES(us) tries for a specified amount of time
        @return Whether the access was successful
        */
        template <typename TFunc>
        bool access(TFunc&& cb, int32_t cycles = WAIT_FOREVER) volatile {
            const uint32_t pid = 0;
            const uint32_t cid = (__CORTEX_M == 7) ? M7_CID : M4_CID;
            const uint32_t write_lock = (cid << HSEM_R_COREID_Pos) | (pid << HSEM_R_PROCID_Pos);
            const uint32_t expected_lock = HSEM_R_LOCK_Msk | write_lock;

            bool acquired = false;

            if (cycles == DONT_WAIT) {
                HSEM->R[SHM_HSEM] = write_lock; //hsem uses 2 step write to register then read back value and compare with expected
                __DSB();
                if (HSEM->R[SHM_HSEM] == expected_lock) {
                    acquired = true;
                } 
                if (!acquired) return false;
            }
            else if (cycles <= WAIT_FOREVER) { 
                while (!acquired) {
                    HSEM->R[SHM_HSEM] = write_lock; 
                    __DSB();
                    if (HSEM->R[SHM_HSEM] == expected_lock) {
                        acquired = true;
                    } 
                    else {
                        __WFE(); //sleep until event happens
                    }
                }
            } 
            else {
                if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0) {
                    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; //enable trace
                }

                #if defined(__CORTEX_M) && (__CORTEX_M == 7)
                if ((DWT->LSR & (1UL << 1)) != 0) { 
                    DWT->LAR = 0xC5ACCE55; //unlock dwt if locked cuz coresight 
                }
                #endif /* #if defined(__CORTEX_M) && (__CORTEX_M == 7) */

                if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0) {
                    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; //start the dwt hardware cycle counter if not already started
                }

                uint32_t start_cycles = DWT->CYCCNT; //record start ticks

                while ((DWT->CYCCNT - start_cycles) < static_cast<uint32_t>(cycles)) { 
                    HSEM->R[SHM_HSEM] = write_lock;
                    __DSB();
                    if (HSEM->R[SHM_HSEM] == expected_lock) { 
                        acquired = true;
                        break; 
                    }
                    __NOP();
                }
                if (!acquired) return false;
            }

            __DSB(); //enforce sequential memory ops

            #if defined(__CORTEX_M) && (__CORTEX_M == 7)
                auto* cache_addr = const_cast<uint32_t*>(reinterpret_cast<volatile uint32_t*>(this));
                SCB_InvalidateDCache_by_Addr(cache_addr, sizeof(SHMBlock<T>)); //cortex m7 has l1 cache so invalidate it
            #endif

            std::forward<TFunc>(cb)(); //execute shm access

            #if defined(__CORTEX_M) && (__CORTEX_M == 7)
                SCB_CleanDCache_by_Addr(cache_addr, sizeof(SHMBlock<T>)); //clean m7 l1 cache
            #endif /* #if defined(__CORTEX_M) && (__CORTEX_M == 7) */

            __DSB(); //make sure it finishes

            HSEM->RLR[SHM_HSEM] = write_lock; //release the semaphore
            __SEV(); //signal any sleeping cores to wakeup

            return true;
        }

    static_assert(sizeof(SHMBlock<T>) % 32 == 0, "sizeof SHMBlock<T> must be a multiple of 32 bytes");
};
