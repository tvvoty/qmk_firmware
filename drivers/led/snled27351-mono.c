/* Copyright 2021 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "snled27351-mono.h"
#include "i2c_master.h"

#define SNLED27351_PWM_REGISTER_COUNT 192
#define SNLED27351_LED_CONTROL_REGISTER_COUNT 24

#ifndef SNLED27351_I2C_TIMEOUT
#    define SNLED27351_I2C_TIMEOUT 100
#endif

#ifndef SNLED27351_I2C_PERSISTENCE
#    define SNLED27351_I2C_PERSISTENCE 0
#endif

#ifndef SNLED27351_PHASE_CHANNEL
#    define SNLED27351_PHASE_CHANNEL SNLED27351_SCAN_PHASE_12_CHANNEL
#endif

#ifndef SNLED27351_CURRENT_TUNE
#    define SNLED27351_CURRENT_TUNE \
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
#endif

uint8_t i2c_transfer_buffer[20];

// These buffers match the SNLED27351 PWM registers.
// The control buffers match the PG0 LED On/Off registers.
// Storing them like this is optimal for I2C transfers to the registers.
// We could optimize this and take out the unused registers from these
// buffers and the transfers in snled27351_write_pwm_buffer() but it's
// probably not worth the extra complexity.
uint8_t g_pwm_buffer[SNLED27351_DRIVER_COUNT][SNLED27351_PWM_REGISTER_COUNT];
bool    g_pwm_buffer_update_required[SNLED27351_DRIVER_COUNT] = {false};

uint8_t g_led_control_registers[SNLED27351_DRIVER_COUNT][SNLED27351_LED_CONTROL_REGISTER_COUNT] = {0};
bool    g_led_control_registers_update_required[SNLED27351_DRIVER_COUNT]                        = {false};

bool snled27351_write_register(uint8_t addr, uint8_t reg, uint8_t data) {
    // If the transaction fails function returns false.
    i2c_transfer_buffer[0] = reg;
    i2c_transfer_buffer[1] = data;

#if SNLED27351_I2C_PERSISTENCE > 0
    for (uint8_t i = 0; i < SNLED27351_I2C_PERSISTENCE; i++) {
        if (i2c_transmit(addr << 1, i2c_transfer_buffer, 2, SNLED27351_I2C_TIMEOUT) != 0) {
            return false;
        }
    }
#else
    if (i2c_transmit(addr << 1, i2c_transfer_buffer, 2, SNLED27351_I2C_TIMEOUT) != 0) {
        return false;
    }
#endif
    return true;
}

void snled27351_select_page(uint8_t addr, uint8_t page) {
    snled27351_write_register(addr, SNLED27351_REG_COMMAND, page);
}

bool snled27351_write_pwm_buffer(uint8_t addr, uint8_t *pwm_buffer) {
    // Assumes PG1 is already selected.
    // If any of the transactions fails function returns false.
    // Transmit PWM registers in 12 transfers of 16 bytes.
    // i2c_transfer_buffer[] is 20 bytes

    // Iterate over the pwm_buffer contents at 16 byte intervals.
    for (int i = 0; i < SNLED27351_PWM_REGISTER_COUNT; i += 16) {
        i2c_transfer_buffer[0] = i;
        // Copy the data from i to i+15.
        // Device will auto-increment register for data after the first byte
        // Thus this sets registers 0x00-0x0F, 0x10-0x1F, etc. in one transfer.
        for (int j = 0; j < 16; j++) {
            i2c_transfer_buffer[1 + j] = pwm_buffer[i + j];
        }

#if SNLED27351_I2C_PERSISTENCE > 0
        for (uint8_t i = 0; i < SNLED27351_I2C_PERSISTENCE; i++) {
            if (i2c_transmit(addr << 1, i2c_transfer_buffer, 17, SNLED27351_I2C_TIMEOUT) != 0) {
                return false;
            }
        }
#else
        if (i2c_transmit(addr << 1, i2c_transfer_buffer, 17, SNLED27351_I2C_TIMEOUT) != 0) {
            return false;
        }
#endif
    }
    return true;
}

void snled27351_init_drivers(void) {
    i2c_init();

    snled27351_init(SNLED27351_I2C_ADDRESS_1);
#if defined(SNLED27351_I2C_ADDRESS_2)
    snled27351_init(SNLED27351_I2C_ADDRESS_2);
#    if defined(SNLED27351_I2C_ADDRESS_3)
    snled27351_init(SNLED27351_I2C_ADDRESS_3);
#        if defined(SNLED27351_I2C_ADDRESS_4)
    snled27351_init(SNLED27351_I2C_ADDRESS_4);
#        endif
#    endif
#endif

    for (int i = 0; i < SNLED27351_LED_COUNT; i++) {
        snled27351_set_led_control_register(i, true);
    }

    snled27351_update_led_control_registers(SNLED27351_I2C_ADDRESS_1, 0);
#if defined(SNLED27351_I2C_ADDRESS_2)
    snled27351_update_led_control_registers(SNLED27351_I2C_ADDRESS_2, 1);
#    if defined(SNLED27351_I2C_ADDRESS_3)
    snled27351_update_led_control_registers(SNLED27351_I2C_ADDRESS_3, 2);
#        if defined(SNLED27351_I2C_ADDRESS_4)
    snled27351_update_led_control_registers(SNLED27351_I2C_ADDRESS_4, 3);
#        endif
#    endif
#endif
}

void snled27351_init(uint8_t addr) {
    snled27351_select_page(addr, SNLED27351_COMMAND_FUNCTION);

    // Setting LED driver to shutdown mode
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_SOFTWARE_SHUTDOWN, SNLED27351_SOFTWARE_SHUTDOWN_SSD_SHUTDOWN);
    // Setting internal channel pulldown/pullup
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_PULLDOWNUP, SNLED27351_PULLDOWNUP_ALL_ENABLED);
    // Select number of scan phase
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_SCAN_PHASE, SNLED27351_PHASE_CHANNEL);
    // Setting PWM Delay Phase
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_SLEW_RATE_CONTROL_MODE_1, SNLED27351_SLEW_RATE_CONTROL_MODE_1_PDP_ENABLE);
    // Setting Driving/Sinking Channel Slew Rate
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_SLEW_RATE_CONTROL_MODE_2, SNLED27351_SLEW_RATE_CONTROL_MODE_2_DSL_ENABLE | SNLED27351_SLEW_RATE_CONTROL_MODE_2_SSL_ENABLE);
    // Setting Iref
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_SOFTWARE_SLEEP, 0);

    snled27351_select_page(addr, SNLED27351_COMMAND_LED_CONTROL);

    for (int i = 0; i < SNLED27351_LED_CONTROL_ON_OFF_LENGTH; i++) {
        snled27351_write_register(addr, i, 0x00);
    }

    snled27351_select_page(addr, SNLED27351_COMMAND_PWM);

    for (int i = 0; i < SNLED27351_LED_CURRENT_TUNE_LENGTH; i++) {
        snled27351_write_register(addr, i, 0x00);
    }

    snled27351_select_page(addr, SNLED27351_COMMAND_CURRENT_TUNE);

    uint8_t current_tune_reg_list[SNLED27351_LED_CURRENT_TUNE_LENGTH] = SNLED27351_CURRENT_TUNE;
    for (int i = 0; i < SNLED27351_LED_CURRENT_TUNE_LENGTH; i++) {
        snled27351_write_register(addr, i, current_tune_reg_list[i]);
    }

    snled27351_select_page(addr, SNLED27351_COMMAND_LED_CONTROL);

    // Enable LEDs ON/OFF
    for (int i = 0; i < SNLED27351_LED_CONTROL_ON_OFF_LENGTH; i++) {
        snled27351_write_register(addr, i, 0xFF);
    }

    snled27351_select_page(addr, SNLED27351_COMMAND_FUNCTION);

    // Setting LED driver to normal mode
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_SOFTWARE_SHUTDOWN, SNLED27351_SOFTWARE_SHUTDOWN_SSD_NORMAL);
}

void snled27351_set_value(int index, uint8_t value) {
    snled27351_led_t led;
    if (index >= 0 && index < SNLED27351_LED_COUNT) {
        memcpy_P(&led, (&g_snled27351_leds[index]), sizeof(led));

        if (g_pwm_buffer[led.driver][led.v] == value) {
            return;
        }
        g_pwm_buffer[led.driver][led.v]          = value;
        g_pwm_buffer_update_required[led.driver] = true;
    }
}

void snled27351_set_value_all(uint8_t value) {
    for (int i = 0; i < SNLED27351_LED_COUNT; i++) {
        snled27351_set_value(i, value);
    }
}

void snled27351_set_led_control_register(uint8_t index, bool value) {
    snled27351_led_t led;
    memcpy_P(&led, (&g_snled27351_leds[index]), sizeof(led));

    uint8_t control_register = led.v / 8;
    uint8_t bit_value        = led.v % 8;

    if (value) {
        g_led_control_registers[led.driver][control_register] |= (1 << bit_value);
    } else {
        g_led_control_registers[led.driver][control_register] &= ~(1 << bit_value);
    }

    g_led_control_registers_update_required[led.driver] = true;
}

void snled27351_update_pwm_buffers(uint8_t addr, uint8_t index) {
    if (g_pwm_buffer_update_required[index]) {
        snled27351_select_page(addr, SNLED27351_COMMAND_PWM);

        // If any of the transactions fail we risk writing dirty PG0,
        // refresh page 0 just in case.
        if (!snled27351_write_pwm_buffer(addr, g_pwm_buffer[index])) {
            g_led_control_registers_update_required[index] = true;
        }
    }
    g_pwm_buffer_update_required[index] = false;
}

void snled27351_update_led_control_registers(uint8_t addr, uint8_t index) {
    if (g_led_control_registers_update_required[index]) {
        snled27351_select_page(addr, SNLED27351_COMMAND_LED_CONTROL);

        for (int i = 0; i < SNLED27351_LED_CONTROL_REGISTER_COUNT; i++) {
            snled27351_write_register(addr, i, g_led_control_registers[index][i]);
        }
    }
    g_led_control_registers_update_required[index] = false;
}

void snled27351_flush(void) {
    snled27351_update_pwm_buffers(SNLED27351_I2C_ADDRESS_1, 0);
#if defined(SNLED27351_I2C_ADDRESS_2)
    snled27351_update_pwm_buffers(SNLED27351_I2C_ADDRESS_2, 1);
#    if defined(SNLED27351_I2C_ADDRESS_3)
    snled27351_update_pwm_buffers(SNLED27351_I2C_ADDRESS_3, 2);
#        if defined(SNLED27351_I2C_ADDRESS_4)
    snled27351_update_pwm_buffers(SNLED27351_I2C_ADDRESS_4, 3);
#        endif
#    endif
#endif
}

void snled27351_sw_return_normal(uint8_t addr) {
    snled27351_select_page(addr, SNLED27351_COMMAND_FUNCTION);

    // Setting LED driver to normal mode
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_SOFTWARE_SHUTDOWN, SNLED27351_SOFTWARE_SHUTDOWN_SSD_NORMAL);
}

void snled27351_sw_shutdown(uint8_t addr) {
    snled27351_select_page(addr, SNLED27351_COMMAND_FUNCTION);

    // Setting LED driver to shutdown mode
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_SOFTWARE_SHUTDOWN, SNLED27351_SOFTWARE_SHUTDOWN_SSD_SHUTDOWN);
    // Write SW Sleep Register
    snled27351_write_register(addr, SNLED27351_FUNCTION_REG_SOFTWARE_SLEEP, SNLED27351_SOFTWARE_SLEEP_ENABLE);
}
