#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "ws2812.pio.h"


#define NUM_PIXELS 64
#define WS2812_PIN 2
#define POT_PIN 26
#define MODE_SELECTOR 27
#define COLOUR_SELECTOR 19

// Check the pin is compatible with the platform
#if WS2812_PIN >= NUM_BANK0_GPIOS
#error Attempting to use a pin>=32 on a platform that does not support it
#endif

static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}

uint32_t urgb_from_hsv(float H, float S, float V) {
    // TODO: if this is too slow use the fast header from here https://www.vagrearg.org/content/hsvrgb
	float r, g, b;
    float h = H / 360; // might want to drop the divisions here as well maybe 
	float s = S / 100;
	float v = V / 100;
	
	int i = floor(h * 6);
	float f = h * 6 - i;
	float p = v * (1 - s);
	float q = v * (1 - f * s);
	float t = v * (1 - (1 - f) * s);
	
	switch (i % 6) {
		case 0: r = v, g = t, b = p; break;
		case 1: r = q, g = v, b = p; break;
		case 2: r = p, g = v, b = t; break;
		case 3: r = p, g = q, b = v; break;
		case 4: r = t, g = p, b = v; break;
		case 5: r = v, g = p, b = q; break;
	}
    // printf("h: %f, s:%f, v:%f\n", H, S, V);
    return urgb_u32(r*255, g*255, b*255); 
}

typedef struct pixel_value{
    float hue;
    float saturation;
    float direction;
} pixel_value;

void init_pixels(int hue, pixel_value* pixel_array){
    for (int i=0; i < NUM_PIXELS; ++i){
        if (i == 27 || i == 28 || i == 35 || i == 36) pixel_array[i].hue = 25;
        else if ( i == 19 || i == 20 || i == 26 ||i == 29 || i== 34 || i == 37 || i == 43 || i == 44){
            // the outer ring of the inner circle part (pardon my botany) does not change
            continue;
        } else {
            pixel_array[i].hue = hue;
        }
        // setting the default saturation above the turning point creates a breathing effect 
        // increasing the difference between the cutoff point and the default value lengthens the breathing duration
        pixel_array[i].saturation = ((double) rand() / (double) RAND_MAX) * 10.0 + 89.0;  
        pixel_array[i].direction = (rand() >> 30) & 1 ? 0.1: -0.1;
    }
}

void pattern_sakura(PIO pio, uint sm, uint len, float brightness, pixel_value* pixel_array){
    for (uint i = 0; i < len; ++i){
        // sometimes things look ugly but who cares honestly
        if ((i == 19 || i == 20 || i == 26 ||i == 29 || i== 34 || i == 37 || i == 43 || i == 44)) {
            put_pixel(pio, sm, urgb_from_hsv(30, 75, brightness));
            continue;
        } else if (pixel_array[i].saturation > 99) {
            pixel_array[i].direction = -0.1;
        } else if (pixel_array[i].saturation < 85) {
            pixel_array[i].direction = 0.1;
        }
        pixel_array[i].saturation += pixel_array[i].direction;
        put_pixel(pio, sm, urgb_from_hsv(pixel_array[i].hue, pixel_array[i].saturation, brightness));
    }
}

int sparkle[NUM_PIXELS];

void update_sparkles(){
    for (uint i = 0; i < NUM_PIXELS; ++i){
        sparkle[i] = rand() % 16 ? 0 : 1;
    }
}

void pattern_sparkle(PIO pio, uint sm, uint len, float brightness, uint frame, int hue) {
    if (frame % 15 == 0){
       update_sparkles(); 
    }
    for (uint i = 0; i < len; ++i)
        if (sparkle[i])
            put_pixel(pio, sm, urgb_from_hsv(hue, 87, brightness)); 
        else put_pixel(pio, sm, 0);
}

const float scaling_factor = 80.0f / (1 << 10);

float previous = 0;
float alpha = 0.99;

float clean_brightness(){
    int value = adc_read();
    value = (1 << 10) - (value >> 2);  // my dumbass connected the potentiometer in reverse
    float clean_value = (alpha) * previous + (1-alpha) * (float) value;
    previous = clean_value;
    return clean_value * scaling_factor + 5;
}

int main() {
    stdio_init_all();
    printf("What are you doing here, did something break?\n");

    PIO pio;
    uint sm;
    uint offset;

    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, WS2812_PIN, 1, true);
    hard_assert(success);

    adc_init();
    adc_gpio_init(POT_PIN);
    adc_select_input(0);

    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000);

    gpio_init(MODE_SELECTOR);
    gpio_set_dir(MODE_SELECTOR, GPIO_IN);
    gpio_pull_up(MODE_SELECTOR);
    
    gpio_init(COLOUR_SELECTOR);
    gpio_set_dir(COLOUR_SELECTOR, GPIO_IN);
    gpio_pull_up(COLOUR_SELECTOR);


    pixel_value sakura[NUM_PIXELS];
    pixel_value light[NUM_PIXELS];

    init_pixels(343, sakura);
    init_pixels(30, light);


    uint frame = 0;
    while (1) {
        int pat = gpio_get(MODE_SELECTOR) ? 0 : 1;
        int col = gpio_get(COLOUR_SELECTOR) ? 0 : 1;
        float brightness = clean_brightness() ;
        if (pat) {
            pixel_value* pixel_set = col ? sakura : light; 
            pattern_sakura(pio, sm, NUM_PIXELS, brightness, pixel_set);
        }
        else pattern_sparkle(pio, sm, NUM_PIXELS, brightness, frame, col ? 343 : 30);
        sleep_ms(3);
        frame += 1;
    }

    pio_remove_program_and_unclaim_sm(&ws2812_program, pio, sm, offset);
}