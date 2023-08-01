#include <stdint.h>
#include <stdio.h>
#include <avr/io.h> 
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <graphics.h>
#include <macros.h>
#include <string.h>
#include "lcd_model.h"

#define BAUD 9600
#define F_CPU 16000000UL
#define MYUBRR F_CPU/16/BAUD-1

// *** UART ***
// Functions used to facilitate settings menu in the main_menu() function
// NOTE: Functions here taken from teaching materials and AMS content

char buffer[2];
char username_buffer1[2];
char username_buffer2[2];
char username_buffer3[2];
char username_buffer4[2];
char difficulty_buffer[2];
char *diff[5];

void uart_setup(unsigned int ubrr) {
	UBRR0H = (unsigned char)(ubrr>>8);
	UBRR0L = (unsigned char)(ubrr);
	UCSR0B = (1 << RXEN0) | (1 << TXEN0);
	UCSR0C = (3 << UCSZ00);
}

void uart_putbyte(unsigned char data) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = data;
}

void uart_send_chars(char str[]){
	int i = 0;
	while(str[i] != '\0'){
		uart_putbyte(str[i]);
		i++;
	}
	uart_putbyte('\0');
}

int uart_getbyte(char *buffer) {
    if (UCSR0A & (1 << RXC0)) {
        *buffer = UDR0;
        return 1;
    }
    else {
        return 0;
    }
}

// *** ISR TIMERS ***
// Setting up TIMER 0 and 1 for respective interrupt routines.

void timer0_setup(void){
	TCCR0A = 0;			// normal mode
	TCCR0B = 4;			// prescaler 256
	TIMSK0 = 1;			// enable overflow interupt
	sei();
}

void timer1_setup(void){
	TCCR1A = 0;			// normal mode
	TCCR1B = 4;			// prescaler 256
	TIMSK1 = 1;			// enable overflow interupt
	sei();
}

// *** TIMER 1 ISR ***
// Defines key variables of game logic that rely on the ISR of timer 1

volatile uint8_t counter16 = 0;
volatile uint8_t increment_counter16 = 0;
volatile uint8_t enemy_freq = 0;
volatile uint8_t enemy_speed = 0;
uint8_t game_on = 0;

ISR(TIMER1_OVF_vect){
	if(game_on == 1){
		counter16 ++;
		if ((counter16 % enemy_freq) == 0){
			increment_counter16++;
		}
	}
}

// *** ADC ***
// Used for converting the potentiometer input to a value for moving the player
// NOTE: Functions here are from AMS materials

void adc_setup(void) {
	// set ADMUX for AVcc with external capacitor
	ADMUX = 0b01000000;
	// set ADCSRA
	ADCSRA = 0b10000000;
	// set division factor
	ADCSRA |= ((1<<ADPS2) | (1<< ADPS1) | (1<<ADPS0));		// division factor of 128
}

int16_t read_adc(void){
	ADMUX |= 0b0000;
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return (ADC);
}

// *** PWM *** 
// Setting up TIMER 2 in PWM mode. 

void pwm_setup(void){
	DDRB &= ~(1 << PB3);		
	// PD6 is now an output
	OCR2A = 128;
	// set PWM for 50% duty cycle
	TCCR2A |= (1 << COM2A1);
	TCCR2B = (1 << CS21);
	TCCR2A |= (1 << WGM22) | (1 << WGM21) | (1 << WGM20);
}

// *** DISPLAY ***
// NOTE: functions here have mainly been taken or adapted from the graphics library

void display_setup(uint8_t contrast){
	SET_OUTPUT(DDRD, SCEPIN); 
    SET_OUTPUT(DDRD, RSTPIN); 
    SET_OUTPUT(DDRD, DCPIN);  
    SET_OUTPUT(DDRD, DINPIN); 
    SET_OUTPUT(DDRD, SCKPIN); 
	
	CLEAR_BIT(PORTD, RSTPIN);
	SET_BIT(PORTD, SCEPIN);
	SET_BIT(PORTD, RSTPIN);
	
	LCD_CMD(lcd_set_function, lcd_instr_extended);
    LCD_CMD(lcd_set_contrast, contrast);
    LCD_CMD(lcd_set_temp_coeff, 0);
    LCD_CMD(lcd_set_bias, 3);
	
	LCD_CMD(lcd_set_function, lcd_instr_basic);
    LCD_CMD(lcd_set_display_mode, lcd_display_normal);
    LCD_CMD(lcd_set_x_addr, 0);
    LCD_CMD(lcd_set_y_addr, 0);
	
	SET_BIT(PORTD,PD2);
}

uint8_t x, y;
uint8_t turret_direct[8];
uint8_t turret_original[8] = {
	0b00011000,
	0b00011000,
	0b00011000,
	0b00011000,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
};

uint8_t enemy_x, enemy_y;
uint8_t enemy_direct[8];
uint8_t enemy_original[8] = {
	0b00000000,
	0b00110011,
	0b01111011,
	0b11111111,
    0b11111111,
    0b01111011,
    0b00110011,
    0b00000000,
};

uint8_t bulletbase_x, bulletbase_y;
uint8_t bulletbase_direct[8];
uint8_t bulletbase_original[8] = {
	0b00001000,
	0b00011000,
	0b00001000,
	0b00011000,
    0b10010001,
    0b01011010,
    0b10100100,
    0b00000000,
};

uint8_t bullet_x, bullet_y;
uint8_t bullet_direct[8];
uint8_t bullet_original[8] = {
	0b00000000,
	0b00100000,
	0b00100000,
	0b00001000,
    0b00000000,
    0b00100000,
    0b00101000,
    0b00000000,
};

void setup_image(uint8_t image_original[], uint8_t image_direct[]) {
    // Visit each column of output bitmap
    for (int i = 0; i < 8; i++) {

        // Visit each row of output bitmap
        for (int j = 0; j < 12; j++) {
            uint8_t bit_val = BIT_VALUE(image_original[j], (7 - i));
            WRITE_BIT(image_direct[i], j, bit_val);
        }
    }
}

void draw_image(uint8_t image_direct[], uint8_t x, uint8_t y) {
    LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
    LCD_CMD(lcd_set_x_addr, x);
    LCD_CMD(lcd_set_y_addr, y / 8);

    for (int i = 0; i < 8; i++) {
        LCD_DATA(image_direct[i]);
    }
}

void erase_turret(void) {
    LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
    LCD_CMD(lcd_set_x_addr, x);
    LCD_CMD(lcd_set_y_addr, y / 8);

    for (int i = 0; i < 8; i++) {
        LCD_DATA(0);
    }
}

void erase_enemy(uint8_t x, uint8_t y){
	LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
    LCD_CMD(lcd_set_x_addr, x);
    LCD_CMD(lcd_set_y_addr, y / 8);

    for (int i = 0; i < 8; i++) {
        LCD_DATA(0);
    }
}

// *** BUTTON ***
// THE SETUP OF BUTTONS AND THE ISR OF TIMER 0 THAT MANAGES THE BUTTON DEBOUNCING

volatile uint8_t bit_counter = 0;		// 
volatile uint8_t bit_counter2 = 0;
volatile uint8_t switch_state = 0;		// current switch state
volatile uint8_t switch_state2 = 0;
uint8_t old_switch_state = 0;
uint8_t old_switch_state2 = 0;
uint8_t b = 0;	
uint8_t b2 = 0;						// 
volatile uint8_t returned_value = 0;		// current switch state
volatile uint8_t returned_value_direction = 1;


void button_setup(void){
	CLEAR_BIT(DDRB,4);
	CLEAR_BIT(DDRB,2);
}

ISR(TIMER0_OVF_vect){
	if(returned_value == 0){
		returned_value++;
		returned_value_direction = 1;
	}
	else if(returned_value == 255){
		returned_value = returned_value - 1;
		returned_value_direction = 0;
	}
	else if(returned_value_direction == 1){
		returned_value++;
	}
	else if(returned_value_direction == 0){
		returned_value = returned_value - 1;
	}
	returned_value++;
	if((PINB & (1<<4)) != 0){	// check pinb 3
		b = 0b00000001;
	}
	else {
		b = 0b00000000;
	}
	uint8_t mask = 0b00001111;			// define the mask
	bit_counter = (bit_counter << 1);	// left shift the bit_counter
	bit_counter &= mask;				// & operation with the mask
	bit_counter |= b;					// | opertion with the current state of the switch
	if (bit_counter == mask){			// assign the switch_state
		switch_state = 1;
		if(game_on == 1){
			SET_BIT(PORTB,1);
		}
	}
	else if (bit_counter == 0){
		switch_state = 0;
		CLEAR_BIT(PORTB,1);
	}
	
	if((PINB & (1<<2)) != 0){	// check pinb 3
		b2 = 0b00000001;
	}
	else {
		b2 = 0b00000000;
	}
	uint8_t mask2 = 0b00001111;			// define the mask
	bit_counter2 = (bit_counter2 << 1);	// left shift the bit_counter
	bit_counter2 &= mask2;				// & operation with the mask
	bit_counter2 |= b2;					// | opertion with the current state of the switch
	if (bit_counter2 == mask2){			// assign the switch_state
		switch_state2 = 1;
		game_on = 0;
	}
	else if (bit_counter2 == 0){
		switch_state2 = 0;
	}
}

int button_clicked(void){
	if(switch_state == 1 && old_switch_state == 0){
		old_switch_state = 1;
		return 1;
	}
	else if(switch_state == 0 && old_switch_state == 1){
		old_switch_state = 0;
		return 0;
	}
	return 0;
}

// *** LED ***
// SETUP OF THE FIRING LED

void led_setup(void){
	SET_BIT(DDRB,5);
	SET_BIT(DDRB,1);
}

// *** GAME LOGIC ***
// LOGIC THAT DETERMINES ENEMY SPAWNS, MOVEMENTS, AND THE MAIN MENU

uint8_t enemy_status[50];
uint8_t enemy_xpos[50];
uint8_t enemy_ypos[50];
uint8_t enemy_life[50];

void setup_enemy(void){
	for (int i = 0; i < 50; i++){
		enemy_status[i] = 0;
	}
	for (int i = 0; i < 50; i++){
		enemy_xpos[i] = rand() % (LCD_X - 10);
	}
	for (int i = 0; i < 50; i++){
		enemy_ypos[i] = 0;
	}
	for (int i = 0; i < 50; i++){
		enemy_life[i] = 0;
	}
}

void main_menu(void){
	clear_screen(); draw_string(15,10,"UART SETUP",FG_COLOUR);
	draw_string(5,20,"UserName:",FG_COLOUR); draw_string(5,30,"Difficulty:",FG_COLOUR); show_screen();
	uart_send_chars("Welcome! Please Enter a Username (4 characters only):\n\r");
	while(!uart_getbyte(username_buffer1)){}; draw_string(50,20,username_buffer1,FG_COLOUR); show_screen();
	while(!uart_getbyte(username_buffer2)){}; draw_string(55,20,username_buffer2,FG_COLOUR); show_screen();
	while(!uart_getbyte(username_buffer3)){}; draw_string(60,20,username_buffer3,FG_COLOUR); show_screen();
	while(!uart_getbyte(username_buffer4)){}; draw_string(65,20,username_buffer4,FG_COLOUR); show_screen();
	uart_send_chars("\nEnter a Difficulty Level:\n\r1 - Novice\n\r2 - Pro\n\r3 - Master\n\r");
	while(!(uart_getbyte(difficulty_buffer))){}; 
	if(*difficulty_buffer == '1'){ *diff = "nov"; draw_string(60,30,"Nov",FG_COLOUR); enemy_freq = 10; enemy_speed = 3;}
	else if(*difficulty_buffer == '3') { *diff = "mas"; draw_string(60,30,"Mas",FG_COLOUR); enemy_freq = 1; enemy_speed = 5; }
	else { *diff = "pro"; draw_string(60,30,"Pro",FG_COLOUR);  enemy_freq = 3; enemy_speed = 4;} show_screen(); 
	uart_send_chars("\nAll set up! Press any button to continue...\n\n\r");
	while(!uart_getbyte(buffer)){}; 
}

int main(void){
		
	clear_screen();
	
	timer0_setup();
	timer1_setup();
	uart_setup(MYUBRR);
	adc_setup();
	pwm_setup();
	led_setup();
	button_setup();
	display_setup(LCD_DEFAULT_CONTRAST);

	int16_t pot = 0;
	int score = 0;
	int i = 0;
	char score_value[10];
	int setup = 0;
	
	main_menu();
	clear_screen();
	
	while(game_on == 0){
		
		CLEAR_BIT(DDRB,3);
		setup_image(turret_original, turret_direct);
		setup_image(enemy_original, enemy_direct);
		setup_image(bulletbase_original, bulletbase_direct);
		setup_image(bullet_original,  bullet_direct);
		setup_enemy();
		score = 0;
		i = 0;
		counter16 = 0;
		increment_counter16 = 0;
		game_on = 1;
		setup = 0;
		uint8_t shot_fired = 0;
		uint8_t button_x = 0;
		uint8_t button_time_stamp = 0;
		
		clear_screen();
		show_screen();
		
		while(game_on){
	
		if (setup == 0){
			clear_screen();
			itoa(score,score_value,10);
			draw_string(0,40,username_buffer1,FG_COLOUR); draw_string(5,40,username_buffer2,FG_COLOUR);
			draw_string(10,40,username_buffer3,FG_COLOUR); draw_string(15,40,username_buffer4,FG_COLOUR);
			draw_string(72,40,score_value,FG_COLOUR);
			draw_string(0,32,"__________________",FG_COLOUR);
			draw_string(35,40,*diff,FG_COLOUR);
			show_screen();
			setup = 1;
		}
		
		erase_enemy(button_x,20);
		erase_enemy(button_x,10);
		erase_enemy(button_x,0);
		if (shot_fired == 1){
			if (((counter16 - button_time_stamp) < 1 ) && button_time_stamp != 0){
				draw_image(bulletbase_direct,button_x, 20);
				draw_image(bullet_direct,button_x,10);
				draw_image(bullet_direct,button_x,0);
			}
		}
		else {
			shot_fired = 0;
		}

		pot = read_adc();
		int enemy_appr = 0;	

		erase_turret();
		x = (pot/13) % (LCD_X - 8);
		
		if (x % (LCD_X-8) != 0){
			y = 30;
			draw_image(turret_direct, x, y);
		}
		
		if (increment_counter16 != i){
			enemy_life[i] = counter16;
			enemy_status[i] = 1;
			i++;
		}
		
		for (int j = 0; j < increment_counter16; j++){
			if (enemy_status[j] == 1 && game_on == 1){
				erase_enemy(enemy_xpos[j], enemy_ypos[j]);
				enemy_ypos[j] = (counter16 - enemy_life[j]) * enemy_speed;	
				draw_image(enemy_direct, enemy_xpos[j], enemy_ypos[j]);
				if (enemy_ypos[j] > 40 && enemy_status[j] == 1){
					CLEAR_BIT(DDRB, PB3);
					while(game_on == 1){
						draw_string(15,10, "Game Over",FG_COLOUR);
						draw_string(20,20, "Score: ",FG_COLOUR);
						draw_string(50,20,score_value,FG_COLOUR);
						show_screen();
					}
				}
				else if (enemy_ypos[j] > 10 && i > 0){
					DDRB |= (1 << PB3);
					OCR2A = returned_value;
					enemy_appr = 1;
				}
				else if (enemy_appr == 0){
					CLEAR_BIT(DDRB, PB3);
				}
			}
		}
		
		if(button_clicked()){	
			shot_fired = 1;
			button_time_stamp = counter16;
			button_x = x;
			for (int j = 0; j < increment_counter16; j++){
				if (((x > enemy_xpos[j] && x - enemy_xpos[j] < 4) || 
				(x <= enemy_xpos[j] && enemy_xpos[j] - x < 5)) 
				&& enemy_status[j] == 1){
					enemy_status[j] = 0;
					score++;
					erase_enemy(enemy_xpos[j], enemy_ypos[j]);
					setup = 0;
				}
			}
		}
		
		if(score == 50){
			clear_screen();
			while(game_on == 1){
				draw_string(20,20, "You Won!", FG_COLOUR);
				show_screen();
			}
		}	
		}		
	}
	
	return 0;
	
}
