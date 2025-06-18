#include "timer.h"
#include "uart.h"
#include "adc.h"

#include <xc.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define INPUT_BUFF_LEN 10
#define OUTPUT_BUFF_LEN 48

// this define the frequency of the tasks based on the frequency of the main.
#define CLOCK_LD_TOGGLE 50 // led2 blinking at 1Hz
#define CLOCK_SHOW_ADC 100 // send ADC values to UART at 10Hz

char input_buff[INPUT_BUFF_LEN];
char output_buff[OUTPUT_BUFF_LEN];

struct circular_buffer UART_input_buff = {
    .len = INPUT_BUFF_LEN,
    .buff = input_buff,
};

struct circular_buffer UART_output_buff = {
    .len = OUTPUT_BUFF_LEN,
    .buff = output_buff,
};

int main(void) {

    UART_input_buff.buff = input_buff;
    UART_output_buff.buff = output_buff;

    TRISA = TRISG = 0x0000; // setting port A and G as output
    ANSELA = ANSELC = ANSELD = ANSELE = ANSELG = 0x0000; // disabling analog function

    init_uart();
    init_adc();

    // our largerst string is 20 bytes, this should be changed in case of differnt print messages
    char output_str [20]; 

    int LD2_toggle_counter = 0;
    int show_adc_counter = 0;

    double dist, v_adc_batt;

    const int main_hz = 100;
    tmr_setup_period(TIMER1, 1000 / main_hz); // 100 Hz frequency
    
    TRISBbits.TRISB9 = 0;
    LATBbits.LATB9 = 1; // IR enable
    
    while (1) {

        if (++LD2_toggle_counter >= CLOCK_LD_TOGGLE) {
            LD2_toggle_counter = 0;
            LATGbits.LATG9 = !LATGbits.LATG9;
        }
        
        if(++show_adc_counter >= CLOCK_SHOW_ADC){
            show_adc_counter = 0;

            sprintf(output_str, "$SENS,%.3f,%.3f*", dist,v_adc_batt);
            print_to_buff(output_str, &UART_output_buff);
        }

        while(!AD1CON1bits.DONE){
            ;
        }

        AD1CON1bits.SAMP = 0;
        int adcb = ADC1BUF0;
        double v_adc = (adcb / 1023.0) * 3.3; // assuming Vref+ = 3.3 V
        v_adc_batt = v_adc * 3;

        int adcff = ADC1BUF2;
        double v_adc_ir = (adcff / 1023.0) * 3.3; // assuming Vref+ = 3.3 V
        dist = 2.34 - 4.74 * v_adc_ir + 4.06 * pow(v_adc_ir,2) - 1.6 * pow(v_adc_ir,3) + 0.24 * pow(v_adc_ir,4);
        AD1CON1bits.SAMP = 1;
        

        tmr_wait_period(TIMER1);
    }
    return 0;
}

void __attribute__((__interrupt__, no_auto_psv)) _U1TXInterrupt(void){
    IFS0bits.U1TXIF = 0; // clear TX interrupt flag


    if(UART_output_buff.read == UART_output_buff.write){
        UART_INTERRUPT_TX_MANUAL_TRIG = 1;
    } 

    while(!U1STAbits.UTXBF && UART_output_buff.read != UART_output_buff.write){
        U1TXREG = UART_output_buff.buff[UART_output_buff.read];
        UART_output_buff.read = (UART_output_buff.read + 1) % OUTPUT_BUFF_LEN;
    }
}

void __attribute__((__interrupt__, no_auto_psv)) _U1RXInterrupt(void) {
    IFS0bits.U1RXIF = 0; //resetting the interrupt flag to 0

    while(U1STAbits.URXDA) {
        const char read_char = U1RXREG;

        const int new_write_index = (UART_input_buff.write + 1) % INPUT_BUFF_LEN;
        if (new_write_index != UART_input_buff.read) {
            UART_input_buff.buff[UART_input_buff.write] = read_char;
            UART_input_buff.write = new_write_index;
        }
    }
}