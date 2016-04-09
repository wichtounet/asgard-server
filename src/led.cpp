//=======================================================================
// Copyright (c) 2015-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <cstddef>

#ifdef __RPI__
#include <wiringPi.h>
#endif

const std::size_t gpio_led_pin = 1;

void set_led_off() {
#ifdef __RPI__
    digitalWrite(gpio_led_pin, LOW);
#endif
}

void set_led_on() {
#ifdef __RPI__
    digitalWrite(gpio_led_pin, HIGH);
#endif
}

void init_led() {
#ifdef __RPI__
    pinMode(gpio_led_pin, OUTPUT);
#endif
}

void setup_led_controller() {
#ifdef __RPI__
    pinMode(gpio_led_pin, OUTPUT);
#endif
}
