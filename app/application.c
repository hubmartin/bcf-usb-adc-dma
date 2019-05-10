#include <application.h>

#include "bc_adc_dma.h"
#include "bc_adc.h"

// LED instance
bc_led_t led;

// Button instance
bc_button_t button;

/*
If no PLL is NOT enabled, then the core clock is 2 MHz

Prescaler:
2 MHz / 1000 = 2 kHz = 2000 Hz
Autoreload:
2000 Hz / 2000 = 1 Hz
*/
#define ADC_DMA_1HZ_PRESCALER 1000
#define ADC_DMA_1HZ_AUTORELOAD 2000

uint8_t buffer[10];

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_PRESS)
    {
        bc_led_set_mode(&led, BC_LED_MODE_TOGGLE);
        mic_measure_circular(BC_ADC_CHANNEL_A0, ADC_DMA_1HZ_PRESCALER, ADC_DMA_1HZ_AUTORELOAD);
    }

    // Logging in action
    bc_log_info("Button event handler - event: %i", event);
}

void application_init(void)
{
    // Initialize logging
    bc_log_init(BC_LOG_LEVEL_DUMP, BC_LOG_TIMESTAMP_ABS);

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_ON);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    mic_init(buffer, sizeof(buffer));

    bc_scheduler_disable_sleep();
}

void application_task(void)
{
    //bc_scheduler_plan_current_from_now(10);
}
