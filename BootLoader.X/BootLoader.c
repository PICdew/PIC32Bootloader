// code for a boot loader for PIC32 (and other?) MCUs
// Chris Lomont May 2015
#include <stdint.h>  // basic types like uint16_t, etc.
#include <stdbool.h> // type bool
#include <xc.h>      // standard part port definitions

#include "BootLoader.h"

/************************** Overview *******************************************
 *
 * This is a bootloader for PIC32 MCUs, written by Chris Lomont for Hypnocube.
 * It allows flashing PICs from (optionally) encrypted images for upgrading PIC
 * applications in the field. Read the Usage section to see how to use it.
 * www.hypnocube.com
 *  May 2015 - Version 0.5 - initial release
 *
 ******************************************************************************/


/************************** Usage: *********************************************
 * todo
 *  1. Add this file to your program (it makes keeping the config bits in sync)
 *  2. To position it in memory, you must add and modify the linker script.
 *     This is in Microchip distro, has the same name as your PIC, like
 *     p32MX150F128B.ld. We put this in normal flash (see notes), at the
 *     beginning, by splitting the linker script line in the MEMORY section thus
 *
 *
 *       kseg0_program_mem     (rx)  : ORIGIN = 0x9D000000, LENGTH = 0x1F000
 *     into two
 *       hypnocube_bootcode    (rx)  : ORIGIN = 0x9D000000, LENGTH = 0x01000
 *       kseg0_program_mem     (rx)  : ORIGIN = 0x9D001000, LENGTH = 0x1E000
 *
 *     Also reserve one word in RAM by splitting one line
 *
 *        kseg1_data_mem       (w!x)  : ORIGIN = 0xA0000000, LENGTH = 0x8000
 *     into two
 *       hypnocube_bootram    (w!x)  : ORIGIN = 0xA0000000, LENGTH = 0x4
 *       kseg1_data_mem       (w!x)  : ORIGIN = 0xA0000004, LENGTH = 0x8000-4
 *
 *     Next, before the .text: section definition,  add these sections to
 *     map the code to this memory section:
 *
 *        .hcbcode :
 *        {
 *          *(.hcbcode.*)
 *          *(.hcbcode)
 *          KEEP (*(.hcbcode))
 *          KEEP (*(.hcbcode.*))
 *          . = ALIGN(4) ;
 *        } >hypnocube_bootcode
 *        .hcbram :
 *        {
 *          *(.hcbram.*)
 *          *(.hcbram)
 *          KEEP (*(.hcbram))
 *          KEEP (*(.hcbram.*))
 *          . = ALIGN(4) ;
 *        } >hypnocube_bootram
 *
 *     Now the bootloader code and single ram location will be located nicely.
 *
 *
 *  2. set crt0
 *
_startup:

        ##################################################################
        # Bootloader jump added by Chris Lomont from Hypnocube. If this is
        # changed, the check code in the bootloader must be changed, since
        # exactly this code sequence must appear early in the reset area
        ##################################################################
        la      sp,0xA0000000 + 8*1024 # all PIC32s have at least 16K RAM, use 8
        la      t0,BootloaderEntry     # boot address, always same location
        jalr    t0                     # jump and link so we can return here
        nop                            # required branch delay slot, executed

 *
 *
 *  4. edit parameters in the user defines section
 *  5. test
 *
 * todo - uses lots of stack!
 *
 *  . Compile with optimized, else bigger linker spaces
 *
 * n. Define for your PIC if not present
 * Read notes for help, info
 *
 * TODO: explain if use modified CRT, remove link option from linker
 * Theory of operation: todo
 ******************************************************************************/


/************************** User defines **************************************/
// You must set the clock speed the same as the boot up settings for the device
// this clock is used for timing and setting the UART baud rate
#define SYS_CLOCK 48000000L           // system clock, ticks go half this rate
#define TICKS_PER_MILLISECOND ((SYS_CLOCK)/2000)
#define TICKS_PER_MICROSECOND ((SYS_CLOCK)/2000000)

// define this to allow flashing the BOOT flash section
// this is recommended - the bootloader still protects its own
// code which is stored at the lowest FLASH addresses
#define ALLOW_BOOTFLASH_OVERWRITE

// UART baud rate
#define DESIRED_BAUDRATE 1000000   
// Define a UART (UART1, UART2, etc) to use. You must add code cases as needed
#define HC_UART1
// #define HC_UART2

// Number of milliseconds to look for flashing tool at boot. 
#define BOOT_WAIT_MS 1000

// define this to use encrypted images, else unencrypted
#define USE_CRYPTO

#ifdef USE_CRYPTO
// If encrypted, you need a 32 byte key, stored here as eight 4 byte values
// These get written as the key, word 0 first (lowest address), each stored
// big endian into a byte array
#define PASSWORD_WORD0 0x12345678
#define PASSWORD_WORD1 0x12345678
#define PASSWORD_WORD2 0x12345678
#define PASSWORD_WORD3 0x12345678
#define PASSWORD_WORD4 0x12345678
#define PASSWORD_WORD5 0x12345678
#define PASSWORD_WORD6 0x12345678
#define PASSWORD_WORD7 0x12345678
#endif


// To have some blinking LED feedback, define these for your device
// otherwise leave blank
//#define LED_INIT()
//#define LED_ON()
//#define LED_OFF()
//#define LED_TOGGLE()
// example, if PORTA, pin RA1, is tied to a LED:
#define LED_INIT()   LATACLR = 1<<1; TRISACLR = 1<<1
#define LED_ON()     PORTAbits.RA1=1
#define LED_OFF()    PORTAbits.RA1=0
#define LED_TOGGLE() PORTAbits.RA1^=1


// needed defines for each PIC type - todo - group them nicely
#if defined(__32MX150F128C__) || defined(__32MX150F128B__)
#define FLASH_PAGE_SIZE 1024 // bytes
#define FLASH_ROW_SIZE   128 // bytes

// memory regions, end is one past usable end
// all values are PHYSICAL addresses, not  logical
// most (all) are the same across chips
#define RAM_START            0x00000000
#define RAM_END              ((RAM_START) + (BMXDRMSZ))
#define FLASH_START          0x1D000000
#define FLASH_SIZE           (BMXPFMSZ)
#define FLASH_END            ((FLASH_START) + (FLASH_SIZE))
#define PERIPHERAL_START     0x1F800000
#define PERIPHERAL_END       0x1F900000
#define BOOT_START           0x1FC00000
#define BOOT_SIZE            (BMXBOOTSZ)
#define BOOT_END             ((BOOT_START) + (BOOT_SIZE))
// note config may be in the boot flash region
#define CONFIGURATION_START  0x1FC00BF0
#define CONFIGURATION_END    0x1FC00C00

// logical address for flash regions
#define FLASH_START_LOGICAL  0xBD000000
#define BOOT_START_LOGICAL   0xBFC00000



#else

error! need defines for your chip

#endif




/*********************** Notes *************************************************
 * 
 * Original idea to make a boot flash bootloader is fraught with problems:
 *  1. Boot flash is small, 3K = 0xD00, on many PICs
 *  2. Boot flash has a debug executive in it, and several fixed addresses
 *     that need exception vectors (Reset, boot exception, etc.)
 *  3. Debug executive takes significant space (0x760)
 *  4. These vectors are set to point to code in the CRT in non-boot flash at
 *     link time, so any recompile of the use app may move these vectors
 *  5. Could fiddle with linker script, and crt enough to make all work, ....
 *  6. But this is hard to maintain over time as crt changes....
 *  7. So - we make boot code at a fixed, protected address, and the user
 *     program should call it first thing. Then boot loader allows flashing the
 *     boot area itself to allow user changes to crt, etc.
 *  8. Downside - some chance of the boot loader wiping the code that is called
 *     before it.
 *  9. All this would be ok on 12K boot flash PICs. Make version for there.
 *
 * BOOT FLASH layout (3K version)
 * 0x0000 - 0x037F : 0x380 space, execution starts here
 * 0x0380 - 0x047F : space? boot exception vector
 * 0x0480 - 0x048F : 0x10 space, debug exception vector jumps into normal FLASH
 * 0x0490 - 0x0BDF : 0x760 space, debug code executive, needed to debug
 * 0x0BF0 - 0x0BFF configuration registers
 *
 * NOTE: config in boot flash - erasing this page erases config bits. Cannot do this!
 *       Thus we block all access to this page (0x800-0xBFF). This may break the
 *       debug executive, but should allow changes to be made to the exception vectors
 *       which is more important
 *
 * DEVCFG0 bits
 *    CP  : prevent flash reading/modification from external device 0=enabled, 1=disabled
 *    BWP : prevent boot flash from being modified during code execution 0=not writeable, 1=writeable
 *    PWP : prevent selected flash pages form being modified during code execution
 *        111111111 = disabled
 *        111111110 = address below 0x0400 protected
 *        111111101 = address below 0x0800 protected
 *        ....
 *        011111111 = address below 0x40000 (256K) protected
 *        ...
 *        000000000 = all possible memory protected
 *   ICESEL : ICD pins select PGECx/PGEDx x = 1,2,3,4 for bits 11,10,01,00
 *   JTAGEN, DEBUG,
 * DEVCFG1 bits : lots of watchdog timer stuff, clock oscillator stuff
 * DEVCFG2 bits : more clock stuff, USB stuff,
 * DEVCFG4 bits : USB stuff, peripheral pin stuff, USERID
 *
 *    
 *
 *
 *
 * // compile with size flags to make small enough
// why not in boot sector (contention...), avoids address of main using crt.0

 *

 * _RESET_ADDR, _BEV_EXCPT_ADDR, _DBG_EXCPT_ADDR and _DBG_CODE_ADDR are all
 * determined by the chip's hardware, cannot change them.
 *
 * _RESET_ADDR                    -- Reset Vector
 * _BEV_EXCPT_ADDR                -- Boot exception Vector
 * _DBG_EXCPT_ADDR                -- In-circuit Debugging Exception Vector
 * _DBG_CODE_ADDR                 -- In-circuit Debug Executive address
 * _DBG_CODE_SIZE                 -- In-circuit Debug Executive size
 * _GEN_EXCPT_ADDR                -- General Exception Vector
 *************************************************************************
_RESET_ADDR                    = 0xBFC00000;
_BEV_EXCPT_ADDR                = 0xBFC00380;
_DBG_EXCPT_ADDR                = 0xBFC00480;
_DBG_CODE_ADDR                 = 0x9FC00490;
_DBG_CODE_SIZE                 = 0x760;
_GEN_EXCPT_ADDR                = _ebase_address + 0x180;

 * Logical addresses
 * KSEG0  0x80000000 - 0x9FFFFFFF is the same (overlaps) as
 * KSEG1  0xA0000000 - 0xBFFFFFFF
 * RAM    0x80000000 & 0xA0000000
 * FLASH  0x9D000000 & 0xBD000000
 * Peripherals         0xBF8000000 ONLY

 * BOOT   0x9FC00000 & 0xBFC00000, LENGTH = 0xD00  (3K) or 0x3000 (12K)
 * CONFIG 0x9FC00BF0 & 0xBFC00BF0, LENGTH = 0x010  NOTE: overlaps boot flash!
 *
 * So, BOOT flash in 3K bootloaders is mostly used by Debug Executive, only have
 * 0x000 - 0x380 and 0x390-0x480 =
 *
 *
 * http://microchip.wikidot.com/32bit:mx-arch-exceptions-entry-points
 * The Reset, Soft Reset, and NMI exceptions are always vectored to location 0xBFC00000 (uncached, start-up safe KSEG1 region).


/*
 * TODO - future versions
 * Bootloader version at fixed address
 *
 *

 *
 C:\Users\Chris\SkyDrive\Hypnocube\Code\BootLoader.X\build\default\production>"\P
rogram Files (x86)\Microchip\xc32\v1.30\bin\xc32-objdump.exe" -S BootLoader.o |
more*
 *
// to use DEBUG_BOOTLOADER must make linker script mem larger

// all functions start with Boot and use BOOT_FUNC to locate properly
// can use x32-objdump -x Bootloader.o to see where items went
// can use gcc options to place data and functions in own sections to get more info
        // (in XC32/bin directory)

/* Features needed/desired
 *   1. UART (serial) support, possible Ethernet, SD card, SPI, etc?
 *   2. Can identify device to make sure not loading wrong items
 *   3. Protected from overwriting itself
 *   4. Setting of baud rate at compile time
 *   5. Encryption
 *   6. Compression
 *   7. Blinking LED when available
 *   8. Small RAM footprint (small enough for all PICs)
 *   9. Page size set by PIC type
 *  10. Error checking/re-transmission
 *  11. Fires on boot, waits, then if nothing, does normal main
 *  12. Small, works on all(?) PIC32?
 * . how to identify boot loading
 * . verify mode?
 * . CRC32K throughout
 *   . can also load unencrypted -
 *  . bootloader Version #
 * . error messages/bytes/stats
 * . NO: did not need - modified crt0.s must modify linker script - possibly can make same file with defines used here and included in linker script?
 * . explain how to check linker/load sections
 * . looked at symmetric and public key methods - if either decryption key lifted from device, irrelevant, so pick symmetric:
 * .chacha?
 *   - XXTEA seems good enough even though much weaker than AES. It's also very small
 *   - use ciphertext stealing to keep data short
 *   - use some block chaining format
 *   - send data in blocks with CRC allowing resends? Or one large block with CRC and verify?
 *   -
 */


// switch ACK in some cases to ACK,reason,CRC? used for counting progress on erase?
// //  x when erasing: send to the PC the number of pages to be erased, then send ack (BTLDR_ERASE) for each
//    erased page (in order to use progress bar in PC software). When finished send BTLDR_OK


/* Protocol
 * 1. Flasher application sends repeating ACK command over the serial port
 *    Note there may be no serial port, since the device is not powered up,
 *    so the flasher will poll the port.
 * 2. Power up Device to be flashed. It waits some amount of time (1 sec? 2 secs?)
 *    looking for an ACK command from the Flasher.
 * 3. If Device sees ACK, sends back ACK, enters command loop
 * 4. If Device does not see ACK within timeout, returns into user code.
 * Command Loop - this is a loop, in client/server mode:
 *    Flasher sends command, Device responds.
 *
 * Commands: (each ASCII byte, some have additional data)
 *    'I' (0x49) = Identify. No data. Respond ? bytes info
 *        Useful fields: bootloader version, product version, PIC version?
 *        memory locations
 *        Responds with DATA CRC
 *    'C' (0x43) = CRC. Compute CRC32K over all flash, and output text, then ACK.
 *    'E' (0x45) = Erase. Send 'E' Address CRC, responds ACK CRC
 *    'W' (0x57) = Write. Send 'W' Address Length CRC, returns ACK CRC or NACK CRC
 *    'Q' (0x51) = Quit. Send 'Q'CRC. Return ACK then Device exits boot loader.
 */

// todo - make sure no strings in here that are not in this boot flash
        
/* Notes:
 * 1. Boot loader and app must have same config bits. Needed to compile together to make all work nicely. TODO - test standalone app.
 *
 *
 *
 * From http://microchip.wikidot.com/faq:85  "(XC32) How do I link in my own custom startup-code, with the XC32 Compiler? "
 * 1. Copy the default startup code "crt0.S?, available under the folder ?<Compiler-DIR>\pic32-libs\libpic32\startup?, to your project directory.
 * 2. Customize this Startup code, by making the changes as required.
 * 3. Add this modified startup code (crt0.S) to the MPLAB� X IDE Project under ?Source Files?.
 *    You may also change the name of your custom startup code to something like 'my_startup.S'.
 * 4. Set the options to exclude linking of default Startup code.
 *    Under MPLAB X IDE go to:
 *    File ? Project Properties ? xc32-ld ? Option categories = Libraries ? Check the option 'Do not link startup code' ? Click 'Ok'
 * 5. The code will now link with the customized startup code.
 *    Note that the default startup code source, crt0.S, uses many device-specific macros that should be defined as appropriate for your target device.
 *
 *
 * I modified the file by adding the 4 lines:
 *      # added by Chris Lomont at hypnocube to enable the boot loader
        la      t0,BootloaderEntry
        jalr    t0
        nop
 *
 * before the section starting with:

        ##################################################################
        # Call main. We do this via a thunk in the text section so that
        # a normal jump and link can be used, enabling the startup code
        # to work properly whether main is written in MIPS16 or MIPS32
        # code. I.e., the linker will correctly adjust the JAL to JALX if
        # necessary
        ##################################################################
        and     a0,a0,0
        and     a1,a1,0
        la      t0,_main_entry
        jr      t0
        nop

 */

/* Usage
 * 1. Make new linker script from default one
 *    a. copy C:\Program Files (x86)\Microchip\xc32\v1.30\pic32mx\lib\ldscripts\elf32pic32mx.x
 *       to new script with .ld extension in linker files portion of the project.
 *    b.
 *
 *
 *
 *
 */

//


/*********************** defines, types, storage ******************************/
// define this to get state and other debugging messages
// moves the bootloader and does some other things
// makes image larger, so linker script may need modified to make room
// #define DEBUG_BOOTLOADER

// define this to cause flash operations to be ignored
// useful for testing when you don't want flash being changed
// #define IGNORE_FLASH_OPS

// addresses to protect the bootloader
#define BOOTLOADER_SIZE            0x2000 // must match linker script
#define BOOT_BASE_ADDRESS      0x1D000000 // note PHYSICAL address
#define BOOT_ENTRY_ADDRESS     0x9D000000 // note LOGICAL address

// number of 32-bit instructions matched to detect bootloader
#define BOOT_INSTRUCTION_COUNT 6
// max number of 32-bit instructions scanned to detect bootloader
#define BOOT_INSTRUCTION_SEEK  12


// space needed for buffer overhead when loading write packets
#define BUFFER_OVERHEAD 20

// internal ram buffer size used for temp storage
#define BUFFER_SIZE (FLASH_PAGE_SIZE+BUFFER_OVERHEAD)

// used to put code items into the boot rom section we defined in the linker script
#define BOOT_CODE __attribute__((section(".hcbcode")))

// used to put entry point into the boot rom section we defined in the linker script
// needs an extra section extension, else placed incorrectly in the section
#define BOOT_ENTRY __attribute__((section(".hcbcode.entry")))

// used to put data items into the boot rom section we defined in the linker script
// the ',r' part marks the data with a readonly attribute, for linker use
// use x for executable, b for BSS, r for read only, and d for writeable data
#define BOOT_DATA __attribute__((section(".hcbcode")))

// used to put items into the boot ram section we defined in the linker script
#define BOOT_RAM  __attribute__((section(".hcbram")))

// Any strings needed must be defined using this, to ensure the storage
// is in the proper flash section, otherwise the linker may put the
// text elsewhere
#define BOOTSTRING(name,text) BOOT_DATA static const char name[] = text

// use to set an item address. Requires logical address
#define FIX_ADDRESS(addr) __attribute__((address(addr)))

// times to retry a write before giving up
#define WRITE_RETY_MAX 5
        
// how to map logical addresses to physical addresses
#define LOGICAL_TO_PHYSICAL_ADDRESS(addr) ((addr)&0x1FFFFFFF)

// bootloader code version
#define BOOTLOADER_VERSION_MAJOR 0
#define BOOTLOADER_VERSION_MINOR 5

// Send \r\n to UART
#define ENDLINE()  {BootUARTWriteByte('\r');BootUARTWriteByte('\n'); }

#define CRYPTO_ROUNDS 20 // for 20 rounds of Salsa20

// the byte that signals a positive outcome to the flashing utility
// has nice property that becomes different values at nearby baud rates
#define ACK  0xFC

// NACK is a byte, starts with 0xF0, has 16 lower nibble (0x0C is ACK)
#define NACK(reason) BootUARTWriteByte(0xF0+reason)
// NACK reasons
enum {

    // write problems
    NACK_CRC_MISMATCH             = 0x00,
    NACK_PACKET_SIZE_TOO_LARGE    = 0x01,
    NACK_WRITE_WITHOUT_ERASE      = 0x02,
    NACK_WRITE_SIZE_ERROR         = 0x03,
    NACK_WRITE_MISALIGNED_ERROR   = 0x04,
    NACK_WRITE_WRAPS_ERROR        = 0x05,
    NACK_WRITE_OUT_OF_BOUNDS      = 0x06,
    NACK_WRITE_OVER_CONFIGURATION = 0x07,
    NACK_WRITE_BOOT_MISSING       = 0x08,
    NACK_WRITE_FLASH_FAILED       = 0x09,
    NACK_COMPARE_FAILED           = 0x0A,
    NACK_WRITES_FAILED            = 0x0B,

    // reserved for ACK
    NACK_ACK_RESERVED             = 0x0C, 

    // system problems
    NACK_UNKNOWN_COMMAND          = 0x0D,

    // erase problems
    NACK_ERASE_OUT_OF_BOUNDS      = 0x0E,
    NACK_ERASE_FAILED             = 0x0F,

};

// outcomes of the bootloader code, for querying
// by the application
enum {
    BOOT_RESULT_SKIPPED = 0,
    BOOT_RESULT_SUCCESSFUL = 1,
    BOOT_RESULT_POWER_EXIT = 2,
    BOOT_RESULT_STARTED = 3,
    BOOT_SET_HARDWARE_FAILED = -1,
    BOOT_RESULT_ASSUMPTIONS_FAILED = -2,
};

// one return variable at bottom of ram
// to access from main code, use :
// extern __attribute__((section(".hcram"))) uint8_t bootResult;
// this variable needs to persist past crt0, so uses the
// "persistent" attribute, which has linker script support
// by default from microchip. Otherwise this result will not be seen in the
// user code. See
// http://www.microchip.com/forums/m594242.aspx
// http://www.microchip.com/forums/m434737.aspx#434882
BOOT_RAM uint8_t bootResult  __attribute__ ((persistent));

// this define must match the address of the boot result
#define BOOT_RESULT_VIRTUAL_ADDRESS 0xA0000000

// structure for tracking a timer
typedef struct
{
    // for timing
    uint32_t timerLast, timerNext, timerExcess;
    uint32_t timerCount;// counts ms or us depending on the timer mode
    uint32_t ticksPerCount; // can count many things, 
} Timer_t;

#ifdef USE_CRYPTO
// crypto state
typedef struct
{
    int i;
    uint32_t state[16]; // crypto state
    uint32_t x[16];     // crypto temp
    uint8_t output[64];
    int32_t bytes,d64;
} Crypto_t;
#endif

// this is all the local storage the boot loader needs
typedef struct
{
    // used to see if flash erased command has completed
    // must be done before writing allowed
    bool flashErased;

    // used to track timeouts and delays
    Timer_t timeoutTimerMs;

    // used for delays while writing flash
    Timer_t nvmTimerUs;

    // buffer for receiving pacekt of data from the flash utility
    uint8_t buffer[BUFFER_SIZE];
    // where in buffer to put next byte read
    int readPos;
    // number of bytes in packet when finished
    int readMax;

    // place to store CRC calculations
    uint32_t transmittedCrc,computedCrc;

    // temp address for iterating over memory
    uint32_t curAddress;

    // number of pages attempted to be erased
    uint32_t pageEraseAttemptCount;

    // number of pages failing to erase
    uint32_t pageEraseFailureCount;
    
    // address and size of data to write from the buffer into flash
    uint32_t writeSize, writeAddress;

    // number of writes that failed
    // could be rows or single words
    uint32_t writeFailureCount;

    // count packets since last erase
    uint32_t packetCounter;

    // set to true on last write packet seen
    bool writesFinished;

    // counter used to retry writes a few times when flashing
    uint32_t writeRetryCounter;

    // space for result of a flash write
    uint32_t flashWriteResult;

#ifdef USE_CRYPTO
    Crypto_t crypto;
#endif

} Boot_t;

/**************************** UART section ***********************************/
#if 0
// if there is any UART error, return 1, else return 0
// todo - clear errors?
BOOT_CODE static int BootUARTError()
{
        int err =
        U1STAbits.OERR | // overrun error
        U1STAbits.FERR | // framing error
        U1STAbits.PERR ; // parity error

        err |=
        U2STAbits.OERR | // overrun error
        U2STAbits.FERR | // framing error
        U2STAbits.PERR ; // parity error

        return err!=0?1:0;
}

#endif

// read byte if one is ready.
// if exists, return true and byte
// if return false, byte = 0 is none avail, else
// byte != 0 means error
BOOT_CODE static bool BootUARTReadByte(uint8_t * byte)
    {
    // if data ready, get it
    if (U1STAbits.URXDA != 0)
    {
        *byte = U1RXREG;
        return true;
    }
    *byte = 0;
    return false;
    } // UARTReadByte


// blocking call to write byte to UART
BOOT_CODE static void BootUARTWriteByte(char byte)
{
#ifdef HC_UART1
        while (!U1STAbits.TRMT);   // wait till bit clear, then ready to transmit
        U1TXREG = byte; // write a byte
#else
        while (!U2STAbits.TRMT);   // wait till bit clear, then ready to transmit
        U2TXREG = byte; // write a byte
#endif
}

//#ifdef DEBUG_BOOTLOADER // removed this function from release version
// print the message to the serial port.
BOOT_CODE static void BootPrintSerial(const char * message)
{
    while (*message != 0)
    {
        BootUARTWriteByte(*message);
        message++;
    }
}
//#endif

// print the integer to the serial port.
BOOT_CODE static void BootPrintSerialInt(uint32_t value)
{
    uint32_t pow10 = 1; // stack, ends on 0, so ok

    // want value/10 < pow10 < value
    while (pow10 <= value/10)
        pow10 *= 10;

    do
    {
        uint32_t digit = value/pow10;
        BootUARTWriteByte(digit+'0');
        value -= digit*pow10;
        pow10 /=10;
    } while (pow10 > 0);
}

// print the integer to the serial port as a n byte hex value
BOOT_CODE static void BootPrintSerialHexN(uint32_t value, int n)
{
    int i; // todo - remove from stack? ends on zero, so ok?
    for (i = n; i > 0; --i)
    {
        int val = (value>>(n*4-4))&15;
        if (val < 10)
            BootUARTWriteByte(val+'0');
        else
            BootUARTWriteByte(val+'A'-10);
        value<<=4;
    }
}

// print the integer to the serial port as a 4 byte hex value
BOOT_CODE static void BootPrintSerialHex(uint32_t value)
{
    BootUARTWriteByte('0');
    BootUARTWriteByte('x');
    BootPrintSerialHexN(value, 8);
}

// initialize the serial port
BOOT_CODE static void BootUARTInit()
{

#if defined(__32MX150F128B__)

#ifdef HC_UART1

    // UART1
    // U1RX on RPA4
    // U1TX on RPA0
    // clear PORTA bits
    LATACLR = (unsigned int)((1<<0) | (1<<4)); // BIT 0 and 4
    // set PORTA input pins
    TRISASET = (unsigned int)(1<<4);
    ANSELACLR = (unsigned int)(1<<4);
    // set PORTA output pins
    TRISACLR = (unsigned int)(1<<0);
    ANSELACLR = (unsigned int)(1<<0);

    U1RXR = 2; // RPB2 = U1RX
    RPA0R = 1; // PIN RPA0 = U1TX

    // clock divider
    U1BRG = SYS_CLOCK /(4*DESIRED_BAUDRATE) - 1;

    // mode
    U1MODE =
            (1<<15) | // module enable
            (1<< 3) | // 4x clock speed, high speed
            0;

    // TX/RX features
    U1STA =
            (1<<12) | // RX_ENABLE
            (1<<10) | // TX_ENABLE
            0;
#else
    todo - UART2
#endif

#else
    #pragma error "Unknown PIC version"
    todo - error!
#endif

}

// print debug messages if the debugging define is set, else ignore them
#ifdef DEBUG_BOOTLOADER
#define BootDebugPrint(message) BootPrintSerial(message)
#define BootDebugPrintE(message) {BootPrintSerial(message); ENDLINE();}
#else
#define BootDebugPrint(message)
#define BootDebugPrintE(message)
#endif

#ifdef DEBUG_BOOTLOADER
// helper function that dumps memory over serial as text
BOOT_CODE static void BootPrintMemory(const char * msg, uint32_t address, int length)
{
    // some memory dump to help debugging
    BootDebugPrintE(msg);
    int i;
    for (i = 0; i < length; ++i)
    {
        uint32_t addr = address + i;
        if ((i&7)==0)
        {
            BootPrintSerialHex(addr);
            BootPrintSerial(" : ");
        }
        BootPrintSerialHexN((uint8_t)(*((uint8_t*)addr)),2);
        BootPrintSerial(" ");
        if ((i&7)==7)
        {
            ENDLINE();
        }
    }
}
#else
#define BootPrintMemory(msg,address,length)
#endif

/*************************** Utility section **********************************/
// compute overlap of intervals [a0,a1) and [b0,b1)
BOOT_CODE static uint32_t BootOverlap(
    uint32_t a0, uint32_t a1,
    uint32_t b0, uint32_t b1)
{
    // trim [a0,a1) to the interval
    a0 = a0>b0?a0:b0; // max, lower bound of intersection interval, inclusive
    a1 = a1<b1?a1:b1; // min, upper bound of intersection interval, exclusive
    return a1<=a0?0:a1-a0; //if empty, return 0, else positive length
}

// set the core tick counter
BOOT_CODE static inline void BootWriteTimer(uint32_t time)
{
    asm volatile("mtc0   %0, $9": "+r"(time));
}

// read the core tick counter
BOOT_CODE static inline uint32_t BootReadTimer()
{
    uint32_t time;
    asm volatile("mfc0   %0, $9" : "=r"(time));
    return time;
}

// initialize the timer fields
// call with TICKS_PER_MILLISECOND or TICKS_PER_MICROSECOND
BOOT_CODE static void BootStartTimer(Timer_t * timer, uint32_t ticksPerCount)
{
    timer->timerLast   = BootReadTimer();
    timer->timerCount  = 0;
    timer->timerExcess = 0;
    timer->ticksPerCount = ticksPerCount;
}

// initialize the timer fields
// update the timer fields, and return current time since start in
// whatever tick sizes were selected at timer start
BOOT_CODE static uint32_t BootUpdateTimer(Timer_t * timer)
{
    timer->timerNext = BootReadTimer();
    if (timer->timerNext - timer->timerLast + timer->timerExcess >= timer->ticksPerCount)
    {
        timer->timerExcess = timer->timerNext - timer->timerLast + timer->timerExcess - timer->ticksPerCount;
        timer->timerCount++;
    }
    else
        timer->timerExcess += timer->timerNext - timer->timerLast;
    timer->timerLast = timer->timerNext;
    return timer->timerCount;
}


// wait the given number of counts based on the ticks per count parameter
// uses the Boot timer
BOOT_CODE static void BootDelay(Timer_t * timer, uint32_t ticksPerCount, int counts)
{
    BootStartTimer(timer, ticksPerCount);
    while (BootUpdateTimer(timer)<counts)
    {
        // do nothing
    }
}

BOOT_CODE static void BootReadBigEndian(uint32_t * answer, uint8_t * buffer, int32_t bytes)
{
    *answer = 0;
    while (bytes>0)
    {
        *answer <<= 8;
        *answer += *buffer;
        buffer++;
        bytes--;
    }
}


/*************************** CRC32K section ***********************************/

// Compute the CRC32K of the given data.
// done without tables to save space in bootloader
// The initial crc value should be 0, and this can be chained across calls.
// returns the new CRC
BOOT_CODE static uint32_t BootCrc32AddByteBitwise(uint8_t datum, uint32_t crc32)
{
    #define poly 0x741B8CD7U
    crc32 ^= (uint32_t)(datum << 24);
#if 1
    // 1 bit per line
    crc32 = (crc32 & 0x80000000U) == 0 ? (crc32<<1) : (crc32<<1)^poly;
    crc32 = (crc32 & 0x80000000U) == 0 ? (crc32<<1) : (crc32<<1)^poly;
    crc32 = (crc32 & 0x80000000U) == 0 ? (crc32<<1) : (crc32<<1)^poly;
    crc32 = (crc32 & 0x80000000U) == 0 ? (crc32<<1) : (crc32<<1)^poly;
    crc32 = (crc32 & 0x80000000U) == 0 ? (crc32<<1) : (crc32<<1)^poly;
    crc32 = (crc32 & 0x80000000U) == 0 ? (crc32<<1) : (crc32<<1)^poly;
    crc32 = (crc32 & 0x80000000U) == 0 ? (crc32<<1) : (crc32<<1)^poly;
    crc32 = (crc32 & 0x80000000U) == 0 ? (crc32<<1) : (crc32<<1)^poly;
#else
    // 2 bits per line
    static const uint32_t const tbl4 [] = {0,poly,poly<<1,(poly<<1)^poly};
    crc32 = tbl4[crc32>>30]^(crc32<<2);
    crc32 = tbl4[crc32>>30]^(crc32<<2);
    crc32 = tbl4[crc32>>30]^(crc32<<2);
    crc32 = tbl4[crc32>>30]^(crc32<<2);
#endif

    #undef poly
    return crc32;
}


/*************************** Decryption section *******************************/
#ifdef USE_CRYPTO

#define ROTATE(v,left) (((v)<<left)|((v)>>(32-left)))

#define QUARTERROUND(a,b,c,d) \
  cs->x[a] += cs->x[b]; cs->x[d] = ROTATE(cs->x[d]^cs->x[a],16); \
  cs->x[c] += cs->x[d]; cs->x[b] = ROTATE(cs->x[b]^cs->x[c],12); \
  cs->x[a] += cs->x[b]; cs->x[d] = ROTATE(cs->x[d]^cs->x[a], 8); \
  cs->x[c] += cs->x[d]; cs->x[b] = ROTATE(cs->x[b]^cs->x[c], 7)


BOOT_CODE static void BootCryptoUnpack(uint8_t * output, int index, uint32_t val)
{
    output[index++] = val;
    output[index++] = val >> 8;
    output[index++] = val >> 16;
    output[index]   = val >> 24;
}

BOOT_CODE static uint32_t BootCryptoPack(uint8_t * k, int index)
{
    return
        (uint32_t) (
            k[index] +
            (k[index + 1] << 8) +
            (k[index + 2] << 16) +
            (k[index + 3] << 24));
}

// output 64 bytes, input 16 uint32_t
BOOT_CODE static void BootCryptoNextState(
    Crypto_t * cs,
    uint8_t * output, 
    uint32_t *  input,
    int rounds)
{
    for (cs->i = 0; cs->i < 16; ++(cs->i))
        cs->x[cs->i] = input[cs->i];
    for (cs->i = rounds; cs->i > 0; cs->i -= 2)
    {
        QUARTERROUND( 0, 4, 8,12);
        QUARTERROUND( 1, 5, 9,13);
        QUARTERROUND( 2, 6,10,14);
        QUARTERROUND( 3, 7,11,15);
        QUARTERROUND( 0, 5,10,15);
        QUARTERROUND( 1, 6,11,12);
        QUARTERROUND( 2, 7, 8,13);
        QUARTERROUND( 3, 4, 9,14);
    }
    for (cs->i = 0; cs->i < 16; ++cs->i)
        cs->x[cs->i] += input[cs->i];
    for (cs->i = 0; cs->i < 16; ++cs->i)
        BootCryptoUnpack(output, 4*cs->i, cs->x[cs->i]);
}


// Set the key, given a key of 128 or 256 bits in length
// Set the initialization vector, 64 bits
BOOT_CODE static void BootCryptoSetKeyAndInitializationVector(
    Crypto_t * cs,
    uint8_t * keyBytes,
    uint32_t keyBits,
    uint8_t * initializationVectorBytes
    )
{
// 16 byte ASCII constants as bytes
// these must be located in this section!
// linker may move strings, so cannot use them.

    cs->i = 0; // key offset
    if (keyBits == 256)
    {
        cs->i = 16;
        //uint8_t * sigma = "expand 32-byte k";
        //cs->constants = sigma;
        // set constants
        //cs->state[0] = BootCryptoPack(cs->constants, 0);
        //cs->state[1] = BootCryptoPack(cs->constants, 4);
        //cs->state[2] = BootCryptoPack(cs->constants, 8);
        //cs->state[3] = BootCryptoPack(cs->constants, 12);

        // hex for ASCII for "expand 32-byte k";
        cs->state[0] = 0x61707865;
        cs->state[1] = 0x3320646E;
        cs->state[2] = 0x79622D32;
        cs->state[3] = 0x6B206574;        
    }
    else if (keyBits == 128)
    {
        //uint8_t * tau   = "expand 16-byte k";
        // cs->constants = tau;
        // set constants
        //cs->state[0] = BootCryptoPack(cs->constants, 0);
        //cs->state[1] = BootCryptoPack(cs->constants, 4);
        //cs->state[2] = BootCryptoPack(cs->constants, 8);
        //cs->state[3] = BootCryptoPack(cs->constants, 12);

        // hex for ASCII for "expand 16-byte k";
        cs->state[0] = 0x61707865;
        cs->state[1] = 0x3120646E;
        cs->state[2] = 0x79622D36;
        cs->state[3] = 0x6B206574;
    }
    else
    {
        BootDebugPrintE("ERROR: Key invalid length");
        return;
    }
    // set key bytes
    cs->state[4] = BootCryptoPack(keyBytes, 0);
    cs->state[5] = BootCryptoPack(keyBytes, 4);
    cs->state[6] = BootCryptoPack(keyBytes, 8);
    cs->state[7] = BootCryptoPack(keyBytes, 12);
    cs->state[8] = BootCryptoPack(keyBytes, 0 + cs->i);
    cs->state[9] = BootCryptoPack(keyBytes, 4 + cs->i);
    cs->state[10] = BootCryptoPack(keyBytes, 8 + cs->i);
    cs->state[11] = BootCryptoPack(keyBytes, 12 + cs->i);
    // block counter to 0
    cs->state[12] = 0;
    cs->state[13] = 0;
    // set IV
    cs->state[14] = BootCryptoPack(initializationVectorBytes, 0);
    cs->state[15] = BootCryptoPack(initializationVectorBytes, 4);
}

// note encrypt and decrypt are the same function
BOOT_CODE static void BootCryptoDecrypt(
        Crypto_t * cs,
        uint8_t * messageBytes,
        uint32_t messageLength,
        uint8_t * cypherBytes,
        int rounds)
{
    cs->bytes = messageLength;
    if (rounds < 1)
    {
        BootDebugPrintE("ERROR: Crypto rounds must be positive");
        return;
    }

    if (cs->bytes == 0) return;
    cs->d64 = 0;
    for (;;)
    {
        // update internal state and increment 64 bit counter
        BootCryptoNextState(cs,cs->output, cs->state, rounds);

#ifdef DEBUG_BOOTLOADER
        if (cs->state[12] < 3)
        {
            BootPrintMemory("Enc output: ", (uint32_t)(cs->output), 64);
        }
#endif

        cs->state[12]++;
        if (cs->state[12] == 0)
        {
            cs->state[13]++;
            /* stopping at 2^70 bytes per nonce is user's responsibility */
        }
        if (cs->bytes <= 64)
        {
            for (cs->i = 0; cs->i < cs->bytes; ++cs->i)
                cypherBytes[cs->i + cs->d64] = (messageBytes[cs->i + cs->d64] ^ cs->output[cs->i]);
            return;
        }
        for (cs->i = 0; cs->i < 64; ++cs->i)
            cypherBytes[cs->i + cs->d64] = (messageBytes[cs->i + cs->d64] ^ cs->output[cs->i]);
        cs->bytes -= 64;
        cs->d64 += 64;
    }
}
#endif // USE_CRYPTO

/*************************** Flash writing section*****************************/

#define NVM_OP_CLEAR_ERROR   0x4000      // clears the error by executing a nop
#define NVM_OP_WRITE_WORD    0x4001      // write a 32 bit word
#define NVM_OP_WRITE_ROW     0x4003      // write a row
#define NVM_OP_ERASE_PAGE    0x4004      // erase a page

// return true on success, else false
BOOT_CODE static bool BootNVMemOperation(Boot_t * bs, uint32_t nvmop)
{
#ifdef IGNORE_FLASH_OPS
    return true; // do not change anything
#else
    // Enable Flash Write/Erase Operations
    NVMCON = NVMCON_WREN | nvmop;
    // Data sheet prescribes 6us delay for LVD to become stable.
    // we wait 7
    BootDelay(&(bs->nvmTimerUs), TICKS_PER_MICROSECOND,7);

    // write enable sequence
    NVMKEY 	= 0xAA996655;
    NVMKEY 	= 0x556699AA;
    NVMCONSET 	= NVMCON_WR;

    // Wait for WR bit to clear
    while(NVMCON & NVMCON_WR);

    // Disable Flash Write/Erase operations
    NVMCONCLR = NVMCON_WREN;

    // check success
    nvmop = NVMCON;
    if (nvmop & (1<<12))
    {
        BootUARTWriteByte('L'); // low voltage detect error bit

    }
    if (nvmop & (1<<13))
    {
        BootUARTWriteByte('W'); // write error bit
    }

    if (nvmop & 0x3000)
    { // must clear error bit
        BootUARTWriteByte('C'); // write noise
        if (!BootNVMemOperation(bs,NVM_OP_CLEAR_ERROR))
        {
            // massive error? infinite loop?
            while (1)
            {
                BootUARTWriteByte('@'); // write noise
            }
        }
    }

    return (nvmop&0x3000)?false:true;
#endif
}

// write 32 bit value to given physical address
// address must be word aligned else fails
// return true on success, else false
BOOT_CODE static bool BootNVMemWriteWord(Boot_t * bs, uint32_t physicalDestinationAddress, uint32_t data)
{
    if (physicalDestinationAddress&(3U))
    {
        BootUARTWriteByte('a');
        return false; // not aligned
    }
    NVMADDR = physicalDestinationAddress;
    NVMDATA = data;
    return BootNVMemOperation(bs,NVM_OP_WRITE_WORD);
}

// write 32 bit value to given physical address
// address must be row aligned else fails
// the Boot Struct must have the proper page size set
// return true on success, else false
BOOT_CODE static bool BootNVMemWriteRow(Boot_t * bs, uint32_t physicalDestinationAddress,  const uint32_t * physicalData)
{
    if (physicalDestinationAddress & (FLASH_ROW_SIZE-1))
    {
        BootUARTWriteByte('a');
        return false;
    }

    // must be word aligned
    if (((uint32_t)physicalData) & (3U))
    {
        BootUARTWriteByte('b');
        return false;
    }

    NVMADDR = physicalDestinationAddress;
    NVMSRCADDR = (uint32_t)physicalData;
    return BootNVMemOperation(bs,NVM_OP_WRITE_ROW);
}

// erase page
// address must be page aligned else fails
// the Boot Struct must have the proper page size set
// return true on success, else false
BOOT_CODE static bool BootNVMemErasePage(Boot_t * bs, uint32_t physicalDestinationAddress)
{
    if (physicalDestinationAddress & (FLASH_PAGE_SIZE-1))
        return false;
    NVMADDR = physicalDestinationAddress;
    return BootNVMemOperation(bs,NVM_OP_ERASE_PAGE);
}

/*************************** Bootloader logic *********************************/

// instructions needed to jump into the bootloader. Checked below
BOOT_DATA static const uint32_t const bootloaderShim[BOOT_INSTRUCTION_COUNT] =
{0x3C1DA000, 0x37BD2000, 0x3C089D00, 0x25080000, 0x0100F809, 0x00000000};

// Check the address for bootloader code that jumps into the
// BootloaderEntry. This is used before overwriting the BOOT FLASH
// to ensure it looks like the bootloader will be called
BOOT_CODE bool BootDetectBootloaderShim(const uint32_t * address)
{
    /*
     * In the BOOT_FLASH, within a few instructions the following code sequence
     * must appear in order to jump back into the boot loader. The bootloader
     * will not erase and write this page without this code present.
     *
     *  la      sp,0xA0000000 + 8*1024 # all PIC32s have at least 16K RAM, use 8
     *  la      t0,BootloaderEntry     # boot address, always same location
     *  jalr    t0                     # jump and link so we can return here
     *  nop                            # required branch delay slot, executed
     *
     * To see what this code is in memory, assemble it in the crt0.S, and call
     *     BootPrintMemory("Boot flash entry bytes: ", 0xBFC00000,32);
     *
     * Boot flash entry bytes:
     * 0xBFC00000 : 00 A0 1D 3C 00 20 BD 37
     * 0xBFC00008 : 00 9D 08 3C 00 00 08 25
     * 0xBFC00010 : 09 F8 00 01 00 00 00 00
     * 0xBFC00018 : 00 60 1A 40 C0 04 5A 7F
     *
     * This decodes to
     *         3C1DA000        lui     sp,0xA000
     *         37BD2000        addiu   sp,0x2000
     *         3C089D00        lui     t0,0x9D00
     *         25080000        addiu   t0,0x0000
     *         0100F809        jalr    t0
     *         00000000        nop
     *
     * as can be deduced by disassembling the object file with
     *
     *  "\Program Files (x86)\Microchip\xc32\v1.30\bin\xc32-objdump.exe" -d
     *     BootLoader_crt0.o | more
     *
     * So we look for the sequence
     *    0x3C1DA000, 0x27BD2000, 0x3C089D00, 0x25080000, 0x0100F809, 0x00000000
     * 
     * */

    int i,j; 
    for (i = 0; i < BOOT_INSTRUCTION_SEEK - BOOT_INSTRUCTION_COUNT; ++i)
    {       
        bool found = true;
        for (j = 0; j < BOOT_INSTRUCTION_COUNT; ++j)
        {
            if (address[i+j] != bootloaderShim[j])
                found = false;       
        }
        if (found)
        {
            i=j=0;
            return true;
        }
    }

    i=j=0;
    return false; // did not match
}

// check assumptions needed for proper function
// return true on success, else false
BOOT_CODE bool BootTestAssumptions()
{
    /* TODO
     * Things to check
     *  1. flash writeable
     *  2. program entry is right address
     *  3. check where strings are stored (if any) - must be in proper FLASH place
     *
     */

    // check entry address
    if ((uint32_t)(&BootloaderEntry) != BOOT_ENTRY_ADDRESS)
        return false;

    // check string addresses working
    // BootDebugPrintE("TODO - test string addresses in BootTestAssumptions")
    //if ((uint32_t)BOOTLOADER_VERSION < BOOT_ENTRY_ADDRESS || (BOOT_ENTRY_ADDRESS+BOOTLOADER_SIZE) < (uint32_t)BOOTLOADER_VERSION)
    //    return false;

    // test bootloaderShim, want in boot code
    if ((uint32_t)bootloaderShim < BOOT_ENTRY_ADDRESS || (BOOT_ENTRY_ADDRESS+BOOTLOADER_SIZE) < (uint32_t)bootloaderShim)
        return false;

    //// check boot code in place in crt0 at the reset vector
    if (!BootDetectBootloaderShim((uint32_t*)BOOT_START_LOGICAL))
        return false;
    
    // check ram address working
    if ((uint32_t)&bootResult != BOOT_RESULT_VIRTUAL_ADDRESS)
        return false;

    return true;
}

BOOTSTRING(infoText01,"Testing info strings");

BOOT_CODE static void BootCommandInfo(Boot_t * bs)
{
    BootUARTWriteByte('T');
    BootPrintSerial(infoText01);


#ifdef DEBUG_BOOTLOADER
#define DUMPHEX(txt,val) BootPrintSerial(txt); \
    BootPrintSerialHex(val); \
    ENDLINE();

#define DUMPINT(txt,val) BootPrintSerial(txt); \
    BootPrintSerialInt(val); \
    ENDLINE();
#else
#define DUMPHEX(txt,val) \
    BootPrintSerialHex(val); 

#define DUMPINT(txt,val) \
    BootPrintSerialInt(val); 
#endif

    // BootPrintSerialHex((uint32_t)BOOTLOADER_VERSION);

    BootUARTWriteByte('1');

    // chip and code versions
    BootDebugPrint("Bootloader Version    : ");
    
    BootUARTWriteByte('2');

    BootUARTWriteByte('0'+BOOTLOADER_VERSION_MAJOR);

    BootUARTWriteByte('3');

    BootUARTWriteByte('.');

    BootUARTWriteByte('3');

    BootUARTWriteByte('0'+BOOTLOADER_VERSION_MINOR);

    ENDLINE();

    DUMPHEX("DEVID                 : ", DEVIDbits.DEVID);
    DUMPHEX("DEVID Ver             : ", DEVIDbits.VER);

    BootUARTWriteByte('V');


#ifdef DEBUG_BOOTLOADER

    BootPrintMemory("Boot flash entry bytes: ", 0xBFC00000,32);



    int seen = BootDetectBootloaderShim((uint32_t*)BOOT_START_LOGICAL);
    DUMPHEX("Boot shim detected    : ", seen);

    // memory
    DUMPHEX("RAM size              : ", RAM_END - RAM_START);
    DUMPHEX("FLASH size            : ", FLASH_END - FLASH_START);
    DUMPHEX("Peripheral size       : ", PERIPHERAL_END - PERIPHERAL_START);
    DUMPHEX("BOOT size             : ", BOOT_END - BOOT_START);
    DUMPHEX("Configuration size    : ", CONFIGURATION_END - CONFIGURATION_START);
    DUMPINT("BOOT struct size      : ", sizeof(Boot_t));

    // addresses
    DUMPHEX("Bootloader address    : ",(uint32_t)(&BootloaderEntry));
    // DUMPHEX("String address        : ",(uint32_t)BOOTLOADER_VERSION);
    DUMPHEX("Boot result address   : ",(uint32_t)&bootResult);

    // some memory dump to help debugging
    // BootPrintMemory("Mem at 0x9D00 2000: ", 0x9D002000, 8*16);

#endif
    

#undef DUMPHEX // clean up
#undef DUMPINT

    // final ack to denote finised
    BootUARTWriteByte(ACK);
    
} // BootCommandInfo

// return true if can modify this address range without hurting bootloader
// or configuration bits. 
BOOT_CODE static bool BootModifyAddressesAllowed(uint32_t address, uint32_t length)
{
    // protect the bootloader code itself
    if (BootOverlap(
            address, address+length,
            BOOT_BASE_ADDRESS, BOOT_BASE_ADDRESS+BOOTLOADER_SIZE) != 0
            )
    {
        BootUARTWriteByte('B');
        return false; // would overwrite boot code
    }

#ifndef ALLOW_BOOTFLASH_OVERWRITE
    // protect the boot flash section
    if (BootOverlap(
            address, address+length,
            BOOT_START, BOOT_END) != 0
            )
    {
        BootUARTWriteByte('S');
        return false; // would overwrite boot flash
    }
#endif
    // protect the config flash entire PAGE
    // since we disallow page erasing, disallow page writing, else corruption
    if (BootOverlap(
            address, address+length,
            // round down to page
            (CONFIGURATION_START) & (~(FLASH_PAGE_SIZE-1)),
            // round up to page
            (CONFIGURATION_END+FLASH_PAGE_SIZE-1) & (~(FLASH_PAGE_SIZE-1)) 
            ) != 0
            )
    {
        BootUARTWriteByte('C');
        return false; // would overwrite configuration bits
    }

    return true;
}


// erase the range of pages stored in
// boot struct writeAddress of writeSize length
// skips overwrites of bootloader protected regions
BOOT_CODE static void BootEraseHelper(Boot_t * bs)
{
    for (bs->curAddress = bs->writeAddress; bs->curAddress < bs->writeAddress + bs->writeSize; bs->curAddress += FLASH_PAGE_SIZE)
    {
#ifdef DEBUG_BOOTLOADER
        BootDebugPrint("Erase address: ");
        BootPrintSerialHex(bs->curAddress);

        BootDebugPrint(" (");
        BootPrintSerialInt((bs->curAddress - FLASH_START)/FLASH_PAGE_SIZE);
        BootDebugPrintE(")");
#else
#endif

        BootPrintSerialHex(bs->curAddress);

        // do not erase bootloader addresses - just skip them
        if (BootModifyAddressesAllowed(bs->curAddress,FLASH_PAGE_SIZE))
        {

            // special case. Even if we're allowed to erase the BOOT_START page,
            // we do not do so here. We only do it when it is about to be
            // written, in order to protect the bootloader path
            if (bs->curAddress != BOOT_START && BootNVMemErasePage(bs,bs->curAddress))
            { // success
                // allows progress bar
                BootUARTWriteByte(ACK);
            }
            else
            { // erase failed
                bs->pageEraseFailureCount++;
                // allows progress bar
                NACK(NACK_ERASE_FAILED);
            }
        }
        else
        { // write blocked
            // allows progress bar
            NACK(NACK_ERASE_OUT_OF_BOUNDS);
        }

        bs->pageEraseAttemptCount++;
        LED_TOGGLE();

        //BootPrintSerial('\r');
        //BootPrintSerial('\n');
    }

#ifdef DEBUG_BOOTLOADER
        BootPrintSerialInt(bs->pageEraseFailureCount);
        BootDebugPrint(" pages failed out of ");
        BootPrintSerialInt(bs->pageEraseAttemptCount);
        BootDebugPrintE(" total.");
#endif

} // BootCommandErase

// erase FLASH
BOOT_CODE static void BootCommandErase(Boot_t * bs)
{
    BootDebugPrintE("Erasing flash....");

#ifdef DEBUG_BOOTLOADER
    BootDebugPrint("Page size: ");
    BootPrintSerialHex(FLASH_PAGE_SIZE);
    ENDLINE();
    BootDebugPrint("Page start: ");
    BootPrintSerialHex(FLASH_START);
    ENDLINE();
    BootDebugPrint("Page end: ");
    BootPrintSerialHex(FLASH_END);
    ENDLINE();
#endif

    bs->pageEraseAttemptCount = 0; // track for stats
    bs->pageEraseFailureCount = 0; // count failures


    // erase normal flash
    bs->writeAddress = FLASH_START;
    bs->writeSize    = FLASH_END - FLASH_START;
    BootEraseHelper(bs);

    // erase boot flash
    // thse addressed are protected if necessary in a deeper function
    // to keep a consistent count of pages tried for progress bars
    bs->writeAddress = BOOT_START;
    bs->writeSize    = BOOT_END - BOOT_START;
    BootEraseHelper(bs);


    // needed done before writing is allowed
    // todo - if ever set up so no errors on a correct erase, then
    // make this only set to true during success
    bs->flashErased = true;

    // one final ack or nack based on success
    if (bs->pageEraseFailureCount != 0)
        NACK(NACK_ERASE_FAILED); // send an error reply
    else
    {
        BootUARTWriteByte(ACK);
    }
}


// write the data in bs fields: buffer, writeSize, writeAddress
// return NACK_ACK_RESERVED on success, else a NACK_ code for the error
BOOT_CODE static uint32_t BootWriteFlash(Boot_t * bs)
{
    // check writing is allowed (required an erase first)
    if (bs->flashErased == false)
    {
        // failed, since erase not done
        BootDebugPrintE("Write requires erase first");
        return NACK_WRITE_WITHOUT_ERASE;
    }

    // check length (must be page length? or multiple?)
    // only allow at most one page for now
    if (FLASH_PAGE_SIZE < bs->writeSize || bs->writeSize <= 0 || (bs->writeSize&3)!=0)
    {
        // failed, too large write
        BootDebugPrintE("Write too large or zero or not multiple of 4");
        return NACK_WRITE_SIZE_ERROR;
    }

    // check address is 4 byte aligned
    if ((bs->writeAddress & 3) != 0)
    {
        BootDebugPrintE("Write not aligned");
        return NACK_WRITE_MISALIGNED_ERROR;
    }

    // check no wraparound (note size > 0 here)
    if (bs->writeAddress > 0xFFFFFFFF - (bs->writeSize-1))
    {
        BootDebugPrintE("Write wraps around address space");
        return NACK_WRITE_WRAPS_ERROR;
    }

    // check address (in range, proper boundary)
    if (!BootModifyAddressesAllowed(bs->writeAddress, bs->writeSize))
    {
        // failed, out of flash range
        BootDebugPrintE("Write outside flash range");
        return NACK_WRITE_OUT_OF_BOUNDS;
    }

    // special BOOT_START page handling
    // if the address is the start of the boot page, and the write contains
    // code to jump into the bootloader, erase the page.
    if (bs->writeAddress == BOOT_START)
    {
        // this is a special case. If we got here, and we are about to overwrite 
        // the boot page, ensure what we are writing contains the proper boot
        // code, is long enough. If all is ok, then erase the page first

        if (bs->writeSize < BOOT_INSTRUCTION_SEEK*4)
        {
            // failed, we require the boot sector to be aligned and at least long enough
            BootDebugPrintE("Write boot requires enough size");
            return NACK_WRITE_OUT_OF_BOUNDS;
        }

        // Check the address for bootloader code that jumps into the
        // BootloaderEntry. This is used before overwriting the BOOT FLASH
        // to ensure it looks like the bootloader will be called
        if (!BootDetectBootloaderShim((uint32_t*)&(bs->buffer)))
        {
            // failed, does not contain the bootloader header needed
            BootDebugPrintE("Bootloader shim missing");
            return NACK_WRITE_BOOT_MISSING;
        }

        if (!BootNVMemErasePage(bs,bs->writeAddress))
        { // failure to erase page
            // failed, erase no good
            BootDebugPrintE("Bootloader erase failed!");
            return NACK_ERASE_FAILED;
        }
    }



    // do not overwrite cfg - requires special writing mode - can do if careful
    // currently redundant, but left in for future error catching
    if (
        BootOverlap(bs->writeAddress, bs->writeAddress + bs->writeSize,
        CONFIGURATION_START, CONFIGURATION_END) > 0)
    {
        // failed, overwriting configuration bits
        BootDebugPrintE("Cannot write over configuration");
        return NACK_WRITE_OVER_CONFIGURATION;
    }

    // write FLASH loop

    // address to write
    bs->curAddress = bs->writeAddress;
    bs->writeFailureCount = 0;
    while (bs->curAddress < bs->writeAddress + bs->writeSize)
    { // write largest chunk possible        
        if (((bs->curAddress & (FLASH_ROW_SIZE-1))==0) && (FLASH_ROW_SIZE <= bs->writeAddress + bs->writeSize - bs->curAddress))
        { // row aligned and long enough
            BootUARTWriteByte('R');

            if (BootNVMemWriteRow(bs,
                bs->curAddress,
                (uint32_t*)(LOGICAL_TO_PHYSICAL_ADDRESS(((uint32_t)(bs->buffer + (bs->curAddress - bs->writeAddress)))))
                ) == false)
            {
                BootUARTWriteByte('-');
                bs->writeFailureCount++;
            }
            bs->curAddress  += FLASH_ROW_SIZE;
        }
        else
        { // long enough for word write
            BootUARTWriteByte('W');
            if (BootNVMemWriteWord(bs,bs->curAddress,
                    *((uint32_t*)(bs->buffer + bs->curAddress - bs->writeAddress))
                    ) == false)
            {
                BootUARTWriteByte('-');
                bs->writeFailureCount++;
            }
            bs->curAddress  += 4;
        }
    }

    if (bs->writeFailureCount != 0)
    {
        BootDebugPrintE("Writes failed");
        return NACK_WRITES_FAILED;
    }

    return NACK_ACK_RESERVED; // success
} // BootWriteFlash

/*
 * Each write block has the following format
 * byte  0           : 'W' (0x57) the write command.
 * bytes 1-2         : big endian 16-bit unsigned payload length P
 *                     in 0-65535. 
 * bytes 3-(P+2)     : P bytes of payload
 *
 * The payload is encrypted or not depending on bootloader configuration.
 * If encrypted, the payload (including CRC!) is decrypted first. 
 * 
 * If encrypted, the first write block contains a 16-bit unsigned big endian
 * length P = 12, then an 8 byte initialization vector (IV) to be used to
 * initialize the crypto, followed by a 32-bit CRC value over the 8 byte IV.
 *
 * After the possible first encrypted block, all blocks contain data to be
 * written to flash. Such a data block is stored in the length P payload,
 * with data bytes first, then a 32 bit big endian unsigned 32-bit address
 * where the data goes, then a big endian 16-bit length of data to write
 * (allowing padding the payload to assist encryption by hiding the actual
 * data size), followed by a 32-bit CRC value over the data, (optional) padding,
 * address, and length fields.
 *
 * If encrypted, the entire payload needs decrypted before reading the values.
 *
 * Thus the block looks like (offsets from the start of the payload, add 3 for
 * offsets from the block start):
 *
 * bytes 0-(P-11)     : P-11 bytes of data.
 * bytes (P-10)-(P-7) : big endian unsigned 32-bit start address A.
 * bytes (P-6)-(P-5)  : big endian 16-bit actual length L to write.
 * bytes (P-4)-(P-1)  : 4 byte CRC32K of unencrypted data and address and length
 * A packet with payload length 0 (no address, no CRC, nothing) marks the end of
 * the packets.
 * */

// write incoming flash packet
BOOT_CODE static void BootCommandWrite(Boot_t * bs)
{
    // on entry, the 'W' command byte is already read...

    // get two length bytes
    bs->readPos = 0;
    while (bs->readPos < 2)
    {
        if (BootUARTReadByte(&bs->buffer[bs->readPos]))
            bs->readPos++;
    }

    // compute length of payload and reset the counter
    bs->readMax = 256*bs->buffer[0] + bs->buffer[1];
    bs->readPos = 0;  // start back at buffer start

    // check size
    if (bs->readMax >= BUFFER_SIZE)
    {
        BootDebugPrintE("Packet length larger than buffer");
        NACK(NACK_PACKET_SIZE_TOO_LARGE);
        return;
    }

    // see if was last packet
    if (bs->readMax == 0)
    {
        BootDebugPrintE("Last packet seen");
        bs->writesFinished = true;
        // final ACK
        BootUARTWriteByte(ACK);
        return;
    }

    // read rest of packet
    while (bs->readPos < bs->readMax)
    {
        if (BootUARTReadByte(&(bs->buffer[bs->readPos])))
            bs->readPos++;
    }

    // increment packets received
    bs->packetCounter++;


    // payload now in buffer 0-(P-1)
#ifdef DEBUG_BOOTLOADER
    BootDebugPrint("Write packet length ");
    BootPrintSerialHex(bs->readMax);
    ENDLINE();
#endif

#ifdef USE_CRYPTO
    // decrypt if needed before testing checksums
    if (bs->packetCounter != 1)
    { // was not the crypto IV packet, so decrypt

        BootDebugPrintE("Decrypting packet");

        //BootPrintMemory("Encrypted bytes: ", bs->buffer, 16);
        //BootPrintMemory("Encryptor state ", bs->crypto.state, 64);

        // note encrypt and decrypt are the same function
        BootCryptoDecrypt(
            &(bs->crypto),
            bs->buffer,  // the output message
            bs->readMax, // message length
            bs->buffer,  // the cipher bytes
            CRYPTO_ROUNDS);

        //BootPrintMemory("Decrypted bytes: ", bs->buffer, 16);
    }

#endif

    // verify packet checksum
    bs->computedCrc = 0;
    for (bs->readPos = 0; bs->readPos <= bs->readMax-4-1; bs->readPos++)
    {
#if 0 // DEBUG_MESSAGES
        // show initial and final bytes, for debugging
        int i = bs->readPos;
        if (i < 5 || i > bs->readMax-4-1 - 5)
        {
        BootDebugPrint("Check byte ");
        BootPrintSerialHex(i);
        BootDebugPrint(" -> ");
        BootPrintSerialHex(bs->buffer[bs->tempInt1]);
        ENDLINE();
        }
#endif
        bs->computedCrc = BootCrc32AddByteBitwise(bs->buffer[bs->readPos], bs->computedCrc);
    }

#ifdef DEBUG_BOOTLOADER
    BootDebugPrint("Computed checksum     : ");
    BootPrintSerialHex(bs->computedCrc);
    ENDLINE();
#endif

    // read transmitted CRC
    BootReadBigEndian(&(bs->transmittedCrc),bs->buffer+bs->readMax-4,4);

#ifdef DEBUG_BOOTLOADER
    BootDebugPrint("Transmitted checksum  : ");
    BootPrintSerialHex(bs->transmittedCrc);
    ENDLINE();
#endif

    if (bs->computedCrc != bs->transmittedCrc)
    { // crc mismatch
        BootDebugPrintE("CRC mismatch");
        NACK(NACK_CRC_MISMATCH);
        return;
    }

#ifdef USE_CRYPTO
    if (bs->packetCounter == 1)
    { // packet contains 8 byte decryption initialization vector (IV)

        BootDebugPrintE("Crypto info packet read");

        // write password into buffer
        // stored as key, word 0 first (lowest address), each stored
        // big endian into a byte array as the key

#define WRITEKEY(ptr,val) (ptr)[3] = (uint8_t)val; \
        (ptr)[2] = (uint8_t)((val)>>8);            \
        (ptr)[1] = (uint8_t)((val)>>16);           \
        (ptr)[0] = (uint8_t)((val)>>24)

        WRITEKEY(bs->buffer+ 8, PASSWORD_WORD0);
        WRITEKEY(bs->buffer+12, PASSWORD_WORD1);
        WRITEKEY(bs->buffer+16, PASSWORD_WORD2);
        WRITEKEY(bs->buffer+20, PASSWORD_WORD3);
        WRITEKEY(bs->buffer+24, PASSWORD_WORD4);
        WRITEKEY(bs->buffer+28, PASSWORD_WORD5);
        WRITEKEY(bs->buffer+32, PASSWORD_WORD6);
        WRITEKEY(bs->buffer+36, PASSWORD_WORD7);
#undef WRITEKEY // clean up to avoid errors
        
        // Set the 32 byte key
        // Set the initialization vector, 64 bits
        BootCryptoSetKeyAndInitializationVector(
            &(bs->crypto),
            bs->buffer + 8, // 32 byte key (from internal values)
            32*8,           // key length in bits
            bs->buffer      // 8 byte IV
        );

        // ack success
        BootUARTWriteByte(ACK);
        return;
    }
#endif


    // get write size and address
    BootReadBigEndian(&(bs->writeSize),bs->buffer+bs->readMax-4-2,2);
    BootReadBigEndian(&(bs->writeAddress),bs->buffer+bs->readMax-4-6,4);

//#ifdef DEBUG_BOOTLOADER
    BootDebugPrint("Writing ");
    BootPrintSerialHex(bs->writeSize);
    BootDebugPrint(" to address ");
    BootPrintSerialHex(bs->writeAddress);
    ENDLINE();
//#endif

    // loop on writing a few times in case first pass fails
    bs->writeRetryCounter = 0;

    do 
    {
        bs->flashWriteResult = BootWriteFlash(bs);
        if (bs->flashWriteResult != NACK_ACK_RESERVED)
        {
            BootDebugPrintE("Flash write failed ");
            // here is the reason
            NACK(bs->flashWriteResult);
            if (bs->writeRetryCounter > WRITE_RETY_MAX)
            {
                return; // had enough, so bail
            }
        }

        // check what was written
        BootDebugPrintE("Comparing flash to buffer....");
        for (bs->readPos = 0; bs->readPos < bs->writeSize; bs->readPos++)
        {
            bs->curAddress = bs->writeAddress+bs->readPos;
            bs->curAddress |= 0x80000000; // physical to logical
            if (bs->buffer[bs->readPos] != (*((uint8_t*)(bs->curAddress))))
            {
                bs->flashWriteResult = NACK_COMPARE_FAILED;
    #ifdef DEBUG_BOOTLOADER
                BootDebugPrint("Compare flash to buffer failed. Address ");
                BootPrintSerialHex(bs->curAddress);
                BootDebugPrint(", ");
                BootPrintSerialInt((uint32_t)bs->buffer[bs->readPos]);
                BootDebugPrint(" != ");
                BootPrintSerialInt((uint32_t) (*((uint8_t*)(bs->curAddress))) );
                ENDLINE();
    #endif
                NACK(NACK_COMPARE_FAILED);
                if (bs->writeRetryCounter > WRITE_RETY_MAX)
                {
                    return; // had enough, so bail
                }

            }
        }
        bs->writeRetryCounter++;
    } while (bs->writeRetryCounter < WRITE_RETY_MAX &&
             bs->flashWriteResult != NACK_ACK_RESERVED);

    // final ACK
    BootUARTWriteByte(ACK);
}

// compute and output CRC32 for all flash
BOOT_CODE static void BootCommandCRC(Boot_t * bs)
{
    bs->computedCrc = 0;

    // main flash
    for (bs->curAddress = FLASH_START_LOGICAL;
            bs->curAddress < FLASH_START_LOGICAL+FLASH_SIZE; ++ bs->curAddress)
    {
        bs->computedCrc = BootCrc32AddByteBitwise(*((uint8_t*)bs->curAddress), bs->computedCrc);
    }

    // BOOT flash
    for (bs->curAddress = BOOT_START_LOGICAL;
            bs->curAddress < BOOT_START_LOGICAL+BOOT_SIZE; ++ bs->curAddress)
    {
        bs->computedCrc = BootCrc32AddByteBitwise(*((uint8_t*)bs->curAddress), bs->computedCrc);
    }

    BootPrintSerialHex(bs->computedCrc);

    // final ACK
    BootUARTWriteByte(ACK);

}


/*
 * Command Loop - this is a loop, in client/server mode, Flasher sends command,
 *    Device responds.
 *
 * Commands: have form [command byte]([address 32 bits][length][data])[CRC]
 *    'I' (0x49) = Identify. No data. Respond ? bytes info
 *        Useful fields: bootloader version, product version, PIC version?
 *        Responds with DATA CRC
 *    'E' (0x45) = Erase. Send 'E' Address CRC, responds ACK CRC
 *    'W' (0x57) = Write. Send 'W' Address Length CRC, returns ACK CRC or NACK CRC
 *    'Q' (0x51) = Quit. Send 'Q'CRC. Return ACK then Device exits boot loader.
 */

BOOT_CODE static void BootRunCommandLoop(Boot_t * bs)
{
    // set the packet counter back to zero
    bs->packetCounter = 0;
    bs->writesFinished = false;
    
    while (1)
    {
        // get command on timeout
        while (!BootUARTReadByte(bs->buffer))
        {
            // do nothing
        }

        BootUARTWriteByte('Z');

        switch (bs->buffer[0])
        {
            case 'I' : // information
                // BootDebugPrintE("Information command");
                BootCommandInfo(bs);
                break;
            case 'E' : // erase
                //BootDebugPrintE("Erase command");
                BootCommandErase(bs);
                bs->packetCounter = 0;
                bs->writesFinished = false;

                BootUARTWriteByte('X');
                
                break;
            case 'C' : // CRC everything
                BootCommandCRC(bs);
                break;
            case 'W' : // write
                //BootDebugPrintE("Write command");
                BootCommandWrite(bs);
                break;
            case 'Q' : // quit
                //BootDebugPrintE("Quit command");
                return;
            case ACK :// ACK - late entry? if so, ACK back to resync
                BootUARTWriteByte(ACK);
                break;
            default :
#ifdef DEBUG_BOOTLOADER
                BootDebugPrint("DEVICE: Unknown command ");
                BootPrintSerialInt((int)(bs->buffer[0]));
                ENDLINE();
#endif
                NACK(NACK_UNKNOWN_COMMAND);
                break;
        }
    }
}

// see if a flash attempt is occurring
// return true if it is, else false if none detected or timeout happens
BOOT_CODE static bool BootDetectFlashingAttempt(Boot_t * bs)
{
    BootStartTimer(&(bs->timeoutTimerMs), TICKS_PER_MILLISECOND);

    while (BootUpdateTimer(&(bs->timeoutTimerMs)) < BOOT_WAIT_MS)
    {
        if (BootUARTReadByte(bs->buffer) && bs->buffer[0] == ACK)
        {
            BootUARTWriteByte(ACK);
            return true;
        }
    }

#ifdef DEBUG_BOOTLOADER
    ENDLINE();
    BootDebugPrint("Flash attempt timeout ");
    BootPrintSerialInt(BootUpdateTimer(&(bs->timeoutTimerMs)));
    BootDebugPrintE(".");
#endif

    return false;
}


// setup any hardware for outside communication
// return true on success
BOOT_CODE static bool BootSetHardware()
{
    BootUARTInit();
    LED_INIT();
    return  true;
}

BOOT_CODE static void BootSoftReset()
{
    /* The following code illustrates a software Reset */
    // assume interrupts are disabled
    // assume the DMA controller is suspended
    // assume the device is locked
    /* perform a system unlock sequence */
    // starting critical sequence
    SYSKEY = 0x00000000; // write invalid key to force lock
    SYSKEY = 0xAA996655; // write key1 to SYSKEY
    SYSKEY = 0x556699AA; // write key2 to SYSKEY
    // OSCCON is now unlocked
    /* set SWRST bit to arm reset */
    RSWRSTSET = 1;
    /* read RSWRST register to trigger reset */
    unsigned int dummy;
    dummy = RSWRST;

    /* prevent any unwanted code execution until reset occurs*/
    while(1);
}

// run the bootloader
BOOT_ENTRY FIX_ADDRESS(BOOT_ENTRY_ADDRESS) uint8_t BootloaderEntry()
{
    // assume power check succeeds, set reason
    bootResult = BOOT_RESULT_POWER_EXIT;
    
    // if not power on reset, jump to user code (old boot code from application)
    if ((RCON & 0x0003) == 0)
        return; // jump to app code

    // assume power check succeeds, set reason
    bootResult = BOOT_RESULT_ASSUMPTIONS_FAILED;

    // check assumptions about memory, etc., are met
    if (BootTestAssumptions() == false)
        return; // jump to app code

    // set current return code
    bootResult = BOOT_RESULT_STARTED;

    // start timing events
    BootWriteTimer(0);

    // keep all memory on stack to prevent linker from
    // reserving RAM. There is one global variable for the
    // application to read. It also has the effect of
    // keeping the stack fairly clean

    // we need the buffer in the boot structure to be word aligned,
    // so we allocate extra space (enough in two directions so we don't have
    // to worry about stack directions)
    uint8_t space[sizeof(Boot_t)+10];

    Boot_t * bs = (Boot_t*)(space+5);

    while (((uint32_t)(bs->buffer)) & (3))
    {
        // move one byte ahead
        bs = (Boot_t*)(((uint8_t*)bs)+1);
    }

    // 1. init listening hardware
    if (BootSetHardware())
    {
        BootDebugPrintE("Hardware set");
        BootDebugPrintE("TODO - check flash writeable!");
        
        // show LED if present
        LED_ON();

        // needed done before writing is allowed
        bs->flashErased = false;

        // 2. See if flashing attempted
        if (BootDetectFlashingAttempt(bs))
        {
            // print time connected, useful for debugging
            BootDebugPrint("Flasher detected at ");
            BootPrintSerialInt(BootReadTimer()/TICKS_PER_MILLISECOND);
            BootUARTWriteByte('m'); // detect time in ms
            BootUARTWriteByte('s');
            ENDLINE();

            // 3. If flashing, do it
            BootRunCommandLoop(bs);

            BootDebugPrintE("Command loop exited");
        }
    }
    else
        bootResult = BOOT_SET_HARDWARE_FAILED;

    // 4. zero used memory to avoid leaks. Still leaks return addresses and some stack stuff
    // count down to end on zero, which is more likely then to be the value left on the stack
    int i;
    for (i = sizeof(Boot_t); i > 0; i--)
        ((uint8_t*)(bs))[i-1] = 0;

    // todo - some options
    // infinite loop till power cycle if flashed
    // clear all ram to prevent leakage?
    // softreset device?

    BootDebugPrintE("Leaving bootloader code....");

    // reset device
    //BootDebugPrint("and resetting device...");
    //BootSoftReset();

    return bootResult;
}

// end of file

