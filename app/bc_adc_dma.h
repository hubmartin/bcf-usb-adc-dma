#ifndef _BC_ADC_DMA
#define _BC_ADC_DMA

#include <bc_common.h>

#include "bc_tick.h"
#include "bc_gpio.h"
#include "bc_adc.h"
#include "bc_scheduler.h"

void mic_init(uint8_t *buffer, size_t buffer_size);

bool mic_measure(bc_adc_channel_t adc_channel, uint16_t prescaler, uint16_t autoreload);
bool mic_measure_circular(bc_adc_channel_t adc_channel, uint16_t prescaler, uint16_t autoreload);

bool mic_measure_is_done(void);
void mic_on_update(void (*update_callback)(void));
void mic_on_error(void (*error_callback)(void));
bool mic_signal_remove_dc(void);
float mic_signal_get_rms(void);
uint32_t mic_signal_get_energy(void);
float mic_get_signal_power(void);

bool mic_get_value(uint16_t *value);

#endif
