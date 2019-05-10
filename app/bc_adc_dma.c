#include "bc_adc_dma.h"



//#define _MIC_NUMBER_OF_SAMPLES 4096
#define _MIC_TIMER_PERIOD_DEBUG 0

typedef enum
{
    MIC_STATE_READY = 0,
    MIC_STATE_ENABLE = 1,
    MIC_STATE_MEASURE = 2,
    MIC_STATE_UPDATE = 3,
    MIC_STATE_ERROR = 4

} mic_state_t;

typedef struct
{
    uint8_t *buffer;
    size_t buffer_size;

    bc_adc_channel_t adc_channel;
    mic_state_t state;
    bc_tick_t timeout;
    void (*update_callback)(void);
    void (*error_callback)(void);
    bool circular;

    uint16_t prescaler;
    uint16_t autoreload;

    bc_scheduler_task_id_t task_id;

} mic_t;

static mic_t _mic;

void mic_task(void *param);
static void _mic_adc_init(void);
static void _mic_tim6_init(uint16_t prescaler, uint16_t autoreload);
static void _mic_dma_init(void);
static bool _mic_adc_calibration(void);

void mic_init(uint8_t *buffer, size_t buffer_size)
{
    memset(&_mic, 0, sizeof(_mic));

    _mic.buffer = buffer;
    _mic.buffer_size = buffer_size;

    _mic_adc_init();
    _mic_adc_calibration();

    _mic.task_id = bc_scheduler_register(mic_task, NULL, 0);

}

void mic_task(void *param)
{
    switch(_mic.state)
    {
        case MIC_STATE_READY:
        {
            break;
        }
        case MIC_STATE_MEASURE:
        {
            break;
        }
        case MIC_STATE_ENABLE:
        {
            if (_mic.timeout < bc_tick_get())
            {
                _mic.timeout = 0;

                bc_scheduler_disable_sleep();

                _mic_tim6_init(_mic.prescaler, _mic.autoreload);

                // Reinit the DMA
                _mic_dma_init();

                // Set ADC channel 0
                ADC1->CHSELR = 1 << _mic.adc_channel;

                // Enable DMA - this must be set after calibration phase!
                ADC1->CFGR1 |= ADC_CFGR1_DMAEN;

                if (_mic.circular)
                {
                    ADC1->CFGR1 |= ADC_CFGR1_DMACFG;
                }

                // Configure trigger by timer TRGO signal
                // Rising edge, TRG0 (TIM6_TRGO)
                ADC1->CFGR1 |= ADC_CFGR1_EXTEN_0;

                // Start the AD measurement
                ADC1->CR |= ADC_CR_ADSTART;

                // Start sampling timer
                TIM6->CR1 |= TIM_CR1_CEN;

                _mic.state = MIC_STATE_MEASURE;
            }

            break;
        }
        case MIC_STATE_UPDATE:
        {
            _mic.state = MIC_STATE_READY;

            if (_mic.update_callback != NULL)
            {
                _mic.update_callback();
            }
            break;
        }
        case MIC_STATE_ERROR:
        {
            _mic.state = MIC_STATE_READY;

            if (_mic.error_callback != NULL)
            {
                _mic.error_callback();
            }
            break;
        }
        default:
        {
            break;
        }
    }

    bc_scheduler_plan_current_relative(10);
}

static bool _mic_adc_calibration(void)
{
    if ((ADC1->CR & ADC_CR_ADEN) == 0)
    {
        return false;
    }

    // Perform ADC calibration
    ADC1->CR |= ADC_CR_ADCAL;

    while ((ADC1->ISR & ADC_ISR_EOCAL) == 0)
    {
        continue;
    }

    return true;
}

bool mic_measure(bc_adc_channel_t adc_channel, uint16_t prescaler, uint16_t autoreload)
{
    if (_mic.state != MIC_STATE_READY)
    {
        return false;
    }

    _mic.state = MIC_STATE_ENABLE;

    _mic.prescaler = prescaler;
    _mic.autoreload = autoreload;

    _mic.timeout = bc_tick_get() + 500;
    _mic.adc_channel = adc_channel;
    _mic.circular = false;

    return true;
}

bool mic_measure_circular(bc_adc_channel_t adc_channel, uint16_t prescaler, uint16_t autoreload)
{
    if (_mic.state != MIC_STATE_READY)
    {
        return false;
    }

    _mic.state = MIC_STATE_ENABLE;

    _mic.prescaler = prescaler;
    _mic.autoreload = autoreload;

    _mic.timeout = bc_tick_get() + 500;
    _mic.adc_channel = adc_channel;
    _mic.circular = true;

    return true;
}


static void _mic_tim6_init(uint16_t prescaler, uint16_t autoreload)
{
    // Enable TIM6 clock
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;

    // Errata workaround
    RCC->APB1ENR;

    // Disable counter if it is running
    TIM6->CR1 &= ~TIM_CR1_CEN;

    // Set prescaler
    //TIM6->PSC = 10 - 1;
    TIM6->PSC = prescaler - 1;

    // Set auto-reload
    //TIM6->ARR = 100 - 1;
    TIM6->ARR = autoreload - 1;

    // Configure update event TRGO to ADC
    TIM6->CR2 |= TIM_CR2_MMS_1;

#if _MIC_TIMER_PERIOD_DEBUG
    // Debug IRQ
    TIM6->DIER |= TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
#endif
}

#if _MIC_TIMER_PERIOD_DEBUG
// Debug the period
void TIM6_IRQHandler(void)
{
    TIM6->SR &= ~TIM_SR_UIF;

    static uint8_t toggle = false;

    if (toggle)
    {
        GPIOH->BSRR = GPIO_BSRR_BS_1;
    } else {
        GPIOH->BSRR = GPIO_BSRR_BR_1;
    }
    toggle = !toggle;
}
#endif

void DMA1_Channel1_IRQHandler(void)
{
    if ((DMA1->ISR & DMA_ISR_GIF1) != 0)
    {
        if ((DMA1->ISR & DMA_ISR_TEIF1) != 0)
        {
            // DMA Error
            _mic.state = MIC_STATE_ERROR;

            //bc_scheduler_enable_sleep();
        }
        else if ((DMA1->ISR & DMA_ISR_TCIF1) != 0)
        {
            // DMA Transfer complete
            _mic.state = MIC_STATE_UPDATE;

            if (!_mic.circular)
            {
                // Stop sampling timer
                TIM6->CR1 &= ~TIM_CR1_CEN;
                bc_scheduler_enable_sleep();
            }

            // Clear DMA flag
            DMA1->IFCR |= DMA_IFCR_CTCIF1;

            //bc_scheduler_enable_sleep();
        }
        else if ((DMA1->ISR & DMA_ISR_HTIF1) != 0)
        {
            // Clear DMA flag
            DMA1->IFCR |= DMA_IFCR_CHTIF1;
        }
    }
}

static void _mic_adc_init(void)
{
    // Enable ADC clock
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    // Errata workaround
    RCC->APB2ENR;

    // Set auto-off mode, right align
    ADC1->CFGR1 = ADC_CFGR1_AUTOFF;

    // Set 8bit resolution
    ADC1->CFGR1 |= ADC_CFGR1_RES_1;

    // Set PCLK as a clock source
    ADC1->CFGR2 = ADC_CFGR2_CKMODE_0 | ADC_CFGR2_CKMODE_1;

    // Sampling time selection (12.5 cycles)
    ADC1->SMPR |= ADC_SMPR_SMP_1 | ADC_SMPR_SMP_0;

    // Enable ADC voltage regulator
    ADC1->CR |= ADC_CR_ADVREGEN;
}

static void _mic_dma_init(void)
{
    // DMA1, Channel 1, Request number 0
    DMA_Channel_TypeDef *dma_channel = DMA1_Channel1;

    // Enable DMA1
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;

    // Errata workaround
    RCC->AHBENR;

    // Disable DMA
    dma_channel->CCR &= ~DMA_CCR_EN;

    // Peripheral to memory
    dma_channel->CCR &= ~DMA_CCR_DIR;

    // Memory data size 8 bits
    dma_channel->CCR &= ~DMA_CCR_MSIZE_Msk;

    // Peripheral data size 8 bits
    dma_channel->CCR &= ~DMA_CCR_PSIZE_Msk;

    // DMA Mode
    if (_mic.circular)
    {
        dma_channel->CCR |= DMA_CCR_CIRC;
    }
    else
    {
        dma_channel->CCR &= ~DMA_CCR_CIRC_Msk;
    }

    // Set memory incrementation
    dma_channel->CCR |= DMA_CCR_MINC;

    // DMA channel selection
    DMA1_CSELR->CSELR &= ~DMA_CSELR_C1S_Msk;

    // Configure DMA channel data length
    dma_channel->CNDTR = _mic.buffer_size;

    // Configure DMA channel source address
    dma_channel->CPAR = (uint32_t) &ADC1->DR;

    // Configure DMA channel destination address
    dma_channel->CMAR = (uint32_t) _mic.buffer;

    // Enable the transfer complete, half transfer and error interrupts
    dma_channel->CCR |= DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE;

    // Enable DMA 1 channel 1 interrupt
    NVIC_SetPriority(DMA1_Channel1_IRQn, 0);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    // Enable DMA
    dma_channel->CCR |= DMA_CCR_EN;
}
