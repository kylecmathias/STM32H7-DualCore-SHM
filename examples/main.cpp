/**
 * EXAMPLE: STM32H7 Dual-Core Shared Memory Usage
 * 
 * Note: This is an integration snippet. It assumes hardware, 
 * system clocks, and MPU have already been initialized by CubeMX/HAL.
 */

#include "stm32h755xx.h"
#include "stm32h7_dualcore_shm.hpp"

struct State {
    uint32_t message_count = 0;
    float pitch = 0.0f;
    bool flag = false;
};

// 2. Instantiate the shared memory block
SHM_SECTION SHMBlock<State> shm;

int main(void) {
    // Configuration stuff 

    // 3. Hardware Semaphore Clock MUST be enabled before any access
    __HAL_RCC_HSEM_CLK_ENABLE();

    while (1) {
        
#if defined(__CORTEX_M) && (__CORTEX_M == 7)
        // M7 Stuff
        
        // Wait indefinitely for the lock
        shm.access([&]() {
            shm.data.message_count++;
            shm.data.pitch += 0.5f;
            
            if (/* some hardware fault */ false) {
                shm.data.flag = true;
            }
        }, SHMBlock<State>::WAIT_FOREVER);
        
        HAL_Delay(10); // Simulate some work

#elif defined(__CORTEX_M) && (__CORTEX_M == 4)
        // M4 Stuff
        
        // Try to read, but give up after 5 milliseconds if M7 is busy
        bool ok = shm.access([&]() {
            if (shm.data.flag) {
                // Do something
            } else {
                // Do something else
            }
        }, MS_TO_CYCLES(5));

        if (!ok) {
            // Timeout, the M7 held it too long
            // Handle the timeout
        }
        
        // Loop
#endif
    }
}
