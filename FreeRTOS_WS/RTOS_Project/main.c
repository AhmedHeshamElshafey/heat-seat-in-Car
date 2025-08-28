/* Kernel includes. */
#include "MCAL/tm4c123gh6pm_registers.h"
#include "Common/std_types.h"
#include <HAL/POTS/pots.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "GPTM.h"
#include "gpio.h"
#include "uart0.h"
#include "event_groups.h"

#define DRIVERSEAT_EVENTBIT                (1UL<<0UL)
#define PASSENGERSEAT_EVENTBIT             (1UL<<1UL)
#define DRIVERMASSAGE                          0
#define PASSENGERMASSAGE                       1

typedef enum
{
    OFF,
    LEVEL_LOW=25,
    LEVEL_MEDIUM=30,
    LEVEL_HIGH=35
} HEATINGLEVEL_t;

typedef enum
{
    DISABLED,
    LOW_INTENSITY,
    MEDIUM_INTENSITY,
    HIGH_INTENSITY
} HEATERSTATE_t;
typedef struct
{
    TaskHandle_t xTaskHandle;
    uint8 ucMassage[2];
} TASKMASSAGE_t;

typedef struct
{
    uint8 ucCurrentTemp;
    HEATINGLEVEL_t xHeatingLevel;
    HEATERSTATE_t  xHeaterState;
} DISPLAYMASSAGE_t;

typedef struct
{
    uint8 Diagnostics_TEMP;
    uint32 Diagnostics_TIME;
    HEATERSTATE_t Diagnostics_HEATERINTENSITY;
} Diagnostics_MASSAGE;

DISPLAYMASSAGE_t xDisplayMassages[2]= {{0,OFF,DISABLED},{0,OFF,DISABLED}};
Diagnostics_MASSAGE xFAILER_STATES[2];
uint32 ulTimeIn[7]={0};
uint32 ulTimeOut[7]={0};
uint32 ulExecutionTime[7]={0};
uint32 CPU_LOAd=0;
/**********************************************TASK HANDELS ***********************************************/
TaskHandle_t BUTTONTASKHANDER;
TaskHandle_t MEASURMENTTEMPTASHHANDLE;
TaskHandle_t DiagnosticsTASKHANDLE;
TaskHandle_t DISPLAYTASKHANDLE;
TaskHandle_t PROCESSINGTASKHANDLE;
TaskHandle_t RuntimeMeasurementTaskHandle;
/**********************************************************************************************************/


/**********************************************KERNEL OBJECTS HANDELS *************************************/

EventGroupHandle_t xBUTTONFLAGs;
QueueHandle_t xDisplayQUEUE;
SemaphoreHandle_t xDiagnosticsSem;
SemaphoreHandle_t xMutex;
/**********************************************************************************************************/

/*************************************************INTERRUPT CALLBACKs**************************************/
void vButtonPressed (void)
{
    BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
       if(GPIO_PORTF_RIS_REG & (1<<0))           /* PF0 handler code */
       {
           xEventGroupSetBitsFromISR(xBUTTONFLAGs, DRIVERSEAT_EVENTBIT,&pxHigherPriorityTaskWoken);
           GPIO_PORTF_ICR_REG   |= (1<<0);       /* Clear Trigger flag for PF0 (Interrupt Flag) */
       }
       else if(GPIO_PORTF_RIS_REG & (1<<4))      /* PF4 handler code */
       {
           xEventGroupSetBitsFromISR(xBUTTONFLAGs, PASSENGERSEAT_EVENTBIT,&pxHigherPriorityTaskWoken);
           GPIO_PORTF_ICR_REG   |= (1<<4);       /* Clear Trigger flag for PF4 (Interrupt Flag) */
       }
       portYIELD_FROM_ISR( pxHigherPriorityTaskWoken );
}


/**********************************************************************************************************/
void vButtonHandlerTask (void *pvParameters)
{
    TASKMASSAGE_t xSendedMassage;
    uint8 ucStatesMassages[4]= {'O','L','M','H'};
    uint8 ucStatusNumberDriverSeat=0;
    uint8 ucStatusNumberPassengerSeat=0;
    HEATINGLEVEL_t xHeatingStatus[4]= {OFF,LEVEL_LOW,LEVEL_MEDIUM,LEVEL_HIGH};
    const EventBits_t xEventWaitedbits = DRIVERSEAT_EVENTBIT | PASSENGERSEAT_EVENTBIT;
    EventBits_t xEventGroupValue ;
    HEATINGLEVEL_t xDriverSeat_HeatingValue=OFF;
    HEATINGLEVEL_t xPassengerSeat_HeatingValue=OFF;
    while(1)
    {
        xEventGroupValue=xEventGroupWaitBits(xBUTTONFLAGs,xEventWaitedbits,pdTRUE,pdFALSE,portMAX_DELAY);
        xSendedMassage.xTaskHandle= BUTTONTASKHANDER;
        if (DRIVERSEAT_EVENTBIT == xEventGroupValue)
        {
            xSendedMassage.ucMassage[0]='D';
            if (xDriverSeat_HeatingValue == LEVEL_HIGH)
            {
                xDriverSeat_HeatingValue = OFF;
                ucStatusNumberDriverSeat=0;
            }
            else
            {
                xDriverSeat_HeatingValue=xHeatingStatus[++ucStatusNumberDriverSeat];
            }
            xSendedMassage.ucMassage[1]=ucStatesMassages[ucStatusNumberDriverSeat];
            xQueueSend(xDisplayQUEUE,&xSendedMassage,portMAX_DELAY);
        }

        if (PASSENGERSEAT_EVENTBIT == xEventGroupValue)
        {
            xSendedMassage.ucMassage[0]='P';
            if (xPassengerSeat_HeatingValue == LEVEL_HIGH)
            {
                xPassengerSeat_HeatingValue = OFF;
                ucStatusNumberPassengerSeat=0;
            }
            else
            {
                xPassengerSeat_HeatingValue=xHeatingStatus[++ucStatusNumberPassengerSeat];
            }
            xSendedMassage.ucMassage[1]=ucStatesMassages[ucStatusNumberPassengerSeat];
            xQueueSend(xDisplayQUEUE,&xSendedMassage,portMAX_DELAY);
        }
    }
}

void vProcessingTask (void *pvParameters)
{
    TASKMASSAGE_t xReceivedMassage;
    uint8 MassegeNumber=DRIVERMASSAGE;
    sint8 TEMP=0;
    while(1)
    {

        if(xQueueReceive(xDisplayQUEUE,&xReceivedMassage,portMAX_DELAY)==pdTRUE)
        {

            if (BUTTONTASKHANDER == xReceivedMassage.xTaskHandle )
            {
                if ('D' == xReceivedMassage.ucMassage[0])
                {
                    MassegeNumber = DRIVERMASSAGE;
                }
                else if ('P' == xReceivedMassage.ucMassage[0])
                {
                    MassegeNumber = PASSENGERMASSAGE;
                }
                else
                {

                }
                switch (xReceivedMassage.ucMassage[1])
                {
                    case 'O' :
                    xDisplayMassages[MassegeNumber].xHeatingLevel = OFF;
                    break;
                    case 'L' :
                    xDisplayMassages[MassegeNumber].xHeatingLevel = LEVEL_LOW;
                    break;
                    case 'M' :
                    xDisplayMassages[MassegeNumber].xHeatingLevel = LEVEL_MEDIUM;
                    break;
                    case 'H' :
                    xDisplayMassages[MassegeNumber].xHeatingLevel = LEVEL_HIGH;
                    break;
                    default:
                    ;
                }
            }

            if (MEASURMENTTEMPTASHHANDLE == xReceivedMassage.xTaskHandle )
            {
                xDisplayMassages[DRIVERMASSAGE].ucCurrentTemp=xReceivedMassage.ucMassage[0];
                xDisplayMassages[PASSENGERMASSAGE].ucCurrentTemp=xReceivedMassage.ucMassage[0];
                TEMP=xReceivedMassage.ucMassage[0];
                GPIO_RedLedOff();
                GPIO_BlueLedOff();
                GPIO_GreenLedOff();
                if ((TEMP<=40)  && (TEMP>=5))
                {

                    for ( MassegeNumber = DRIVERMASSAGE ; MassegeNumber<=PASSENGERMASSAGE;  MassegeNumber++)
                    {
                        if (xDisplayMassages[MassegeNumber].xHeatingLevel!=OFF)
                        {
                            TEMP=((xDisplayMassages[MassegeNumber].xHeatingLevel)-xDisplayMassages[MassegeNumber].ucCurrentTemp);
                            if (TEMP>10)
                            {
                                xDisplayMassages[MassegeNumber].xHeaterState=HIGH_INTENSITY;
                                GPIO_BlueLedOn();
                                GPIO_GreenLedOn();  //  cyan color
                            }
                            else if ((5<TEMP) && (TEMP<=10))
                            {
                                xDisplayMassages[MassegeNumber].xHeaterState=MEDIUM_INTENSITY;
                                GPIO_BlueLedOn();  //  blue color
                            }
                            else if ((2<TEMP) && (TEMP<=5))
                            {
                                xDisplayMassages[MassegeNumber].xHeaterState=LOW_INTENSITY;
                                GPIO_GreenLedOn();  //  GREAN color
                            }
                            else
                            {
                                xDisplayMassages[MassegeNumber].xHeaterState=DISABLED;
                            }

                        }
                    }
                }
                else
                {
                    xSemaphoreGive(xDiagnosticsSem);
                    xDisplayMassages[DRIVERMASSAGE].xHeaterState=DISABLED;
                    xDisplayMassages[PASSENGERMASSAGE].xHeaterState=DISABLED;
                    GPIO_RedLedOn();
                }
            }


        }
    }
}
void vdisplayTask (void *pvParameters)
{

    TickType_t xDelay3000=pdMS_TO_TICKS(3000);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint8 MassegeNumber;
    while(1)
    {
        vTaskDelayUntil(&xLastWakeTime, xDelay3000);

        UART0_SendString("CAR TEMPREATURE  : ");
        UART0_SendInteger( xDisplayMassages[0].ucCurrentTemp);
        UART0_SendString("\r\n---------------------------------------\r\n");
        for ( MassegeNumber = DRIVERMASSAGE ; MassegeNumber<=PASSENGERMASSAGE;  MassegeNumber++)
        {
            if (MassegeNumber == DRIVERMASSAGE)
            {
                UART0_SendString("DRIVER SEAT : \r\n");
            }
            else
            {
                UART0_SendString("PASSENGER SEAT : \r\n");
            }
            UART0_SendString("HEATING LEVEL -->  ");
            switch (xDisplayMassages[MassegeNumber].xHeatingLevel)
            {
                case OFF :
                    UART0_SendString("OFF \r\n");
                break;
                case LEVEL_LOW :
                    UART0_SendString("LOW \r\n");
                break;
                case LEVEL_MEDIUM :
                    UART0_SendString("MEDIUM \r\n");
                break;
                case LEVEL_HIGH :
                    UART0_SendString("HIGH \r\n");
                break;
                default:
                ;
            }
            UART0_SendString("HEATER STATE -->  ");
            switch (xDisplayMassages[MassegeNumber].xHeaterState)
            {
                case DISABLED :
                    UART0_SendString(" DISABLED\r\n");
                break;
                case LOW_INTENSITY :
                    UART0_SendString("LOW INTENSITY \r\n");
                break;
                case MEDIUM_INTENSITY :
                    UART0_SendString("MEDIUM INTENSITY\r\n");
                break;
                case HIGH_INTENSITY :
                    UART0_SendString("HIGH INTENSITY \r\n");
                break;
                default:
                ;
            }


        }
        UART0_SendString("================================================= \r\n");
        UART0_SendString("Runtime measurment :\r\n");
        UART0_SendString("vButtonHandlerTask : ");
        UART0_SendInteger(ulExecutionTime[1]);
        UART0_SendString("ms                  |       vdisplayTask : ");
        UART0_SendInteger(ulExecutionTime[2]);
        UART0_SendString("ms\rvProcessingTask : ");
        UART0_SendInteger(ulExecutionTime[3]);
        UART0_SendString("ms                  |       vTempretureMeasurementTask : ");
        UART0_SendInteger(ulExecutionTime[4]);
        UART0_SendString("ms\rvDiagnosticsTask : ");
        UART0_SendInteger(ulExecutionTime[5]);
        UART0_SendString("ms                  |       vRuntimeMeasurementTask : ");
        UART0_SendInteger(ulExecutionTime[6]);
        UART0_SendString("ms\rCPU LOAD ----------->  ");
        UART0_SendInteger(CPU_LOAd);
        UART0_SendString("%\r================================================= \r\n");


    }
}

void vTempretureMeasurementTask(void *pvParameters)
{
    TASKMASSAGE_t xSendedMassage;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    TickType_t xDelay1000=pdMS_TO_TICKS(1000);
    uint32 POT_PERCENTAGE=0;
    uint8 TEM_VALUE=0;
    while(1)
    {
        vTaskDelayUntil(&xLastWakeTime, xDelay1000);
        POT_PERCENTAGE= POT_getValue() ;
        TEM_VALUE=(POT_PERCENTAGE * 45) / POT_MAX_VALUE;
        xSendedMassage.xTaskHandle=MEASURMENTTEMPTASHHANDLE;
        xSendedMassage.ucMassage[0]=TEM_VALUE;
        xQueueSend(xDisplayQUEUE,&xSendedMassage,portMAX_DELAY);
    }
}

 void vDiagnosticsTask (void *pvParameters)
 {
     uint8 MassegeNumber;
  while(1)
  {
      if (xSemaphoreTake(xDiagnosticsSem,portMAX_DELAY)==pdTRUE)
      {
          if (xSemaphoreTake(xMutex,portMAX_DELAY)==pdTRUE)
          {
              for ( MassegeNumber = DRIVERMASSAGE ; MassegeNumber<=PASSENGERMASSAGE;  MassegeNumber++)
              {
                  xFAILER_STATES[MassegeNumber].Diagnostics_TIME=GPTM_WTimer0Read();
                  xFAILER_STATES[MassegeNumber].Diagnostics_TEMP=xDisplayMassages[MassegeNumber].ucCurrentTemp;
                  xFAILER_STATES[MassegeNumber].Diagnostics_HEATERINTENSITY=xDisplayMassages[MassegeNumber].xHeaterState;
              }
              xSemaphoreGive(xMutex);
          }
          GPIO_RedLedOn();

      }
  }
 }

 void vRuntimeMeasurementTask (void *pvParameters)
 {
  uint8 Counter;
  TickType_t xLastWakeTime = xTaskGetTickCount();
  TickType_t xDelay3000=pdMS_TO_TICKS(3000);
  while(1)
  {
      vTaskDelayUntil(&xLastWakeTime, xDelay3000);
      for ( Counter=1;Counter<7;Counter++)
      {
          CPU_LOAd+=ulExecutionTime[Counter];
      }
      CPU_LOAd=(CPU_LOAd*100)/GPTM_WTimer0Read();
  }
 }

void prvHWInit(void)
{

    UART0_Init();
    GPIO_BuiltinButtonsLedsInit();
    GPIO_SW1EdgeTriggeredInterruptInit();
    GPIO_SW2EdgeTriggeredInterruptInit();
    POT_init ();
    GPTM_WTimer0Init();

}

void vApplicationMallocFailedHook( void )
{
     GPIO_RedLedOn();
     GPIO_BlueLedOn();
     GPIO_GreenLedOn();   // white color indicate development error
    for(;;);
}
void vApplicationStackOverflowHook( TaskHandle_t xTask, char * pcTaskName )
{
    GPIO_RedLedOn();
    GPIO_BlueLedOn();
    GPIO_GreenLedOn();   // white color indicate development error
    UART0_SendString((const uint8 *)pcTaskName);
    for(;;);

}

int main(void)
{
    prvHWInit();

    /******************************************* TASK CREATION *********************************/
    xTaskCreate(vButtonHandlerTask,"TASK1",100,NULL,6,&BUTTONTASKHANDER);
    xTaskCreate(vdisplayTask,"TASK2",80,NULL,1,&DISPLAYTASKHANDLE);
    xTaskCreate(vProcessingTask,"TASK3",80,NULL,4,&PROCESSINGTASKHANDLE);
    xTaskCreate(vTempretureMeasurementTask,"TASK4",70,NULL,5,&MEASURMENTTEMPTASHHANDLE);
    xTaskCreate(vDiagnosticsTask,"TASK5",80,NULL,7,&DiagnosticsTASKHANDLE);
    xTaskCreate(vRuntimeMeasurementTask,"Task6",70,NULL,2,&RuntimeMeasurementTaskHandle);
    /*******************************************************************************************/

/******************************************* TASK TAGS *********************************/
vTaskSetApplicationTaskTag(BUTTONTASKHANDER, ( TaskHookFunction_t ) 1 );
vTaskSetApplicationTaskTag(DISPLAYTASKHANDLE, ( TaskHookFunction_t ) 2 );
vTaskSetApplicationTaskTag(PROCESSINGTASKHANDLE, ( TaskHookFunction_t ) 3 );
vTaskSetApplicationTaskTag(MEASURMENTTEMPTASHHANDLE, ( TaskHookFunction_t ) 4 );
vTaskSetApplicationTaskTag(DiagnosticsTASKHANDLE, ( TaskHookFunction_t ) 5 );
vTaskSetApplicationTaskTag(RuntimeMeasurementTaskHandle, ( TaskHookFunction_t ) 6 );
/*******************************************************************************************/
    /******************************************* KERNEL OBJECTS CREATION ***********************/
    xBUTTONFLAGs= xEventGroupCreate();
    xDisplayQUEUE = xQueueCreate(10, sizeof(TASKMASSAGE_t));
    xDiagnosticsSem=xSemaphoreCreateBinary();
    xMutex=xSemaphoreCreateMutex();
    /*******************************************************************************************/
    vTaskStartScheduler();
    for(;;);
}
