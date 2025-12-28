#pragma once

#include <Arduino.h>
#include <SimpleFOC.h>
#include <Servo.h>
#include "util.h"

struct MotorInfo {
    BLDCMotor *motor;

    float zero_shaft_angle;   // after rewind
    float last_shaft_angle;   // last measurement
    float w;                  // measured angular velocity rad/s
    float w_average;
    float numturns;           // number of turns after rewind
    
    float total_numturns;     // calibrated value
};

enum class DeckDirection {
    FORWARD,
    REVERSE
};

enum class DeckState {
    STOP,
    STOP_RAMPDOWN, // slowing down
    STOP_TENSION,
    PLAY,
};

enum class SetSpeedOption {
    NONE,
    AUTOSTOP
};

enum class DeckButton {
    NO_BUTTON,
    STOP,
    PLAY_FORWARD,
    PLAY_REVERSE,
    FAST_FORWARD,
    FAST_REVERSE
};

enum class HeadPosition {
    UP,
    MID,
    DOWN
};

class DeckControl {
private:
    BLDCMotor *motor1;
    BLDCMotor *motor2;

    MotorInfo mi1;
    MotorInfo mi2;

    MotorInfo *supply;
    MotorInfo *takeup;

    float tape_speed_sp;     // speed setpoint (sign == direction)
    float tape_velocity;

    uint64_t last_measure_micros;

    Servo *head_lift_servo;


public:
    static constexpr float OVER_2PI = 1.0f / M_TWOPI;
    //static constexpr uint32_t W_INTERVAL_US = 1'000'000; // 1s for scale w in rad/s
    //static constexpr float W_SCALE = 1.0f;  // or e.g. interval=100'000, scale=10.f
    static constexpr uint32_t W_INTERVAL_US = 1'000'000 / 8; // measure 8 times per second
    static constexpr float W_SCALE = 8.0f;  // scale to rad/s

    static constexpr float TORQUE_SUPPLY = 0.03f;
    static constexpr float TORQUE_BRAKE = 0.08f;

    static constexpr float NORMAL_SPEED = 47.7e-3f;
    static constexpr float FF_SPEED = 30 * 47.7e-3f;
    // empty reel radius = 11.5mm (TASCAM cassette, approx)
    static constexpr float R0 = 11.5e-3f;
    static constexpr float MIN_W = 0.01f; // if spindle doesn't go at least this fast, autostop
    float tape_thickness = 13e-6; // 13um

    int print_update_ctr = 0;
    static constexpr int PRINT_UPDATE_INTERVAL = 8; // skip 8 measurements between printing debug info

    DeckState state = DeckState::STOP;
    DeckDirection direction;
    int autostop_count;
    bool autostopping;
    static constexpr int AUTOSTOP_LOCKED_THRESHOLD = 1; // autostop if spindles don't spin
    static constexpr int AUTOSTOP_FREESPIN_THRESHOLD = 40; // autostop if seem to spin freely

    int stopping_cycles_ctr; // (update cycles) tension the tape before relaxing motors
    static constexpr int STOPPING_CYCLES = 40; // intervals in VELOCITY_RAMP_MS

    DeckButton next_action = DeckButton::NO_BUTTON;

    // servo positions in microseconds
    static constexpr int HEAD_SERVO_UP_POS = 1750;
    static constexpr int HEAD_SERVO_MID_POS = 1400;
    static constexpr int HEAD_SERVO_DOWN_POS = 1000;

    float tape_counter;
    int optical_counter;
    float optical_squal;

public:
    DeckControl() = delete;
    DeckControl(DeckControl&) = delete;
    DeckControl(DeckControl&&) = delete;
    DeckControl(BLDCMotor *m1, BLDCMotor *m2, Servo *head_lift_servo)
    {
        mi1.motor = m1;
        mi2.motor = m2;

        supply = &mi1;
        takeup = &mi2;
        direction = DeckDirection::FORWARD;
        state = DeckState::STOP;
        set_zero();

        // default calibration
        mi2.total_numturns = 818;
        mi1.total_numturns = 818;
        tape_thickness = 13e-6; // 13um

        this->head_lift_servo = head_lift_servo;
    }

    void calibrate()
    {
        if (direction != DeckDirection::FORWARD) {
            Serial.println("calibrate: not forward EOT");
            return;
        }
        
        mi1.total_numturns = mi2.total_numturns = mi2.numturns;
        // R0 + delta = total roll thickness
        float delta = (mi1.w_average / mi2.w_average - 1) * R0;
        tape_thickness = delta / mi1.total_numturns;
        Serial.printf("calibrate: numturns=%f delta=%f tape_thickness=%f\n",
                mi1.total_numturns, delta, tape_thickness);
    }

    bool stopped() const 
    {
        return state == DeckState::STOP;
    }

    void press_button(DeckButton btn)
    {
        switch (btn) {
            case DeckButton::STOP:
                if (state != DeckState::STOP_TENSION && state != DeckState::STOP) {
                    stop(DeckButton::NO_BUTTON);
                    lift_head(HeadPosition::UP);
                }
                break;
            case DeckButton::PLAY_FORWARD:
                if (state == DeckState::STOP) {
                    set_dir(DeckDirection::FORWARD);
                }
                if (direction == DeckDirection::FORWARD) {
                    lift_head(HeadPosition::DOWN);
                    set_speed(NORMAL_SPEED);
                }
                else {
                    stop(DeckButton::PLAY_FORWARD); // full stop, then try again
                }
                break;
            case DeckButton::PLAY_REVERSE:
                if (state == DeckState::STOP) {
                    set_dir(DeckDirection::REVERSE);
                }
                if (direction == DeckDirection::REVERSE) {
                    lift_head(HeadPosition::DOWN);
                    set_speed(NORMAL_SPEED);
                }
                else {
                    stop(DeckButton::PLAY_REVERSE); // full stop, then try again
                }
                break;
            case DeckButton::FAST_FORWARD:
                if (state == DeckState::STOP) {
                    set_dir(DeckDirection::FORWARD);
                }
                if (direction == DeckDirection::FORWARD) {
                    lift_head(HeadPosition::UP);
                    set_speed(FF_SPEED);
                }
                else {
                    stop(DeckButton::FAST_FORWARD); // full stop, then try again
                }
                break;
            case DeckButton::FAST_REVERSE:
                if (state == DeckState::STOP) {
                    set_dir(DeckDirection::REVERSE);
                }
                if (direction == DeckDirection::REVERSE) {
                    lift_head(HeadPosition::UP);
                    set_speed(FF_SPEED);
                }
                else {
                    stop(DeckButton::FAST_REVERSE); // full stop, then try again
                }
                break;
        }
    }

    void stop(DeckButton next)
    {
        next_action = next;
        set_speed(0);
    }

    // after rewind
    void set_zero()
    {
        supply->last_shaft_angle = supply->zero_shaft_angle = supply->motor->shaft_angle;
        supply->numturns = 0.f;
        supply->w = 0.f;

        takeup->last_shaft_angle = takeup->zero_shaft_angle = takeup->motor->shaft_angle;
        takeup->numturns = 0.f;
        takeup->w = 0.f;

        tape_counter = 0.f;
        optical_counter = 0;

        autostopping = false;
        stopping_cycles_ctr = 0;
    }

    // set desired tape travel speed, 0 = stop, 47.7e-3 = normal tape (positive)
    void set_speed(float speed, SetSpeedOption option = SetSpeedOption::NONE)
    {
        Serial.print("DeckControl::set_speed "); Serial.println(speed);

        switch (direction) {
            case DeckDirection::FORWARD:
                speed = -fabs(speed);
                break;
            case DeckDirection::REVERSE:
                speed = fabs(speed);
                break;
        }

        tape_speed_sp = speed;

        if (tape_speed_sp != 0) {
            takeup->motor->controller = MotionControlType::velocity;
            set_torque(supply, TORQUE_SUPPLY);
            autostop_count = 0;
            enter_state(DeckState::PLAY, "set_speed > 0");
        }
        else {
            // don't force STOP_TENSION before the speed is dropped
            enter_state(DeckState::STOP_RAMPDOWN, "set_speed 0");
            autostopping = option == SetSpeedOption::AUTOSTOP;
        }
    }

    void enter_state(DeckState next_state, const char *why)
    {
        switch (next_state) {
            case DeckState::STOP_RAMPDOWN:
                Serial.printf("enter_state: %d -> %d (STOP_RAMPDOWN, %s)\n", (int)state, (int)next_state, why);
                set_torque(supply, TORQUE_BRAKE); // stronger brake when rampdown
                state = next_state;
                break;
            case DeckState::STOP_TENSION:
                Serial.printf("enter_state: %d -> %d (STOP_TENSION, %s)\n", (int)state, (int)next_state, why);
                set_torque(takeup, TORQUE_SUPPLY);
                set_torque(supply, TORQUE_SUPPLY);
                //for (;;) {
                //    supply->motor->loopFOC(); supply->motor->move();
                //    takeup->motor->loopFOC(); takeup->motor->move();
                //}
                state = next_state;
                stopping_cycles_ctr = 1 + STOPPING_CYCLES;
                break;
            case DeckState::STOP:
                set_torque(supply, 0);
                set_torque(takeup, 0);
                Serial.printf("enter_state: %d -> %d (STOP, %s)\n", (int)state, (int)next_state, why);
                state = next_state;

                // autostop means we can reset turn count
                if (autostopping) {
                    if (direction == DeckDirection::REVERSE) {
                        set_zero();
                    }
                }

                if (next_action != DeckButton::NO_BUTTON) {
                    DeckButton btn = next_action;
                    next_action = DeckButton::NO_BUTTON;
                    Serial.printf("enter_state: next_action scheduled -> press_button(%d)\n", (int)btn);
                    press_button(btn);
                }
                break;
            case DeckState::PLAY:
                Serial.printf("enter_state: %d -> %d (PLAY, %s)\n", (int)state, (int)next_state, why);
                state = next_state;
                optical_holdoff_us = micros() + 1'000'000U; // don't slow down based on squal
                break;
            default:
                Serial.printf("enter_state: unknown state %d -> %d\n", (int)state, (int)next_state);
                assert(0);
                for(;;);
                break;
        }
    }

    // torque is always set "outwards" to tension the tape
    void set_torque(MotorInfo * mi, float target)
    {
        float value = fabs(target);
        if (mi == &mi2) {
            value = -value;
        }
        if ((mi->motor->controller != MotionControlType::torque)
                || (fabs(mi->motor->target - value) > 0.001)) {
            mi->motor->controller = MotionControlType::torque;
            mi->motor->target = value;
            Serial.printf("set_torque: M%d %f\n", (mi == &mi1) ? 1 : 2, value);
        }
    }

    // 0 = normal  (motor1 = supply, motor2 = takeup)
    // 1 = reverse (motor1 = takeup, motor1 = supply)
    void set_dir(DeckDirection dir) 
    {
        direction = dir;
        switch (dir) {
            case DeckDirection::FORWARD:
                supply = &mi1;
                takeup = &mi2;
                break;
            case DeckDirection::REVERSE:
                supply = &mi2;
                takeup = &mi1;
                break;
        }
    }

    void measure_velocities()
    {
        uint64_t now = micros();
        if (now - last_measure_micros >= W_INTERVAL_US) {
            last_measure_micros = now;
            const float supply_angle = supply->motor->shaft_angle;
            supply->w = (supply_angle - supply->last_shaft_angle) * W_SCALE;
            supply->last_shaft_angle = supply_angle;
            supply->numturns = -(supply_angle - supply->zero_shaft_angle) * OVER_2PI;

            const float takeup_angle = takeup->motor->shaft_angle;
            const float delta_takeup_angle = takeup_angle - takeup->last_shaft_angle;
            takeup->w = delta_takeup_angle * W_SCALE;
            takeup->last_shaft_angle = takeup_angle;

            takeup->numturns = -(takeup_angle - takeup->zero_shaft_angle) * OVER_2PI;

            //const float delta_travel = delta_takeup_angle * (R0 + takeup->numturns * tape_thickness);

            float r2;
            if (direction == DeckDirection::FORWARD) {
                r2 = R0 + tape_thickness * takeup->numturns;
            }
            else {
                r2 = R0 + (tape_thickness * (takeup->total_numturns - takeup->numturns));
            }
            const float delta_travel = delta_takeup_angle * r2;
            tape_velocity = takeup->w * r2;
            tape_counter += delta_travel;

            if (state == DeckState::PLAY) {
                check_autostop();
            }

            supply->w_average = supply->w_average * 0.8 + supply->w * 0.2;
            takeup->w_average = takeup->w_average * 0.8 + takeup->w * 0.2;

            if (--print_update_ctr <= 0) {
                print_update_ctr = PRINT_UPDATE_INTERVAL;
                Serial.printf("measure_velocities: supply a=%f w=%f (%f) numturns=%f "
                        "takeup a=%f w=%f (%f) numturns=%f "
                        "linear velocity=%fm/s counter=%fm optical=%d squal=%f ovel=%f m/s\n",
                        supply->last_shaft_angle, supply->w, supply->w_average, supply->numturns,
                        takeup->last_shaft_angle, takeup->w, takeup->w_average, takeup->numturns,
                        tape_velocity, tape_counter, optical_counter, optical_squal, opt_velocity
                        );
            }
        }
    }

    void check_autostop()
    {
        const char *why = "no reason";
        static const char *LOCKED = "hubs locked";
        static const char *FREESPIN = "hubs spin in opposite directions";

        if (state == DeckState::PLAY) {
            // autostop situation: both spindles stopped
            if (fabs(supply->w) < MIN_W || fabs(takeup->w) < MIN_W) {
                // update calibration
                calibrate();
                ++autostop_count;
                why = LOCKED;

            }
            //else if (fsgn(supply->w) != fsgn(takeup->w)) {
            //    ++autostop_count;
            //    why = FREESPIN;
            //}
            else {
                autostop_count = 0;
            }

            if ((why == LOCKED && autostop_count >= AUTOSTOP_LOCKED_THRESHOLD)
                    || (why == FREESPIN && autostop_count >= AUTOSTOP_FREESPIN_THRESHOLD)) {
                Serial.printf("check_autostop: %s, stop\n", why);
                set_speed(0, SetSpeedOption::AUTOSTOP);
            }
        }
    }

    uint64_t velocity_update_ms;
    static constexpr int VELOCITY_RAMP_MS = 3;  // velocity update step time
    static constexpr float VELOCITY_RAMP_P = 0.2;//0.05;  // P coefficient for velocity ramp 
    static constexpr float VELOCITY_RAMP_LIMIT = 1.f; // limit velocity change rate
    static constexpr float VELOCITY_RAMP_LIMIT_DOWN = 0.1f; // limit velocity change rate


    void update_takeup_velocity()
    {
        uint64_t now = millis();
        if (now >= velocity_update_ms) {
            velocity_update_ms = now + VELOCITY_RAMP_MS;

            // problem: when reversing we don't know takeup->numturns unless we count
            float required_w;
            if (direction == DeckDirection::FORWARD) {
                required_w = tape_speed_sp / (R0 + (tape_thickness * takeup->numturns));
            }
            else {
                required_w = tape_speed_sp / (R0 + (tape_thickness * (takeup->total_numturns - takeup->numturns)));
            }
            // but setpoint is computed for the current takeup
            float current_w = takeup->motor->target;

            float step = (required_w - current_w) * VELOCITY_RAMP_P;
            float limit = VELOCITY_RAMP_LIMIT;

            // slowing down when going stupid fast, need harder braking
            if (state == DeckState::PLAY) {
                if (fabs(required_w) < fabs(current_w) && fabs(step) > limit) {
                    set_torque(supply, TORQUE_BRAKE);
                }
                else {
                    set_torque(supply, TORQUE_SUPPLY);
                }
            }
            step = constrain(step, -limit, +limit);
            current_w = current_w + step;

            //Serial.printf("## sp=%f required_w=%f current_w=%f\n", tape_speed_sp, required_w, current_w);

            // TODO: break up state STOPPING (ramp down to 0 and TENSIONING (torque up before STOP)
            if (takeup->motor->controller == MotionControlType::velocity) {
                takeup->motor->target = current_w;
            }

            //Serial.printf("target=%f diff=%f state=%d cycles=%d\n", 
            //        takeup->motor->target, takeup->motor->target - current_w,
            //        (int)state,
            //        stopping_cycles_ctr);

            // ramped down speed, enter tensioning state
            if ((state == DeckState::STOP_RAMPDOWN) && (fabs(takeup->motor->target) < MIN_W)) {
                // tension the tape during stop
                enter_state(DeckState::STOP_TENSION, "target = 0");
                Serial.println("STATE->STOP_TENSION (update_takeup_velocity)");
            }

            if (state == DeckState::STOP_TENSION) {
                if (--stopping_cycles_ctr == 0) {
                    enter_state(DeckState::STOP, "tensioned");
                }
            }
        }
    }

    uint64_t opt_last_us = 0;
    float opt_velocity = 0.f;
    uint64_t optical_holdoff_us = 0;

    void optical_input(int motion, int squal)
    {
        if (squal < 40) return;
        //Serial.printf("[%d]", motion);  - 1..5 at 4.77, ~61 at 30x

        float motion_m = motion / 2000.f * 0.0254f;
        uint64_t micros_now = micros();
        float velocity = motion_m / (micros_now - opt_last_us) * 1e6;
        opt_last_us = micros_now;

        opt_velocity = opt_velocity * 0.95 + velocity * 0.05;
        
        optical_counter += motion;
        float optical_squal_prev = optical_squal;
        optical_squal = optical_squal * 0.8f + squal * 0.2f;
        if (optical_squal_prev < 80 && optical_squal > 80) {
            Serial.printf("SQUAL SUDDEN INCREASE %d %f\n", state, tape_speed_sp);
            if (micros_now < optical_holdoff_us) {
                Serial.println("BUT OPTICAL HOLDOFF");
                return;
            }
        }

        if (state == DeckState::PLAY && fabs(tape_speed_sp) > NORMAL_SPEED) {
            if (optical_squal_prev < 80 && optical_squal > 80) {
                //set_speed(NORMAL_SPEED);
                set_speed(0, SetSpeedOption::AUTOSTOP);
                Serial.println("SQUAL AUTOSTOP");
            }
        }
    }

    void lift_head(HeadPosition pos)
    {
        switch (pos) {
            case HeadPosition::UP:
                head_lift_servo->writeMicroseconds(HEAD_SERVO_UP_POS);
                break;
            case HeadPosition::MID:
                head_lift_servo->writeMicroseconds(HEAD_SERVO_MID_POS);
                break;
            case HeadPosition::DOWN:
                head_lift_servo->writeMicroseconds(HEAD_SERVO_DOWN_POS);
                break;
        }
    }

    void loop()
    {
        measure_velocities();
        update_takeup_velocity();
    }
};
