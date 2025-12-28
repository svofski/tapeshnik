#include <Arduino.h>
#include <SimpleFOC.h>
#include "PMW3360/PMW3360.h"
#include "deckcontrol.h"
#include "util.h"

#define WITH_MOTORS
#define WITH_DECK_CONTROLS

void doPlayForward(char *cmd);
void doFastForward(char *cmd);

void doPlayReverse(char *cmd);
void doFastReverse(char *cmd);

void doStop(char *cmd);

void doZeroCounter(char *cmd);

// normal tape rpm 40..100   rpm or 0.6..1.66 rps
// ff tape rpm     400..1000 rpm or 6.7..16.6 rps

constexpr int PIN_TAPE_SENSOR_CS_N = 6;
constexpr int PIN_TAPE_SENSOR_RESET_N = 7;

// motor sensor pins
constexpr int PIN_MOTOR1_SENSOR_CS_N = 13;
constexpr int PIN_MOTOR2_SENSOR_CS_N = 14;

// motor controller pins
constexpr int PIN_STANDBY = 15;

constexpr int HEAD_LIFT_SERVO_PIN = 29;
constexpr int HEAD_LIFT_SERVO_UP = 90;
constexpr int HEAD_LIFT_SERVO_MID = 90;
constexpr int HEAD_LIFT_SERVO_DOWN = 90;

constexpr float speed_max = 40.0;
constexpr float speed_min = -40.0;
constexpr float speed_init = 6.28; // exactly 1 turn/sec
float dspeed = 0;


constexpr float TAPE_PLAY_SPEED = 3.14;
constexpr float TAPE_FAST_SPEED = 6.28 * 4;
constexpr float TAPE_FAST_SPEED_REW = 6.28 * 4;

int tape_counter = 0;
int tape_sensor_squal = 0;

// BLDC motor instance: 
// DC-2813C http://www.jdpowersky.com/en/p-info.aspx?cid=1&id=116
// DC-2813C is 12N14P, 12 slots, 14 poles
// 11.3 ohm

// MOTOR1 (supply spindle)
BLDCMotor motor1 = BLDCMotor(/*pp=*/7, /*R=*/11.3, /*KV=*/ 196); 
BLDCDriver3PWM driver1 = BLDCDriver3PWM(3, 4, 5); // PWM1_U/V/W, no enable
MagneticSensorSPI sensor1 = MagneticSensorSPI(AS5047_SPI, PIN_MOTOR1_SENSOR_CS_N);

// MOTOR2 (takeup spindle)
BLDCMotor motor2 = BLDCMotor(/*pp=*/7, /*R=*/11.3, /*KV=*/ 196); 
BLDCDriver3PWM driver2 = BLDCDriver3PWM(0, 1, 2); // PWM1_U/V/W, no enable
MagneticSensorSPI sensor2 = MagneticSensorSPI(AS5047_SPI, PIN_MOTOR2_SENSOR_CS_N);

// somehow changing SPI (SPI0) parameters seems to kill everything, not sure what's going on
//                        rx, cs,    sck,tx
SPIClassRP2040 SPIn(spi1, 12, NOPIN, 10, 11); // need be valid pins for same SPI channel, else fails blinking 4 long 4 short

// head lift servo
Servo head_lift_servo;

// instantiate the commander
Commander command = Commander(Serial);
void doMotor1(char* cmd) { command.motor(&motor1, cmd); }
void doMotor2(char* cmd) { command.motor(&motor2, cmd); }
void doHeadLift(char *cmd) {
  int us = atoi(&cmd[0]);
  Serial.printf("Head lift: %d us\n", us);
  //if (us < 1000 || us > 2000) {
  //  Serial.printf("Head lift: arg is microseconds must be in range [1000..2000] (got %d)\n", us);
  //  return;
  //}
  head_lift_servo.writeMicroseconds(us);
}

const char * fail = 0;

PMW3360 tape_sensor;

DeckControl deckControl(&motor1, &motor2, &head_lift_servo);

void pidSetup(BLDCMotor& motor)
{
  motor.motion_downsample = 0;

  // velocity loop PID
  motor.PID_velocity.P = 0.08;
  motor.PID_velocity.I = 1;//0.025; // maybe 1 is too heavy?
  motor.PID_velocity.D = 0.0;
  motor.PID_velocity.output_ramp = 1000.0;
  motor.PID_velocity.limit = 2.0;//20.0;

  // angle loop PID
  motor.P_angle.P = 30.0;
  motor.P_angle.I = 0.0;
  motor.P_angle.D = 5.0;
  motor.P_angle.output_ramp = 100.0;
  motor.P_angle.limit = 20.0;

//  // Low pass filtering time constant
  motor.LPF_velocity.Tf = 0.03f;    // larger value -> bigger start/stop jerk
  // pwm modulation settings
  motor.foc_modulation = FOCModulationType::SinePWM;
  motor.modulation_centered = 1.0;

  // with sx1308 there seems to be not enough power
  // the motors tend to go brrrrr if stopped abruptly, such as when reaching EOT
  // limiting the voltage helps this: 3 is fine but boring, 4 rather good, 6 motor goes brrr
  motor.voltage_limit = 4;    
}

void motorInit(BLDCMotor &motor, BLDCDriver3PWM &driver, MagneticSensorSPI &sensor, uint8_t pin_standby)
{
  // initialise magnetic sensor hardware
  sensor.clock_speed = 10'000'000;
  sensor.init(&SPIn);
  motor.linkSensor(&sensor);
  
  // driver config
  driver.voltage_power_supply = 9; // Replace with your power supply voltage
  driver.init();
  motor.linkDriver(&driver);
  motor.voltage_sensor_align = 2;

  motor.torque_controller = TorqueControlType::voltage;
  //motor.controller = MotionControlType::torque;
  //motor.target = 0.2; // Volts 

  motor.controller = MotionControlType::velocity;
  motor.target = 6.28; // radians/sec 

  pidSetup(motor);

  //motor.useMonitoring(Serial); 

  // set the target velocity (rad/s)
  //motor.target = 6.28; // Example: 5 radians per second

  
  // init motor
  if (!motor.init()) {
    fail = "motor init failed!";
    return;
  }
  
  // initFOC needs motor to actually work
  // enable motori
  pinMode(pin_standby, OUTPUT);
  digitalWrite(pin_standby, HIGH);

  //digitalWrite(PIN_STANDBY, LOW);

  // align sensor and start FOC
  if(!motor.initFOC()){
    fail = "FOC init failed!";
    return;
  }
} 

void sensor_dumbtest(MagneticSensorSPI &sensor1, MagneticSensorSPI &sensor2)
{
  sensor1.clock_speed = 10'000'000;
  sensor1.init(&SPIn);
  sensor2.clock_speed = 10'000'000;
  sensor2.init(&SPIn);
  for (;;) {
    sensor1.update();
    sensor2.update();
    Serial.print(sensor1.getAngle());
    Serial.print(" ");
    Serial.println(sensor2.getAngle());
  }
}

void motorLoop(BLDCMotor &motor)
{
  motor.loopFOC();
  motor.move();
}

void tape_sensor_setup()
{
  pinMode(PIN_TAPE_SENSOR_RESET_N, OUTPUT);
  digitalWrite(PIN_TAPE_SENSOR_RESET_N, LOW);
  _delay(2);
  digitalWrite(PIN_TAPE_SENSOR_RESET_N, HIGH);
  _delay(100);
  bool succ;
  do {
    succ = tape_sensor.begin(&SPIn, PIN_TAPE_SENSOR_CS_N);
    Serial.print("PMW3360 init ok: "); Serial.println(succ);
    _delay(1000);
  } while(!succ);
}

void tape_sensor_poll()
{
  PMW3360_DATA data = tape_sensor.readBurst();
  
  if(data.isOnSurface && data.isMotion)
  {
    //Serial.print(data.dx);
    //Serial.print("\t");
    //Serial.print(data.dy);
    //Serial.println();
    deckControl.optical_input(data.dy, data.SQUAL);

    tape_counter += data.dy;
    tape_sensor_squal = data.SQUAL;
    //if (data.SQUAL < 20) {
    //  Serial.print("SQUAL LOW! "); Serial.println(data.SQUAL);
    //}
  }
  else {
    //Serial.print("pmw3360 isOnSurface="); Serial.print(data.isOnSurface);
    //Serial.print("pmw3360 isMotion="); Serial.println(data.isMotion);
  }
}


void setup() {
  //_delay(1000);
  Serial.begin(115200);

  pinMode(PIN_MOTOR1_SENSOR_CS_N, OUTPUT);
  digitalWrite(PIN_MOTOR1_SENSOR_CS_N, HIGH);
  pinMode(PIN_MOTOR2_SENSOR_CS_N, OUTPUT);
  digitalWrite(PIN_MOTOR2_SENSOR_CS_N, HIGH);

  head_lift_servo.attach(HEAD_LIFT_SERVO_PIN);
  head_lift_servo.write(HEAD_LIFT_SERVO_UP);

#ifdef WITH_MOTORS
  //_delay(2000);
  //_delay(250);
  SimpleFOCDebug::enable(&Serial);
  Serial.println("simpleFOC setup begins");

  motor1.useMonitoring(Serial); 
  motorInit(motor1, driver1, sensor1, PIN_STANDBY);

  // pull the slack on supply reel after initialisation
  motor1.controller = MotionControlType::torque;
  motor1.target = 0.02; // Volts 
  for (int i = 0; i < 50; ++i) {
    motorLoop(motor1);
    _delay(1);
  }

  //motor2.useMonitoring(Serial); 
  motorInit(motor2, driver2, sensor2, PIN_STANDBY);

  motor1.target = 0;
  motor2.target = 0;

  // head lift calibration
  command.add('H', doHeadLift, "Head lift");

  // add target command M
  command.add('M', doMotor1, "Motor 1");
  command.add('N', doMotor2, "Motor 2");

#ifdef WITH_DECK_CONTROLS
  command.add('f', doPlayForward, "PLAY.F");
  command.add('F', doFastForward, "FAST.F");
  command.add('r', doPlayReverse, "PLAY.R");
  command.add('R', doFastReverse, "FAST.R");

  command.add('S', doStop, "STOP");
  command.add('s', doStop, "STOP");
  
  command.add('0', doZeroCounter, "ZERO");
#endif

  Serial.println(F("Motor ready."));
  Serial.println(F("Set the target using serial terminal and command M:"));
#endif
  _delay(1000);
  Serial.println("Initialising PMW3360");
  tape_sensor_setup();
}

enum state { 
  S_DEFAULT,
  S_STOP,
  S_STOP1,
  S_STOP2,
  S_STOP3,
  S_PLAY,
  S_PLAY_FF1,
  S_PLAY1,
  S_PLAY2,
  S_FF,
  S_FF1,
  S_FF2,
  S_REW,
  S_REW1,
  S_REW2
  };

int tape_state = S_DEFAULT;
int tape_state_req = S_DEFAULT;
int tape_state_next = -1;
unsigned long tape_state_timer = 0;

BLDCMotor *stopmotor1, *stopmotor2;

void control_fsm()
{
  switch (tape_state) {
    case S_DEFAULT:
    case S_PLAY:
    case S_FF:
    case S_REW:
      if (tape_state_req == S_STOP) {
        if (tape_state != S_DEFAULT && !(tape_state >= S_STOP1 && tape_state <= S_STOP3)) {
          if (motor2.controller == MotionControlType::velocity) {
            stopmotor1 = &motor1;
            stopmotor2 = &motor2;
          }
          else {
            stopmotor1 = &motor2;
            stopmotor2 = &motor1;
          }
          tape_state = S_STOP1;
        }
      }
      else if (tape_state_req == S_PLAY) {
        if (tape_state == S_FF) {
          Serial.println("quick FF->PLAY_FF1");
          tape_state = S_PLAY_FF1; // no need to apply pretension from FF
        }
        else if (!(tape_state >= S_PLAY1 && tape_state <= S_PLAY2)) {
          tape_state = S_PLAY1;
        }
      }
      else if (tape_state_req == S_FF) {
        if (tape_state == S_REW) {
          tape_state = S_STOP1;
          tape_state_next = S_FF1;
        }
        else if (!(tape_state >= S_FF1 && tape_state <= S_FF2)) {
          tape_state = S_FF1;
        }
      }
      else if (tape_state_req == S_REW) {
        if (tape_state == S_FF) {
          tape_state = S_STOP1;
          tape_state_next = S_REW1;
        }
        if (!(tape_state >= S_REW1 && tape_state <= S_REW2)) {
          tape_state = S_REW1;
        }
      }
      tape_state_req = S_DEFAULT;
      break;

    case S_STOP1:
      if (motor2.controller == MotionControlType::velocity) {
        if (fabs(motor2.shaft_velocity) > 3.14f) {
          motor2.target = fsgn(motor2.target) * (fabs(motor2.target) - 0.1f);
        }
        else {
          tape_state = S_STOP2;
        }
      }
      else if (motor1.controller == MotionControlType::velocity) {
        if (fabs(motor1.shaft_velocity) > 3.14f) {
          motor1.target = fsgn(motor1.target) * (fabs(motor1.target) - 0.1f);
        }
        else {
          tape_state = S_STOP2;
        }
      }
      else {
        tape_state = S_STOP2;
      }
      tape_state_timer = millis() + 25;
      break;
    case S_STOP2:
      if (millis() > tape_state_timer) {
        // revert motor swap before applying torque
        motor1.controller = MotionControlType::torque;
        motor1.target = 0.025;

        motor2.controller = MotionControlType::torque;
        motor2.target = -0.025;

        tape_state_timer = millis() + 50;
        tape_state = S_STOP3;
        Serial.println("->S_STOP3");
      }
      break;
    case S_STOP3:
      if (millis() > tape_state_timer) {
        stopmotor1->target = 0;
        stopmotor2->target = 0;
        tape_state = S_DEFAULT;
        if (tape_state_next >= 0) {
          tape_state = tape_state_next;
          tape_state_next = -1;
        }
        Serial.print("STOP->"); Serial.println(tape_state);
      }
      break;
    case S_PLAY1:
      // pull the slack on supply reel after initialisation
      motor1.controller = MotionControlType::torque;
      motor1.target = 0.02; 
      motor2.controller = MotionControlType::torque;
      motor2.target = -0.02; 
      tape_state_timer = millis() + 30;
      tape_state = S_PLAY2;
      break;
    case S_PLAY2:
      if (millis() > tape_state_timer) {
        motor1.controller = MotionControlType::torque;
        motor1.target = 0.02; 

        motor2.controller = MotionControlType::velocity;
        motor2.target = -TAPE_PLAY_SPEED; // 1 rps
        tape_state = S_PLAY;
      }
      break;
    case S_PLAY_FF1:
      if (motor2.target < -TAPE_PLAY_SPEED) {
        motor2.target += 0.01;
      }
      else {
        tape_state = S_PLAY2;
      }
      break;
    case S_FF1:
      // pull the slack on supply reel after initialisation
      motor1.controller = MotionControlType::torque;
      motor1.target = 0.02; // Volts
      motor2.controller = MotionControlType::velocity;
      motor2.target = -TAPE_PLAY_SPEED;
      tape_state_timer = millis() + 250;
      tape_state = S_FF2;
      break;
    case S_FF2:
      if (millis() > tape_state_timer) {
        motor2.controller = MotionControlType::velocity;
        motor2.target = -TAPE_FAST_SPEED;
        tape_state = S_FF;
      }
      break;
    case S_REW1:
      motor1.controller = MotionControlType::velocity;
      motor1.target = TAPE_PLAY_SPEED;
      motor2.controller = MotionControlType::torque;
      motor2.target = -0.02;
      tape_state_timer = millis() + 250;
      tape_state = S_REW2;
      break;
    case S_REW2:
      if (millis() > tape_state_timer) {
        Serial.println("->S_REW");
        motor1.target = TAPE_FAST_SPEED_REW;
        tape_state = S_REW;
      }
      break;
  }
}

void doPlayForward(char *cmd) {
    deckControl.press_button(DeckButton::PLAY_FORWARD);
}

void doFastForward(char *cmd) {
    deckControl.press_button(DeckButton::FAST_FORWARD);
}

void doPlayReverse(char *cmd) {
    deckControl.press_button(DeckButton::PLAY_REVERSE);
}

void doFastReverse(char *cmd) {
    deckControl.press_button(DeckButton::FAST_REVERSE);
}

void doStop(char *cmd) {
    deckControl.press_button(DeckButton::STOP);
}

void doZeroCounter(char *cmd) {
  tape_counter = 0;
}

unsigned long tape_counter_print_time = millis();
unsigned long tape_sensor_poll_time = millis();
unsigned long poll_interval = 0;
unsigned long last_poll_us = 0;


void loop() {
#ifdef WITH_MOTORS
  motorLoop(motor1);
  motorLoop(motor2);

  //motor1.monitor();
  command.run();

#endif
#ifdef WITH_DECK_CONTROLS
  //control_fsm();
  tape_sensor_poll();
  deckControl.loop();
#endif


  //uint64_t new_micros = micros();
  //poll_interval = (poll_interval + (new_micros - last_poll_us)) >> 1;
  //last_poll_us = micros();

  //if (millis() - tape_counter_print_time > 250) {
  //  tape_counter_print_time = millis();
  //  Serial.print(" M1 w: "); Serial.print(motor1.shaft_velocity); Serial.print(" A:"); Serial.print(motor1.shaft_angle);
  //  Serial.print(" M2 w: "); Serial.print(motor2.shaft_velocity); Serial.print(" A:"); Serial.print(motor2.shaft_angle);

  //  Serial.print(" POS:"); Serial.print(tape_counter);
  //  Serial.print(" poll_us:"); Serial.print(poll_interval);
  //  Serial.print(" SQUAL: "); Serial.println(tape_sensor_squal);
  //}
}
