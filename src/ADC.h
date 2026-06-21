#ifndef ADC_H
#define ADC_H

#include <stdbool.h>
#include <stdint.h>


// ======================================================================================
//  TYPES
// ======================================================================================

typedef enum
{
    ADC_OWNER_NONE = 0,
    ADC_OWNER_VALVES,
    ADC_OWNER_DEBUG,
    ADC_OWNER_TEMPERATURE,
    ADC_OWNER_PUMPS,
    ADC_OWNER_HEATERS,
    ADC_OWNER_SOLENOIDS,
    ADC_OWNER_COUNT
} adc_owner_t;

typedef void (*adc_callback_t)(uint16_t sample, void *context);


// ======================================================================================
//  API
// ======================================================================================

void adc_init();

bool adc_acquire(adc_owner_t owner);
void adc_release(adc_owner_t owner);
bool adc_is_busy();
bool adc_is_owned_by(adc_owner_t owner);

bool adc_read_blocking(adc_owner_t owner, uint8_t channel, uint16_t *value);
uint16_t adc_read(uint8_t channel);

bool adc_start_conversion(adc_owner_t owner,
                          uint8_t channel,
                          adc_callback_t callback,
                          void *context);

#endif /* ADC_H */
