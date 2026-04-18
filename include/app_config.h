#pragma once

#include <Arduino.h>

#if __has_include("app_config_local.h")
#include "app_config_local.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef AP_SSID
#define AP_SSID "FitPass-Setup"
#endif

#ifndef AP_PASSWORD
#define AP_PASSWORD "fitpass123"
#endif

#ifndef DEVICE_NAME
#define DEVICE_NAME "FitPass"
#endif

#ifndef APP_TIMEZONE
#define APP_TIMEZONE "CST-8"
#endif

#ifndef STEPPER_IN1_PIN
#define STEPPER_IN1_PIN 16
#endif

#ifndef STEPPER_IN2_PIN
#define STEPPER_IN2_PIN 17
#endif

#ifndef STEPPER_IN3_PIN
#define STEPPER_IN3_PIN 18
#endif

#ifndef STEPPER_IN4_PIN
#define STEPPER_IN4_PIN 19
#endif

#ifndef STEPPER_FORWARD_DIRECTION
#define STEPPER_FORWARD_DIRECTION 1
#endif

#ifndef STEPPER_STEPS_PER_PORTION
#define STEPPER_STEPS_PER_PORTION 1365
#endif

#ifndef STEPPER_STEP_INTERVAL_US
#define STEPPER_STEP_INTERVAL_US 600
#endif

#ifndef DEBUG_STEP_INTERVAL_US
#define DEBUG_STEP_INTERVAL_US 1000
#endif

#ifndef DEBUG_SPEED_LEVEL_DEFAULT
#define DEBUG_SPEED_LEVEL_DEFAULT 6
#endif

#ifndef DEBUG_SPEED_LEVEL_MIN
#define DEBUG_SPEED_LEVEL_MIN 1
#endif

#ifndef DEBUG_SPEED_LEVEL_MAX
#define DEBUG_SPEED_LEVEL_MAX 10
#endif

#ifndef DEBUG_INTERVAL_US_FAST
#define DEBUG_INTERVAL_US_FAST 400
#endif

#ifndef DEBUG_INTERVAL_US_SLOW
#define DEBUG_INTERVAL_US_SLOW 2800
#endif

#ifndef MAX_FEED_SLOTS
#define MAX_FEED_SLOTS 6
#endif

#ifndef DEFAULT_FEED_SLOTS
#define DEFAULT_FEED_SLOTS "08:00,18:00"
#endif

#ifndef DEFAULT_PORTION_STEPS
#define DEFAULT_PORTION_STEPS 1
#endif

#ifndef MAX_PORTION_STEPS
#define MAX_PORTION_STEPS 6
#endif

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 15000
#endif

#ifndef REMOTE_CONTROL_ENABLED
#define REMOTE_CONTROL_ENABLED 0
#endif

#ifndef REMOTE_DEVICE_ID
#define REMOTE_DEVICE_ID "fitpass-device"
#endif

#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 8883
#endif

#ifndef MQTT_USE_TLS
#define MQTT_USE_TLS 1
#endif

#ifndef MQTT_TLS_INSECURE
#define MQTT_TLS_INSECURE 1
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME ""
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif

#ifndef MQTT_TOPIC_ROOT
#define MQTT_TOPIC_ROOT "fitpass"
#endif

#ifndef MQTT_RECONNECT_INTERVAL_MS
#define MQTT_RECONNECT_INTERVAL_MS 5000
#endif

#ifndef MQTT_STATUS_INTERVAL_MS
#define MQTT_STATUS_INTERVAL_MS 5000
#endif

#ifndef DEVICE_ACCESS_PIN
#define DEVICE_ACCESS_PIN ""
#endif

#ifndef OTA_ENABLED
#define OTA_ENABLED 1
#endif

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "fitpass"
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD DEVICE_ACCESS_PIN
#endif

#ifndef COMMAND_INDICATOR_LED_PIN
#define COMMAND_INDICATOR_LED_PIN 2
#endif

#ifndef COMMAND_INDICATOR_LED_ACTIVE_LEVEL
#define COMMAND_INDICATOR_LED_ACTIVE_LEVEL LOW
#endif

#ifndef COMMAND_INDICATOR_PULSE_MS
#define COMMAND_INDICATOR_PULSE_MS 2000
#endif
