#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "app_config.h"

namespace {

struct FeedSlot {
  bool enabled = false;
  uint8_t hour = 0;
  uint8_t minute = 0;
  int lastTriggeredDay = -1;
};

enum class MotionType {
  Idle,
  ManualFeed,
  ScheduleFeed,
  Debug,
};

WebServer server(80);
WiFiClientSecure mqttSecureClient;
PubSubClient mqttClient(mqttSecureClient);
Preferences preferences;
FeedSlot feedSlots[MAX_FEED_SLOTS];

uint8_t defaultPortionSteps = DEFAULT_PORTION_STEPS;
unsigned long lastStatusLogAt = 0;
unsigned long lastMqttReconnectAttemptAt = 0;
unsigned long lastRemoteStatusPublishAt = 0;

MotionType motionType = MotionType::Idle;
bool motorRunning = false;
bool motionContinuous = false;
int motionDirection = STEPPER_FORWARD_DIRECTION;
long motionStepsRemaining = 0;
unsigned long lastMotorStepAtUs = 0;
uint32_t currentStepIntervalUs = STEPPER_STEP_INTERVAL_US;
uint8_t currentSequenceIndex = 0;
uint8_t debugSpeedLevel = DEBUG_SPEED_LEVEL_DEFAULT;
bool remoteBrokerConnected = false;

constexpr uint8_t kStepperPins[4] = {
    STEPPER_IN1_PIN,
    STEPPER_IN2_PIN,
    STEPPER_IN3_PIN,
    STEPPER_IN4_PIN,
};

constexpr uint8_t kHalfStepSequence[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1},
};

String twoDigits(int value) {
  if (value < 10) {
    return "0" + String(value);
  }
  return String(value);
}

bool isTimeSynced() {
  return time(nullptr) > 1700000000;
}

String getStationIp() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
}

String getApIp() {
  return WiFi.softAPIP().toString();
}

String getPinMapLabel() {
  return "IN1=GPIO" + String(STEPPER_IN1_PIN) + ", IN2=GPIO" + String(STEPPER_IN2_PIN) +
         ", IN3=GPIO" + String(STEPPER_IN3_PIN) + ", IN4=GPIO" + String(STEPPER_IN4_PIN);
}

bool isRemoteControlEnabled() {
  return REMOTE_CONTROL_ENABLED && strlen(MQTT_HOST) > 0 && strlen(REMOTE_DEVICE_ID) > 0;
}

String remoteTopicBase() {
  return String(MQTT_TOPIC_ROOT) + "/" + REMOTE_DEVICE_ID;
}

String remoteStatusTopic() {
  return remoteTopicBase() + "/status";
}

String remoteCommandTopic() {
  return remoteTopicBase() + "/command";
}

String remoteAvailabilityTopic() {
  return remoteTopicBase() + "/availability";
}

String remoteClientId() {
  uint64_t chipId = ESP.getEfuseMac();
  char buffer[40];
  snprintf(buffer, sizeof(buffer), "%s-%04llX", REMOTE_DEVICE_ID, chipId & 0xFFFF);
  return String(buffer);
}

bool isAccessPinEnabled() {
  return strlen(DEVICE_ACCESS_PIN) > 0;
}

bool isAuthorizedPin(const String &pin) {
  return !isAccessPinEnabled() || pin == String(DEVICE_ACCESS_PIN);
}

void sendJson(int code, const String &body);

String extractCommandValue(const String &payload, const char *key) {
  const String needle = String(key) + "=";
  const int start = payload.indexOf(needle);
  if (start < 0) {
    return "";
  }

  int valueStart = start + needle.length();
  int valueEnd = payload.indexOf('&', valueStart);
  if (valueEnd < 0) {
    valueEnd = payload.length();
  }

  String value = payload.substring(valueStart, valueEnd);
  value.replace("+", " ");
  return value;
}

bool ensureLocalAuthorized() {
  if (isAuthorizedPin(server.arg("pin"))) {
    return true;
  }

  sendJson(401, "{\"error\":\"invalid pin\"}");
  return false;
}

void publishRemoteStatus(bool force = false);

const char *motionKey(MotionType type) {
  switch (type) {
    case MotionType::ManualFeed:
      return "manual-feed";
    case MotionType::ScheduleFeed:
      return "schedule-feed";
    case MotionType::Debug:
      return "debug";
    case MotionType::Idle:
    default:
      return "idle";
  }
}

const char *motionLabel(MotionType type) {
  switch (type) {
    case MotionType::ManualFeed:
      return "运行投喂中";
    case MotionType::ScheduleFeed:
      return "定时投喂中";
    case MotionType::Debug:
      return "调试运行中";
    case MotionType::Idle:
    default:
      return "空闲";
  }
}

const char *directionKey(int direction) {
  return direction >= 0 ? "forward" : "reverse";
}

const char *directionLabel(int direction) {
  return direction >= 0 ? "正转" : "反转";
}

uint8_t clampDebugSpeedLevel(int value) {
  return static_cast<uint8_t>(constrain(value, DEBUG_SPEED_LEVEL_MIN, DEBUG_SPEED_LEVEL_MAX));
}

uint32_t debugSpeedLevelToInterval(uint8_t level) {
  const uint8_t clamped = clampDebugSpeedLevel(level);
  const long mapped = map(clamped,
                          DEBUG_SPEED_LEVEL_MIN,
                          DEBUG_SPEED_LEVEL_MAX,
                          DEBUG_INTERVAL_US_SLOW,
                          DEBUG_INTERVAL_US_FAST);
  return static_cast<uint16_t>(max(1L, mapped));
}

void writeStepperPhase(uint8_t phaseIndex) {
  for (int i = 0; i < 4; ++i) {
    digitalWrite(kStepperPins[i], kHalfStepSequence[phaseIndex][i] ? HIGH : LOW);
  }
}

void releaseStepper() {
  for (uint8_t pin : kStepperPins) {
    digitalWrite(pin, LOW);
  }
}

String slotToString(const FeedSlot &slot) {
  if (!slot.enabled) {
    return "";
  }
  return twoDigits(slot.hour) + ":" + twoDigits(slot.minute);
}

bool parseTimeToken(String token, FeedSlot &slot) {
  token.trim();
  if (token.length() != 5 || token.charAt(2) != ':') {
    return false;
  }

  const int hour = token.substring(0, 2).toInt();
  const int minute = token.substring(3, 5).toInt();
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return false;
  }

  slot.enabled = true;
  slot.hour = static_cast<uint8_t>(hour);
  slot.minute = static_cast<uint8_t>(minute);
  slot.lastTriggeredDay = -1;
  return true;
}

String currentSlotsCsv() {
  String output;
  for (int i = 0; i < MAX_FEED_SLOTS; ++i) {
    if (!feedSlots[i].enabled) {
      continue;
    }
    if (!output.isEmpty()) {
      output += ",";
    }
    output += slotToString(feedSlots[i]);
  }
  return output;
}

bool applySlotsCsv(const String &csv) {
  FeedSlot nextSlots[MAX_FEED_SLOTS];
  int index = 0;
  int start = 0;
  String working = csv;
  working.trim();

  if (working.isEmpty()) {
    for (int i = 0; i < MAX_FEED_SLOTS; ++i) {
      feedSlots[i] = nextSlots[i];
    }
    return true;
  }

  while (start <= working.length()) {
    int comma = working.indexOf(',', start);
    if (comma < 0) {
      comma = working.length();
    }

    if (index >= MAX_FEED_SLOTS) {
      return false;
    }

    FeedSlot parsed;
    if (!parseTimeToken(working.substring(start, comma), parsed)) {
      return false;
    }
    nextSlots[index++] = parsed;
    start = comma + 1;
    if (comma == working.length()) {
      break;
    }
  }

  for (int i = 0; i < MAX_FEED_SLOTS; ++i) {
    feedSlots[i] = nextSlots[i];
  }
  return true;
}

void saveSchedule() {
  preferences.putString("slots", currentSlotsCsv());
  preferences.putUChar("portion", defaultPortionSteps);
}

void loadSchedule() {
  if (!preferences.isKey("slots")) {
    applySlotsCsv(DEFAULT_FEED_SLOTS);
    defaultPortionSteps = DEFAULT_PORTION_STEPS;
    saveSchedule();
    return;
  }

  const String slots = preferences.getString("slots", DEFAULT_FEED_SLOTS);
  const uint8_t savedPortion = preferences.isKey("portion")
                                   ? preferences.getUChar("portion", DEFAULT_PORTION_STEPS)
                                   : DEFAULT_PORTION_STEPS;
  defaultPortionSteps = static_cast<uint8_t>(constrain(savedPortion, 1, MAX_PORTION_STEPS));

  if (!applySlotsCsv(slots)) {
    applySlotsCsv(DEFAULT_FEED_SLOTS);
    saveSchedule();
  }
}

String jsonEscape(String input) {
  input.replace("\\", "\\\\");
  input.replace("\"", "\\\"");
  input.replace("\n", "\\n");
  input.replace("\r", "");
  return input;
}

String formattedNow() {
  if (!isTimeSynced()) {
    return "waiting-for-ntp";
  }

  struct tm info;
  if (!getLocalTime(&info)) {
    return "time-read-failed";
  }

  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &info);
  return String(buffer);
}

void stopMotion(const char *reason) {
  if (!motorRunning && motionType == MotionType::Idle) {
    return;
  }

  Serial.printf("[MOTOR] stop reason=%s type=%s remaining=%ld\n",
                reason,
                motionKey(motionType),
                motionStepsRemaining);

  motorRunning = false;
  motionContinuous = false;
  motionStepsRemaining = 0;
  motionDirection = STEPPER_FORWARD_DIRECTION;
  currentStepIntervalUs = STEPPER_STEP_INTERVAL_US;
  motionType = MotionType::Idle;
  releaseStepper();
  publishRemoteStatus(true);
}

bool startMotion(long totalSteps,
                 int direction,
                 uint32_t intervalUs,
                 MotionType type,
                 const char *reason) {
  if (motorRunning) {
    return false;
  }

  motorRunning = true;
  motionContinuous = totalSteps < 0;
  motionStepsRemaining = totalSteps;
  motionDirection = direction >= 0 ? 1 : -1;
  currentStepIntervalUs = intervalUs == 0 ? 1 : intervalUs;
  motionType = type;
  lastMotorStepAtUs = micros();

  Serial.printf("[MOTOR] start reason=%s type=%s dir=%s steps=%ld interval_us=%lu\n",
                reason,
                motionKey(type),
                directionLabel(motionDirection),
                totalSteps,
                currentStepIntervalUs);

  writeStepperPhase(currentSequenceIndex);
  publishRemoteStatus(true);
  return true;
}

bool startFeedCycle(uint8_t portions, MotionType type, const char *reason) {
  portions = static_cast<uint8_t>(constrain(portions, 1, MAX_PORTION_STEPS));
  const long totalSteps = static_cast<long>(portions) * STEPPER_STEPS_PER_PORTION;
  return startMotion(totalSteps, STEPPER_FORWARD_DIRECTION, STEPPER_STEP_INTERVAL_US, type, reason);
}

bool startDebugRun(int direction) {
  debugSpeedLevel = clampDebugSpeedLevel(debugSpeedLevel);
  return startMotion(-1,
                     direction,
                     debugSpeedLevelToInterval(debugSpeedLevel),
                     MotionType::Debug,
                     "debug");
}

bool startDebugRun(int direction, uint8_t speedLevel) {
  debugSpeedLevel = clampDebugSpeedLevel(speedLevel);
  return startMotion(-1,
                     direction,
                     debugSpeedLevelToInterval(debugSpeedLevel),
                     MotionType::Debug,
                     "debug");
}

String scheduleJson() {
  String json = "[";
  bool first = true;
  for (int i = 0; i < MAX_FEED_SLOTS; ++i) {
    if (!feedSlots[i].enabled) {
      continue;
    }
    if (!first) {
      json += ",";
    }
    json += "{\"time\":\"" + slotToString(feedSlots[i]) + "\"}";
    first = false;
  }
  json += "]";
  return json;
}

String statusJson() {
  String json = "{";
  json += "\"device\":\"" + jsonEscape(String(DEVICE_NAME)) + "\",";
  json += "\"now\":\"" + jsonEscape(formattedNow()) + "\",";
  json += "\"timeSynced\":";
  json += isTimeSynced() ? "true," : "false,";
  json += "\"motorRunning\":";
  json += motorRunning ? "true," : "false,";
  json += "\"motion\":\"" + String(motionKey(motionType)) + "\",";
  json += "\"motionLabel\":\"" + jsonEscape(String(motionLabel(motionType))) + "\",";
  json += "\"direction\":\"" + String(directionKey(motionDirection)) + "\",";
  json += "\"directionLabel\":\"" + jsonEscape(String(directionLabel(motionDirection))) + "\",";
  json += "\"stepsRemaining\":" + String(motionContinuous ? -1 : motionStepsRemaining) + ",";
  json += "\"debugSpeedLevel\":" + String(debugSpeedLevel) + ",";
  json += "\"stepIntervalUs\":" + String(currentStepIntervalUs) + ",";
  json += "\"defaultPortion\":" + String(defaultPortionSteps) + ",";
  json += "\"pinMap\":\"" + jsonEscape(getPinMapLabel()) + "\",";
  json += "\"wifiConnected\":";
  json += WiFi.status() == WL_CONNECTED ? "true," : "false,";
  json += "\"remoteEnabled\":";
  json += isRemoteControlEnabled() ? "true," : "false,";
  json += "\"remoteConnected\":";
  json += remoteBrokerConnected ? "true," : "false,";
  json += "\"accessPinEnabled\":";
  json += isAccessPinEnabled() ? "true," : "false,";
  json += "\"remoteDeviceId\":\"" + jsonEscape(String(REMOTE_DEVICE_ID)) + "\",";
  json += "\"stationIp\":\"" + getStationIp() + "\",";
  json += "\"apIp\":\"" + getApIp() + "\",";
  json += "\"schedule\":" + scheduleJson();
  json += "}";
  return json;
}

void sendJson(int code, const String &body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}

void handleStatus() {
  sendJson(200, statusJson());
}

void handleIndex() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(500, "text/plain; charset=utf-8", "index.html not found in LittleFS");
    return;
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.streamFile(file, "text/html; charset=utf-8");
  file.close();
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.send(204);
}

void handleManualFeed() {
  if (!ensureLocalAuthorized()) {
    return;
  }

  const int requestedPortion = server.hasArg("portion") ? server.arg("portion").toInt() : defaultPortionSteps;
  if (requestedPortion < 1 || requestedPortion > MAX_PORTION_STEPS) {
    sendJson(400, "{\"error\":\"portion out of range\"}");
    return;
  }

  if (!startFeedCycle(static_cast<uint8_t>(requestedPortion), MotionType::ManualFeed, "manual")) {
    sendJson(409, "{\"error\":\"motor busy\"}");
    return;
  }

  sendJson(200, statusJson());
}

void handleDebugStart() {
  if (!ensureLocalAuthorized()) {
    return;
  }

  String direction = server.hasArg("direction") ? server.arg("direction") : "forward";
  direction.toLowerCase();
  const int requestedSpeed = server.hasArg("speed") ? server.arg("speed").toInt() : debugSpeedLevel;

  int resolvedDirection = STEPPER_FORWARD_DIRECTION;
  if (direction == "reverse") {
    resolvedDirection = -STEPPER_FORWARD_DIRECTION;
  }

  if (!startDebugRun(resolvedDirection, static_cast<uint8_t>(requestedSpeed))) {
    sendJson(409, "{\"error\":\"motor busy\"}");
    return;
  }

  sendJson(200, statusJson());
}

void handleStop() {
  if (!ensureLocalAuthorized()) {
    return;
  }

  stopMotion("user-stop");
  sendJson(200, statusJson());
}

void handleScheduleSave() {
  if (!ensureLocalAuthorized()) {
    return;
  }

  const String slots = server.arg("slots");
  const int portion = server.hasArg("portion") ? server.arg("portion").toInt() : defaultPortionSteps;

  if (portion < 1 || portion > MAX_PORTION_STEPS) {
    sendJson(400, "{\"error\":\"portion out of range\"}");
    return;
  }

  if (!applySlotsCsv(slots)) {
    sendJson(400, "{\"error\":\"slots must be comma-separated HH:MM values\"}");
    return;
  }

  defaultPortionSteps = static_cast<uint8_t>(portion);
  saveSchedule();
  publishRemoteStatus(true);
  sendJson(200, statusJson());
}

void handleRemoteCommand(const String &payload) {
  const String action = extractCommandValue(payload, "action");
  if (action.isEmpty()) {
    Serial.printf("[REMOTE] ignored payload without action: %s\n", payload.c_str());
    return;
  }

  Serial.printf("[REMOTE] action=%s payload=%s\n", action.c_str(), payload.c_str());

  if (action != "status" && !isAuthorizedPin(extractCommandValue(payload, "pin"))) {
    Serial.printf("[REMOTE] rejected action=%s because pin is invalid\n", action.c_str());
    return;
  }

  if (action == "feed") {
    const int portion = extractCommandValue(payload, "portion").toInt();
    const uint8_t resolvedPortion = static_cast<uint8_t>(constrain(portion > 0 ? portion : defaultPortionSteps,
                                                                   1,
                                                                   MAX_PORTION_STEPS));
    if (!startFeedCycle(resolvedPortion, MotionType::ManualFeed, "remote-manual")) {
      Serial.println("[REMOTE] feed ignored because motor is busy");
    }
    return;
  }

  if (action == "debug") {
    String direction = extractCommandValue(payload, "direction");
    direction.toLowerCase();
    const int speed = extractCommandValue(payload, "speed").toInt();
    const int resolvedDirection = direction == "reverse" ? -STEPPER_FORWARD_DIRECTION : STEPPER_FORWARD_DIRECTION;
    const uint8_t resolvedSpeed = clampDebugSpeedLevel(speed > 0 ? speed : debugSpeedLevel);
    if (!startDebugRun(resolvedDirection, resolvedSpeed)) {
      Serial.println("[REMOTE] debug ignored because motor is busy");
    }
    return;
  }

  if (action == "stop") {
    stopMotion("remote-stop");
    return;
  }

  if (action == "schedule") {
    const String slots = extractCommandValue(payload, "slots");
    const int portion = extractCommandValue(payload, "portion").toInt();
    const uint8_t resolvedPortion = static_cast<uint8_t>(constrain(portion > 0 ? portion : defaultPortionSteps,
                                                                   1,
                                                                   MAX_PORTION_STEPS));

    if (!applySlotsCsv(slots)) {
      Serial.println("[REMOTE] schedule update ignored because slots are invalid");
      return;
    }

    defaultPortionSteps = resolvedPortion;
    saveSchedule();
    publishRemoteStatus(true);
    return;
  }

  if (action == "status") {
    publishRemoteStatus(true);
    return;
  }

  Serial.printf("[REMOTE] unknown action=%s\n", action.c_str());
}

void onRemoteMqttMessage(char *topic, byte *payload, unsigned int length) {
  String body;
  body.reserve(length);
  for (unsigned int i = 0; i < length; ++i) {
    body += static_cast<char>(payload[i]);
  }

  if (String(topic) != remoteCommandTopic()) {
    return;
  }

  handleRemoteCommand(body);
}

void publishRemoteStatus(bool force) {
  if (!isRemoteControlEnabled() || !mqttClient.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (!force && now - lastRemoteStatusPublishAt < MQTT_STATUS_INTERVAL_MS) {
    return;
  }

  lastRemoteStatusPublishAt = now;
  const String body = statusJson();
  mqttClient.publish(remoteStatusTopic().c_str(), body.c_str(), true);
}

bool connectRemoteBroker() {
  if (!isRemoteControlEnabled() || WiFi.status() != WL_CONNECTED) {
    remoteBrokerConnected = false;
    return false;
  }

  if (MQTT_USE_TLS) {
#if MQTT_TLS_INSECURE
    mqttSecureClient.setInsecure();
#endif
  }

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(1024);
  mqttClient.setCallback(onRemoteMqttMessage);

  const String clientId = remoteClientId();
  const bool connected = mqttClient.connect(clientId.c_str(),
                                            strlen(MQTT_USERNAME) > 0 ? MQTT_USERNAME : nullptr,
                                            strlen(MQTT_PASSWORD) > 0 ? MQTT_PASSWORD : nullptr,
                                            remoteAvailabilityTopic().c_str(),
                                            0,
                                            true,
                                            "offline");

  remoteBrokerConnected = connected;
  if (!connected) {
    Serial.printf("[REMOTE] mqtt connect failed state=%d\n", mqttClient.state());
    return false;
  }

  mqttClient.publish(remoteAvailabilityTopic().c_str(), "online", true);
  mqttClient.subscribe(remoteCommandTopic().c_str());
  Serial.printf("[REMOTE] mqtt connected host=%s device=%s\n", MQTT_HOST, REMOTE_DEVICE_ID);
  publishRemoteStatus(true);
  return true;
}

void maintainRemoteBroker() {
  if (!isRemoteControlEnabled()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    remoteBrokerConnected = false;
    return;
  }

  if (mqttClient.connected()) {
    remoteBrokerConnected = true;
    mqttClient.loop();
    publishRemoteStatus(false);
    return;
  }

  remoteBrokerConnected = false;
  if (millis() - lastMqttReconnectAttemptAt < MQTT_RECONNECT_INTERVAL_MS) {
    return;
  }

  lastMqttReconnectAttemptAt = millis();
  connectRemoteBroker();
}

void connectWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(DEVICE_NAME);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("[WIFI] AP ready ssid=%s ip=%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[WIFI] no station credentials configured, AP mode only");
    return;
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const unsigned long startAt = millis();
  Serial.printf("[WIFI] connecting to %s\n", WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED && millis() - startAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] connected, ip=%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WIFI] station connect timeout, AP fallback remains available");
  }
}

void syncClock() {
  configTzTime(APP_TIMEZONE, "ntp.aliyun.com", "pool.ntp.org", "time.cloudflare.com");
}

void setupRoutes() {
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
      return;
    }
    sendJson(404, "{\"error\":\"not found\"}");
  });
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/feed", HTTP_POST, handleManualFeed);
  server.on("/api/debug/start", HTTP_POST, handleDebugStart);
  server.on("/api/debug/stop", HTTP_POST, handleStop);
  server.on("/api/schedule", HTTP_POST, handleScheduleSave);
}

void updateMotor() {
  if (!motorRunning) {
    return;
  }

  const unsigned long now = micros();
  if (now - lastMotorStepAtUs < currentStepIntervalUs) {
    return;
  }

  lastMotorStepAtUs = now;
  currentSequenceIndex = static_cast<uint8_t>((currentSequenceIndex + motionDirection + 8) % 8);
  writeStepperPhase(currentSequenceIndex);

  if (!motionContinuous) {
    motionStepsRemaining--;
    if (motionStepsRemaining <= 0) {
      stopMotion("completed");
    }
  }
}

void processSchedule() {
  if (!isTimeSynced() || motorRunning) {
    return;
  }

  struct tm info;
  if (!getLocalTime(&info)) {
    return;
  }

  for (int i = 0; i < MAX_FEED_SLOTS; ++i) {
    FeedSlot &slot = feedSlots[i];
    if (!slot.enabled) {
      continue;
    }
    if (slot.hour == info.tm_hour && slot.minute == info.tm_min && slot.lastTriggeredDay != info.tm_yday) {
      if (startFeedCycle(defaultPortionSteps, MotionType::ScheduleFeed, "schedule")) {
        slot.lastTriggeredDay = info.tm_yday;
      }
    }
  }
}

void logStatusPeriodically() {
  if (millis() - lastStatusLogAt < 15000) {
    return;
  }

  lastStatusLogAt = millis();
  Serial.printf("[STATUS] now=%s sta=%s ap=%s motion=%s portion=%u remaining=%ld\n",
                formattedNow().c_str(),
                getStationIp().c_str(),
                getApIp().c_str(),
                motionLabel(motionType),
                defaultPortionSteps,
                motionContinuous ? -1L : motionStepsRemaining);
}

}  // namespace

void setup() {
  Serial.begin(115200);

  for (uint8_t pin : kStepperPins) {
    pinMode(pin, OUTPUT);
  }
  releaseStepper();

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] failed to mount LittleFS");
  }

  preferences.begin("fitpass", false);
  loadSchedule();

  connectWiFi();
  connectRemoteBroker();
  syncClock();
  setupRoutes();
  server.begin();

  Serial.printf("[MOTOR] pin map %s\n", getPinMapLabel().c_str());
  if (isRemoteControlEnabled()) {
    Serial.printf("[REMOTE] enabled device=%s topic=%s\n",
                  REMOTE_DEVICE_ID,
                  remoteTopicBase().c_str());
  } else {
    Serial.println("[REMOTE] disabled");
  }
  Serial.println("[BOOT] FitPass ready");
}

void loop() {
  server.handleClient();
  maintainRemoteBroker();
  updateMotor();
  processSchedule();
  logStatusPeriodically();
  delay(0);
}
