
const char* configParams[] = {
    "ssid",
    "password",
    "hostname",
    "resolution",
    "framesize",
    "quality",
    "xclk",
    "fps",
    "device_pass",
    // Drive/camera calibration (applied live; defaults = no-op):
    "mot_swap",  "mot_inv_l", "mot_inv_r",                // swap L/R, invert each motor
    "mot_l_min", "mot_l_max", "mot_r_min", "mot_r_max",   // per-motor PWM floor/cap (0-255)
    "cam_swap",  "pan_inv",   "tilt_inv",                 // swap pan/tilt axes, invert each servo
    "pan_min",   "pan_max",   "tilt_min",  "tilt_max",    // servo travel limits (deg 0-180)
    nullptr  // terminator
};

