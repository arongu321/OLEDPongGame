// Include FreeRTOS Libraries
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Include xilinx Libraries
#include "xparameters.h"
#include "xgpio.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include "xil_cache.h"

// Other miscellaneous libraries
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include "pmodkypd.h"
#include "sleep.h"
#include "PmodOLED.h"
#include "OLEDControllerCustom.h"
#include <stdbool.h>

#define FRAME_DELAY 50000


// Declaring the devices
PmodOLED oledDevice;
PmodKYPD KYPDInst;
XGpio btnInst;

// Keypad ID declaration
#define KYPD_DEVICE_ID		XPAR_KEYPAD_DEVICE_ID

// Button ID declaration
#define BTN_DEVICE_ID	   XPAR_GPIO_0_DEVICE_ID

// Button pressed values
#define BTN0 1
#define BTN1 2
#define BTN2 4
#define BTN3 8

// Keypad key table
#define DEFAULT_KEYTABLE	"0FED789C456B123A"

// Function prototypes
void initializeScreen();
static void oledTask( void *pvParameters );
static void oledTaskSweep(void* pvParameters);
static void GameTask( void *pvParameters );
void InitializeKeypad();
static void keypadTask( void *pvParameters );
static void buttonTask( void *pvParameters );

static QueueHandle_t xGameInputQueue = NULL;
static QueueHandle_t xButtonQueue = NULL;

// To change between PmodOLED and OnBoardOLED is to change Orientation
const u8 orientation = 0x0; // Set up for Normal PmodOLED(false) vs normal
                            // Onboard OLED(true)
const u8 invert = 0x0; // true = whitebackground/black letters
                       // false = black background /white letters

// Global variable to monitor if game is paused
bool paused = false;

int main()
{
	int status;
	// Initialize Devices
	initializeScreen();
	InitializeKeypad();

	status = XGpio_Initialize(&btnInst, BTN_DEVICE_ID); // Initialize buttons on Zybo
	if(status != XST_SUCCESS){
		xil_printf("GPIO Initialization for SSD failed.\r\n");
		return XST_FAILURE;
	}

	xil_printf("Initialization Complete, System Ready!\n");

	xTaskCreate( GameTask					/* The function that implements the task. */
					   , "Ping Pong Game"         /* Text name for the task, provided to assist debugging only. */
					   , configMINIMAL_STACK_SIZE	/* The stack allocated to the task. */
					   , NULL						/* The task parameter is not used, so set to NULL. */
					   , tskIDLE_PRIORITY			/* The task runs at the idle priority. */
					   , NULL
					   );

	xTaskCreate(keypadTask,					/* The function that implements the task. */
					"keypad task", 				/* Text name for the task, provided to assist debugging only. */
					configMINIMAL_STACK_SIZE, 	/* The stack allocated to the task. */
					NULL, 						/* The task parameter is not used, so set to NULL. */
					tskIDLE_PRIORITY,			/* The task runs at the idle priority. */
					NULL);

	xTaskCreate(buttonTask,					/* The function that implements the task. */
					"button task", 				/* Text name for the task, provided to assist debugging only. */
					configMINIMAL_STACK_SIZE, 	/* The stack allocated to the task. */
					NULL, 						/* The task parameter is not used, so set to NULL. */
					tskIDLE_PRIORITY,			/* The task runs at the idle priority. */
					NULL);

	xGameInputQueue = xQueueCreate(1, sizeof(char));
	xButtonQueue = xQueueCreate(1, sizeof(char));

	vTaskStartScheduler();


   while(1);

   return 0;
}


void initializeScreen()
{
   OLED_Begin(&oledDevice, XPAR_PMODOLED_0_AXI_LITE_GPIO_BASEADDR,
         XPAR_PMODOLED_0_AXI_LITE_SPI_BASEADDR, orientation, invert);
}

void InitializeKeypad(){
	KYPD_begin(&KYPDInst, XPAR_KEYPAD_BASEADDR);
	KYPD_loadKeyTable(&KYPDInst, (u8*) DEFAULT_KEYTABLE);
}

static void buttonTask( void *pvParameters ) {
	unsigned int buttonVal = 0, lastButtonVal = BTN0;

	while (1) {
		// Read button clicked from user
		buttonVal = XGpio_DiscreteRead(&btnInst, 1);

		// BTN3 is clicks so send the x character to queue to pause/resume game
		if(buttonVal == BTN3 && lastButtonVal == 0){
			u8 pause = 'x';
			xQueueSend(xButtonQueue, &pause, 0);
		}

		lastButtonVal = buttonVal;
	}
}

static void keypadTask( void *pvParameters )
{
   u16 keystate;
   XStatus status, last_status = KYPD_NO_KEY;
   u8 new_key, current_key = 'x', previous_key = 'x';

   xil_printf("Pmod KYPD app started. Press any key on the Keypad.\r\n");
      while (1) {
		  // Capture state of the keypad
		  keystate = KYPD_getKeyStates(&KYPDInst);

		  // Determine which single key is pressed, if any
		  // if a key is pressed, store the value of the new key in new_key
		  status = KYPD_getKeyPressed(&KYPDInst, keystate, &new_key);

		  // Print key detect if a new key is pressed or if status has changed
		  if (status == KYPD_SINGLE_KEY && last_status == KYPD_NO_KEY){
			 xil_printf("Key Pressed: %c\r\n", (char) new_key);
			 // pass the value of current_key to previous_key
			 previous_key = current_key;
			 xQueueSend(xGameInputQueue, &new_key, 0);
			 // store the new key pressed by the user
			 current_key = new_key;
		  } else if (status == KYPD_MULTI_KEY && status != last_status){
			 xil_printf("Error: Multiple keys pressed\r\n");
		  }

		  last_status = status;
      }
}

static void GameTask(void* pvParameters) {
	while(1) {
		// Menu for pong game
		char entered_char;
		OLED_ClearBuffer(&oledDevice);
		OLED_SetCursor(&oledDevice, 0, 0);
		OLED_PutString(&oledDevice, "Welcome to the \n Pong Game!");
		vTaskDelay(100);
		OLED_ClearBuffer(&oledDevice);
		OLED_SetCursor(&oledDevice, 0, 0);
		OLED_PutString(&oledDevice, "Press 1 to play");
		OLED_Update(&oledDevice);

		// Wait until user enters 1 in keypad to begin pong game
		while(1){
			if(xQueueReceive(xGameInputQueue, &entered_char, 0) == pdTRUE){
				if(entered_char == '1'){
					break;
				}
			}
			else{
				continue;
			}
		}

		// Game on message for pong game
		OLED_ClearBuffer(&oledDevice);
		OLED_SetCursor(&oledDevice, 0, 0);
		OLED_PutString(&oledDevice, "Game On!");
		OLED_Update(&oledDevice);

		// Draw paddle on left side(user paddle)
		OLED_ClearBuffer(&oledDevice);
		OLED_MoveTo(&oledDevice, 10, 12);
		OLED_DrawLineTo(&oledDevice, 10, 28);

		// Draw paddle on right side(CPU paddle)
		OLED_MoveTo(&oledDevice, 117, 12);
		OLED_DrawLineTo(&oledDevice, 117, 28);

		OLED_Update(&oledDevice);

		// Initial pong ball coordinates
		float irow = 16;
		float icol = 64;

		// Initial left y coordinate of user paddle
		int userRow = 12;

		// Initial left y coordinate of CPU paddle
		int CPURow = 12;

		// Variables to change direction of pong ball and CPU paddle movement
		bool increment_rows = false;
		bool increment_cols = false;
		bool CPUIncrement = false;

		// Draw ball in middle of screen
		OLED_MoveTo(&oledDevice, 64, 16);
		OLED_DrawPixel(&oledDevice);
		OLED_Update(&oledDevice);

		// Score counters for user and CPU
		int userScore = 0;
		int CPUScore = 0;

		// Pause status
		u8 pauseStatus;


		// Play game until either the user or CPU wins 3 matches
		while((userScore < 3) && (CPUScore < 3)) {

			// CPU paddle is moving up
			if (CPUIncrement) {
				CPURow++;

				// CPU paddle reaches top of OLED screen so move CPU paddle down
				if (CPURow >= OledRowMax - 16){
					CPUIncrement = false;
					CPURow--;
				}

			// CPU paddle is moving down
			} else {
				CPURow--;

				// CPU paddle reaches top of OLED screen so move CPU paddle down
				if (CPURow < 0){
					CPUIncrement = true;
					CPURow++;
				}
			}

			if(xQueueReceive(xGameInputQueue, &entered_char, 0) == pdTRUE){
				// User enters 6 on keypad to move user paddle right
				if(entered_char == '6'){
					if((userRow+3) < (OledRowMax - 17)) { // If left coordinate of paddle is less than 32-17=15
						userRow+=3; // Move paddle right
					}

				}

				// User enters 5 on keypad to move user paddle left
				else if (entered_char == '5') {
					if (userRow-3 > 0) { // If left coordinate of paddle is greater than 0
						userRow-=3; // Move paddle left
					}
				}
			}

			// Draw paddle on left side(user paddle)
			OLED_ClearBuffer(&oledDevice);
			OLED_MoveTo(&oledDevice, 10, userRow);
			OLED_DrawLineTo(&oledDevice, 10, userRow+16);

			// Draw paddle on right side(CPU paddle)
			OLED_MoveTo(&oledDevice, 117, CPURow);
			OLED_DrawLineTo(&oledDevice, 117, CPURow+16);

			// Change y coordinate of pong ball
			// y coordinate is incrementing so continue incrementing
			if (increment_rows) {
				irow+=0.5;

				// Pong ball hits right of OLED screen so decrement y coordinate from now on
				if (irow == OledRowMax){
					increment_rows = false;
					irow-=0.5;
				}

			// y coordinate is decrementing so continue decrementing
			} else {
				irow-=0.5;

				// Pong ball hits left of OLED screen so increment y coordinate from now on
				if (irow < 0){
					increment_rows = true;
					irow+=0.5;
				}
			}

			// Change x coordinate of pong ball
			// x coordinate is incrementing so continue incrementing
			if (increment_cols) {
				icol+=0.5;

				// Pong ball hits top of OLED screen so decrement x coordinate from now on
				if (icol >= OledColMax){
					increment_cols = false;
					icol-=0.5;
				}

			// x coordinate is decrementing so continue decrementing
			} else {
				icol-=0.5;

				// Pong ball hits bottom of OLED screen so increment x coordinate from now on
				if (icol < 0){
					increment_cols = true;
					icol+=0.5;
				}
			}

			// Pong ball hits user paddle
			if (icol == 10) {
				if ((irow >= userRow) && (irow <= userRow+16)) {
					if (increment_rows) {
						increment_rows = false;
						irow-=0.5;
					} else {
						increment_rows = true;
						irow+=0.5;
					}

					if (increment_cols) {
						increment_cols = false;
						icol-=0.5;
					} else {
						increment_cols = true;
						icol+=0.5;
					}
				}
			}

			// Pong ball hits CPU paddle
			else if (icol == 117) {
				if ((irow >= CPURow) && (irow <= CPURow+10)) {
					if (increment_rows) {
						increment_rows = false;
						irow-=0.5;
					} else {
						increment_rows = true;
						irow+=0.5;
					}

					if (increment_cols) {
						increment_cols = false;
						icol-=0.5;
					} else {
						increment_cols = true;
						icol+=0.5;
					}
				}
			}

			// Pong ball either hits user or CPU side
			else if ((icol <= 0) || (icol >= 127)) {

				// Pong ball hits the user side so CPU score is added
				if (icol <= 0) {
					CPUScore += 1;
					increment_cols = true;

				// Pong ball hits the CPU side so user score is added
				} else if (icol >= 127) {
					userScore += 1;
					increment_cols = false;
				}
				irow = 16;
				icol = 64;
			}

			// Move pong ball
			OLED_MoveTo(&oledDevice, icol, irow);
			OLED_DrawPixel(&oledDevice);
			OLED_Update(&oledDevice);

			// User clicked one of the buttons
			if(xQueueReceive(xButtonQueue, &pauseStatus, 0) == pdTRUE){

				// User clicked BTN3 (as x character was received from queue)
				if(pauseStatus == 'x'){

					// Pause game if game wasn't paused or resume game if game is paused
					paused = !paused;

					// Pause menu
					OLED_ClearBuffer(&oledDevice);
					OLED_SetCursor(&oledDevice, 0, 0);
					OLED_PutString(&oledDevice, "Game Paused!");
					OLED_SetCursor(&oledDevice, 0, 1);
					OLED_PutString(&oledDevice, "Press BTN3 to Resume");
					OLED_Update(&oledDevice);

				}
			}

			// If paused, wait until the user clicks on BTN3 to resume game
			while (paused) {
				if(xQueueReceive(xButtonQueue, &pauseStatus, 0) == pdTRUE){
					if(pauseStatus == 'x'){
						paused = !paused;

					}
				}
			}

		}
		// Character arrays to store the score strings
		char userBuffer[20];
		char CPUBuffer[20];


		OLED_ClearBuffer(&oledDevice);
		OLED_SetCursor(&oledDevice, 0, 0);

		// CPU won game so set string to CPU wins
		if (CPUScore == 3) {
			OLED_PutString(&oledDevice, "CPU Wins!");

		// User won game so set string to user wins
		} else if (userScore == 3) {
			OLED_PutString(&oledDevice, "User Wins!");
		}

		// Show user score
		OLED_SetCursor(&oledDevice, 0, 1);
		sprintf(userBuffer, "User Score: %d", userScore);
		OLED_PutString(&oledDevice, userBuffer);

		// Show CPU score
		OLED_SetCursor(&oledDevice, 0, 2);
		sprintf(CPUBuffer, "CPU Score:  %d", CPUScore);
		OLED_PutString(&oledDevice, CPUBuffer);

		OLED_SetCursor(&oledDevice, 0, 3);
		OLED_PutString(&oledDevice, "2 to menu");
		OLED_Update(&oledDevice);

		// User must enter 2 to play pong game again
		while(1){
			if(xQueueReceive(xGameInputQueue, &entered_char, 0) == pdTRUE){
				if(entered_char == '2'){
					break;
				}
			}
			else{
				continue;
			}
		}

	}

}