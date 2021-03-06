// Talk I2C with the Wii Nunchuk and output the result on the UART
// ===============================================================

// References:
//
// http://www.windmeadow.com/node/42
//   I tried to follow this reference but never managed to read more than one
//   package from the Nunchuk. It worked better when I changed the
//   start sequence 0x52 0x40 0x00 to the longer and more generic init sequence.
//     This was probably because I didn't NACK the last read byte.
//
// http://thelast16bits.blogspot.se/2013/02/pic18f4550-interface-to-wii-nunchuck.html
//   Gave hints about needed delays. One of many places where the longer init
//   sequence and package layout where given.
//
// http://www.wiibrew.org/wiki/Wiimote/Extension_Controllers/Nunchuck
//   This is the page that a lot of other pages link to.
//

// NOTE: The interaction with the Nunchuk is very sensitive
//       to the amount of delay between the operations.

// DSPIC33FJ128MC802 Configuration Bit Settings

#include <xc.h>

// FBS
#pragma config BWRP = WRPROTECT_OFF     // Boot Segment Write Protect (Boot Segment may be written)
#pragma config BSS = NO_FLASH           // Boot Segment Program Flash Code Protection (No Boot program Flash segment)
#pragma config RBS = NO_RAM             // Boot Segment RAM Protection (No Boot RAM)

// FSS
#pragma config SWRP = WRPROTECT_OFF     // Secure Segment Program Write Protect (Secure segment may be written)
#pragma config SSS = NO_FLASH           // Secure Segment Program Flash Code Protection (No Secure Segment)
#pragma config RSS = NO_RAM             // Secure Segment Data RAM Protection (No Secure RAM)

// FGS
#pragma config GWRP = OFF               // General Code Segment Write Protect (User program memory is not write-protected)
#pragma config GSS = OFF                // General Segment Code Protection (User program memory is not code-protected)

// FOSCSEL
#pragma config FNOSC = FRCPLL           // Oscillator Mode (Internal Fast RC (FRC) w/ PLL)
#pragma config IESO = ON                // Internal External Switch Over Mode (Start-up device with FRC, then automatically switch to user-selected oscillator source when ready)

// FOSC
#pragma config POSCMD = NONE            // Primary Oscillator Source (Primary Oscillator Disabled)
#pragma config OSCIOFNC = OFF           // OSC2 Pin Function (OSC2 pin has clock out function)
#pragma config IOL1WAY = ON             // Peripheral Pin Select Configuration (Allow Only One Re-configuration)
#pragma config FCKSM = CSDCMD           // Clock Switching and Monitor (Both Clock Switching and Fail-Safe Clock Monitor are disabled)

// FWDT
#pragma config WDTPOST = PS32768        // Watchdog Timer Postscaler (1:32,768)
#pragma config WDTPRE = PR128           // WDT Prescaler (1:128)
#pragma config WINDIS = OFF             // Watchdog Timer Window (Watchdog Timer in Non-Window mode)
#pragma config FWDTEN = OFF             // Watchdog Timer Enable (Watchdog timer enabled/disabled by user software)

// FPOR
#pragma config FPWRT = PWR128           // POR Timer Value (128ms)
#pragma config ALTI2C = OFF             // Alternate I2C  pins (I2C mapped to SDA1/SCL1 pins)
#pragma config LPOL = ON                // Motor Control PWM Low Side Polarity bit (PWM module low side output pins have active-high output polarity)
#pragma config HPOL = ON                // Motor Control PWM High Side Polarity bit (PWM module high side output pins have active-high output polarity)
#pragma config PWMPIN = ON              // Motor Control PWM Module Pin Mode bit (PWM module pins controlled by PORT register at device Reset)

// FICD
#pragma config ICS = PGD1               // Comm Channel Select (Communicate on PGC1/EMUC1 and PGD1/EMUD1)
#pragma config JTAGEN = OFF             // JTAG Port Enable (JTAG is Disabled)


// Hard-coded Fcy. See calculation below.
#define FCY 29840000

// Setup (optional) I2C_ERROR before including i2c_helper.h
#define I2C_ERROR(message) \
    printf(message);       \
    fflush(stdout);        \
    pulse_forever(LATAbits.LATA0, 500, 500)

#include "communication/i2c_helper.h"
#include "debug/led_debug.h"
#include "time/delayer.h"

#include <stdio.h>

// This buffer is used to dump the data from the Nunchuk.
#define NUNCHUK_READ_BYTES 6
static unsigned char data[NUNCHUK_READ_BYTES];

// Setup the frequency.
void init_fcy(void) {
    // Timing for Internal FRC /w PPL

    // Fin     : 7370000
    //  Fin    : FRC / FRCDIV
    //  FRC    : 7370000 // Internal Fast RC Oscillator: 7.37 MHz
    //  FRCDIV : 1       // Internal Fast RC Oscillator Postscaler bits
    //   Default: CLKDIV.FRCDIV = 1;

    // PPL calculation
    //  VCOin  :    Fin / N1 must be within 0.8 MHz and 8 MHz
    //  VCOout :  VCOin * M  must be within 100 MHz and 200 MHz
    //  Fosc   : VCOout / N2 must be within 12.5 MHz and 80 MHz
    // Final frequency
    //  Fcy    : Fosc / 2

    // N1 : 2
    //  Default: CLKDIVbits.PLLPRE = 0;

    // M : 64 (default: 50)
    PLLFBDbits.PLLDIV = 64; // Set to 64 to match Fcy of dsPIC32f2011 FRC /w 16 PLL

    // N2 : 4
    //  Default: CLKDIVbits.PLLPOST = 1;

    // Fcy = Fin / N1 * M / N2 / 2
    // Fcy = 737000 / 2 * 64 / 4 / 2
    // Fcy = 29840000

    // Processor Clock Reduction Select Bits
    //  Default: CLKDIV.DOZE = 0x3; // Fcy/8
}

void init_pins(void) {
    // Unlock Pin Select
    OSCCON = 0x46;
    OSCCON = 0x47;
    OSCCONbits.IOLOCK = 0;

    // RP10 => UART1 RX
    RPINR18bits.U1RXR = 10;

    // RP11 => UART1 TX
    RPOR5bits.RP11R = 0b11; // UART1 TX

    // Lock Pin Select
    OSCCON = 0x46;
    OSCCON = 0x47;
    OSCCONbits.IOLOCK = 1;
}

void init_uart(void) {
    // Baud Rate Generator calculation
    // Baud_rate = Fcy / (16 * (BRG + 1))
    // BRG       = Fcy / (16 * Baud_rate) - 1
    // Fcy       = 29840000
    // Baud_rate = 9600
    // BRG = 29840000 / (16 * 9600) - 1
    // BRG ~= 193

    U1BRG = 193;

    U1MODE = 0;
    U1MODEbits.UARTEN = 1; // Enable UART
    U1MODEbits.UEN    = 0;
    U1MODEbits.PDSEL  = 0; // 8-bit data, no parity
    U1MODEbits.STSEL  = 0; // 1 stop bit
    U1MODEbits.ABAUD  = 0; // No auto baud
    U1MODEbits.LPBACK = 0; // No loopback mode
    U1STA             = 0;
    U1STAbits.UTXEN   = 1; // Enable transmit
}

void read_bytes_from_nunchuk(unsigned char bytes[NUNCHUK_READ_BYTES]) {
    int i;

    i2c_start_and_wait();
    i2c_write_and_wait(0xA5);
    for (i = 0; i < NUNCHUK_READ_BYTES; i++) {
        bytes[i] = i2c_read_and_wait(i == NUNCHUK_READ_BYTES - 1);
    }
    // Wait to prevent bus collision stat. 1ms didn't work.
    delay_ms(10);
    i2c_stop_and_wait(2);
}

void init_nunchuk() {
    // Init nunchuk
    i2c_start_and_wait();
    i2c_write_and_wait(0xA4);
    i2c_write_and_wait(0xF0);
    i2c_write_and_wait(0x55);
    i2c_stop_and_wait(0);

    delay_ms(1);

    i2c_start_and_wait();
    i2c_write_and_wait(0xA4);
    i2c_write_and_wait(0xFB);
    i2c_write_and_wait(0x00);
    i2c_stop_and_wait(1);

    delay_ms(1);

    // Read the Device ID
    read_bytes_from_nunchuk(data);

    // Print the Device ID
    printf("Device ID: ");
    int i;
    for (i = 0; i < NUNCHUK_READ_BYTES; i++) {
        printf("%X ", data[i]);
    }
    printf("\n");
    fflush(stdout);
}


typedef struct Nunchuk {
    struct {
        char x;
        char y;
    } joystick;
    // int since 10 bits resolution
    struct {
        int x;
        int y;
        int z;
    } accelerometer;
    struct {
        char c;
        char z;
    } buttons;
} Nunchuk;


Nunchuk bytes_to_nunchuk(unsigned char bytes[NUNCHUK_READ_BYTES]) {
    Nunchuk n;
    n.joystick.x      = bytes[0];
    n.joystick.y      = bytes[1];
    n.accelerometer.x = bytes[2] << 2;
    n.accelerometer.y = bytes[3] << 2;
    n.accelerometer.z = bytes[4] << 2;

    typedef union {
        unsigned char init_dummy;
        struct {
            unsigned int z  : 1;
            unsigned int c  : 1;
            unsigned int ax : 2;
            unsigned int ay : 2;
            unsigned int az : 2;
        } bits;
    } last_byte;

    last_byte lb = { bytes[5] };

    // Button bits are reversed.
    n.buttons.z        = !lb.bits.z;
    n.buttons.c        = !lb.bits.c;
    // Lower accelerometer bits
    n.accelerometer.x |= lb.bits.ax;
    n.accelerometer.y |= lb.bits.ay;
    n.accelerometer.z |= lb.bits.az;

    return n;
}

Nunchuk read_data_from_nunchuk(unsigned char bytes[NUNCHUK_READ_BYTES]) {
    i2c_start_and_wait();
    i2c_write_and_wait(0xA4);
    i2c_write_and_wait(0x00);
    i2c_stop_and_wait(4);

    delay_ms(1);

    read_bytes_from_nunchuk(bytes);
    return bytes_to_nunchuk(bytes);
}

void print_nunchuk(Nunchuk* this) {
    printf("joystick X: %3d Y: %3d "
           "accelermoter X: %3d Y: %3d Z: %3d "
           "buttons c: %d z: %d",
            (int)this->joystick.x,
            (int)this->joystick.y,
            (int)this->accelerometer.x,
            (int)this->accelerometer.y,
            (int)this->accelerometer.z,
            (int)this->buttons.c,
            (int)this->buttons.z);
}

int main(void) {
    init_fcy();
    init_pins();
    init_uart();
    init_i2c(270, TRISBbits.TRISB8, TRISBbits.TRISB9, LATBbits.LATB9);

    printf("\n=== main_wii_chuck.c ===\n");
    fflush(stdout);

    init_nunchuk();

    while (1) {
        // Request data from nunchuk
        Nunchuk package = read_data_from_nunchuk(data);
        printf("\r");
        print_nunchuk(&package);

        delay_ms(10);
    }


    CloseI2C1();

    while (1);

    return 0;
}
