// IRHardwarePhoton.h — on-device IRHardware adapter for the AnalysIR A.IR Shield
// on a Particle Photon (Gen2, STM32F205). Compiled only when PARTICLE is defined;
// host Catch2 builds never see it, so the core stays Device-OS-free.
//
// ============================================================================
// PROVENANCE
// ----------------------------------------------------------------------------
// The carrier-send and capture-ISR hardware bodies in this file are taken
// VERBATIM from the original "AnalysIR Firmware for Photon + A.IR Shield Photon"
// sketch (reference/AnalysIR_AIR_Shield_Photon.ino, dated 24 Nov 2015, v1.0.x).
// The original ran SYSTEM_MODE(MANUAL) with a raw TCP server on port 25 for the
// AnalysIR desktop app.  This production firmware stays cloud-connected
// (SYSTEM_MODE(AUTOMATIC)) and exposes Particle.function/variable surfaces;
// only the hardware bodies (PWM carrier send path and IR-capture ISR/period
// detection) are reused, wrapped inside the IRHardware seam class.
//
// Intentionally NOT copied from the original:
//   * SYSTEM_MODE(MANUAL), TCPServer/TCPClient, heartbeat, WiFi/Serial comms.
//   * startup_AIR_Shield_Photon() — init logic is folded into IRHardwarePhoton::init().
//   * loop() main logic  — replaced by armCapture()/pollCapture() below.
//   * reportPulses()     — reporting is the caller's job, not the hardware layer.
// ============================================================================
#ifndef AUTOMATICA_IR_HARDWARE_PHOTON_H
#define AUTOMATICA_IR_HARDWARE_PHOTON_H

#if defined(PARTICLE) || defined(SPARK)

#include "Particle.h"
#include "IRHardware.h"

#include <vector>

namespace automatica_ir {

// ---- A.IR Shield pin map (Photon) — verbatim from original sketch ----------
// Original defines:
//   #define IR_Rx_PIN D2        — demodulated IR receiver (TSOP-style)
//   #define IR_Mod_PIN D3       — modulated IR receiver (raw carrier), freq detect
//   #define LEDPIN    D7        — on-board status LED
//   #define pinTxIR TX          — IR LED carrier output (timer-capable PWM pin)
static const pin_t IR_Rx_PIN  = D2;   // demodulated RX — edge capture (CHANGE ISR)
static const pin_t IR_Mod_PIN = D3;   // modulated RX   — carrier freq (FALLING ISR)
static const pin_t LEDPIN     = D7;   // status LED
static const pin_t pinTxIR    = TX;   // IR LED carrier output (TX = PA0, TIM1 CH2)

// ---- Capture buffer sizes — verbatim from original -------------------------
// Original: #define modPULSES 256 / #define maxPULSES 1024
#define modPULSES 256
#define maxPULSES 1024

// ---- Capture state (ISR-shared) — verbatim originals as file-scope statics -
// These mirror the original sketch's globals exactly so the ISR bodies can be
// lifted verbatim.
namespace detail {

    // Original globals (declared volatile where original did):
    volatile byte          state      = 127; // current RX pin state (set by ISR)
    byte                   oldState   = 127; // previous state (main-loop driven)
    volatile unsigned long newMicros  = 0;   // time of last RX edge (ISR → loop)
    unsigned long          oldMicros  = 0;   // time of previous RX edge
    unsigned long          oldTime    = 0;   // last time IR was received (usLoop snapshot)

    unsigned short         countD     = 0;   // write pointer into pulseIR[]
    volatile byte          countM     = 0;   // write pointer into modIR[]
    byte                   countM2    = 0;   // sample count for reportPeriod()

    unsigned short         pulseIR[maxPULSES]; // demodulated pulse durations
    volatile byte          modIR[modPULSES];   // modulation timestamps (LSB of micros())

    byte                   j          = 0;
    unsigned int           sum        = 0;
    byte                   sigLen_b   = 0;   // byte-sized sigLen used in reportPeriod

    // Capture-armed flag (not in the original globals; added to gate ISRs during send)
    volatile bool          g_capturing = false;

    // ---- Verbatim ISR bodies from original AnalysIR sketch ------------------

    // rxIR_Interrupt_Handler — D2, CHANGE edge
    // "important to use few instruction cycles here"
    void rxIR_Interrupt_Handler() {
        newMicros = micros();           // record time stamp for main loop
        state = pinReadFast(IR_Rx_PIN); // read changed state of interrupt pin D2
    }

    // rxIR3_Interrupt_Handler — D3, FALLING edge
    // "just continually record the time-stamp, will be mostly modulations"
    // "just save LSB as we are measuring values of 20-50 uSecs only"
    void rxIR3_Interrupt_Handler() {
        modIR[countM++] = (byte)micros(); // save LSB of micros() timestamp
    }

    // reportPeriod — verbatim from original sketch (adapted: sends data via
    // return value + out-param instead of Serial/TCP, since reporting is the
    // caller's responsibility in this architecture).
    // Returns the averaged period in nanoseconds (same as original's sum after
    // the multiply), and sets freqKHz_out to the derived carrier frequency.
    // Returns 0 and freqKHz_out=0 if no valid modulation samples were found.
    int reportPeriod(int& freqKHz_out) {
        sum      = 0;
        sigLen_b = 0;
        countM2  = 0;
        for (j = 1; j < (modPULSES - 1); j++) { // j is byte
            sigLen_b = (byte)(modIR[j] - modIR[j - 1]); // sigLen is byte (LSB diff)
            if (sigLen_b > 50 || sigLen_b < 10) continue; // period range: 10..50 µs
            sum += sigLen_b;
            countM2++;
            modIR[j - 1] = 0; // finished with it so clear for next time
        }
        modIR[j - 1] = 0; // clear last one, which was missed in loop
        if (countM2 == 0) {
            freqKHz_out = 0;
            return 0; // avoid div by zero — nothing to report
        }
        sum = (sum * 1000 + countM2 / 2) / countM2; // get it in nano secs
        // Derive kHz: period_ns → freq_Hz → freq_kHz
        // period_us = sum/1000; freq_Hz = 1e6/period_us = 1e9/sum
        // freq_kHz  = 1e6/sum  (sum is in ns)
        if (sum > 0) freqKHz_out = (int)((1000000UL + sum / 2) / sum);
        else         freqKHz_out = 0;
        return (int)sum; // period in nanoseconds
    }

} // namespace detail

// ---- STM32 PWM carrier globals — verbatim from original sketch --------------
// Original declared these at file scope; they live here as namespace-scope
// statics so the verbatim initPhotonPWM / updateOC / updateCCR / mark / space
// bodies can be lifted unchanged.

static unsigned int  PWM_Freq         = 38000; // default carrier freq (Hz)
static uint16_t      period_pwm       = 0;     // computed TIM period register value
static int           dutyCyclePhoton  = 33;    // default duty cycle %

// Original Timer structures (file-scope in original):
static TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
static NVIC_InitTypeDef        NVIC_InitStructure;
static TIM_OCInitTypeDef       TIM_OCInitStructure;

// ---- Mark / Space timing globals — verbatim from original -------------------
static unsigned long sigTime  = 0; // running end-time cursor for mark/space
// (sigStart is used in some AnalysIR repeat variants; not needed here but
// declared so any future verbatim paste compiles.)
static unsigned long sigStart = 0;

// ---- Verbatim carrier functions from original AnalysIR sketch ---------------

// initPhotonPWM — verbatim body.
// "Assumes Photon device with A.IR Shield Photon from www.AnalysIR.com"
static void initPhotonPWM() {
    period_pwm = (uint16_t)(((SystemCoreClock) / PWM_Freq) - 1);
    /* Compute the pulse value to generate a PWM signal with variable duty cycle */
    int pulse = period_pwm; // set to period to start, which should be 0 duty cycle effectively

    /* GPIOA clock enable */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    /* Initialise GPIO for PWM function */
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure); // Note the use of GPIOA

    // Map the pin to the timer
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource0, GPIO_AF_TIM1);
    /* TIMER 1 clock enable */
    RCC_APB1PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    /* Timer Base configuration */
    TIM_TimeBaseStructure.TIM_Prescaler   = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // symmetrical PWM
    TIM_TimeBaseStructure.TIM_Period      = period_pwm;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure); // TIM1 = Timer 1

    /* Timer 1 Channel 2 PWM output configuration */
    TIM_OCStructInit(&TIM_OCInitStructure);
    // PWMINVERTED=false: PWM2 is normal PWM signal (output LOW on idle)
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM2;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = pulse;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;

    // Channel 2 init timer 1
    TIM_OC2Init(TIM1, &TIM_OCInitStructure);
    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
}

// initPhotonDutyCycle — verbatim body.
static void initPhotonDutyCycle(unsigned char dutyCycle) {
    switch (dutyCycle) {
        case 50: dutyCyclePhoton = 50; break;
        case 40: dutyCyclePhoton = 40; break;
        case 33: dutyCyclePhoton = 33; break;
        case 30: dutyCyclePhoton = 30; break;
        case 25: dutyCyclePhoton = 25; break;
        case 20: dutyCyclePhoton = 20; break;
        case 10: dutyCyclePhoton = 10; break;
        default: dutyCyclePhoton = 50; break; // 50% for any invalid values
    }
}

// updateCCR — verbatim body. Declared before updateOC so updateOC can call it.
static void updateCCR(int pin, int dutyPulse) {
    STM32_Pin_Info* PIN_MAP = HAL_Pin_Map();

    TIM_OCInitStructure.TIM_Pulse = dutyPulse;

    if (PIN_MAP[pin].timer_ch == TIM_Channel_1) {
        PIN_MAP[pin].timer_peripheral->CCR1 = TIM_OCInitStructure.TIM_Pulse;
    } else if (PIN_MAP[pin].timer_ch == TIM_Channel_2) {
        PIN_MAP[pin].timer_peripheral->CCR2 = TIM_OCInitStructure.TIM_Pulse;
    } else if (PIN_MAP[pin].timer_ch == TIM_Channel_3) {
        PIN_MAP[pin].timer_peripheral->CCR3 = TIM_OCInitStructure.TIM_Pulse;
    } else if (PIN_MAP[pin].timer_ch == TIM_Channel_4) {
        PIN_MAP[pin].timer_peripheral->CCR4 = TIM_OCInitStructure.TIM_Pulse;
    }
    // else: no matching TIM channel — silently ignore (original had commented-out debug print)
}

// updateOC — verbatim body. Calls updateCCR (declared above).
static void updateOC(int pin, int percent) {
    int pulse = 0;
    if (percent) {
        pulse = (uint16_t)(period_pwm - (period_pwm + 1) * percent / 100 - 1);
    } else {
        // pulse = period is 0 duty cycle, strange but true
        pulse = period_pwm;
    }
    // Set the CCR to new pulse value
    updateCCR(pin, pulse);
}

// markPhoton — verbatim body.
static void markPhoton(unsigned int mLen) { // uses sigTime as end parameter
    updateOC(pinTxIR, dutyCyclePhoton); // activate carrier
    sigTime += mLen;                    // mark ends at new sigTime
    unsigned long startTime = micros();
    unsigned long dur = sigTime - startTime; // rolling time adjustment for code delays
    if (dur > 0) {
        while ((micros() - startTime) < dur) {} // just wait here until time is up
    }
    // now turn off the carrier at end of mark
    updateOC(pinTxIR, 100); // deactivate carrier
}

// spacePhoton — verbatim body.
static void spacePhoton(unsigned int sLen) { // uses sigTime as end parameter
    sigTime += sLen; // space ends at new sigTime
    unsigned long startTime = micros();
    unsigned long dur = sigTime - startTime; // rolling time adjustment
    if (dur == 0) return;
    while ((micros() - startTime) < dur) ; // just wait here until time is up
}

// ---- The Photon hardware adapter (implements IRHardware) --------------------

class IRHardwarePhoton : public IRHardware {
public:
    IRHardwarePhoton() : rxAttached_(false) {}

    // init — matches startup_AIR_Shield_Photon() logic from original sketch.
    void init() override {
        pinMode(IR_Rx_PIN,  INPUT);
        pinMode(IR_Mod_PIN, INPUT);
        pinMode(LEDPIN,     OUTPUT);
        pinMode(pinTxIR,    OUTPUT);
        analogWrite(pinTxIR, 0); // required for PWM to work (verbatim from original)

        // init PWM stuff first (important) — as early as possible
        initPhotonDutyCycle(dutyCyclePhoton); // usually set once as part of set-up
        initPhotonPWM();                      // initialise PWM but leave dormant
        updateOC(pinTxIR, 100);              // activate carrier (full duty = off in PWM2 mode)
    }

    // sendRaw — send timings[] `repeat` times at freqKHz.
    // detachInterrupt / attachInterrupt wrapping is MANDATORY (PLAN decision).
    void sendRaw(int freqKHz,
                 const std::vector<unsigned int>& timings,
                 int repeat) override {
        if (timings.empty()) return;
        int n = (repeat < 1) ? 1 : repeat;

        detachRx(); // MANDATORY: do not capture our own carrier
        digitalWrite(LEDPIN, HIGH);
        for (int r = 0; r < n; ++r) {
            _AIR_sendRAW(freqKHz, timings);
        }
        digitalWrite(LEDPIN, LOW);
        // RX remains detached; caller calls armCapture() to re-enable.
    }

    // sendSequence — replay an ordered sequence of signals with a gap between them.
    void sendSequence(const std::vector<CaptureResult>& signals,
                      unsigned int gapMicros) override {
        if (signals.empty()) return;

        detachRx(); // MANDATORY
        digitalWrite(LEDPIN, HIGH);
        for (size_t i = 0; i < signals.size(); ++i) {
            _AIR_sendRAW(signals[i].freqKHz, signals[i].timings);
            if (i + 1 < signals.size() && gapMicros > 0) {
                // Inter-signal gap: carrier off, busy-wait in <=16 ms chunks.
                unsigned int remaining = gapMicros;
                while (remaining > 16000U) {
                    delayMicroseconds(16000U);
                    remaining -= 16000U;
                }
                if (remaining) delayMicroseconds(remaining);
            }
        }
        digitalWrite(LEDPIN, LOW);
        // RX remains detached; caller calls armCapture() to re-enable.
    }

    // armCapture — reset all ISR state and re-attach both interrupts.
    bool armCapture() override {
        noInterrupts();
        detail::g_capturing = false;
        // Reset verbatim-original state variables
        detail::state     = 127;
        detail::oldState  = 127;
        detail::newMicros = 0;
        detail::oldMicros = 0;
        detail::oldTime   = 0;
        detail::countD    = 0;
        detail::countM    = 0;
        detail::countM2   = 0;
        detail::sum       = 0;
        detail::j         = 0;
        detail::sigLen_b  = 0;
        interrupts();

        attachRx();

        noInterrupts();
        detail::g_capturing = true;
        // Initialise timing mirrors — verbatim-original setup() pattern:
        //   oldState = digitalRead(IR_Rx_PIN); state = oldState;
        //   newMicros = micros(); oldMicros = newMicros;
        detail::oldState  = (byte)pinReadFast(IR_Rx_PIN);
        detail::state     = detail::oldState;
        detail::newMicros = micros();
        detail::oldMicros = detail::newMicros;
        interrupts();
        return true;
    }

    // cancelCapture — disarm the receiver without delivering a result.
    void cancelCapture() override {
        noInterrupts();
        detail::g_capturing = false;
        detail::countD = 0;
        interrupts();
        detachRx();
    }

    // pollCapture — non-blocking poll. Mirrors the original loop()'s two key blocks:
    //
    //   Block 1 (oldState != state): on each state change, compute delta from
    //     oldMicros to newMicros, handle the >0xFFFF overflow, store into pulseIR[],
    //     encode mark (LSB=1) vs. space (LSB=0) in the stored value.
    //
    //   Block 2 (inter-frame gap): when state is HIGH (space), countD > 0, and
    //     125 ms have elapsed since oldTime, call reportPeriod(), assemble CaptureResult.
    //
    // The function runs both blocks on each call, consistent with the original loop().
    bool pollCapture(CaptureResult& out) override {
        if (!detail::g_capturing) return false;

        unsigned long usLoop = micros(); // mirrors original: usLoop = micros()

        // ---- Block 1: state-change processing (verbatim original logic) ----
        if (detail::oldState != detail::state && detail::countD < maxPULSES) {
            detail::oldState = detail::state;

            unsigned long newM, oldM;
            noInterrupts();
            newM = detail::newMicros;
            interrupts();
            oldM = detail::oldMicros;

            if (detail::oldState) { // mark (HIGH from demodulated TSOP: carrier present)
                detail::sum = (unsigned int)(newM - oldM);
                if (detail::sum > 0xFFFF && detail::countD < (maxPULSES - 1) && detail::countD) {
                    detail::pulseIR[detail::countD++] = 0xFFFF | 0x0001; // overflow mark chunk
                }
                detail::pulseIR[detail::countD++] = (unsigned short)((detail::sum & 0xFFFF) | 0x0001);
            } else {                // space (LOW)
                detail::sum = (unsigned int)(newM - oldM);
                if (detail::sum > 0xFFFF && detail::countD < (maxPULSES - 1) && detail::countD) {
                    detail::pulseIR[detail::countD++] = 0xFFFF & 0xFFFE; // overflow space chunk
                }
                detail::pulseIR[detail::countD++] = (unsigned short)(detail::sum & 0xFFFE);
            }
            detail::oldMicros = newM; // remember for next time
            detail::oldTime   = usLoop; // last time IR was received
        }

        // ---- Block 2: inter-frame-gap completion (verbatim original logic) -
        // Original: state && countD > 0 && (countD == maxPULSES || (usLoop - oldTime) > 125000UL)
        if (detail::state && detail::countD > 0 &&
            (detail::countD == maxPULSES || (usLoop - detail::oldTime) > 125000UL)) {

            detachRx();
            noInterrupts();
            detail::g_capturing = false;
            interrupts();

            // reportPeriod — verbatim, returns period_ns, sets freqKHz_out
            int freqKHz_out = 0;
            detail::reportPeriod(freqKHz_out);

            // Assemble CaptureResult from pulseIR[].
            // The flag-encoded pulseIR[] values need the LSB stripped for timings;
            // the LSB was the mark/space state flag in the original.
            // We present timings as plain µs values (state context is implicit in
            // alternating mark/space ordering guaranteed by the send convention).
            out.freqKHz = freqKHz_out;
            out.timings.clear();
            out.timings.reserve(detail::countD);
            for (unsigned short i = 0; i < detail::countD; ++i) {
                // Strip the state flag bit (LSB): value & 0xFFFE gives the duration.
                out.timings.push_back((unsigned int)(detail::pulseIR[i] & 0xFFFE));
            }

            detail::countD = 0; // reset for next capture
            return true;
        }

        return false;
    }

private:
    // _AIR_sendRAW — verbatim AIR_sendRAW() body from original sketch, adapted to
    // accept freq+timings vector instead of the global pulseIR[]/countD/PWM_Freq.
    // The original's flag-encoding (odd = mark via LSB, >0xFFFF = extend by 0x10000)
    // is preserved; the vector contains plain µs values so the extension is applied
    // by the mark/space selection only (i&1 parity rule, same as original).
    void _AIR_sendRAW(int freqKHz, const std::vector<unsigned int>& timings) {
        // Set carrier frequency then re-initialise PWM (verbatim: initPhotonPWM called)
        PWM_Freq = (unsigned int)((freqKHz > 0 ? freqKHz : 38) * 1000);
        initPhotonPWM(); // init PWM ready for sending with correct carrier frequency

        digitalWriteFast(LEDPIN, !pinReadFast(LEDPIN)); // TOGGLELED

        unsigned long sigLenIR = 0;
        sigTime = micros(); // start recording elapsed time for IR signal

        for (size_t i = 0; i < timings.size(); ++i) {
            // Original flag encoding: if LSB set, add 0x10000 to get the full duration.
            // Since our timings[] are plain µs (no flag encoding), we check i&1 for
            // mark/space parity. The 0x10000 extension was only needed when the buffer
            // held flag-encoded values; here we use the raw value directly.
            sigLenIR = (unsigned long)timings[i];
            if (i & 1) spacePhoton((unsigned int)(sigLenIR & 0xFFFFFFFEUL));
            else        markPhoton((unsigned int)(sigLenIR & 0xFFFFFFFEUL));
        }
        // If the signal didn't end on a space, add an extra delay (verbatim original)
        if (timings.size() & 1) delayMicroseconds(300);

        digitalWriteFast(LEDPIN, !pinReadFast(LEDPIN)); // TOGGLELED
    }

    void attachRx() {
        if (rxAttached_) return;
        // Verbatim from original: CHANGE on IR_Rx_PIN, FALLING on IR_Mod_PIN
        attachInterrupt(IR_Rx_PIN,  detail::rxIR_Interrupt_Handler,  CHANGE);
        attachInterrupt(IR_Mod_PIN, detail::rxIR3_Interrupt_Handler, FALLING);
        rxAttached_ = true;
    }

    void detachRx() {
        if (!rxAttached_) return;
        detachInterrupt(IR_Rx_PIN);
        detachInterrupt(IR_Mod_PIN);
        rxAttached_ = false;
    }

    bool rxAttached_;
};

} // namespace automatica_ir

#endif  // PARTICLE || SPARK
#endif  // AUTOMATICA_IR_HARDWARE_PHOTON_H
