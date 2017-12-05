/*
 * freertos_LQR.c
 *
 *  Created on: Dec 4, 2017
 *      Author: Matthew
 */


#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"
#include "xparameters.h"
#include <unistd.h>
#include <stdio.h>
#include "matrixLib.h"
#include <xuartps.h>
#include <xuartps_hw.h>
#include <math.h>

//defines
#define SEND_TIMEOUT 1000
#define RECV_TIMEOUT 1000
#define MESSAGE_LENGTH 9
#define PENDULUM_START_LOWER_BOUND -0.017453
#define PENDULUM_START_UPPER_BOUND 0.017453
//init uart
#ifdef STDOUT_IS_16550
    XUartNs550_SetBaud(STDOUT_BASEADDR, XPAR_XUARTNS550_CLOCK_HZ, UART_BAUD);
    XUartNs550_SetLineControlReg(STDOUT_BASEADDR, XUN_LCR_8_DATA_BITS);
#endif

#define TICKS_PER_RADIAN	(4*1024 / (2*M_PI))

//Prototypes
static void do_the_LQR_thing(void* params);
void init_matricies();
int init_our_uart();
int uart0_sendBuffer(XUartPs *InstancePtr, u8 *data, size_t num_bytes);
int uart0_recvBuffer(XUartPs *InstancePtr, u8 *buffer, size_t num_bytes);
int uart_read(u8 buffer[MESSAGE_LENGTH]);
void buffer_to_matrix(matrix_t* sensorData, u8 recv_buffer[MESSAGE_LENGTH]);
void calc_LQR(matrix_t* voltage, matrix_t* x_next, matrix_t* x_current, matrix_t* sensorData);
void transmit_voltage(matrix_t* voltage);
void uart_send( u16 data);
void cleanup_LQR(matrix_t* voltage);

//Globals
XUartPs uart0;		/* The instance of the UART Driver */
matrix_t matA;
matrix_t matB;
matrix_t matC;
matrix_t matD;
matrix_t matK;

static void testTask(void* parameters){
	xil_printf("Started the test task\r\n");
	while(1){
		xil_printf("God damn it 2: electric boogaloo\r\n");
		sleep(1);
	}
}

int main(void){
	xil_printf("Starting FreeRTOS\r\n");

	xTaskCreate( testTask, 					    /* The function that implements the task. */
				 "Test Task", 		            /* Text name for the task, provided to assist debugging only. */
				 configMINIMAL_STACK_SIZE, 	    /* The stack allocated to the task. */
				 NULL, 						    /* The task parameter is not used, so set to NULL. */
				 tskIDLE_PRIORITY,			/* The task runs at the idle priority. */
				 NULL );					    /* Not sure what this argument does. */

	xTaskCreate( do_the_LQR_thing,
			 	 "LQR in FreeRTOS",
				 1024*4,
				 NULL,
				 tskIDLE_PRIORITY,
				 NULL);

	/* Start the tasks and timer running. */
	vTaskStartScheduler();
	xil_printf("THIS SHOULD NEVER PRINT!!!\r\n");

	return 0;
}

static void do_the_LQR_thing(void* params)
{

    int i;
    for(i=0; i<3; i++){
    	xil_printf("God Damn it!\r\n");
    }

    //initialize variables
    matA = createMatrix(2, 2, 0);
    matB = createMatrix(2, 3, 0);
    matC = createMatrix(4, 2, 0);
    matD = createMatrix(4, 2, 0);
    matK = createMatrix(1, 4, 0);
    matrix_t voltage = createMatrix(1, 1, 0);
    matrix_t x_next = createMatrix(2, 1, 0);
    matrix_t x_current = createMatrix(2, 1, 0);
    matrix_t sensorData = createMatrix(2, 1 ,0);

    //setup constant matricies
    init_matricies(matA, matB, matC, matD, matK);
    init_our_uart();
    int balancing = 0;

    u8 recv_buffer[MESSAGE_LENGTH];
    memset(recv_buffer, 0, MESSAGE_LENGTH);


    //initialize sensorData
    uart_read(recv_buffer);
	buffer_to_matrix(&sensorData, recv_buffer);
	float pendulumRadians = get(&sensorData, 1, 0);
	xil_printf("Swing pendulum clockwise until it is upright to start\r\n");

	//Matt said we can't do cool things right now because he is a poopy head.
	//This will eventually start balancing again when we bring the pendulum back up after it fell.
//	while(1){
//		uart_read(recv_buffer);
//		buffer_to_matrix(&sensorData, recv_buffer);
//
//		//do the LQR calculations
//		calc_LQR(&voltage, &x_next, &x_current, &sensorData);
//		pendulumRadians = get(&sensorData, 1, 0);
//
//		if (!balancing && pendulumRadians > PENDULUM_START_LOWER_BOUND && pendulumRadians < PENDULUM_START_UPPER_BOUND){
//			balancing = 1;
//		}
//
//		if (pendulumRadians > 8*PENDULUM_START_LOWER_BOUND && pendulumRadians < 8*PENDULUM_START_UPPER_BOUND && balancing){
//			transmit_voltage(&voltage);
//		}
//		else{
//			balancing = 0;
//		}
//
//	}

    //Don't start until the pendulum is brought upright.
    while( pendulumRadians < PENDULUM_START_LOWER_BOUND || pendulumRadians > PENDULUM_START_UPPER_BOUND ){
    	//get data from sensors
		uart_read(recv_buffer);
		buffer_to_matrix(&sensorData, recv_buffer);
		pendulumRadians = get(&sensorData, 1, 0);
    }


    xil_printf("Starting while loop\r\n");
	while(pendulumRadians > 8*PENDULUM_START_LOWER_BOUND && pendulumRadians < 8*PENDULUM_START_UPPER_BOUND){
		//get data from sensors
		int bytes_recv = uart_read(recv_buffer);
		buffer_to_matrix(&sensorData, recv_buffer);


		//do the LQR calculations
		calc_LQR(&voltage, &x_next, &x_current, &sensorData);

		//transmit the voltage to the decoder
		transmit_voltage(&voltage);


		/******Debug statments. Uncomment as needed. ******/
		//xil_printf("%X %X\n", recv_buffer[4] << 24 | recv_buffer[3] << 16 | recv_buffer[2] << 8 | recv_buffer[1],
		//					  recv_buffer[8] << 24 | recv_buffer[7] << 16 | recv_buffer[6] << 8 | recv_buffer[5]);

		//int i;
		//for(i=0; i<MESSAGE_LENGTH; i++){
		//	xil_printf("%X ", recv_buffer[i]);
		//}
		//xil_printf("\r\n");

		//printf("%f, %f\r\n", get(&sensorData, 0, 0), get(&sensorData, 1, 0));
		//printf("%f\r\n", get(&voltage, 0, 0) );


		//update loop condition
		pendulumRadians = get(&sensorData, 1, 0);
	}


	cleanup_LQR(&voltage);

	xil_printf("Zybo program stopped\r\n");
}


/* observer code */
void calc_LQR(matrix_t* voltage, matrix_t* x_next, matrix_t* x_current, matrix_t* sensorData){
	matrix_t u_ref = createMatrix(4,1,0);

	// LQR calculations
	// Calculations for voltage
	// Voltage = matK * (u_ref - (matD * sensorData + matC * x_current))
	matrix_t temp = matrixMult(&matD, sensorData);	// matD * sensorData
	matrix_t temp2 = matrixMult(&matC, x_current);	// matC * x_current
	matrix_t temp3 = matrixAdd(&temp, &temp2);		// (matD * sensorData + matC * x_current)
	matrix_t temp4 = matrixSub(&u_ref, &temp3);		// u_ref - (matD * sensorData + matC * x_current)
	matrix_t temp5 = matrixMult(&matK, &temp4);		// matK * (u_ref - (matD * sensorData + matC * x_current)) = Voltage
	copyMatrix(&temp5, voltage);

	// Calculations for x_next
	// x_next = matA * x_current + matB * [sensorData; voltage]
	temp = createMatrix(3, 1, 0);					// empty matrix for [sensorData; voltage] concatenation
	set(&temp, 0, 0, get(sensorData, 0, 0));		// assigning sensorData(1) into first row of temp
	set(&temp, 1, 0, get(sensorData, 1, 0));		// assigning sensorData(2) into second row of temp
	set(&temp, 2, 0, get(voltage, 0, 0));			// assigning voltage into third row of temp
	temp2 = matrixMult(&matB, &temp);				// matB * [sensorData; voltage]
	temp3 = matrixMult(&matA, x_current);			// matA * x_current
	temp4 = matrixAdd(&temp2, &temp3);				// matA * x_current + matB * [sensorData; voltage]
	copyMatrix(&temp4, x_next);						// x_next = matA * x_current + matB * [sensorData; voltage]

	copyMatrix(x_next, x_current);					// x_current = x_next

}

void init_matricies(){
	set(&matA, 0, 0, 0.928);
	set(&matA, 0, 1, 0.0);
	set(&matA, 1, 0, 0.0);
	set(&matA, 1, 1, 0.928);

	set(&matB, 0, 0, 0.27);
	set(&matB, 0, 1, 0.016);
	set(&matB, 0, 2, 0.0146);
	set(&matB, 1, 0, 1.83);
	set(&matB, 1, 1, -0.273);
	set(&matB, 1, 2, 0.0336);

	set(&matC, 0, 0, 0.0);
	set(&matC, 0, 1, 0.0);
	set(&matC, 1, 0, 0.0);
	set(&matC, 1, 1, 0.0);
	set(&matC, 2, 0, 1.0);
	set(&matC, 2, 1, 0.0);
	set(&matC, 3, 0, 0.0);
	set(&matC, 3, 1, 1.0);

	set(&matD, 0, 0, 1.0);
	set(&matD, 0, 1, 0.0);
	set(&matD, 1, 0, 0.0);
	set(&matD, 1, 1, 1.0);
	set(&matD, 2, 0, -3.74);
	set(&matD, 2, 1, 0.003);
	set(&matD, 3, 0, -25.26);
	set(&matD, 3, 1, 7.28);

	set(&matK, 0, 0, -60.5);
	set(&matK, 0, 1, 136.62);
	set(&matK, 0, 2, -47.94);
	set(&matK, 0, 3, 26.12);

}

int init_our_uart(){
	int status;
	XUartPs_Config *Config;
	xil_printf("Ready to Start Uart crap\r\n");
	/*
	 * Initialize the UART driver so that it's ready to use
	 * Look up the configuration in the config table and then initialize it.
	 */
	Config = XUartPs_LookupConfig(XPAR_PS7_UART_0_DEVICE_ID);
	if (Config == NULL) {
		return XST_FAILURE;
	}
	xil_printf("Look up Config Success\r\n");

	status = XUartPs_CfgInitialize(&uart0, Config, Config->BaseAddress);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	XUartPs_SetBaudRate(&uart0, 115200);

	xil_printf("Initialize Config Success\r\n");
	return status;
}

int uart_read(u8 buffer[MESSAGE_LENGTH]){
	memset(buffer, 0, MESSAGE_LENGTH);
	int bytes_recv = 0;

	// wait until we see the start of the next message
	// The start is specified by the byte 0xA5
	while(buffer[bytes_recv] != 0xA5){
		if(XUartPs_IsReceiveData(uart0.Config.BaseAddress)){
			buffer[bytes_recv] = XUartPs_RecvByte(uart0.Config.BaseAddress);
		}
	}

	bytes_recv = 1;
	// should receive 9 bytes. 4 bytes ADC * 2 ADCs + 1 start byte
	while(bytes_recv < MESSAGE_LENGTH){
		if(XUartPs_IsReceiveData(uart0.Config.BaseAddress)){
			//index starts at 1 because buffer[0] should be the start of the message
			buffer[bytes_recv] = XUartPs_RecvByte(uart0.Config.BaseAddress);
			++bytes_recv;
		}
	}

	return bytes_recv;

}

void buffer_to_matrix(matrix_t* sensorData, u8 recv_buffer[MESSAGE_LENGTH]){
	int encoder1 = recv_buffer[4] << 24 | recv_buffer[3] << 16 | recv_buffer[2] << 8 | recv_buffer[1] ;
	int encoder2 = recv_buffer[8] << 24 | recv_buffer[7] << 16 | recv_buffer[6] << 8 | recv_buffer[5] ;

	//convert encoder1 ticks to a distance in meters
	float metersPerTicks = 1.0 / 44810.0;

	//conert encoder2 ticks to
	//float radsPerTick = 1.0 / 650.625;

	float angle = encoder2 / TICKS_PER_RADIAN - M_PI;
	//angle = fmodf(angle, 2.f*M_PI);

	set(sensorData, 0, 0, (float)(encoder1*metersPerTicks));
	//bias the encoder2 value because up should be 0. Down should not be 0.
	//set(sensorData, 1, 0, (float)(encoder2*radsPerTick) - 3.1415);
	set(sensorData, 1, 0, angle);
}
void transmit_voltage(matrix_t* voltage){
	float volt = get(voltage, 0, 0);
	if(volt > 10.0) volt = 10.0;
	if(volt < -10.0) volt = -10.0;

	u16 voltTX = (65535/20.0 * volt) + 32768;

	xil_printf("%d\r\n", voltTX);

	uart_send(voltTX);

}


void uart_send( u16 data) {
	XUartPs_SendByte(uart0.Config.BaseAddress, 0xA5);
	XUartPs_SendByte(uart0.Config.BaseAddress, (u8)(data & 0xFF));
	XUartPs_SendByte(uart0.Config.BaseAddress, (u8)(data >> 8));
}


void cleanup_LQR(matrix_t* voltage){
	//send 0V so that the motor stops moving
	set(voltage, 0, 0, 0.0);
	transmit_voltage(voltage);
}

