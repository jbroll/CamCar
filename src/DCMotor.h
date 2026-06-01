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
        
        // Configure ESP32 PWM (pin-based ledc API, core 3.x)
        // Using higher frequency for smoother motor operation
        ledcAttach(mPinA, 20000, 8);  // 20kHz, 8-bit resolution
        ledcAttach(mPinB, 20000, 8);

        stop();
    }

    // Set speed from -100 to +100
    void setSpeed(int8_t speed) {
        mCurrentSpeed = constrain(speed, -100, 100);
        
        if (mCurrentSpeed > 0) {
            // Forward
            ledcWrite(mPinA, map(mCurrentSpeed, 0, 100, 0, 255));
            ledcWrite(mPinB, 0);
        }
        else if (mCurrentSpeed < 0) {
            // Reverse
            ledcWrite(mPinA, 0);
            ledcWrite(mPinB, map(-mCurrentSpeed, 0, 100, 0, 255));
        }
        else {
            // Stop
            stop();
        }
    }

    // Stop motor (coast)
    void stop() {
        mCurrentSpeed = 0;
        ledcWrite(mPinA, 0);
        ledcWrite(mPinB, 0);
    }

    // Brake motor (short both pins to ground)
    void brake() {
        mCurrentSpeed = 0;
        ledcWrite(mPinA, 255);
        ledcWrite(mPinB, 255);
    }

    // Get current speed (-100 to +100)
    int8_t getSpeed() const {
        return mCurrentSpeed;
    }

private:
    const uint8_t mPinA;
    const uint8_t mPinB;
    int8_t mCurrentSpeed;
};

#endif // DC_MOTOR_H
