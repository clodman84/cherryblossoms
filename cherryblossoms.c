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

typedef struct sakura_pixel_value{
    float hue;
    float saturation;
    float direction;
} sakura_pixel_value;

sakura_pixel_value sakura[NUM_PIXELS];

void init_sakura(){
    for (int i=0; i < NUM_PIXELS; ++i){
        if (i == 27 || i == 28 || i == 35 || i == 36) sakura[i].hue = 25;
        else if ( i == 19 || i == 20 || i == 26 ||i == 29 || i== 34 || i == 37 || i == 43 || i == 44){
            // the outer ring of the inner circle part (pardon my botany) does not change
            continue;
        } else {
            sakura[i].hue = 343;
        }
        // setting the default saturation above the turning point creates a breathing effect 
        // increasing the difference between the cutoff point and the default value lengthens the breathing duration
        sakura[i].saturation = ((double) rand() / (double) RAND_MAX) * 10.0 + 89.0;  
        sakura[i].direction = (rand() >> 30) & 1 ? 0.1: -0.1;
    }
}

void pattern_sakura(PIO pio, uint sm, uint len, float brightness, uint frame){
    for (uint i = 0; i < len; ++i){
        // sometimes things look ugly but who cares honestly
        if ((i == 19 || i == 20 || i == 26 ||i == 29 || i== 34 || i == 37 || i == 43 || i == 44)) {
            put_pixel(pio, sm, urgb_from_hsv(30, 75, brightness));
            continue;
        } else if (sakura[i].saturation > 99) {
            sakura[i].direction = -0.1;
        } else if (sakura[i].saturation < 85) {
            sakura[i].direction = 0.1;
        }
        sakura[i].saturation += sakura[i].direction;
        put_pixel(pio, sm, urgb_from_hsv(sakura[i].hue, sakura[i].saturation, brightness));
    }
}

int sparkle[NUM_PIXELS];

void update_sparkles(){
    for (uint i = 0; i < NUM_PIXELS; ++i){
        sparkle[i] = rand() % 16 ? 0 : 1;
    }
}

void pattern_sparkle(PIO pio, uint sm, uint len, float brightness, uint frame) {
    if (frame % 15 == 0){
       update_sparkles(); 
    }
    for (uint i = 0; i < len; ++i)
        if (sparkle[i])
            put_pixel(pio, sm, urgb_from_hsv(343, 87, brightness)); 
        else put_pixel(pio, sm, 0);
}

typedef void (*pattern)(PIO pio, uint sm, uint len, float brightness, uint frame);
const struct {
    pattern pat;
    const char *name;
} pattern_table[] = {
        {pattern_sparkle, "Sparkles"},
        {pattern_sakura, "Sakura"}
};

int main() {
    //set_sys_clock_48();
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
    const float scaling_factor = 80.0f / (1 << 12);

    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000);

    init_sakura();
    uint frame = 0;
    while (1) {
        int pat = rand() % count_of(pattern_table);
        puts(pattern_table[pat].name);
        for (int i = 0; i < 1000; ++i) {
            float brightness = adc_read() * scaling_factor + 5;
            pattern_table[pat].pat(pio, sm, NUM_PIXELS, brightness, frame);
            sleep_ms(3);
            frame += 1;
        }
    }

    pio_remove_program_and_unclaim_sm(&ws2812_program, pio, sm, offset);
}
