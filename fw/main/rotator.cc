#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>

#include "rotator.h"

#define AZI_DEADBAND 1.0f /* degrees */
#define ELE_DEADBAND 1.0f /* degrees */
#define AZI_SPEED 10.0f /* degrees/second */
#define ELE_SPEED 10.0f /* degrees/second */

#define ROTATOR_PIN_UP GPIO_NUM_14
#define ROTATOR_PIN_DOWN GPIO_NUM_27
#define ROTATOR_PIN_LEFT GPIO_NUM_13
#define ROTATOR_PIN_RIGHT GPIO_NUM_12

static const char *TAG = "rotator";

static void IRAM_ATTR
timer_group0_isr(void *param)
{
    timer_spinlock_take(TIMER_GROUP_0);
    RotatorAxis *axes = (RotatorAxis *)param;

    for (int i=0; i < 2; i++)
    {
        axes[i].adc_isr();
    }

    timer_group_enable_alarm_in_isr(TIMER_GROUP_0, TIMER_0);
    timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
    timer_spinlock_give(TIMER_GROUP_0);
}

void IRAM_ATTR RotatorAxis::adc_isr()
{
     adc += adc1_get_raw(adc_channel);
     adc_count ++;
     if (adc_count >= OVERSAMPLING)
     {
         position = adc;
         adc = 0;
         adc_count = 0;
     }
};

static void rotator_task(void *pvParameters)
{
    Rotator *rotator = (Rotator *)pvParameters;
    rotator->run_task();
}

Rotator::Rotator()
    : scales({
        RotatorScale(0, 4095*OVERSAMPLING, 0, 1800),
        RotatorScale(0, 4095*OVERSAMPLING, 0, 3600),
    })
    , axes({
        RotatorAxis(ROTATOR_PIN_UP, ROTATOR_PIN_DOWN, ADC1_CHANNEL_0, scales[0]),
        RotatorAxis(ROTATOR_PIN_LEFT, ROTATOR_PIN_RIGHT, ADC1_CHANNEL_1, scales[1]),
    })
{
    xTaskCreate(rotator_task, "rotator", 4096, this, 5, NULL);
}

void Rotator::set_position(float azi, float ele)
{
}

void Rotator::get_position(float *azi, float *ele)
{
}

void Rotator::run_task()
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1<<ROTATOR_PIN_UP)|(1<<ROTATOR_PIN_DOWN)|(1<<ROTATOR_PIN_LEFT)|(1<<ROTATOR_PIN_RIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_3,ADC_ATTEN_DB_11);

    timer_config_t timer_cfg = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .divider = TIMER_CLOCK_DIVIDER,
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &timer_cfg);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, (TIMER_CLOCK_FREQ/TIMER_CLOCK_DIVIDER)/(UPDATE_RATE*OVERSAMPLING));
    ESP_LOGI(TAG, "ALARM %d", (TIMER_CLOCK_FREQ/TIMER_CLOCK_DIVIDER)/(UPDATE_RATE*OVERSAMPLING));
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_group0_isr,
                       (void *) axes, ESP_INTR_FLAG_IRAM, NULL);
    timer_start(TIMER_GROUP_0, TIMER_0);

    while(1)
    {
        vTaskDelay(1000 / (portTICK_PERIOD_MS));
        ESP_LOGI(TAG, "ele=%1.1f azi=%1.1f", axes[0].get_position()/10.0, axes[1].get_position()/10.0);
#if 0
        gpio_set_level(ROTATOR_PIN_UP, 1);
        vTaskDelay(1000 / (portTICK_PERIOD_MS));

        gpio_set_level(ROTATOR_PIN_UP, 0);
        vTaskDelay(1000 / (portTICK_PERIOD_MS));

        gpio_set_level(ROTATOR_PIN_DOWN, 1);
        vTaskDelay(1000 / (portTICK_PERIOD_MS));

        gpio_set_level(ROTATOR_PIN_DOWN, 0);
        vTaskDelay(1000 / (portTICK_PERIOD_MS));
#endif
    }
}
