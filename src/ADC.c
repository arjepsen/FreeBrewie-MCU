#include "ADC.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util/atomic.h>


// ======================================================================================
//  CONSTANTS
// ======================================================================================

#define ADC_REFERENCE_AVCC_BITS                  (1U << REFS0)


// ======================================================================================
//  TYPES
// ======================================================================================

typedef struct
{
    volatile uint8_t owner;
    volatile bool conversion_in_progress;
    adc_callback_t callback;
    void *callback_context;
} adc_runtime_t;


// ======================================================================================
//  PROTOTYPES
// ======================================================================================

static void adc_select_channel(uint8_t channel);


// ======================================================================================
//  VARIABLES
// ======================================================================================

static volatile adc_runtime_t adc_runtime =
{
    .owner = ADC_OWNER_NONE,
    .conversion_in_progress = false,
    .callback = NULL,
    .callback_context = NULL
};


// ======================================================================================
//  MAIN API
// ======================================================================================

/**************************************************************************************************
 * @brief Initialize the AVR ADC peripheral.
 *
 * Uses AVCC as voltage reference and a 128 prescaler so the ADC clock is 125 kHz from a 16 MHz
 * CPU clock.
 **************************************************************************************************/
void adc_init()
{
    ADMUX = ADC_REFERENCE_AVCC_BITS;
    ADCSRB &= (uint8_t)~(1U << MUX5);

    ADCSRA = (1U << ADEN) |
             (1U << ADPS2) |
             (1U << ADPS1) |
             (1U << ADPS0);

    DIDR0 = 0x00U;
    DIDR2 = 0x7FU;

    adc_runtime.owner = ADC_OWNER_NONE;
    adc_runtime.conversion_in_progress = false;
    adc_runtime.callback = NULL;
    adc_runtime.callback_context = NULL;
}


/**************************************************************************************************
 * @brief Acquire exclusive ADC ownership for one module.
 *
 * @param owner Requesting owner.
 *
 * @return True when ownership was granted.
 **************************************************************************************************/
bool adc_acquire(adc_owner_t owner)
{
    bool acquired;

    if ((owner <= ADC_OWNER_NONE) || (owner >= ADC_OWNER_COUNT))
    {
        return false;
    }

    acquired = false;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if ((adc_runtime.owner == ADC_OWNER_NONE) || (adc_runtime.owner == (uint8_t)owner))
        {
            adc_runtime.owner = (uint8_t)owner;
            acquired = true;
        }
    }

    return acquired;
}


/**************************************************************************************************
 * @brief Release ADC ownership.
 *
 * @param owner Releasing owner.
 **************************************************************************************************/
void adc_release(adc_owner_t owner)
{
    if ((owner <= ADC_OWNER_NONE) || (owner >= ADC_OWNER_COUNT))
    {
        return;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (adc_runtime.owner == (uint8_t)owner)
        {
            ADCSRA &= (uint8_t)~(1U << ADIE);
            adc_runtime.owner = ADC_OWNER_NONE;
            adc_runtime.conversion_in_progress = false;
            adc_runtime.callback = NULL;
            adc_runtime.callback_context = NULL;
        }
    }
}


/**************************************************************************************************
 * @brief Report whether the ADC is owned by any module.
 **************************************************************************************************/
bool adc_is_busy()
{
    return (adc_runtime.owner != ADC_OWNER_NONE);
}


/**************************************************************************************************
 * @brief Report whether one specific owner currently owns the ADC.
 **************************************************************************************************/
bool adc_is_owned_by(adc_owner_t owner)
{
    if ((owner <= ADC_OWNER_NONE) || (owner >= ADC_OWNER_COUNT))
    {
        return false;
    }

    return (adc_runtime.owner == (uint8_t)owner);
}


/**************************************************************************************************
 * @brief Perform one blocking ADC conversion for a caller that already owns the ADC.
 *
 * @param owner Calling owner.
 * @param channel ADC channel number.
 * @param value Output conversion result pointer.
 *
 * @return True on success.
 **************************************************************************************************/
bool adc_read_blocking(adc_owner_t owner, uint8_t channel, uint16_t *value)
{
    if ((owner <= ADC_OWNER_NONE) || (owner >= ADC_OWNER_COUNT) || (value == NULL))
    {
        return false;
    }

    if (adc_runtime.owner != (uint8_t)owner)
    {
        return false;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        adc_runtime.callback = NULL;
        adc_runtime.callback_context = NULL;
        adc_runtime.conversion_in_progress = true;
        ADCSRA &= (uint8_t)~(1U << ADIE);
        adc_select_channel(channel);
        ADCSRA |= (1U << ADSC);
    }

    while ((ADCSRA & (1U << ADSC)) != 0U)
    {
    }

    *value = ADC;
    adc_runtime.conversion_in_progress = false;
    return true;
}


/**************************************************************************************************
 * @brief Backward-compatible helper for one blocking ADC conversion.
 *
 * This temporarily acquires ADC ownership as ADC_OWNER_DEBUG.
 *
 * @param channel ADC channel number.
 *
 * @return 10-bit ADC conversion result, or 0 when the ADC is busy.
 **************************************************************************************************/
uint16_t adc_read(uint8_t channel)
{
    uint16_t value;

    value = 0U;

    if (!adc_acquire(ADC_OWNER_DEBUG))
    {
        return 0U;
    }

    if (!adc_read_blocking(ADC_OWNER_DEBUG, channel, &value))
    {
        adc_release(ADC_OWNER_DEBUG);
        return 0U;
    }

    adc_release(ADC_OWNER_DEBUG);
    return value;
}


/**************************************************************************************************
 * @brief Start one interrupt-driven ADC conversion for the current owner.
 *
 * @param owner Calling owner.
 * @param channel ADC channel number.
 * @param callback Completion callback.
 * @param context Opaque callback context pointer.
 *
 * @return True when the conversion was started.
 **************************************************************************************************/
bool adc_start_conversion(adc_owner_t owner,
                          uint8_t channel,
                          adc_callback_t callback,
                          void *context)
{
    bool started;

    if ((owner <= ADC_OWNER_NONE) || (owner >= ADC_OWNER_COUNT) || (callback == NULL))
    {
        return false;
    }

    started = false;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if ((adc_runtime.owner == (uint8_t)owner) && (adc_runtime.conversion_in_progress == false))
        {
            adc_runtime.callback = callback;
            adc_runtime.callback_context = context;
            adc_runtime.conversion_in_progress = true;
            adc_select_channel(channel);
            ADCSRA = (ADCSRA & (uint8_t)~(1U << ADIF)) | (1U << ADIE) | (1U << ADSC);
            started = true;
        }
    }

    return started;
}


// ======================================================================================
//  INTERNAL HELPERS
// ======================================================================================

/**************************************************************************************************
 * @brief Select one ADC input channel.
 **************************************************************************************************/
static void adc_select_channel(uint8_t channel)
{
    ADMUX = (uint8_t)(ADC_REFERENCE_AVCC_BITS | (channel & 0x07U));
    ADCSRB = (uint8_t)((ADCSRB & (uint8_t)~(1U << MUX5)) |
                       (uint8_t)((((channel >> 3U) & 0x01U) << MUX5)));
}


// ======================================================================================
//  ISR
// ======================================================================================

/**************************************************************************************************
 * @brief ADC conversion-complete ISR.
 **************************************************************************************************/
ISR(ADC_vect)
{
    adc_callback_t callback;
    void *context;
    uint16_t sample;

    sample = ADC;
    ADCSRA &= (uint8_t)~(1U << ADIE);

    callback = adc_runtime.callback;
    context = adc_runtime.callback_context;
    adc_runtime.callback = NULL;
    adc_runtime.callback_context = NULL;
    adc_runtime.conversion_in_progress = false;

    if (callback != NULL)
    {
        callback(sample, context);
    }
}
