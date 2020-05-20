#ifndef __ROTATOR_H_INCLUDED__
#define __ROTATOR_H_INCLUDED__

#include <driver/gpio.h>
#include <driver/adc.h>
#include <string>

#define OVERSAMPLING 8
#define UPDATE_RATE 50 /* Hz */

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
    RotatorAxis(std::string name, gpio_num_t pin_more, gpio_num_t pin_less,
                adc1_channel_t adc_channel, int start_threshold, int stop_threshold, RotatorScale &scale)
        : name(name)
        , pin_more(pin_more)
        , pin_less(pin_less)
        , adc_channel(adc_channel)
        , start_threshold(start_threshold)
        , stop_threshold(stop_threshold)
        , scale(scale)
        , state(STOPPED)
        , sample(0)
        , sample_count(0)
        , current_position(0)
        , wanted_position(0)
        { };
    void poll();
    int get_current_position();
    void set_wanted_position(int position);
    void stop();

private:
    enum State {
        STOPPED,
        TRACKING,
        RUNNING_MORE,
        RUNNING_LESS,
    };

    std::string name;

    /* Physical I/O mapping */
    gpio_num_t pin_more;
    gpio_num_t pin_less;
    adc1_channel_t adc_channel;

    int start_threshold;
    int stop_threshold;
    RotatorScale &scale;
    enum State state;
    int sample;
    int sample_count;
    int current_position;
    int wanted_position;
};

class Rotator
{
public:
    Rotator(RotatorScale &azimuth_scale, RotatorScale &elevation_scale);
    void get_position(float *azi, float *ele);
    void set_position(float azi, float ele);
    void run_task();
private:
    RotatorScale &azimuth_scale;
    RotatorScale &elevation_scale;

    RotatorAxis azimuth_axis;
    RotatorAxis elevation_axis;
};

#endif /* ! __ROTATOR_H_INCLUDED__ */
