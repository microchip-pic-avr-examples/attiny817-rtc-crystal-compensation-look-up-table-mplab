/**
 * \file main.c
 *
 * \author
 *      Microchip Technology: http://www.microchip.com \n
 *      Support at http://www.microchip.com/support/ \n
 *
 * Copyright (C) 2018 Microchip Technology. All rights reserved.
 *
 * \page License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. The name of Microchip may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an
 * Microchip AVR product.
 *
 * THIS SOFTWARE IS PROVIDED BY Microchip "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL Microchip BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *********************************************************
 * Introduction
 * ============
 *
 * This example uses a look-up table to do temperature compensation. The look-up
 * table is built from the manufacturers data on temperature drift. As the values
 * are symmetrical around 25C (parabolic curve), only values for 25C and higher are
 * used, to reduce data size.
 *
 * There are two tables included in this example, one using 8-bit data, and one with
 * 16-bit data. 8-bit data offers reduced datasize, but it gives lower accuracy. There
 * are also two defined values associated with the table. The 16-bit table is used and
 * the 8-bit table is commented out.
 *
 * The application sets up the ADC to read the internal temperature sensor. The resulting
 * temperature value is used to pick a correction value from the look-up table, which is
 * added the accumulated error.
 *
 * The RTC is configured to give an interrupt every second. In the RTC ISR the correction is
 * applied. When the accumulated error is larger that one (or more) 32.768 kHz clock cycle,
 * the RTC period is reduced by one (or more). Any remaining error unaccounted for is kept
 * until the next correction cycle.
 *
 *
 * Related documents / Application notes
 * -------------------------------------
 *
 * This application is described in the following application note: To be published
 *
 * Supported evaluation kit
 * ------------------------
 *
 *  - ATTiny817-XPRO
 *
 *
 * Running the demo
 * ----------------
 *
 * 1. Press Download Pack and save the .atzip file
 * 2. Import .atzip file into Atmel Studio 7, File->Import->Atmel Start Project.
 * 3. Follow the instructions in the ATtiny817 Xplained Pro User Guide to connect the crystal to the device.
 * 4. Build the project and program the supported evaluation board
 **********************************************************
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <stdlib.h>

volatile int32_t  temperature;
int8_t            sigrow_offset;
uint8_t           sigrow_gain;
volatile uint16_t compensation      = 0;
volatile uint16_t accumulated_error = 0;
#define ONE_SECOND_TICK_COUNT 32768

// As the table starts at 25C an offset is needed. A temperature of 25C should give index
// 0. In addition, a conversion from Kelvin to Celsius must be done.
#define TABLE_LOOKUP_OFFSET 297

// The coarse error per second is the static error that larger than one 32.768 kHz crystal
// clock period, i.e. any static compensation in crystal clock periods that must be done.
// The static error can e.g. be measured in production.
#define STATIC_COARSE_ERROR_PER_SECOND 2
// This is the top value used for the RTC
// Nr of cycles in one second minus 1 as period is defined as PER value + 1
// In addition static error larger than one cycle is added
#define ONE_SECOND_TICK (ONE_SECOND_TICK_COUNT - 1 + STATIC_COARSE_ERROR_PER_SECOND)

// This section is an 16-bit implementation of a look-up-table
//
// Measured fine error of 9.6us. This value will have to be changed
#define STATIC_FINE_ERROR_PER_SECOND 960
/* Error of 30.52us (One 32,768Hz period)*/
#define MAX_ERROR 3052
// Error due to temperature drift with fidelity of 10^(-8)X. Table starts with 0 representing 25C.
const uint16_t time_err_vs_temperature[] PROGMEM = {
    0,   0,   1,   1,   2,    3,    5,    6,    8,    11,   13,   16,   19,   22,   26,   29,   34,  38,  42,  47,  52,
    58,  63,  69,  75,  82,   89,   96,   103,  110,  118,  126,  134,  143,  152,  161,  170,  179, 189, 199, 210, 220,
    231, 242, 254, 265, 277,  290,  302,  315,  328,  341,  354,  368,  382,  396,  411,  426,  441, 456, 472, 488, 504,
    520, 537, 554, 571, 588,  606,  624,  642,  661,  679,  698,  718,  737,  757,  777,  797,  818, 839, 860, 881, 903,
    925, 947, 969, 992, 1015, 1038, 1062, 1085, 1109, 1134, 1158, 1183, 1208, 1233, 1259, 1285, 1311};

/*
// This section is an 8-bit alternative implementation of the look-up table
// This implementation has slightly lower fidelity but uses less flash

// Error of 30.5us
#define MAX_ERROR 305
// Measured error of 21.1us
#define STATIC_FINE_ERROR_PER_SECOND 96
// Error due to temperature drift with fidelity of 10^(-7)X 
const uint8_t time_err_vs_temperature[] PROGMEM = {0, 0, 0, 0, 0, 0, 0, 1, 1, 1,\
                                                     1, 2, 2, 2, 3, 3, 3, 4, 4, 5,\
                                                     5, 6, 6, 7, 8, 8, 9, 10, 10, 11,\
                                                     12, 13, 13, 14, 15, 16, 17, 18,\
                                                     19, 20, 21, 22, 23, 24, 25, 27,\
                                                     28, 29, 30, 31, 33, 34, 35, 37,\
                                                     38, 40, 41, 43, 44, 46, 47, 49,\
                                                     50, 52, 54, 55, 57, 59, 61, 62,\
                                                     64, 66, 68, 70, 72, 74, 76, 78,\
                                                     80, 82, 84, 86, 88, 90, 92, 95,\
                                                     97, 99, 102, 104, 106, 109, 111,\
                                                     113, 116, 118, 121, 123, 126,\
                                                     128, 131};
*/ // End 8-bit alternative

/** \brief ADC ISR to read temperature and select compensation value
 *
 * Reads out temperature value from temperature sensor and applies calibration algorithm
 * from datasheet to get kelvin. Value is used to select the correct compensation value.
 */
ISR(ADC0_RESRDY_vect)
{
	// Read out ADC high and low bytes
	uint8_t adc_reading_low = ADC0_RESL;
	int16_t adc_reading     = (ADC0_RESH << 8) | adc_reading_low;
	// Perform steps detailed in datasheet for temperature compensation and
	// conversion to kelvins
	temperature = (adc_reading - sigrow_offset);
	temperature = temperature * sigrow_gain;
	temperature += 0x80;
	temperature >>= 8;

	// Use the lookup-table to find crystal correction values for the measured temperature
	// Using a pointer with offset is needed as workaround to GCC bug
	uint16_t const *ptr_to_data = time_err_vs_temperature + (MAPPED_PROGMEM_START / sizeof(*ptr_to_data));
	compensation                = ptr_to_data[abs((int16_t)temperature - TABLE_LOOKUP_OFFSET)];
}

/*
 * RTC ISR handles calculation and application of compensation. It is run every second. Any error
 * than is less than one 32.768 kHz clock cycle cannot be compensated for, and is kept until next
 * second.
 */
ISR(RTC_CNT_vect)
{
	// Start a new ADC conversion of temperature
	ADC0_COMMAND = ADC_STCONV_bm;
	// Initialize the compensation_cycles variable used to count how
	// many cycles should be subtracted from the RTC period register
	uint8_t compensation_cycles = 0;
	// Add the static error plus the temperature compensation error to the accumulated error.
	accumulated_error = accumulated_error + STATIC_FINE_ERROR_PER_SECOND + compensation;
	// Find how many cycles to subtract from the RTC period register
	while (accumulated_error > MAX_ERROR) {
		compensation_cycles++;
		// When the loop is done accumulated_error will contain the remainder of the error
		// that is not yet compensated for.
		accumulated_error = accumulated_error - MAX_ERROR;
	}
	RTC_PER = ONE_SECOND_TICK - compensation_cycles;
	// Clear RTC interrupt flag
	RTC_INTFLAGS = RTC_OVF_bm;
}

void temperature_drift_compensation()
{
	// Read temperature calibration values from signature row
	sigrow_offset = SIGROW_TEMPSENSE1;
	sigrow_gain   = SIGROW_TEMPSENSE0;
	/*
	 * The crystal is started at the beginning of main in order to give the crystal time to start.
	 * The startup time of the crystal is set to the max value to give the crystal time to settle.
	 * The startup time is based on counting 64K cycles of the crystal. With this startup time it
	 * will take approximately 2 seconds before the crystal is ready to be used by peripherals.
	 *
	 * The run in standby bit is set for the crystal. This will allow the crystal to run in standby
	 * sleep mode. In addition it will allow the crystal to start up even if no peripherals are
	 * requesting the clock.
	 */
	_PROTECTED_WRITE(CLKCTRL_XOSC32KCTRLA, CLKCTRL_ENABLE_bm | CLKCTRL_RUNSTDBY_bm | CLKCTRL_CSUT_64K_gc);

	// Enable global interrupts
	sei();

	// ADC
	// Configure reference, use 1.1V internal
	VREF_CTRLA = VREF_ADC0REFSEL_1V1_gc;
	// Positive input: temperature sensor
	ADC0_MUXPOS = ADC_MUXPOS_TEMPSENSE_gc;
	// Enable interrupt on result ready
	ADC0_INTCTRL = ADC_RESRDY_bm;
	// Enable reduced samp cap and prescale the clock by 16
	ADC0_CTRLC = ADC_SAMPCAP_bm | ADC_PRESC_DIV16_gc;
	// Enable the ADC
	ADC0_CTRLA = ADC_ENABLE_bm;
	// Enable 32 cycles startup delay
	ADC0_CTRLD = ADC_INITDLY_DLY32_gc;
	// Extend sampling length by 2
	ADC0_SAMPCTRL = 0x2 & ADC_SAMPLEN_gm;

	// RTC
	// The RTC_STATUS needs to be 0 before writing to the RTC (could be used for start-up time).
	while (RTC.STATUS != 0) {
	}
	// Select 32.768 kHz external crystal as clock source
	RTC_CLKSEL = RTC_CLKSEL_TOSC32K_gc;
	// Set period value to 1 sec
	RTC_PER = ONE_SECOND_TICK_COUNT;
	// Enable interrupt on overflow
	RTC_INTCTRL = RTC_OVF_bm;
	// Enable RTC
	RTC_CTRLA = RTC_RTCEN_bm;
}

int main(void)
{
	temperature_drift_compensation();
	while (1)
		;
}
