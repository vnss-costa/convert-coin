#include <msp430.h>
#include <stdint.h>
#include <stdio.h>

// Endereço I2C do LCD
#define LCD_ADDR 0x27

// Máscaras de controle do PCF8574
#define LCD_RS  BIT0  // Register Select
#define LCD_RW  BIT1  // Read/Write (não usado, sempre 0)
#define LCD_EN  BIT2  // Enable
#define LCD_BL  BIT3  // Backlight (sempre ligado)

// ========== Funções de LCD via I2C ==========
uint8_t i2cSend(uint8_t addr, uint8_t data) {
    while (UCB0CTL1 & UCTXSTP); // Espera STOP anterior
    UCB0I2CSA = addr;
    UCB0CTL1 |= UCTR | UCTXSTT; // Modo transmissor + START
    while (!(UCB0IFG & UCTXIFG));
    UCB0TXBUF = data;
    while (!(UCB0IFG & UCTXIFG));
    UCB0CTL1 |= UCTXSTP;
    while (UCB0CTL1 & UCTXSTP);
    return 1;
}

void lcdWriteNibble(uint8_t nibble, uint8_t isChar) {
    uint8_t data = (nibble & 0xF0); // Alinha nibble nos bits D4–D7
    data |= LCD_BL;                // Backlight sempre ligado
    if (isChar) data |= LCD_RS;

    i2cSend(LCD_ADDR, data);
    i2cSend(LCD_ADDR, data | LCD_EN);
    __delay_cycles(2000); // Delay para garantir pulso
    i2cSend(LCD_ADDR, data);
}

void lcdWriteByte(uint8_t byte, uint8_t isChar) {
    lcdWriteNibble(byte & 0xF0, isChar);              // Parte alta
    lcdWriteNibble((byte << 4) & 0xF0, isChar);       // Parte baixa
}

void lcdInit() {
    __delay_cycles(50000); // Aguarda LCD iniciar

    lcdWriteNibble(0x30, 0);
    __delay_cycles(5000);
    lcdWriteNibble(0x30, 0);
    __delay_cycles(5000);
    lcdWriteNibble(0x30, 0);
    __delay_cycles(5000);
    lcdWriteNibble(0x20, 0); // Modo 4 bits

    lcdWriteByte(0x28, 0); // 2 linhas, fonte 5x8
    lcdWriteByte(0x0C, 0); // Display on, cursor off
    lcdWriteByte(0x06, 0); // Incrementa cursor
    lcdWriteByte(0x01, 0); // Limpa display
    __delay_cycles(5000);
}

void lcdWrite(char *str) {
    while (*str) {
        if (*str == '\n') {
            lcdWriteByte(0xC0, 0); // Início da 2ª linha
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

// ========== Configurações ==========
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

// ========== Leitura segura de canal ADC ==========
uint8_t readADC(uint8_t channel) {
    ADC12CTL0 &= ~ADC12ENC;
    ADC12MCTL0 = channel;
    ADC12CTL0 |= ADC12ENC;
    ADC12CTL0 |= ADC12SC;
    while (ADC12CTL1 & ADC12BUSY);
    return ADC12MEM0 & 0xFF;
}

// ========== Seleciona moeda ==========
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

// ========== MAIN ==========
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

    int contCoin = 0;

    lcdWrite("Selecione >>\n");
    lcdWrite("a moeda:  >>\n");

    while (1) {
        // Leitura do joystick com troca segura de canais
        unsigned int xValue = readADC(ADC12INCH_0); // eixo X (P6.0)
        unsigned int yValue = readADC(ADC12INCH_1); // eixo Y (P6.1)

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
        static uint8_t valor[5] = {0, 0, 0, 0, 0};
        static uint8_t cursorPos = 0; // 0 a 4

        if (fase == 1) {
            if (xValue > 110 && xValue < 140) {
                lastXDir = 0;
            } else if (xValue >= 180 && lastXDir != 1) { // Direita
                if (cursorPos < 4) cursorPos++;
                lastXDir = 1;
                precisaAtualizarLCD = 1;
                __delay_cycles(200000);
            } else if (xValue <= 50 && lastXDir != 2) { // Esquerda
                if (cursorPos > 0) cursorPos--;
                lastXDir = 2;
                precisaAtualizarLCD = 1;
                __delay_cycles(200000);
            }
            // Detecta movimento vertical (cima/baixo) para alterar valor
            if (yValue > 110 && yValue < 140) {
                lastYDir = 0;
            } else if (yValue >= 180 && lastYDir != 1) { // Para baixo
                if (valor[cursorPos] > 0) valor[cursorPos]--;
                lastYDir = 1;
                precisaAtualizarLCD = 1;
                __delay_cycles(200000);
            } else if (yValue <= 50 && lastYDir != 2) { // Para cima
                if (valor[cursorPos] < 9) valor[cursorPos]++;
                lastYDir = 2;
                precisaAtualizarLCD = 1;
                __delay_cycles(200000);
            }

            if (precisaAtualizarLCD) {
                lcdClear();
                char buffer[16];
                sprintf(buffer, "%d%d%d.%d%d", valor[0], valor[1], valor[2], valor[3], valor[4]);
                lcdWrite(buffer);

                char cursorLine[17] = "                ";
                if (cursorPos < 3)
                    cursorLine[cursorPos] = '^';
                else
                    cursorLine[cursorPos + 1] = '^'; // pula ponto

                lcdWriteByte(0xC0, 0); // segunda linha
                lcdWrite(cursorLine);

                precisaAtualizarLCD = 0; // reseta flag
            }
        }

        // Botão joystick (P2.5)
        unsigned int buttonState = P2IN & BIT5;
        if (buttonState == 0 && lastButtonState != 0) {
            if (fase == 0) {
                selected = coin;
                fase = 1;
                lcdClear();
                lcdWrite("Selecione     ^^\n");
                lcdWrite("a quantidade: ^^\n");
            }
        }
        lastButtonState = buttonState;
    }
}
