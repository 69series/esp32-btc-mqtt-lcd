#include "lcd.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

// pin config
#define RS 23
#define E 22
#define D4 21
#define D5 19 
#define D6 18
#define D7 5

// macros config
#define set(pin, level) gpio_set_level(pin, level)
#define fz_us(us) esp_rom_delay_us(us)
#define iomask ((1ULL<<RS)|(1ULL<<E)|(1ULL<<D4)|(1ULL<<D5)|(1ULL<<D6)|(1ULL<<D7))

// private-api
static void send_nibble(uint8_t dtx)
{
    set(D4, (dtx >> 0) & 1 );
    set(D5, (dtx >> 1) & 1 );
    set(D6, (dtx >> 2) & 1 );
    set(D7, (dtx >> 3) & 1 );
    set(E, 1);
    fz_us(2);
    set(E, 0);
    fz_us(50);
}
    
static void lcd_send(uint8_t val, uint8_t rs_mode)
{
    set(RS, rs_mode);
    set(D4, (val >> 4) & 1 ); //upper_nibble
    set(D5, (val >> 5) & 1 );
    set(D6, (val >> 6) & 1 );
    set(D7, (val >> 7) & 1 );
    set(E, 1); //first latch
    fz_us(2); // E high min 450ns per datasheet
    set(E, 0);
    fz_us(50); // command settle time

    set(D4, (val >> 0) & 1 ); //lower_nibble
    set(D5, (val >> 1) & 1 );
    set(D6, (val >> 2) & 1 );
    set(D7, (val >> 3) & 1 );
    set(E, 1); //second latch
    fz_us(2); // E high min 450ns per datasheet
    set(E, 0);
    fz_us(50); // command settle time
}


// public-api
void lcd_init(void)
{
    //io-config
    gpio_config_t io ={
        .pin_bit_mask   = iomask,
        .mode           = GPIO_MODE_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    //HD44780 init
    fz_us(50000);

    set(RS, 0);
    set(E, 0);

    // handshake to config to 4-bit mode
    send_nibble(0x03); fz_us(4500);
    send_nibble(0x03); fz_us(150);
    send_nibble(0x03); fz_us(150);
    send_nibble(0x02); // 4-bit mode ON

    lcd_send(0x28, 0); // function set: 4-bit, 2 lines
    lcd_send(0x0C, 0); // display on, cursor off, blink off
    lcd_send(0x06, 0); // entry mode: increment cursor, no display shift
    lcd_send(0x01, 0); // clear display
    fz_us(2000);          // clear needs >1.5ms
}

void lcd_clear(void)
{
    lcd_send(0x01, 0); // clear display
    fz_us(2000);          // clear needs >1.5ms
}

void lcd_set_cursor(uint8_t col, uint8_t row)
{
    if (row > 1) row = 1;
    if (col > 15) col = 15;
    //r0: 0x00-0x0F  r1: 0x40-0x4F
    uint8_t row_offset[] = {0x00, 0x40};
    lcd_send(0x80 | (row_offset[row] + col), 0);
}

void lcd_print_char(char c)
{
    lcd_send((uint8_t)c, 1);    // RS=1 means data
}

void lcd_print(const char *str)
{
    while(*str)
    {
        lcd_print_char(*str++);
    }
}