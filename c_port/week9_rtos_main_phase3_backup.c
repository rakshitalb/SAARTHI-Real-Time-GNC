#include "FreeRTOS.h"
#include "task.h"
#include "control.h"
#include "week9_interface.h"

volatile unsigned long gnc_count = 0;
volatile unsigned long sensing_count = 0;
volatile unsigned long communications_count = 0;
volatile unsigned long communications_last_timestamp = 0;
volatile unsigned long communications_last_status = 0;
volatile unsigned long monitoring_count = 0;

/* Week 9 GNC -> Communications/Telemetry interface */
volatile GNC_Telemetry gnc_telemetry = {0};

static StaticTask_t idle_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];


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


/* -----------------------------------------------------------
 * GNC / Control Task
 *
 * Produces the torque command and publishes it through
 * the Week 9 GNC_Telemetry interface.
 * ----------------------------------------------------------- */
static void GNC_Task(void *pvParameters)
{
    (void)pvParameters;

    const double q_err[3] = {
        0.01,
        -0.02,
        0.03
    };

    const double omega[3] = {
        0.10,
        -0.05,
        0.02
    };

    double torque[3];

    for (;;)
    {
        gnc_count++;

        /* Execute the verified PD controller */
        pd_controller(q_err, omega, torque);

        /*
         * Publish GNC result to the shared
         * Communications/Telemetry interface.
         */
        gnc_telemetry.timestamp = gnc_count;

        gnc_telemetry.status = 1;

        gnc_telemetry.torque_x = torque[0];
        gnc_telemetry.torque_y = torque[1];
        gnc_telemetry.torque_z = torque[2];

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


/* -----------------------------------------------------------
 * Sensing / Estimation Task
 * ----------------------------------------------------------- */
static void Sensing_Task(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        sensing_count++;

        /* Sensing/estimation placeholder */

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


/* -----------------------------------------------------------
 * Communications Task
 *
 * Consumes the GNC telemetry artifact produced by GNC_Task.
 * ----------------------------------------------------------- */
static void Communications_Task(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        communications_count++;

        /*
         * Read the latest GNC telemetry artifact.
         *
         * The volatile reads demonstrate that the
         * Communications task is consuming the interface.
         */
        unsigned long timestamp =
            gnc_telemetry.timestamp;

        unsigned long status =
            gnc_telemetry.status;

        double torque_x =
            gnc_telemetry.torque_x;

        double torque_y =
            gnc_telemetry.torque_y;

        double torque_z =
            gnc_telemetry.torque_z;

        /*
         * Prevent compiler removal of the interface reads.
         */
        communications_last_timestamp = timestamp;
        communications_last_status = status;
        (void)torque_x;
        (void)torque_y;
        (void)torque_z;

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


/* -----------------------------------------------------------
 * Monitoring / Telemetry Task
 * ----------------------------------------------------------- */
static void Monitoring_Task(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        monitoring_count++;

        /* Monitoring/telemetry placeholder */

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


/* -----------------------------------------------------------
 * Static task memory
 * ----------------------------------------------------------- */

static StaticTask_t gnc_tcb;
static StaticTask_t sensing_tcb;
static StaticTask_t communications_tcb;
static StaticTask_t monitoring_tcb;

static StackType_t gnc_stack[configMINIMAL_STACK_SIZE];
static StackType_t sensing_stack[configMINIMAL_STACK_SIZE];
static StackType_t communications_stack[configMINIMAL_STACK_SIZE];
static StackType_t monitoring_stack[configMINIMAL_STACK_SIZE];


/* -----------------------------------------------------------
 * Main
 * ----------------------------------------------------------- */

int main(void)
{
    xTaskCreateStatic(
        GNC_Task,
        "GNC",
        configMINIMAL_STACK_SIZE,
        NULL,
        4,
        gnc_stack,
        &gnc_tcb
    );

    xTaskCreateStatic(
        Sensing_Task,
        "SENSE",
        configMINIMAL_STACK_SIZE,
        NULL,
        3,
        sensing_stack,
        &sensing_tcb
    );

    xTaskCreateStatic(
        Communications_Task,
        "COMMS",
        configMINIMAL_STACK_SIZE,
        NULL,
        2,
        communications_stack,
        &communications_tcb
    );

    xTaskCreateStatic(
        Monitoring_Task,
        "MON",
        configMINIMAL_STACK_SIZE,
        NULL,
        1,
        monitoring_stack,
        &monitoring_tcb
    );

    vTaskStartScheduler();

    /* Scheduler should not return. */
    for (;;)
    {
    }
}

