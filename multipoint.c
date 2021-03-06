//For the RX target board, the connection is Gray, White, Orange, Blue, and Black, Red, White, Yellow

#include <xc.h>
#include "constants.h"
#include "config.h"
#include "serlcd.h"
#include "nRF2401.h"

#define LED_GREEN_TRIS TRISBbits.TRISB4
#define LED_GREEN PORTBbits.RB4

#define LED_RED_TRIS TRISBbits.TRISB5
#define LED_RED PORTBbits.RB5

#define STRIP_DATA_TRIS TRISAbits.TRISA0
#define STRIP_DATA PORTAbits.RA0

#define PROBE_TRIS TRISAbits.TRISA1
#define PROBE PORTAbits.RA1

#define STATUS_TRIS TRISBbits.TRISB4
#define STATUS_LED PORTBbits.RB4

#define BUTTON_TRIS TRISBbits.TRISB1
#define BUTTON PORTBbits.RB1

#define MODE_SELECT_TRIS TRISBbits.TRISB0
#define MODE_SELECT PORTBbits.RB0
#define MODE_SEND 1

unsigned char tx_buf[MAX_PAYLOAD];
unsigned char rx_buf[MAX_PAYLOAD];

void setup(void);

////                            MasterCode                                 ////
void masterMain(void);
void masterInterrupt(void);

////                          SlaveCode                                 ////
void slaveMain(void);
void slaveInterrupt(void);

////                            Shared Code                                 ////
void displayStatus(char status);
void delay(void);

////                            System Code                                 ////
void run(void);
void main(void);
void interrupt interrupt_high(void);

void setup(void) {
    //Misc config
    STRIP_DATA_TRIS = OUTPUT;
    STATUS_TRIS = OUTPUT;
    BUTTON_TRIS = INPUT;
    MODE_SELECT_TRIS = INPUT;
    STATUS_LED = 0;
    LED_GREEN_TRIS = OUTPUT;
    LED_RED_TRIS = OUTPUT;
    PROBE_TRIS = OUTPUT;

    //Enable internal pullup resistor for port B
    INTCON2bits.RBPU = CLEAR;
    WPUB = 0b1111;

    //This is to toggle pins from digital to analog
    //unimp, RD3, RD2, RD1     RD1, AN10, AN9, AN8 (in order)
    ANCON0 = 0b00000000;
    ANCON1 = 0b11111000;
    
    //NRF port configure (todo: move me)
    TRIS_CE = OUTPUT;
    TRIS_CSN = OUTPUT;
    TRIS_IRQ = INPUT;
    TRIS_SCK = OUTPUT;
    TRIS_MISO = INPUT;
    TRIS_MOSI = OUTPUT;

    //oscillator setup
    OSCCONbits.IRCF = 0b111; //sets internal osc to 111=16mhz, 110=8mhz
    OSCCONbits.SCS = 0b00;
    OSCTUNEbits.PLLEN = 0b1; //1=pllx4 enabled

    //set up timer for LEDs
    T2CONbits.TMR2ON = 1; //enable timer 2
    T2CONbits.T2CKPS = 0b00; //prescaler 0b10=1/16
    PIE1bits.TMR2IE = 0;
    PR2 = 20;

    //set up timer for interrupt
    T0CONbits.TMR0ON = 1; //enable timer 0
    T0CONbits.T0CS = 0; //select clock (0=internal,1=t0pin)
    T0CONbits.PSA = 1; //disable's prescaler (1=disable, 0=enable)
    T0CONbits.T08BIT = 0; //set mode (1=8bit mode, 0=16bit mode)
    T0CONbits.T0SE = 1; //edge select (1=falling edge, 0=rising edge)
    T0CONbits.T0PS = 0b000; //configure prescaler 000=1:2

    //Set up timer0 interrupts
    INTCONbits.TMR0IE = 1;
    INTCONbits.TMR0IF = 0;
    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;


    //set up USB serial port
    TRISCbits.TRISC6 = 1;
    RCSTA1bits.SPEN = 1;
    TXSTA1bits.TXEN = 1;

    TXSTA1bits.SYNC = 0;
    BAUDCON1bits.BRG16 = 0;
    TXSTA1bits.BRGH = 1;

    // 19.2kbaud = 001, 832
    SPBRG1 = 34;
    
    //9.6kbaud = 000, 103
    //SPBRG1 = 103;
    RCSTA1bits.CREN = SET;
}

////////////////////////////////////////////////////////////////////////////////
////                                                                        ////
////                            Sender Code                                 ////
////                                                                        ////
////////////////////////////////////////////////////////////////////////////////
#define MAX_CLIENTS 10
void masterMain() {
    //master
    short i;
    short l;
    short nextSlot;
    int clients[MAX_CLIENTS];
    int clientInfo[MAX_CLIENTS];

    nrf_init();
    delay();

    nrf_txmode();
    delay();
    
    nextSlot = 0;
    for (i=0; i<MAX_CLIENTS; i++) {
        clients[i] = 0;
    }

    sendLiteralBytes("\nLoop Start\n");
    LED_RED = 0;
    LED_GREEN = 0;
//    while(1) {
//        LED_RED = !LED_RED;
//        LED_GREEN = nrf_send(&tx_buf,&rx_buf);
//        delay();
//    }
    while(1) {
        LED_RED = !LED_RED;
        delay();
        sendIntArray(&clients,MAX_CLIENTS);
        sendLiteralBytes("\n");
        
        nrf_setTxAddr(0); //master channel
        nrf_setRxAddr(0,0); //master channel
        tx_buf[0] = 0x42;
        tx_buf[1] = nextSlot+1;
        //sendLiteralBytes("Attempt Send\n");
        //displayStatus(nrf_getStatus());
        if (nrf_send(&tx_buf,&rx_buf)) {
            sendLiteralBytes("\nClient Connected!\n");

            clients[nextSlot] = 1;

            //find the next available slot
            nextSlot = -1;
            for (i=0; i<MAX_CLIENTS; i++) {
                if (clients[i] == 0) {
                    nextSlot = i;
                    break;
                }
            }
        }

        for (i=0; i<MAX_CLIENTS; i++) {
            if (clients[i] != 0) {
                nrf_setTxAddr(i+1); //slave channel
                nrf_setRxAddr(0,i+1); //slave channel
                if (nrf_send(&tx_buf,&rx_buf)) {
                    clients[i] = 1;
                    clientInfo[i] = 1;
                } else {
                    clients[i]++;
                    if (clients[i] > 3) {
                        sendLiteralBytes("Client Disconnected!\n");
                        clients[i] = 0;
                        clientInfo[i] = 0;
                    }
                    for (l=0; l<MAX_CLIENTS; l++) {
                        if (clients[l] == 0) {
                            nextSlot = l;
                            break;
                        }
                    }
                }
            }
        }
    }
}

void masterInterrupt(void) {
}

////////////////////////////////////////////////////////////////////////////////
////                                                                        ////
////                            Receiver Code                               ////
////                                                                        ////
////////////////////////////////////////////////////////////////////////////////

void slaveMain() {
    //slave
    int i;
    char offset;
    char status;

    nrf_init();
    delay();

    nrf_rxmode();
    delay();

//    while(1) {
//        LED_GREEN = nrf_recieve(&tx_buf, &rx_buf);
//    }

    nrf_setTxAddr(0);
    nrf_setRxAddr(0, 0);

    rx_buf[0] = 0;
    while(rx_buf[0] != 0x42) {
        nrf_recieve(&tx_buf, &rx_buf);
    }
    
    nrf_setTxAddr(rx_buf[1]);
    nrf_setRxAddr(0, rx_buf[1]);
    while(1) {
        LED_GREEN = nrf_recieve(&tx_buf, &rx_buf);
    }
}

void slaveInterrupt() {

}

////////////////////////////////////////////////////////////////////////////////
////                                                                        ////
////                            Shared Code                                 ////
////                                                                        ////
////////////////////////////////////////////////////////////////////////////////

void displayStatus(char status) {
    sendLiteralBytes("stat:");
    sendBinPad(status);
    sendLiteralBytes("\n");
    
    if (status & 0b1000000) sendLiteralBytes("DR ");
    if (status & 0b100000) sendLiteralBytes("DS ");
    if (status & 0b10000) sendLiteralBytes("RT ");
    if (status & 0b1) sendLiteralBytes("TXF ");
    sendLiteralBytes("\n");
}

void delay(void) {
    Delay10KTCYx(254);
}

////////////////////////////////////////////////////////////////////////////////
////                                                                        ////
////                            System Code                                 ////
////                                                                        ////
////////////////////////////////////////////////////////////////////////////////

void run(void) {
    while(1) {
        if (MODE_SELECT == MODE_SEND) {
            masterMain();
        } else {
            slaveMain();
        }
    }
}

void main(void) {
    setup();

    run();

    while(1);
}

void interrupt interrupt_high(void) {
    if (MODE_SELECT == MODE_SEND) {
        masterInterrupt();
    } else {
        slaveInterrupt();
    }

    INTCONbits.TMR0IF = CLEAR;
}
