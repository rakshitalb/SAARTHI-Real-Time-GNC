#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* CPU / RTOS timing */
#define configCPU_CLOCK_HZ              (125000000UL)
#define configTICK_RATE_HZ              (1000U)
#define configTICK_TYPE_WIDTH_IN_BITS   TICK_TYPE_WIDTH_32_BITS

/* Scheduler */
#define configUSE_PREEMPTION             1
#define configUSE_TIME_SLICING           1
#define configMAX_PRIORITIES             5
#define configCHECK_HANDLER_INSTALLATION   1

/* Task configuration */
#define configMINIMAL_STACK_SIZE         128
#define configMAX_TASK_NAME_LEN          16

/* Memory allocation */
#define configSUPPORT_STATIC_ALLOCATION   1
#define configSUPPORT_DYNAMIC_ALLOCATION  0

/* Software timers */
#define configUSE_TIMERS                  0

/* Synchronization */
#define configUSE_MUTEXES                 1
#define configUSE_COUNTING_SEMAPHORES     1

/* Hooks */
#define configUSE_IDLE_HOOK               0
#define configUSE_TICK_HOOK               0
#define configCHECK_FOR_STACK_OVERFLOW    2
#define configUSE_MALLOC_FAILED_HOOK      0

/* Cortex-M interrupt configuration */
#define configPRIO_BITS                   4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* Assertions */
#define configASSERT(x) \
    if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;) {} }

/* API functions required by our application */
#define INCLUDE_vTaskDelay                1
#define INCLUDE_xTaskDelayUntil           1
#define INCLUDE_xTaskGetSchedulerState    1

#endif