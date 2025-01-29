#ifndef DC_MOTOR_H
#define DC_MOTOR_H

#include <Arduino.h>

class DCMotor {
public:
    // Constructor takes the two direction pins
    DCMotor(uint8_t pinA, uint8_t pinB) : 
        mPinA(pinA), 
        mPinB(pinB),
        mCurrentSpeed(0) {}

    // Initialize pins and PWM
    void begin() {
        pinMode(mPinA, OUTPUT);
        pinMode(mPinB, OUTPUT);
        
        // Configure ESP32 PWM
        // Using higher frequency for smoother motor operation
        ledcSetup(mPwmChannel, 20000, 8);  // 20kHz, 8-bit resolution
        ledcAttachPin(mPinA, mPwmChannel);
        ledcAttachPin(mPinB, mPwmChannel + 1);
        
        stop();
    }

    // Set speed from -100 to +100
    void setSpeed(int8_t speed) {
        mCurrentSpeed = constrain(speed, -100, 100);
        
        if (mCurrentSpeed > 0) {
            // Forward
            ledcWrite(mPwmChannel, map(mCurrentSpeed, 0, 100, 0, 255));
            ledcWrite(mPwmChannel + 1, 0);
        }
        else if (mCurrentSpeed < 0) {
            // Reverse
            ledcWrite(mPwmChannel, 0);
            ledcWrite(mPwmChannel + 1, map(-mCurrentSpeed, 0, 100, 0, 255));
        }
        else {
            // Stop
            stop();
        }
    }

    // Stop motor (coast)
    void stop() {
        mCurrentSpeed = 0;
        ledcWrite(mPwmChannel, 0);
        ledcWrite(mPwmChannel + 1, 0);
    }

    // Brake motor (short both pins to ground)
    void brake() {
        mCurrentSpeed = 0;
        ledcWrite(mPwmChannel, 255);
        ledcWrite(mPwmChannel + 1, 255);
    }

    // Get current speed (-100 to +100)
    int8_t getSpeed() const {
        return mCurrentSpeed;
    }

private:
    const uint8_t mPinA;
    const uint8_t mPinB;
    static uint8_t sNextPwmChannel;
    const uint8_t mPwmChannel = sNextPwmChannel++;
    int8_t mCurrentSpeed;
};

// Initialize static member
uint8_t DCMotor::sNextPwmChannel = 0;

#endif // DC_MOTOR_H
