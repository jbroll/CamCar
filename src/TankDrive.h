#ifndef TANK_DRIVE_H
#define TANK_DRIVE_H

#include <Arduino.h>
#include "DCMotor.h"

// Robot drive class to handle two motors
class DifferentialDrive {
public:
    DifferentialDrive(uint8_t leftMotorPinA, uint8_t leftMotorPinB,
                     uint8_t rightMotorPinA, uint8_t rightMotorPinB) :
        mLeftMotor(leftMotorPinA, leftMotorPinB),
        mRightMotor(rightMotorPinA, rightMotorPinB) {}

    void begin() {
        mLeftMotor.begin();
        mRightMotor.begin();
    }

    // Handle arcade drive style input (forward/back + rotation)
    void arcadeDrive(int8_t forward, int8_t rotation) {
        // Mix forward and rotation commands
        int16_t leftSpeed = forward + rotation;
        int16_t rightSpeed = forward - rotation;

        // Normalize speeds if they exceed limits
        int16_t maxValue = max(abs(leftSpeed), abs(rightSpeed));
        if (maxValue > 100) {
            leftSpeed = (leftSpeed * 100) / maxValue;
            rightSpeed = (rightSpeed * 100) / maxValue;
        }

        mLeftMotor.setSpeed(leftSpeed);
        mRightMotor.setSpeed(rightSpeed);
    }

    // Stop both motors
    void stop() {
        mLeftMotor.stop();
        mRightMotor.stop();
    }

    // Brake both motors
    void brake() {
        mLeftMotor.brake();
        mRightMotor.brake();
    }

private:
    DCMotor mLeftMotor;
    DCMotor mRightMotor;
};

#endif // TANK_DRIVE_H
