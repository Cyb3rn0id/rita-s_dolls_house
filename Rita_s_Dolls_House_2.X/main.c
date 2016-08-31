//*****************************************************************************
//
// Casa delle Bambole di Rita
// Rita's Dolls House
// (C)2016 Giovanni Bernardo
// http://www.settorezero.com
//
// MCU: Microchip PIC16F722A
// Compiler: XC8 1.37
//
//*****************************************************************************

#include <xc.h>
#include <stdint.h> // usato per le definizioni di uint_x
#include <stdbool.h> // usato per la definizione di true/false
#include <stdio.h> // usato per printf
#include <stddef.h> // usato per la definizione di NULL
#include "config.h"

// l'istruzione LIGHTS_RE^=1; genera questo assurdo warning:
// main.c:360: advisory: (1395) notable code sequence candidate suitable for compiler validation suite detected (315)
// per tutte le altre porte invece non è generato... ho messo questo pragma per disattivare tale warning che non ha senso
#pragma warning disable 1395

// questo è usato soltanto per le routine di delay
#define _XTAL_FREQ  16000000

// variabili globali
uint8_t floor_actual=0; // rilevazione piano attuale ascensore
uint8_t floor_last=0; // ultimo piano rilevato prima della selezione
uint8_t floor_desired=0; // impostazione piano da raggiungere

// led lampeggiante
uint16_t led_counter=0;
#define LED_TARGET  500 // toggle led ogni 500mS
#define T0_LOAD 6 // valore preload di timer0 per interrupt 1ms, vedi in basso

#define STOP 0
#define UP 1
#define DOWN 2
uint8_t motor_direction=STOP;

// varie
#define ON  1
#define OFF 0

// (uscite) comando accensione luci nelle stanze
#define LIGHTS_RA   PORTAbits.RA1   // Room A
#define LIGHTS_RB   PORTAbits.RA2   // Room B
#define LIGHTS_RC   PORTAbits.RA3   // Room C
#define LIGHTS_RD   PORTAbits.RA4   // Room D
#define LIGHTS_RE   PORTAbits.RA5   // Room E
#define LIGHTS_RF   PORTAbits.RA7   // Room F

// (uscita) led di stato
#define LED         PORTAbits.RA6

// (uscite) comando ponte H per controllo motore ascensore
#define MOTOR_IN1   PORTCbits.RC4
#define MOTOR_IN2   PORTCbits.RC5
#define MOTOR_ENA   PORTCbits.RC3

// (ingressi) segnale di presenza ascensore al piano x
#define FC_P3       PORTCbits.RC0
#define FC_P2       PORTCbits.RC1
#define FC_P1       PORTCbits.RC2

// prototipi funzione
void system_init(void);
void motor_up(void);
void motor_down(void);
void motor_stop(void);
void move_elevator(void);
void check_elevator(void);
void putch(uint8_t data);
void main(void);
void __interrupt ISR(void);

void main(void) 
    {
    system_init();
    while (true)
        {
        // movimento ascensore
        move_elevator();
        // controllo a quale piano si trova l'ascensore
        check_elevator();
        }
    } // \main

// inizializzazione pic
void system_init(void)
    {
    // setup oscillatore. nella config word è abilitato il PLL 32x
    OSCCONbits.IRCF=0b11; // oscillatore interno a 16MHz
    // aspetto che l'oscillatore interno si stabilizzi. In OSCCON ci sono due bits:
    // il bit ICSL dice che l'oscillatore si è agganciato
    // il bit ICSS dice che l'oscillatore gira alla massima accuratezza possibile
    while (!OSCCONbits.ICSL && !OSCCONbits.ICSS); // oscillatore agganciato e stabile
    ADCON0bits.ADON = 0; // modulo A/D disattivato
    // Banco A destinato a controllo accensione luci, tranne RA0 che è usato per il regolatore di tensione interno
    // RA6 utilizzato per led lampeggiante
    TRISA=0x00; // banco A tutto come uscite
    ANSELA=0x00; // funzione analogica disabilitata su tutto il banco A
    PORTA=0x00; // tutte le uscite a livello basso
    // tutte le luci spente all'avvio
    LIGHTS_RA=OFF;
    LIGHTS_RB=OFF;
    LIGHTS_RC=OFF;
    LIGHTS_RD=OFF;
    LIGHTS_RE=OFF;
    LIGHTS_RF=OFF;
    
    // Banco B non utilizzato
    TRISB=0xFF; // banco B tutto come ingressi digitali
    ANSELB=0x00; // funzione analogica disabilitata su tutto il banco B
    WPUB=0xFF; // resistenze di pullup abilitate su tutto il banco B
    CCP1CONbits.CCP1M=0; // modulo CCP1 disabilitato
    CCP2CONbits.CCP2M=0; // modulo CCP2 disabilitato
    // Banco C destinato a sensori di piano e controllo motore
    // RC0, RC1 e RC2 destinati a ingresso fotocellule
    TRISCbits.TRISC0=1;
    TRISCbits.TRISC1=1;
    TRISCbits.TRISC2=1;
    // RC3, RC4 e RC5 destinati a controllo ponte H motore ascensore
    TRISCbits.TRISC3=0;
    TRISCbits.TRISC4=0;
    TRISCbits.TRISC5=0;
    // motore bloccato all'avvio
    MOTOR_IN1=false;
    MOTOR_IN2=false;
    MOTOR_ENA=false;
    // setup modulo ausart
    RCSTAbits.SPEN=1; // abilito modulo ausart (RC7=RX, RC6=TX)
    RCSTAbits.RX9=0; // disabilito ricezione a 9bit
    RCSTAbits.CREN=1; // abilito ricezione
    TXSTAbits.TX9=0; // disabilito trasmissione a 9bit
    TXSTAbits.TXEN=1; // abilito trasmissione
    TXSTAbits.SYNC=0; // modalità asincrona
    TXSTAbits.BRGH=1; // baudrate generator hi speed
    // baudrate 9600 - @16MHz, con BRGH=1 => 103 (tabella a pagina 145 datasheet)
    SPBRG=103;
    // setup timer0
    OPTION_REGbits.T0CS=0; // sorgente clock timer0 da FOSC/4
    OPTION_REGbits.PSA=0; // prescaler assegnato a timer0
    OPTION_REGbits.PS=0b011; // prescaler 1:16
    // periodo interrupt = 1/[(FOSC/4)/Prescaler/(256-T0_LOAD)]
    // 1/[(16000000/4)/16/(256-6)] = 1/(4000000/16/250) = 1/1000 = 0.001S = 1mS
    TMR0=T0_LOAD;
    // setup interrupts
    T0IF=0; // azzero flag interrupt overflow timer0
    T0IE=1; // abilito flag interrupt su overflow timer0
    RCIF=0; // azzero flag interrupt ricezione seriale
    RCIE=1; // abilito interrupt su ricezione seriale
    __delay_ms(50);
    INTCONbits.PEIE=1; // abilito interrupts di periferica
    INTCONbits.GIE=1; // abilito interrupts generali
    __delay_ms(50);
    } // \system_init

// motore verso l'alto
void motor_up(void)
    {
    // se è attiva la fotocellula del 3° piano
    // non è possibile andare su ulteriormente
    if (FC_P3)
        {
        motor_stop();
        }
    else
        {
        MOTOR_ENA=true;
        __delay_ms(10);
        MOTOR_IN1=ON;
        MOTOR_IN2=OFF;
        motor_direction=UP;
        }
    } // \motor_up

// motore verso il basso
void motor_down(void)
    {
    // se è attiva la fotocellula del 1° piano
    // non è possibile andare giù ulteriormente
    if (FC_P1)
        {
        motor_stop();
        }
    else
        {
        MOTOR_ENA=true;
        __delay_ms(10);
        MOTOR_IN1=OFF;
        MOTOR_IN2=ON;
        motor_direction=DOWN;
        }
    } // \motor_down

// motore completamente fermo
void motor_stop(void)
    {
    MOTOR_ENA=false;
    __delay_ms(10);
    MOTOR_IN1=OFF;
    MOTOR_IN2=OFF;
    motor_direction=STOP;
    } // \motor_stop

// imposto movimento ascensore confrontando piano attuale con piano impostato
void move_elevator(void)
    {
    // ultimo piano visitato inferiore a quello impostato => ascensore su
    if (floor_last<floor_desired) 
        {
        // se il motore stava andando giù, lo fermo per un istante
        // in modo da non eseguire un cambio di direzione repentino
        if (motor_direction==DOWN)
            {
            motor_stop();
            __delay_ms(10);
            }
        motor_up();
        }
    // ultimo piano visitato superiore a quello impostato => ascensore giu
    if (floor_last>floor_desired) 
        {
        // se il motore stava andando sù, lo fermo per un istante
        // in modo da non eseguire un cambio di direzione repentino
        if (motor_direction==UP)
            {
            motor_stop();
            __delay_ms(10);
            }
        motor_down();
        }
    // piano attuale uguale a quello impostato => ascensore fermo
    // qui anzichè controllare la variabile floor_last, controllo lo stato
    // attuale della fotocellula
    if (floor_actual==floor_desired) 
        {
        motor_stop();
        }
    }

// controllo presenza al piano ascensore
void check_elevator(void)
    {
    // questo controllo viene eseguito sempre per memorizzare l'ultimo piano visitato
    // in maniera tale che sia sempre disponibile anche se si passa da modalità
    // manuale a modalità automatica. 
    
    // Il piano attuale (floor_actual) serve a capire
    // se l'ascensore si trova in mezzo tra un piano e l'altro, perciò viene
    // sempre reimpostato a zero
    floor_actual=0;
    if (FC_P1)
        {
        floor_actual=1;
        floor_last=1;
        printf("@piano:1#");
        }
    else if (FC_P2)
        {
        floor_actual=2;
        floor_last=2;
        printf("@piano:2#");
        }
    else if (FC_P3)
        {
        floor_actual=3;
        floor_last=3;
        printf("@piano:3#");
        }
    }

// utilizzato da printf
void putch(uint8_t data)
    {
    while (!TXIF)
        {continue;}
    TXREG=data;
    }

// interrupt
void __interrupt ISR(void)
    {
    static bit command_receiving = false; // flag che indica se sto ricevendo dati
    static uint8_t rxdata=NULL; // byte ricevuto su seriale
    static uint8_t command=NULL; // comando ricevuto
    static uint8_t command_counter=0; // contatore byte di comandi ricevuti
    
    // interrupt su ricezione seriale
    if (RCIF)
        {
        // controllo errori di ricezione
        if (RCSTAbits.OERR) // overrun error
            {
            // resetto l'errore di ricezione
            RCSTAbits.CREN=0;
            RCSTAbits.CREN=1;
            // altro errore è il framing error (FERR), quello però viene resettato
            // dopo aver letto il registro
            // in caso di errore, resetto i flag
            command=NULL;
            command_receiving=false;
            command_counter=0;
            }
        
        // l'interrupt di ricezione viene già resettato quando si esegue la lettura del registro
        rxdata=RCREG;
        
        switch (rxdata)
            {
            case '@':
                 // inizio a ricevere un comando
                 command_receiving=true;
                 // resetto il comando ricevuto
                 command=NULL;
                 // azzero il contatore di comandi ricevuti
                 command_counter=0;
                 break;
            
            case '#':
                // ho smesso di ricevere un comando
                command_receiving=false;
                break;
            
            default:
                // se è stata avviata la ricezione di comandi
                // memorizzo il byte ricevuto
                if (command_receiving)
                    {
                    command=rxdata;
                    command_counter++;
                    // al massimo la stringa deve essere @x#
                    // quindi tra @ e # ci deve essere un solo byte
                    if (command_counter>1)
                        {
                        command_receiving=false;
                        command=NULL;
                        command_counter=0;
                        }
                    }
                break;
            } // \switch
        
        // ho smesso di ricevere un comando e c'è un comando valido?
        if (!command_receiving && (command != NULL))
            {
            switch (command)
                {
                case 'A':
                    LIGHTS_RA^=1;   
                break;
                case 'B':
                    LIGHTS_RB^=1;   
                break;
                case 'C':
                    LIGHTS_RC^=1;   
                break;
                case 'D':
                    LIGHTS_RD^=1;   
                break;
                case 'E':
                    LIGHTS_RE^=1;   
                break;
                case 'F':
                    LIGHTS_RF^=1;   
                break;
                case '0':
                    floor_desired=1;
                break;
                case '1':
                    floor_desired=2;
                break;
                case '2':
                    floor_desired=3;
                break;
                } // \SWITCH
            // resetto i flag
            command=NULL;
            command_counter=0;
            } // \command
        } // \RCIF
    
    // interrupt su overflow timer0, ogni millisecondo
    if (T0IF)
        {
        T0IF=0; // azzero flag di interrupt
        TMR0=T0_LOAD; // ricarico il timer
        // faccio lampeggiare il led su RA6
        led_counter++;
        if (led_counter==LED_TARGET)
            {
            LED^=1;
            led_counter=0;
            }
        } // \T0IF
    } // \interrupt