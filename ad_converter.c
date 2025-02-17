#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "pico/bootrom.h"
#include "math.h"

#define I2C_PORT i2c1 // Define o barramento I2C
#define I2C_SDA 14 // Define o pino SDA
#define I2C_SCL 15 // Define o pino SCL
#define endereco 0x3C // Endereço do display OLED

#define pwm_wrap 4096 // Wrap do PWM

const uint led_pin_red = 13; // LED vermelho
const uint led_pin_blue = 12; // LED azul
const uint led_pin_green = 11;  // LED verde

const uint joystick_x_pin = 27; // Pino do eixo X do joystick
const uint joystick_y_pin = 26; // Pino do eixo Y do joystick

const uint btn_j = 22; // Pino do botão do joystick
const uint btn_a = 5; // Pino do botão A
const uint btn_b = 6; // Pino do botão B

uint32_t last_time = 0; // Variável para armazenar o tempo do último evento para o debouncing

static volatile bool pwm_state = 1; // Estado do PWM

static volatile uint16_t border_x_max = 128; // Borda máxima do eixo X
static volatile uint16_t border_y_max = 64; // Borda máxima do eixo Y

// Função para configurar as GPIOs
void setup_GPIOs() { 
    // Inicializa os LEDs e define como saída
    gpio_init(led_pin_red); 
    gpio_set_dir(led_pin_red, GPIO_OUT);
    gpio_init(led_pin_blue);
    gpio_set_dir(led_pin_blue, GPIO_OUT);
    gpio_init(led_pin_green);
    gpio_set_dir(led_pin_green, GPIO_OUT);
    
    // Inicializa os botões e define como entrada
    gpio_init(btn_a);
    gpio_set_dir(btn_a, GPIO_IN);
    gpio_pull_up(btn_a);

    gpio_init(btn_b);
    gpio_set_dir(btn_b, GPIO_IN);
    gpio_pull_up(btn_b);

    gpio_init(btn_j);
    gpio_set_dir(btn_j, GPIO_IN);
    gpio_pull_up(btn_j);
}

// Função para configurar o PWM
void pwm_setup_gpio(uint gpio, uint wrap) {
    gpio_set_function(gpio, GPIO_FUNC_PWM); // Define a função do pino como PWM
    
    uint slice_num = pwm_gpio_to_slice_num(gpio); // Obtém o número do slice do PWM
    pwm_set_wrap(slice_num, wrap); // Define o wrap do PWM

    pwm_set_enabled(slice_num, true); // Habilita o PWM
}

// Função para ajustar o brilho do LED
uint16_t set_led_brightness(uint16_t adc_value) {
    int32_t centered_value = adc_value - 2048;  // Centraliza no meio
    int32_t abs_value = abs(centered_value);    // Obtém distância do centro (valor absoluto)
    
    uint16_t pwm_value = (abs_value * abs_value) / 4095; // Aplica uma curva quadrática para suavizar o brilho

    return pwm_value;
}

// Alterna o tamanho da borda, diminuindo a cada chamada e depois reiniciando
void switch_borders() {
    static uint8_t current_border = 0;
    switch (current_border) { 
        case 0: 
            border_x_max = 96;
            border_y_max = 48;
            current_border++;
            break;
        case 1:
            border_x_max = 64;
            border_y_max = 32;
            current_border++;
            break;
        case 2:
            border_x_max = 32;
            border_y_max = 16;
            current_border++;
            break;
        default:
            border_x_max = 128;
            border_y_max = 64;
            current_border = 0;
            break;
    }
}

// Função de callback para tratamento de interrupção dos botões
void gpio_irq_handler(uint gpio, uint32_t events) { 
    uint32_t current_time = to_us_since_boot(get_absolute_time()); // Pega o tempo atual em ms
    if (current_time - last_time > 250000) { // Debouncing de 250ms
        last_time = current_time;
        if (gpio == btn_a) { // Verifica se o botão A foi pressionado e alterna o estado do PWM
            printf("Botão A pressionado!\n");   
            pwm_state = !pwm_state;
        } else if (gpio == btn_b) { // Verifica se o botão B foi pressionado e entra no modo bootsel
            printf("Botão B pressionado!\n");
            reset_usb_boot(0, 0);   
        } else if (gpio == btn_j) { // Verifica se o botão do joystick foi pressionado, altera o estado do LED verde e alterna o tamanho da borda
            printf("Botão do joystick pressionado!\n");
            gpio_put(led_pin_green, !gpio_get(led_pin_green));
            switch_borders();
        }
    }
    
}

// Função para mapear ADC (0-4095) para coordenadas do display (0-128, 0-64)
int map_adc_to_display(int adc_value, int max_adc, int max_display) {
    return (adc_value * max_display) / max_adc;
}

int main()
{
    stdio_init_all(); // Inicializa a comunicação serial

    setup_GPIOs(); // Configura os GPIOs

    // Configura as interrupções para os botões
    gpio_set_irq_enabled_with_callback(btn_a, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(btn_b, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(btn_j, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Inicializa o ADC e configura os pinos do joystick
    adc_init();
    adc_gpio_init(joystick_x_pin);
    adc_gpio_init(joystick_y_pin);


    // Inicializa o I2C e configura os pinos SDA e SCL para o display OLED 
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicializa e configura o display OLED
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); 
    ssd1306_config(&ssd);

    ssd1306_fill(&ssd, false); // Limpa o display
    ssd1306_send_data(&ssd); // Envia os dados para o display

    uint16_t adc_value_x = 0; // Variável para armazenar o valor do ADC do eixo X
    uint16_t adc_value_y = 0; // Variável para armazenar o valor do ADC do eixo Y

    pwm_setup_gpio(led_pin_red, pwm_wrap); // Configura o LED vermelho para PWM
    pwm_setup_gpio(led_pin_blue, pwm_wrap); // Configura o LED azul para PWM

    while (true) {
        // Lê os valores do ADC dos eixos X e Y
        adc_select_input(1);
        adc_value_x = adc_read();
        adc_select_input(0);
        adc_value_y = adc_read();

        // Atualiza o brilho dos LEDs se o PWM estiver habilitado
        if (pwm_state == 1) {
            pwm_set_gpio_level(led_pin_red, set_led_brightness(adc_value_x));
            pwm_set_gpio_level(led_pin_blue, set_led_brightness(adc_value_y));
        }

        // Mapeia os valores do ADC para as coordenadas do display considerando o tamanho do quadrado (8x8) e a borda
        int display_x = map_adc_to_display(adc_value_x, 4095, border_x_max - 8) + (128 - border_x_max) / 2; // Centraliza o display e garante que o retângulo não ultrapasse a borda
        int display_y = border_y_max - 8 - map_adc_to_display(adc_value_y, 4095, border_y_max - 8) + (64 - border_y_max) / 2; // Centraliza o display e garante que o retângulo não ultrapasse a borda, invertendo o eixo Y

        ssd1306_fill(&ssd, false); // Limpa o display
        ssd1306_rect(&ssd, display_y, display_x, 8, 8, true, true); // Desenha um retângulo preenchido
        ssd1306_rect(&ssd, (64 - border_y_max)/2, (128 - border_x_max)/2, border_x_max, border_y_max, true, false); // Desenha um retângulo preenchido
        
        ssd1306_send_data(&ssd); // Envia os dados para o display    

        sleep_ms(50); // Aguarda 50ms para o próximo loop
    }
}
