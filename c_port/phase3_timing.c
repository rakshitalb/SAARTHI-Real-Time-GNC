
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
} TaskTiming;

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
volatile TaskTiming gnc_timing = {0, UINT32_MAX, 0, UINT32_MAX, 0, 0, 0, 0};
volatile TaskTiming sensing_timing = {0, UINT32_MAX, 0, UINT32_MAX, 0, 0, 0, 0};
volatile TaskTiming communications_timing = {0, UINT32_MAX, 0, UINT32_MAX, 0, 0, 0, 0};
volatile TaskTiming telemetry_timing = {0, UINT32_MAX, 0, UINT32_MAX, 0, 0, 0, 0};

/* Raw traces: activation tick and execution duration in ticks */
volatile uint32_t gnc_activation_trace[TRACE_LENGTH];
volatile uint32_t gnc_execution_trace[TRACE_LENGTH];

volatile uint32_t sensing_activation_trace[TRACE_LENGTH];
volatile uint32_t sensing_execution_trace[TRACE_LENGTH];

volatile uint32_t communications_activation_trace[TRACE_LENGTH];
volatile uint32_t communications_execution_trace[TRACE_LENGTH];

volatile uint32_t telemetry_activation_trace[TRACE_LENGTH];
volatile uint32_t telemetry_execution_trace[TRACE_LENGTH];

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

    if (duration > (uint32_t)expected_period)
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

    const double q_err[3] = {0.01, -0.02, 0.03};
    const double omega[3] = {0.10, -0.05, 0.02};
    double torque[3];

    for (;;)
    {
        TickType_t start = xTaskGetTickCount();
        uint32_t slot = BeginMeasurement(
            &gnc_timing, gnc_activation_trace,
            start, &previous_start, period);

        gnc_count++;

        pd_controller(q_err, omega, torque);

        gnc_telemetry.timestamp = gnc_count;
        gnc_telemetry.status = 1;
        gnc_telemetry.torque_x = torque[0];
        gnc_telemetry.torque_y = torque[1];
        gnc_telemetry.torque_z = torque[2];

        EndMeasurement(
            &gnc_timing, gnc_execution_trace,
            slot, start, xTaskGetTickCount(), period);

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

    for (;;)
    {
        TickType_t start = xTaskGetTickCount();
        uint32_t slot = BeginMeasurement(
            &sensing_timing, sensing_activation_trace,
            start, &previous_start, period);

        sensing_count++;

        /* TODO: insert the validated sensing/estimation workload. */

        EndMeasurement(
            &sensing_timing, sensing_execution_trace,
            slot, start, xTaskGetTickCount(), period);

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

    for (;;)
    {
        TickType_t start = xTaskGetTickCount();
        uint32_t slot = BeginMeasurement(
            &communications_timing, communications_activation_trace,
            start, &previous_start, period);

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

        EndMeasurement(
            &communications_timing, communications_execution_trace,
            slot, start, xTaskGetTickCount(), period);

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

    for (;;)
    {
        TickType_t start = xTaskGetTickCount();
        uint32_t slot = BeginMeasurement(
            &telemetry_timing, telemetry_activation_trace,
            start, &previous_start, period);

        monitoring_count++;

        /* TODO: insert the validated telemetry workload. */

        EndMeasurement(
            &telemetry_timing, telemetry_execution_trace,
            slot, start, xTaskGetTickCount(), period);

        (void)xTaskDelayUntil(&last_wake, period);
    }
}

int main(void)
{
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
