// TE149-motor-brushless
// Controlador PID Digital para motor brushless
// Disciplina TE149 - Instrumentação Eletrônica - UFPR 2016/1
// https://github.com/wsilverio/TE149-motor-brushless
//
// compilado com "msp430-gcc", C99 para mcu=msp430g2553

#include <msp430.h>     // PxOUT, PxIN, ...
#include <stdint.h>     // int8_t, int16_t, ...
#include <stdbool.h>    // bool

typedef enum { STOP, MIN, MED, MAX } MODE;

#define LEDPIN          BIT0    // P1.0 (RED LED)
#define BUTTONPIN       BIT3    // P1.3 (S2)
#define SERIALRXPIN     BIT1    // P1.1
#define SERIALTXPIN     BIT2    // P1.2
#define MOTORINPIN      BIT4    // P1.4
#define MOTOROUTPIN     BIT6    // P1.6 / TA01 / GREEN LED

#define MOTORPOLES      7
#define SERVOSTOPPULSE  1000    // 1ms
#define SERVOMINPULSE   1150    // 1.15ms
#define SERVOMAXPULSE   2000    // 2ms
#define SERVOMEDPULSE   (SERVOSTOPPULSE+SERVOMAXPULSE)/2
#define SERVOMINDEGREE  0
#define SERVOMAXDEGREE  179
#define RPMMAX          5000
#define RPMMIN          2000

void delay_us(uint16_t us);
void delay_ms(uint16_t ms);
void itoa(long unsigned int value, char* result);
void serial_config();
void serial_print_byte(int8_t data);
void serial_print_string(char* data);
void servo_config();
void servo_write_degree(uint8_t degree);
void servo_write_pulse(uint16_t ms);
uint16_t servo_get_pulse();
uint16_t degree_to_ms(uint8_t degree);

volatile uint16_t servoPulse = 0;

volatile bool buttonFlag = false;
volatile bool loopFlag = false;

volatile uint16_t rpm = 0;
volatile uint16_t flagTimer = 0;

volatile MODE step = STOP;

char strValue[10]; // string de uso geral

void main(){
    // desabilita watchdog
    WDTCTL = WDTPW + WDTHOLD;

    //16Mhz
    if (CALBC1_16MHZ==0xFF){
        while(1);
    }

    DCOCTL = 0;
    BCSCTL1 = CALBC1_16MHZ;
    DCOCTL = CALDCO_16MHZ;

    P1DIR |= (MOTOROUTPIN | LEDPIN);    // saidas
    P1SEL |= (MOTOROUTPIN);             // pwm out
    P1REN |= (MOTORINPIN | BUTTONPIN);  // habilita resistores
    P1OUT |= (MOTORINPIN | BUTTONPIN);  // resistores pullup

    P1OUT &= ~(LEDPIN);   // limpa as saidas

    // config. perifericos
    serial_config();
    servo_config();

    // configura o timer A1
    TA1CTL    = TASSEL_2 | ID_0 | MC_1; // smclk, div 1, up CCR0
    TA1CCTL0 |= CCIE;                   // interrupcao por comparacao
    TA1CCR0   = 50000-1;                // @16MHz -> 1/320 = 3,125ms para estourar

    // configura interrupcao motor
    P1IE  |= (MOTORINPIN);  // habilita interrupcao
    P1IES |= (MOTORINPIN);  // borda de descida
    P1IFG  = 0;             // limpa IFG

    __enable_interrupt();

    // calibacao brushless
    if (!(P1IN & BUTTONPIN)){
        // pulso maximo por 2s
        servo_write_pulse(SERVOMAXPULSE);

        // sinaliza modo configuracao (pisca 3x)
        uint8_t i;
        for(i = 0; i < 3; ++i){
            P1OUT |= LEDPIN;
            delay_ms(50);
            P1OUT &= ~LEDPIN;
            delay_ms(250);
        }

        // aguarda botao livre
        while(!(P1IN & BUTTONPIN));
        // espera 1s
        delay_ms(1000);

        // configura interrupcao botao
        P1IE  |= (BUTTONPIN);  // habilita interrupcao
        P1IES |= (BUTTONPIN);  // borda de descida
        P1IFG &= ~(BUTTONPIN); // limpa IFG

        P1OUT |= LEDPIN;
        // aguarda usuario
        buttonFlag = false;
        while(!buttonFlag);

        P1OUT &= ~LEDPIN;

        // pulso minimo por 2s
        servo_write_pulse(SERVOSTOPPULSE);
        delay_ms(2000);

    }else{
        // configura interrupcao botao
        P1IE  |= (BUTTONPIN);  // habilita interrupcao
        P1IES |= (BUTTONPIN);  // borda de descida
        P1IFG &= ~(BUTTONPIN); // limpa IFG
    }

    P1OUT |= LEDPIN;
    // aguarda comando de inicio
    buttonFlag = false;
    while(!buttonFlag);
    // apaga o led
    P1OUT &= ~LEDPIN;

    buttonFlag = false;
    loopFlag = true;

    while(1){
        uint32_t vel_rpm = rpm;
        itoa(vel_rpm, strValue);
        serial_print_string(strValue);
        serial_print_string(";\n");

        delay_ms(1000);
    }
}

#pragma vector = PORT1_VECTOR
__interrupt void interrupt_port_1(){
    if (P1IFG & MOTORINPIN){

        // captura tempo atual
        double delta_t = (62.5e-9*TA1R + 3.125e-3*flagTimer);
        flagTimer = TA1R = 0;

        // conversao para RPM
        rpm = (uint16_t)(1.0/delta_t / MOTORPOLES * 60.0);

        // limpa flag de interrupcao
        P1IFG &= ~MOTORINPIN;

    }else if (P1IFG & BUTTONPIN){

        buttonFlag = true;

        if (loopFlag){

            if(++step > MAX)
                step = STOP;

            switch (step){
                case STOP:
                    servo_write_pulse(SERVOSTOPPULSE);
                    break;
                case MIN:
                    servo_write_pulse(SERVOMINPULSE);
                    break;
                case MED:
                    servo_write_pulse(SERVOMEDPULSE);
                    break;
                case MAX:
                    servo_write_pulse(SERVOMAXPULSE);
                    break;
                default:
                    break;
            }
        }

        // debouncing
        delay_ms(10);
        while(!(P1IN & BUTTONPIN));
        delay_ms(10);

        // limpa flag de interrupcao
        P1IFG &= ~BUTTONPIN;
    }
}

#pragma vector = TIMER1_A0_VECTOR
__interrupt void interrupt_timer_A1(){

    flagTimer++;

    TA1CCTL0 &= ~CCIFG;
}

void servo_config(){
    TA0CTL = TASSEL_2 | ID_3 | MC_1;    // SMCLK, DIV(8), UP CCR0
    TA0CCTL1 |= OUTMOD_7;               // PWM set/reset
    TA0CCR0 = 20000-1;                  // periodo: (16Mhz / 8 / 1000ms) * 20ms
    TA0CCR1 = 0;                        // reset
}

void servo_write_pulse(uint16_t ms){
    TA0CCR1 = 2*ms;
}

void servo_write_degree(uint8_t degree){
    servo_write_pulse( degree_to_ms(degree) );
}

uint16_t servo_get_pulse(){
    return TA0CCR1;
}

uint16_t degree_to_ms(uint8_t degree){
    if(degree > SERVOMAXDEGREE){
        degree = SERVOMAXDEGREE;
    }else if(degree < SERVOMINDEGREE){
        degree = SERVOMINDEGREE;
    }

    return (uint16_t)(SERVOSTOPPULSE + (SERVOMAXPULSE - SERVOSTOPPULSE) * ((float)degree / SERVOMAXDEGREE));
}

void serial_config(){
    P1SEL  |= (SERIALRXPIN | SERIALTXPIN);
    P1SEL2 |= (SERIALRXPIN | SERIALTXPIN);

    UCA0CTL1 |= UCSWRST;
    UCA0CTL0 &= ~(UCMODE1 + UCMODE0 + UCSYNC);
    UCA0CTL1 |= UCSSEL_2;
    UCA0MCTL &= ~UCOS16;

    // 9600 bps
    UCA0BR1 = 0x06;
    UCA0BR0 = 0x82;
    UCA0MCTL |= 0x0A;

    UCA0CTL1 &= ~UCSWRST;

    IE2 |= UCA0RXIE;
}

void serial_print_byte(int8_t data){
    // aguarda buffer vazio
    while(!(IFG2 & UCA0TXIFG));
    // escreve o dado no registrador
    UCA0TXBUF = data;
}

void serial_print_string(char* data){
    while(*data){
        serial_print_byte(*data);
        data++;
    }
}

void delay_ms(uint16_t ms){
    while (ms--) {
        __delay_cycles(16000);
    }
}

void delay_us(uint16_t us){
    while(us--){
        __delay_cycles(16);
    }
}

void itoa(long unsigned int value, char* result){
    char* ptr = result, *ptr1 = result;
    int tmp_value;

    do {
        tmp_value = value;
        value /= 10;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * 10)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';

    *ptr-- = '\0';
    while(ptr1 < ptr) {
        char* tmp_char;
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
}