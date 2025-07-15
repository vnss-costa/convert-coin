#include <msp430.h>
#include <stdint.h>
#include <stdio.h>

#define LCD_ADDR 0x27
#define LCD_RS  BIT0
#define LCD_RW  BIT1
#define LCD_EN  BIT2
#define LCD_BL  BIT3

#define RATE_USD_CENT 20
#define RATE_EUR_CENT 18
#define RATE_BTC_SAT 50

uint8_t i2cSend(uint8_t addr, uint8_t data) {
    while (UCB0CTL1 & UCTXSTP);
    UCB0I2CSA = addr;
    UCB0CTL1 |= UCTR | UCTXSTT;
    while (!(UCB0IFG & UCTXIFG));
    UCB0TXBUF = data;
    while (!(UCB0IFG & UCTXIFG));
    UCB0CTL1 |= UCTXSTP;
    while (UCB0CTL1 & UCTXSTP);
    return 1;
}

void lcdWriteNibble(uint8_t nibble, uint8_t isChar) {
    uint8_t data = (nibble & 0xF0);
    data |= LCD_BL;
    if (isChar) data |= LCD_RS;

    i2cSend(LCD_ADDR, data);
    i2cSend(LCD_ADDR, data | LCD_EN);
    __delay_cycles(2000);
    i2cSend(LCD_ADDR, data);
}

void lcdWriteByte(uint8_t byte, uint8_t isChar) {
    lcdWriteNibble(byte & 0xF0, isChar);
    lcdWriteNibble((byte << 4) & 0xF0, isChar);
}

void lcdInit() {
    __delay_cycles(50000);

    lcdWriteNibble(0x30, 0);
    __delay_cycles(5000);
    lcdWriteNibble(0x30, 0);
    __delay_cycles(5000);
    lcdWriteNibble(0x30, 0);
    __delay_cycles(5000);
    lcdWriteNibble(0x20, 0);

    lcdWriteByte(0x28, 0);
    lcdWriteByte(0x0C, 0);
    lcdWriteByte(0x06, 0);
    lcdWriteByte(0x01, 0);
    __delay_cycles(5000);
}

void lcdWrite(char *str) {
    while (*str) {
        if (*str == '\n') {
            lcdWriteByte(0xC0, 0);
        } else {
            lcdWriteByte(*str, 1);
        }
        str++;
    }
}

void lcdClear() {
    lcdWriteByte(0x01, 0);
    __delay_cycles(5000);
}

void i2cConfig(){
    UCB0CTL1 = UCSWRST;
    UCB0CTL0 = UCMST | UCMODE_3 | UCSYNC;
    UCB0CTL1 |= UCSSEL__SMCLK;
    UCB0BRW = 100;
    P3SEL |= BIT0 | BIT1;
    P3DIR &= ~(BIT0 | BIT1);
    UCB0CTL1 &= ~UCSWRST;
}

void adcConfig() {
    P6SEL |= BIT0 | BIT1;
    ADC12CTL0 = ADC12SHT0_3 | ADC12ON;
    ADC12CTL1 = ADC12SHP;
    ADC12CTL2 = ADC12RES_0;
}

void buttonConfig() {
    P2DIR &= ~BIT5;
    P2REN |= BIT5;
    P2OUT |= BIT5;
}

uint8_t readADC(uint8_t channel) {
    ADC12CTL0 &= ~ADC12ENC;
    ADC12MCTL0 = channel;
    ADC12CTL0 |= ADC12ENC;
    ADC12CTL0 |= ADC12SC;
    while (ADC12CTL1 & ADC12BUSY);
    return ADC12MEM0 & 0xFF;
}

void chooseCoin(int step) {
    switch(step){
        case 1:
            lcdWrite("Dolar\n");
            break;
        case 2:
            lcdWrite("Euro\n");
            break;
        case 3:
            lcdWrite("BitCoin\n");
            break;
    }
}

void calculateConversion(int coin, int amountCents, int* intPart, int* decPart) {
    switch(coin) {
        case 1:
            {
                long totalUSDCents = ((long)amountCents * 20L) / 100L;
                *intPart = (int)(totalUSDCents / 100L);
                *decPart = (int)(totalUSDCents % 100L);
            }
            break;
        case 2:
            {
                long totalEURCents = ((long)amountCents * 18L) / 100L;
                *intPart = (int)(totalEURCents / 100L);
                *decPart = (int)(totalEURCents % 100L);
            }
            break;
        case 3:
            {
                long satoshis = ((long)amountCents * 5L) / 100000L;
                *intPart = (int)satoshis;
                *decPart = 0;
            }
            break;
        default:
            *intPart = amountCents / 100;
            *decPart = amountCents % 100;
    }
}

void showResult(int coin, int amountCents, int intPart, int decPart) {
    char buffer[32];

    lcdClear();

    int brlInt = amountCents / 100;
    int brlDec = amountCents % 100;
    if (brlDec < 10) {
        sprintf(buffer, "BRL %d.0%d = \n", brlInt, brlDec);
    } else {
        sprintf(buffer, "BRL %d.%d = \n", brlInt, brlDec);
    }
    lcdWrite(buffer);

    switch(coin) {
        case 1:
            if (decPart < 10) {
                sprintf(buffer, "USD %d.0%d\n", intPart, decPart);
            } else {
                sprintf(buffer, "USD %d.%d\n", intPart, decPart);
            }
            break;
        case 2:
            if (decPart < 10) {
                sprintf(buffer, "EUR %d.0%d\n", intPart, decPart);
            } else {
                sprintf(buffer, "EUR %d.%d\n", intPart, decPart);
            }
            break;
        case 3:
            sprintf(buffer, "BTC %d sat\n", intPart);
            break;
    }
    lcdWrite(buffer);
}

void main(void) {
    WDTCTL = WDTPW | WDTHOLD;

    i2cConfig();
    adcConfig();
    buttonConfig();
    lcdInit();

    unsigned int coin = 0;
    unsigned int selected = 0;
    unsigned int fase = 0;

    unsigned int lastXDir = 0;
    unsigned int lastYDir = 0;
    unsigned int lastButtonState = 1;

    static uint8_t valor[5] = {0, 0, 0, 0, 0};
    static uint8_t cursorPos = 0;

    int convertedInt = 0;
    int convertedDec = 0;

    lcdWrite("Selecione >>\n");
    lcdWrite("a moeda:  >>\n");

    while (1) {
        unsigned int xValue = readADC(ADC12INCH_0);
        unsigned int yValue = readADC(ADC12INCH_1);

        uint8_t precisaAtualizarLCD = 0;

        if (fase == 0) {
            if (xValue > 110 && xValue < 140) {
                lastXDir = 0;
            } else if (xValue >= 180 && lastXDir != 1) {
                if (coin < 3) coin++;
                lcdClear();
                chooseCoin(coin);
                lastXDir = 1;
                __delay_cycles(200000);
            } else if (xValue <= 50 && lastXDir != 2) {
                if (coin > 1) coin--;
                lcdClear();
                chooseCoin(coin);
                lastXDir = 2;
                __delay_cycles(200000);
            }
        }

        if (fase == 1) {
            if (xValue > 110 && xValue < 140) {
                lastXDir = 0;
            } else if (xValue >= 180 && lastXDir != 1) {
                if (cursorPos < 4) cursorPos++;
                lastXDir = 1;
                precisaAtualizarLCD = 1;
                __delay_cycles(200000);
            } else if (xValue <= 50 && lastXDir != 2) {
                if (cursorPos > 0) cursorPos--;
                lastXDir = 2;
                precisaAtualizarLCD = 1;
                __delay_cycles(200000);
            }

            if (yValue > 110 && yValue < 140) {
                lastYDir = 0;
            } else if (yValue >= 180 && lastYDir != 1) {
                if (valor[cursorPos] > 0) valor[cursorPos]--;
                lastYDir = 1;
                precisaAtualizarLCD = 1;
                __delay_cycles(200000);
            } else if (yValue <= 50 && lastYDir != 2) {
                if (valor[cursorPos] < 9) valor[cursorPos]++;
                lastYDir = 2;
                precisaAtualizarLCD = 1;
                __delay_cycles(200000);
            }

            if (precisaAtualizarLCD) {
                lcdClear();
                char buffer[16];
                sprintf(buffer, "%d%d%d.%d%d BRL", valor[0], valor[1], valor[2], valor[3], valor[4]);
                lcdWrite(buffer);

                char cursorLine[17] = "                ";
                if (cursorPos < 3)
                    cursorLine[cursorPos] = '^';
                else
                    cursorLine[cursorPos + 1] = '^';

                lcdWriteByte(0xC0, 0);
                lcdWrite(cursorLine);

                precisaAtualizarLCD = 0;
            }
        }

        unsigned int buttonState = P2IN & BIT5;
        if (buttonState == 0 && lastButtonState != 0) {
            if (fase == 0) {
                selected = coin;
                fase = 1;
                valor[0] = valor[1] = valor[2] = valor[3] = valor[4] = 0;
                cursorPos = 0;
                lcdClear();
                char buffer[16];
                sprintf(buffer, "%d%d%d.%d%d BRL", valor[0], valor[1], valor[2], valor[3], valor[4]);
                lcdWrite(buffer);
                char cursorLine[17] = "^               ";
                lcdWriteByte(0xC0, 0);
                lcdWrite(cursorLine);
            } else if (fase == 1) {
                int amountCents = valor[0]*10000 + valor[1]*1000 + valor[2]*100 + valor[3]*10 + valor[4];
                calculateConversion(selected, amountCents, &convertedInt, &convertedDec);
                showResult(selected, amountCents, convertedInt, convertedDec);
                fase = 2;
            } else if (fase == 2) {
                fase = 0;
                coin = 1;
                selected = 0;
                valor[0] = valor[1] = valor[2] = valor[3] = valor[4] = 0;
                cursorPos = 0;
                lcdClear();
                lcdWrite("Selecione >>\n");
                lcdWrite("a moeda:  >>\n");
            }

            __delay_cycles(300000);
        }
        lastButtonState = buttonState;
    }
}