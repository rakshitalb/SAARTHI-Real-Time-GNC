#include "FreeRTOS.h"
#include "task.h"
#include "control.h"
#include "week9_interface.h"
#include <stdint.h>

#define TRACE_LENGTH 256U

#define GNC_PERIOD_MS       10U
#define SENSE_PERIOD_MS     20U
#define COMMS_PERIOD_MS     50U
#define TELEMETRY_PERIOD_MS 100U

typedef struct
{
    volatile uint32_t samples;
    volatile uint32_t period_min_ticks;
    volatile uint32_t period_max_ticks;
    volatile uint32_t execution_min_ticks;
    volatile uint32_t execution_max_ticks;
    volatile uint32_t activation_period_violations;
    volatile uint32_t execution_deadline_misses;
    volatile uint32_t peak_to_peak_jitter_ticks;
    volatile uint32_t timer_period_min_counts;
    volatile uint32_t timer_period_max_counts;
    volatile uint32_t timer_execution_min_counts;
    volatile uint32_t timer_execution_max_counts;
    volatile uint32_t timer_peak_to_peak_jitter_counts;
} TaskTiming;


/* TIM2 is exposed by the Renode STM32F4 platform at 0x40000000.
   Platform-reported frequency: 10 MHz (nominal 0.1 us per counter count). */
#define TIM2_BASE 0x40000000UL
#define TIM2_CR1  (*(volatile uint32_t *)(TIM2_BASE + 0x00UL))
#define TIM2_EGR  (*(volatile uint32_t *)(TIM2_BASE + 0x14UL))
#define TIM2_CNT  (*(volatile uint32_t *)(TIM2_BASE + 0x24UL))
#define TIM2_PSC  (*(volatile uint32_t *)(TIM2_BASE + 0x28UL))
#define TIM2_ARR  (*(volatile uint32_t *)(TIM2_BASE + 0x2CUL))

static void Timer2_Init(void)
{
    TIM2_CR1 = 0U;
    TIM2_PSC = 0U;
    TIM2_ARR = 0xFFFFFFFFUL;
    TIM2_CNT = 0U;
    TIM2_EGR = 1U;
    TIM2_CNT = 0U;
    TIM2_CR1 = 1U; /* Counter enable */
}

static uint32_t Timer2_Now(void)
{
    return TIM2_CNT;
}

/* Original application counters */
volatile unsigned long gnc_count = 0;
volatile unsigned long sensing_count = 0;
volatile unsigned long communications_count = 0;
volatile unsigned long communications_last_timestamp = 0;
volatile unsigned long communications_last_status = 0;
volatile unsigned long monitoring_count = 0;

/* Existing GNC-to-communications interface */
volatile GNC_Telemetry gnc_telemetry = {0};

/* Per-task timing summaries */
volatile TaskTiming gnc_timing = {0, UINT32_MAX, 0, UINT32_MAX, 0, 0, 0, 0, UINT32_MAX, 0, UINT32_MAX, 0, 0};
volatile TaskTiming sensing_timing = {0, UINT32_MAX, 0, UINT32_MAX, 0, 0, 0, 0, UINT32_MAX, 0, UINT32_MAX, 0, 0};
volatile TaskTiming communications_timing = {0, UINT32_MAX, 0, UINT32_MAX, 0, 0, 0, 0, UINT32_MAX, 0, UINT32_MAX, 0, 0};
volatile TaskTiming telemetry_timing = {0, UINT32_MAX, 0, UINT32_MAX, 0, 0, 0, 0, UINT32_MAX, 0, UINT32_MAX, 0, 0};

/* Raw traces: activation tick and RTOS-tick execution duration.
   Separate timer-execution traces store high-resolution Timer 2 counts. */
volatile uint32_t gnc_activation_trace[TRACE_LENGTH];
volatile uint32_t gnc_execution_trace[TRACE_LENGTH];
volatile uint32_t gnc_timer_execution_trace[TRACE_LENGTH];

volatile uint32_t sensing_activation_trace[TRACE_LENGTH];
volatile uint32_t sensing_execution_trace[TRACE_LENGTH];
volatile uint32_t sensing_timer_execution_trace[TRACE_LENGTH];

volatile uint32_t communications_activation_trace[TRACE_LENGTH];
volatile uint32_t communications_execution_trace[TRACE_LENGTH];
volatile uint32_t communications_timer_execution_trace[TRACE_LENGTH];

volatile uint32_t telemetry_activation_trace[TRACE_LENGTH];
volatile uint32_t telemetry_execution_trace[TRACE_LENGTH];
volatile uint32_t telemetry_timer_execution_trace[TRACE_LENGTH];

/* Number of measurements collected */
volatile uint32_t phase3_measurement_count = 0;

static StaticTask_t idle_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];

static StaticTask_t gnc_tcb;
static StaticTask_t sensing_tcb;
static StaticTask_t communications_tcb;
static StaticTask_t telemetry_tcb;

static StackType_t gnc_stack[configMINIMAL_STACK_SIZE];
static StackType_t sensing_stack[configMINIMAL_STACK_SIZE];
static StackType_t communications_stack[configMINIMAL_STACK_SIZE];
static StackType_t telemetry_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer,
    configSTACK_DEPTH_TYPE *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_tcb;
    *ppxIdleTaskStackBuffer = idle_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook(
    TaskHandle_t xTask,
    char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    taskDISABLE_INTERRUPTS();

    for (;;)
    {
    }
}

/* Record the activation and calculate start-to-start period. */
static uint32_t BeginMeasurement(
    volatile TaskTiming *timing,
    volatile uint32_t *activation_trace,
    TickType_t start,
    TickType_t *previous_start,
    TickType_t expected_period)
{
    uint32_t slot = timing->samples;
    uint32_t period;

    if (slot < TRACE_LENGTH)
        activation_trace[slot] = (uint32_t)start;

    if (slot > 0U)
    {
        period = (uint32_t)(start - *previous_start);

        if (period < timing->period_min_ticks)
            timing->period_min_ticks = period;

        if (period > timing->period_max_ticks)
            timing->period_max_ticks = period;

        if (timing->period_max_ticks >= timing->period_min_ticks)
        {
            timing->peak_to_peak_jitter_ticks =
                timing->period_max_ticks - timing->period_min_ticks;
        }

        if (period != (uint32_t)expected_period)
            timing->activation_period_violations++;
    }

    *previous_start = start;

    if (timing->samples < UINT32_MAX)
        timing->samples++;

    return slot;
}

/* Record observed execution duration using the 1 ms RTOS tick. */
static void EndMeasurement(
    volatile TaskTiming *timing,
    volatile uint32_t *execution_trace,
    uint32_t slot,
    TickType_t start,
    TickType_t end,
    TickType_t expected_period)
{
    uint32_t duration = (uint32_t)(end - start);

    if (slot < TRACE_LENGTH)
        execution_trace[slot] = duration;

    if (duration < timing->execution_min_ticks)
        timing->execution_min_ticks = duration;

    if (duration > timing->execution_max_ticks)
        timing->execution_max_ticks = duration;

    /* Deadline check: completion must occur within one task period. */
    if (duration >= (uint32_t)expected_period)
        timing->execution_deadline_misses++;
}
/* -----------------------------------------------------------
 * GNC task: 10 ms
 * ----------------------------------------------------------- */
static void GNC_Task(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t period = pdMS_TO_TICKS(GNC_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t previous_start = 0;
    uint32_t previous_timer_start = 0U;

    const double q_err[3] = {0.01, -0.02, 0.03};
    const double omega[3] = {0.10, -0.05, 0.02};
    double torque[3];

    for (;;)
    {
        TickType_t start = xTaskGetTickCount();
        uint32_t timer_start = Timer2_Now();
        uint32_t slot = BeginMeasurement(
            &gnc_timing, gnc_activation_trace,
            start, &previous_start, period);
        

if (slot > 0U)
        {
            uint32_t tp = timer_start - previous_timer_start;

            if (slot == 1U)
            {
                gnc_timing.timer_period_min_counts = tp;
                gnc_timing.timer_period_max_counts = tp;
            }
            else
            {
                if (tp < gnc_timing.timer_period_min_counts)
                    gnc_timing.timer_period_min_counts = tp;

                if (tp > gnc_timing.timer_period_max_counts)
                    gnc_timing.timer_period_max_counts = tp;
            }

            gnc_timing.timer_peak_to_peak_jitter_counts =
                gnc_timing.timer_period_max_counts -
                gnc_timing.timer_period_min_counts;
        }

        previous_timer_start = timer_start;

        gnc_count++;

        pd_controller(q_err, omega, torque);

        gnc_telemetry.timestamp = gnc_count;
        gnc_telemetry.status = 1;
        gnc_telemetry.torque_x = torque[0];
        gnc_telemetry.torque_y = torque[1];
        gnc_telemetry.torque_z = torque[2];

        uint32_t timer_end = Timer2_Now();
        EndMeasurement(
            &gnc_timing, gnc_execution_trace,
            slot, start, xTaskGetTickCount(), period);
        {
            uint32_t timer_duration = timer_end - timer_start;
            if (slot < TRACE_LENGTH) gnc_timer_execution_trace[slot] = timer_duration;
            if (slot == 0U || timer_duration < gnc_timing.timer_execution_min_counts) gnc_timing.timer_execution_min_counts = timer_duration;
            if (timer_duration > gnc_timing.timer_execution_max_counts) gnc_timing.timer_execution_max_counts = timer_duration;
        }

        phase3_measurement_count++;

        (void)xTaskDelayUntil(&last_wake, period);
    }
}

/* -----------------------------------------------------------
 * Sensing task: 20 ms
 * The actual sensor/estimator workload is not supplied yet.
 * ----------------------------------------------------------- */
static void Sensing_Task(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t period = pdMS_TO_TICKS(SENSE_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t previous_start = 0;
    uint32_t previous_timer_start = 0U;

    for (;;)
    {
        TickType_t start = xTaskGetTickCount();
        uint32_t timer_start = Timer2_Now();
        uint32_t slot = BeginMeasurement(
            &sensing_timing, sensing_activation_trace,
            start, &previous_start, period);
        

if (slot > 0U)
        {
            uint32_t tp = timer_start - previous_timer_start;

            if (slot == 1U)
            {
                sensing_timing.timer_period_min_counts = tp;
                sensing_timing.timer_period_max_counts = tp;
            }
            else
            {
                if (tp < sensing_timing.timer_period_min_counts)
                    sensing_timing.timer_period_min_counts = tp;

                if (tp > sensing_timing.timer_period_max_counts)
                    sensing_timing.timer_period_max_counts = tp;
            }

            sensing_timing.timer_peak_to_peak_jitter_counts =
                sensing_timing.timer_period_max_counts -
                sensing_timing.timer_period_min_counts;
        }

        previous_timer_start = timer_start;

        sensing_count++;

        /* TODO: insert the validated sensing/estimation workload. */

        uint32_t timer_end = Timer2_Now();
        EndMeasurement(
            &sensing_timing, sensing_execution_trace,
            slot, start, xTaskGetTickCount(), period);
        {
            uint32_t timer_duration = timer_end - timer_start;
            if (slot < TRACE_LENGTH) sensing_timer_execution_trace[slot] = timer_duration;
            if (slot == 0U || timer_duration < sensing_timing.timer_execution_min_counts) sensing_timing.timer_execution_min_counts = timer_duration;
            if (timer_duration > sensing_timing.timer_execution_max_counts) sensing_timing.timer_execution_max_counts = timer_duration;
        }

        (void)xTaskDelayUntil(&last_wake, period);
    }
}

/* -----------------------------------------------------------
 * Communications task: 50 ms
 * ----------------------------------------------------------- */
static void Communications_Task(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t period = pdMS_TO_TICKS(COMMS_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t previous_start = 0;
    uint32_t previous_timer_start = 0U;

    for (;;)
    {
        TickType_t start = xTaskGetTickCount();
        uint32_t timer_start = Timer2_Now();
        uint32_t slot = BeginMeasurement(
            &communications_timing, communications_activation_trace,
            start, &previous_start, period);
        
if (slot > 0U)
        {
            uint32_t tp = timer_start - previous_timer_start;

            if (slot == 1U)
            {
                communications_timing.timer_period_min_counts = tp;
                communications_timing.timer_period_max_counts = tp;
            }
            else
            {
                if (tp < communications_timing.timer_period_min_counts)
                    communications_timing.timer_period_min_counts = tp;

                if (tp > communications_timing.timer_period_max_counts)
                    communications_timing.timer_period_max_counts = tp;
            }

            communications_timing.timer_peak_to_peak_jitter_counts =
                communications_timing.timer_period_max_counts -
                communications_timing.timer_period_min_counts;
        }

        previous_timer_start = timer_start;

        communications_count++;

        unsigned long timestamp = gnc_telemetry.timestamp;
        unsigned long status = gnc_telemetry.status;

        double torque_x = gnc_telemetry.torque_x;
        double torque_y = gnc_telemetry.torque_y;
        double torque_z = gnc_telemetry.torque_z;

        communications_last_timestamp = timestamp;
        communications_last_status = status;

        (void)torque_x;
        (void)torque_y;
        (void)torque_z;

        /* TODO: insert the validated communications workload. */

        uint32_t timer_end = Timer2_Now();
        EndMeasurement(
            &communications_timing, communications_execution_trace,
            slot, start, xTaskGetTickCount(), period);
        {
            uint32_t timer_duration = timer_end - timer_start;
            if (slot < TRACE_LENGTH) communications_timer_execution_trace[slot] = timer_duration;
            if (slot == 0U || timer_duration < communications_timing.timer_execution_min_counts) communications_timing.timer_execution_min_counts = timer_duration;
            if (timer_duration > communications_timing.timer_execution_max_counts) communications_timing.timer_execution_max_counts = timer_duration;
        }

        (void)xTaskDelayUntil(&last_wake, period);
    }
}

/* -----------------------------------------------------------
 * Telemetry task: 100 ms
 * The actual telemetry workload is not supplied yet.
 * ----------------------------------------------------------- */
static void Telemetry_Task(void *pvParameters)
{
    (void)pvParameters;

    const TickType_t period = pdMS_TO_TICKS(TELEMETRY_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t previous_start = 0;
    uint32_t previous_timer_start = 0U;

    for (;;)
    {
        TickType_t start = xTaskGetTickCount();
        uint32_t timer_start = Timer2_Now();
        uint32_t slot = BeginMeasurement(
            &telemetry_timing, telemetry_activation_trace,
            start, &previous_start, period);
        

if (slot > 0U)
        {
            uint32_t tp = timer_start - previous_timer_start;

            if (slot == 1U)
            {
                telemetry_timing.timer_period_min_counts = tp;
                telemetry_timing.timer_period_max_counts = tp;
            }
            else
            {
                if (tp < telemetry_timing.timer_period_min_counts)
                    telemetry_timing.timer_period_min_counts = tp;

                if (tp > telemetry_timing.timer_period_max_counts)
                    telemetry_timing.timer_period_max_counts = tp;
            }

            telemetry_timing.timer_peak_to_peak_jitter_counts =
                telemetry_timing.timer_period_max_counts -
                telemetry_timing.timer_period_min_counts;
        }

        previous_timer_start = timer_start;

        monitoring_count++;

        /* TODO: insert the validated telemetry workload. */

        uint32_t timer_end = Timer2_Now();
        EndMeasurement(
            &telemetry_timing, telemetry_execution_trace,
            slot, start, xTaskGetTickCount(), period);
        {
            uint32_t timer_duration = timer_end - timer_start;
            if (slot < TRACE_LENGTH) telemetry_timer_execution_trace[slot] = timer_duration;
            if (slot == 0U || timer_duration < telemetry_timing.timer_execution_min_counts) telemetry_timing.timer_execution_min_counts = timer_duration;
            if (timer_duration > telemetry_timing.timer_execution_max_counts) telemetry_timing.timer_execution_max_counts = timer_duration;
        }

        (void)xTaskDelayUntil(&last_wake, period);
    }
}

int main(void)
{
    Timer2_Init();
    xTaskCreateStatic(
        GNC_Task, "GNC", configMINIMAL_STACK_SIZE,
        NULL, 4, gnc_stack, &gnc_tcb);

    xTaskCreateStatic(
        Sensing_Task, "SENSE", configMINIMAL_STACK_SIZE,
        NULL, 3, sensing_stack, &sensing_tcb);

    xTaskCreateStatic(
        Communications_Task, "COMMS", configMINIMAL_STACK_SIZE,
        NULL, 2, communications_stack, &communications_tcb);

    xTaskCreateStatic(
        Telemetry_Task, "TELEMETRY", configMINIMAL_STACK_SIZE,
        NULL, 1, telemetry_stack, &telemetry_tcb);

    vTaskStartScheduler();

    for (;;)
    {
    }
}
