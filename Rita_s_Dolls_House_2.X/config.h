//
// Configurazione PIC16F722A per utilizzo con schema "Casa delle Bambole di Rita"
//
#include <xc.h>
// CONFIG1
#pragma config FOSC = INTOSCIO  // utilizzato oscillatore interno a 4Mhz. RA6 e RA7 possono essere usati come normali I/O
#pragma config WDTE = OFF       // Watchdog timer disabilitato
#pragma config PWRTE = ON       // Power-up Timer abilitato
#pragma config MCLRE = ON       // il pin RE3 (1) funziona come classico MCLR
#pragma config CP = OFF         // memoria programma non protetta
#pragma config BOREN = ON       // Brown-out Reset abilitato
#pragma config BORV = 19        // tensione di Brown-out impostata a 1.9V
#pragma config PLLEN = ON       // PLL 32x per INTOSC abilitato (frequenza INTOSC=16MHz)
// CONFIG2
#pragma config VCAPEN = RA0     // funzione VCAP impostata su RA0