#include "timer.h"
#include "uart.h"


#include <xc.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define INPUT_BUFF_LEN 10
#define OUTPUT_BUFF_LEN 48

// this define the frequency of the tasks based on the frequency of the main.
#define CLOCK_LD_TOGGLE 50 // led2 blinking at 1Hz
#define CLOCK_ACQUIRE_ADC 50

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

//VBAT is connected to AN11 that is the 3rd channel of the ADC 1
//BAT-VSENSE is 1/3 of the VBAT, we have to multiply the readed value by 3

void init_adc(void){
    AD1CON1bits.ADON = 0; // Turn off the ADC

    AD1CON1bits.AD12B = 0; // Selecting 10-bit mode
    AD1CON2bits.VCFG = 0b00;// Voltage reference vdd
    AD1CON3bits.ADCS = 8; // Set the Tad
    AD1CON3bits.SAMC = 16; // Set the automatic end

    AD1CON1bits.ADDMABM = 0; // DMA on
    AD1CSSLbits.CSS11 = 1; // Select AN11
    AD1CON1bits.FORM = 0b00; // Data Output Format integer

    AD1CON1bits.ASAM = 0; // Selecting manual mode starting
    AD1CON1bits.SSRC = 7; // conversion starts after time specified by SAMC

    AD1CON2bits.CHPS = 0b00; // 1 channel mode
    AD1CON2bits.CSCNA = 1; // Scan ch0
    AD1CHS0bits.CH0SA = 11; // Choosing AN11 
    AD1CHS0bits.CH0NA = 0; // Choosing VREFL

}

int main(void) {
    init_uart();
    init_adc();

    UART_input_buff.buff = input_buff;
    UART_output_buff.buff = output_buff;

    TRISA = TRISG = 0x0000; // setting port A and G as output
    ANSELA = ANSELC = ANSELD = ANSELE = ANSELG = 0x0000; // disabling analog function
    TRISB = 0xFFF7;
    ANSELB = 0xFFF7;

    // our largerst string is 20 bytes, this should be changed in case of differnt print messages
    char output_str [20]; 

    int LD2_toggle_counter = 0;
    int acquire_adc_counter = 0;


    const int main_hz = 100;
    tmr_setup_period(TIMER1, 1000 / main_hz); // 100 Hz frequency

    AD1CON1bits.ADON = 1; // Turn on the ADC
    AD1CON1bits.SAMP = 1;
    
    LATBbits.LATB4 = 1; // IR enable
    while (1) {

        if (++LD2_toggle_counter >= CLOCK_LD_TOGGLE) {
            LD2_toggle_counter = 0;
            LATGbits.LATG9 = !LATGbits.LATG9;
        }
        
        if(++acquire_adc_counter >= CLOCK_ACQUIRE_ADC && !AD1CON1bits.DONE){
            acquire_adc_counter = 0;
            AD1CON1bits.SAMP = 1;
        }

        if(AD1CON1bits.DONE){
            AD1CON1bits.DONE = 0;
        
            int data = ADC1BUF0;
            double v_adc = (data / 1023.0) * 3.3; // assuming Vref+ = 3.3 V
            double v_adc_batt = v_adc * 3;
            sprintf(output_str, " ADC:%f ", v_adc_batt);
            print_to_buff(output_str, &UART_output_buff);
        }

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