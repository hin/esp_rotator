#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>

#include "rotator.h"

#define ROTATOR_PIN_UP GPIO_NUM_14
#define ROTATOR_PIN_DOWN GPIO_NUM_27
#define ROTATOR_PIN_LEFT GPIO_NUM_13
#define ROTATOR_PIN_RIGHT GPIO_NUM_12

static const char *TAG = "rotator";

void RotatorAxis::poll()
{
    sample += adc1_get_raw(adc_channel);
    if (sample_count++ >= OVERSAMPLING)
    {
        current_position = scale.convert(sample);
        //ESP_LOGI(TAG, "%s: ADC=%d POS=%d (%d) channel=%d", name.c_str(), sample, current_position, wanted_position, adc_channel);
        sample = 0;
        sample_count = 0;

        switch (state)
        {
        case STOPPED:
            gpio_set_level(pin_more, 0);
            gpio_set_level(pin_less, 0);
            break;
        case TRACKING:
            if ((current_position - start_threshold) > wanted_position)
            {
                gpio_set_level(pin_more, 0);
                gpio_set_level(pin_less, 1);
                ESP_LOGI(TAG, "%s: turning less %d -> %d", name.c_str(), current_position, wanted_position);
                state = RUNNING_LESS;
            }
            else if ((current_position + start_threshold) < wanted_position)
            {
                gpio_set_level(pin_more, 1);
                gpio_set_level(pin_less, 0);
                ESP_LOGI(TAG, "%s: turning more %d -> %d", name.c_str(), current_position, wanted_position);
                state = RUNNING_MORE;
            }
            break;
        case RUNNING_MORE:
            if ((current_position + stop_threshold) > wanted_position)
            {
                gpio_set_level(pin_more, 0);
                gpio_set_level(pin_less, 0);
                ESP_LOGI(TAG, "%s: turning more done %d -> %d", name.c_str(), current_position, wanted_position);
                state = TRACKING;
            }
            break;
        case RUNNING_LESS:
            if ((current_position - stop_threshold) < wanted_position)
            {
                gpio_set_level(pin_more, 0);
                gpio_set_level(pin_less, 0);
                ESP_LOGI(TAG, "%s: turning less done %d -> %d", name.c_str(), current_position, wanted_position);
                state = TRACKING;
            }
            break;
        }

    }

};

int RotatorAxis::get_current_position()
{
    return current_position;
};

void RotatorAxis::set_wanted_position(int position)
{
    wanted_position = position;
    state = TRACKING;
};

void RotatorAxis::stop() {
    state = STOPPED;
    gpio_set_level(pin_more, 0);
    gpio_set_level(pin_less, 0);
}

static void rotator_task(void *pvParameters)
{
    Rotator *rotator = (Rotator *)pvParameters;
    rotator->run_task();
}

Rotator::Rotator(RotatorScale &azimuth_scale, RotatorScale &elevation_scale)
    : azimuth_scale(azimuth_scale)
    , elevation_scale(elevation_scale)
    , azimuth_axis("azimuth", ROTATOR_PIN_RIGHT, ROTATOR_PIN_LEFT, ADC1_CHANNEL_3, 300, 100, azimuth_scale)
    , elevation_axis("elevation", ROTATOR_PIN_DOWN, ROTATOR_PIN_UP, ADC1_CHANNEL_0, 300, 100, elevation_scale)
{
    xTaskCreate(rotator_task, "rotator", 4096, this, 5, NULL);
}

void Rotator::set_position(float azi, float ele)
{
    ESP_LOGI(TAG, "Set position: %1.2f/%1.2f", azi, ele);
    azimuth_axis.set_wanted_position(azi*100);
    elevation_axis.set_wanted_position(ele*100);
}

void Rotator::get_position(float *azi, float *ele)
{
    *azi = azimuth_axis.get_current_position()/100.0;
    *ele = elevation_axis.get_current_position()/100.0;
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

    while(1)
    {
        vTaskDelay((1000 / (UPDATE_RATE*OVERSAMPLING)) / (portTICK_PERIOD_MS));
        azimuth_axis.poll();
        elevation_axis.poll();
        //ESP_LOGI(TAG, "azi=%d", azimuth_axis.get_current_position());
    }
}
