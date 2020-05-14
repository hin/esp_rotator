#ifndef __ROTATOR_H_INCLUDED__
#define __ROTATOR_H_INCLUDED__

#include <driver/gpio.h>
#include <driver/adc.h>
#include <driver/timer.h>

#define OVERSAMPLING 16
#define UPDATE_RATE 50 /* Hz */
#define TIMER_CLOCK_FREQ 80000000
#define TIMER_CLOCK_DIVIDER 80

class RotatorScale
{
public:
    RotatorScale(int32_t adc_min, int32_t adc_max, int32_t degree_min, int32_t degree_max)
        : adc_min(adc_min)
        , adc_max(adc_max)
        , degree_min(degree_min)
        , degree_max(degree_max)
        {};
    int convert(int val)
    {
        if (val <= adc_min)
            return degree_min;
        if (val >= adc_max)
            return degree_max;
        return ((degree_max-degree_min) * (val - adc_min)) / (adc_max-adc_min) + degree_min;
    };
private:
    int32_t adc_min;
    int32_t adc_max;
    int32_t degree_min;
    int32_t degree_max;
};

class RotatorAxis
{
public:
    RotatorAxis(gpio_num_t pin_more, gpio_num_t pin_less,
                adc1_channel_t adc_channel, RotatorScale &scale)
        : pin_more(pin_more)
        , pin_less(pin_less)
        , adc_channel(adc_channel)
        , scale(scale)
        , adc(0)
        , adc_count(0)
        , position(0)
        { };
    void IRAM_ATTR adc_isr(); /* Called from ISR */
    int get_position() { return scale.convert(position); };
private:
    /* Physical I/O mapping */
    gpio_num_t pin_more;
    gpio_num_t pin_less;
    adc1_channel_t adc_channel;

    RotatorScale &scale;

    int adc;
    int adc_count;

    int position;
};

class Rotator
{
public:
    Rotator();
    void get_position(float *azi, float *ele);
    void set_position(float azi, float ele);
    void run_task();
private:
    RotatorScale scales[2];
    RotatorAxis axes[2];
};

#endif /* ! __ROTATOR_H_INCLUDED__ */
