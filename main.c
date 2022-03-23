/* Standard includes. */
// stdlib.h needed for rand, time.h needed for time
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "stm32f4_discovery.h"
/* Kernel includes. */
#include "stm32f4xx.h"
#include "../FreeRTOS_Source/include/FreeRTOS.h"
#include "../FreeRTOS_Source/include/queue.h"
#include "../FreeRTOS_Source/include/semphr.h"
#include "../FreeRTOS_Source/include/task.h"
#include "../FreeRTOS_Source/include/timers.h"

/*-----------------------------------------------------------*/
#define mainQUEUE_LENGTH 100
#define STATIC_TASK_STACK 200

StackType_t xStack [STATIC_TASK_STACK];

enum task_type {PERIODIC,APERIODIC};

// Struct Inits
struct dd_task {
	TaskHandle_t t_handle;
	uint32_t type;
	uint32_t task_id;
	uint32_t release_time;
	uint32_t execution_time;
	uint32_t absolute_deadline;
	uint32_t completion_time;
};

struct dd_task_list {
	struct dd_task task;
	struct dd_task_list *next_task;
};

// DD Prototypes
void release_dd_task(uint32_t type, uint32_t task_id, uint32_t execution_time, uint32_t absolute_deadline);
void complete_dd_task(uint32_t task_id);
void get_active_dd_task_list(void);
void get_complete_dd_task_list(void);
void get_overdue_dd_task_list(void);

// F Prototypes
void xDeadlineScheduler(void *pvParameters);
void xUserTasks(void *pvParameters);
void xDeadlineTask1Generator(void *pvParameters);
void xDeadlineTask2Generator(void *pvParameters);
void xDeadlineTask3Generator(void *pvParameters);
void xMonitorTask(void *pvParameters);

// Queue Definitions
xQueueHandle message_release_queue = 0;
xQueueHandle message_response_queue = 0;

/*-----------------------------------------------------------*/

int main(void) {
	// Test bench variable
	int testBench = 1;

	// Initialize System
	SystemInit();

	// Create the tasks
	xTaskCreate(xDeadlineScheduler, "DeadlineScheduler", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
	xTaskCreate(xUserTasks, "UserTasks", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
	xTaskCreate(xDeadlineTask1Generator, "DeadlineTaskGenerator", configMINIMAL_STACK_SIZE, (void *) testBench, 2, NULL);
	xTaskCreate(xDeadlineTask2Generator, "DeadlineTaskGenerator", configMINIMAL_STACK_SIZE, (void *) testBench, 2, NULL);
	xTaskCreate(xDeadlineTask3Generator, "DeadlineTaskGenerator", configMINIMAL_STACK_SIZE, (void *) testBench, 2, NULL);
	xTaskCreate(xMonitorTask, "MonitorTask", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

	// Get size of struct
	int structSize = sizeof(struct dd_task);

	// Create message queue and message response queue
	message_release_queue = xQueueCreate(1,structSize);
	message_response_queue = xQueueCreate(1,structSize);
	vQueueAddToRegistry(message_release_queue, "ReleaseMessage");
	vQueueAddToRegistry(message_response_queue, "ReponseMessage");

	/* Start Scheduler */
	vTaskStartScheduler();

	return 0;
}

/*-----------------------------------------------------------*/

 void xDeadlineTask1Generator(void *pvParameters) {
	// Read test bench
	int bench = (int) pvParameters;

	uint32_t count = 0;
	uint32_t period;
	uint32_t execution;
	while(1){
		if(bench == 1){
			period = 500; execution = 95;
		} else if(bench == 2){
			period = 250; execution = 95;
		}else{
			period = 500; execution = 100;
		}
		// Increment count by period
		count = count + period;
		// Releasing task
		release_dd_task(0, 1, execution, count);
		// After task released delay by period
		vTaskDelay(pdMS_TO_TICKS(period));
	}
}

void xDeadlineTask2Generator(void *pvParameters) {
	// Read test bench
	int bench = (int) pvParameters;

	uint32_t count = 0;
	uint32_t period;
	uint32_t execution;
	while(1){
		if(bench == 1){
			period = 500; execution = 150;
		} else if(bench == 2){
			period = 500; execution = 150;
		}else{
			period = 500; execution = 200;
		}
		// Increment count by period
		count = count + period;
		// Releasing task
		release_dd_task(0, 2, execution, count);
		// After task released delay by period
		vTaskDelay(pdMS_TO_TICKS(period));
	}
}

void xDeadlineTask3Generator(void *pvParameters) {
	// Read test bench
	int bench = (int) pvParameters;

	uint32_t count = 0;
	uint32_t period;
	uint32_t execution;
	while(1){
		if(bench == 1){
			period = 750; execution = 250;
		} else if(bench == 2){
			period = 750; execution = 250;
		}else{
			period = 500; execution = 200;
		}
		// Increment count by period
		count = count + period;
		// Releasing task
		release_dd_task(0, 3, execution, count);
		// After task released delay by period
		vTaskDelay(pdMS_TO_TICKS(period));
	}
}

/*-----------------------------------------------------------*/

void complete_dd_task(uint32_t task_id) {

}

/*-----------------------------------------------------------*/

void release_dd_task(uint32_t type, uint32_t task_id, uint32_t execution_time, uint32_t absolute_deadline) {
	// Create new task struct with given parameters
	struct dd_task task;
	task.type = type;
	task.task_id = task_id;
	task.execution_time = execution_time;
	task.absolute_deadline = absolute_deadline;

	// Next send the struct via message queue
	xQueueSend(message_release_queue, &task, 50);

	// Lastly await response from xDeadlineScheduler
	int response;
	while(1) if (xQueueReceive(message_response_queue, &response, 50)) break;
}

/*-----------------------------------------------------------*/

void xUserTasks(void *pvParameters) {
	// Get times
	int executionTime = (int) pvParameters;
	int startTime = (int) xTaskGetTickCount();
	int endTime = startTime + executionTime;

	// Wait until endTime has been passed
	while(endTime > (int) xTaskGetTickCount());

	// Send completed task to completed_queue
	complete_dd_task(xTaskGetCurrentTaskHandle());
}

/*-----------------------------------------------------------*/

void xDeadlineScheduler(void *pvParameters) {
	// Create active_task_list
	struct dd_task_list *active_task_list = (struct dd_task_list*) malloc(sizeof(struct dd_task_list));
	// Set second value to NULL
	active_task_list->next_task = NULL;

	// Create completed_task_list
	struct dd_task_list *completed_task_list = (struct dd_task_list*) malloc(sizeof(struct dd_task_list));
	// Set second value to NULL
	completed_task_list->next_task = NULL;

	// Create overdue_task_list
	struct dd_task_list *overdue_task_list = (struct dd_task_list*) malloc(sizeof(struct dd_task_list));
	// Set second value to NULL
	overdue_task_list->next_task = NULL;

	struct dd_task user_task;

	while(1){
		if(xQueueReceive(message_release_queue, &user_task, 50)){
			// Init new list node
			struct dd_task_list *new_user_task = (struct dd_task_list*) malloc(sizeof(struct dd_task_list));
			new_user_task->task = user_task;
			// Create copy of head of active_task_list
			struct dd_task_list *active_head = active_task_list;
			// Iterate through active_task_list and new user_task to list based on absolute_deadline
			while(active_head != NULL) {
				// Check asbolute deadline of task in active list against new task
				if(active_head->next_task->task.absolute_deadline > new_user_task->task.absolute_deadline) {
					new_user_task->next_task = active_head->next_task;
					active_head->next_task = new_user_task;
					break;
				}
				active_head = active_head->next_task;
			}
			new_user_task->next_task = active_head->next_task;
			active_head->next_task = new_user_task;
		}
		// Next check message_release_queue again, if there's a value continue again to start of while loop
		// Otherwise start the task at the head of the active_list
		if(xQueuePeek(message_release_queue, &user_task, 50)) {
			continue;
		} else {
			// Get head of list
			struct dd_task task_to_run = active_task_list->next_task->task;
			// Create new task
			xTaskCreate(xUserTasks, "UserDefinedTask", configMINIMAL_STACK_SIZE,(void *) task_to_run.execution_time, 1, &(task_to_run.t_handle));
			// Set head of list to next_task
			active_task_list = active_task_list->next_task;


			//wait to recieve message from complete_dd_task

			// Add struct to completed_tasks list


		}




	}


}

/*-----------------------------------------------------------*/

void xMonitorTask(void *pvParameters) {
	// Delay the next task by 1000 ticks
	vTaskDelay(1000);
}

/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
	/* The malloc failed hook is enabled by setting
	configUSE_MALLOC_FAILED_HOOK to 1 in FreeRTOSConfig.h.
	Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software 
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( xTaskHandle pxTask, signed char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected.  pxCurrentTCB can be
	inspected in the debugger if the task name passed into this function is
	corrupt. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
volatile size_t xFreeStackSpace;

	/* The idle task hook is enabled by setting configUSE_IDLE_HOOK to 1 in
	FreeRTOSConfig.h.
	This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amount of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if( xFreeStackSpace > 100 )
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}
}
