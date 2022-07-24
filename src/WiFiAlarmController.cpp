/*
 *  WiFi Alarm Controller for Bose Wave Radio
 *
 *  Serves up a webapp on port 80 that allows control of alarm functions
 *  on a Bose Wave Radio II.
 *	IR LED on pin 5; files in data/ uploaded to SPIFFS.
 *
 *  Adapted from EspAsyncWebServer 'simple_server' and Infrared4Arduino
 *  'IrSenderPwm' examples
 */

#include "SPIFFS.h"
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include <IrSenderPwm.h> // https://github.com/bengtmartensson/Infrared4Arduino
#include <Preferences.h>
#include <WiFi.h>

AsyncWebServer server(80);
Preferences alarm_settings;

const char *ssid = "YOUR_WIFI_NETWORK";
const char *password = "YOUR_WIFI_PASSWORD";

static constexpr int HALF_DAY_IN_MINUTES = 12 * 60;

// IR transmit stuff
static constexpr frequency_t necFrequency = 38400U;
static constexpr unsigned long BAUD = 115200UL;
static constexpr pin_t PIN = 5U;

static const microseconds_t onOff[] = {
    // ON, OFF (in 10's of microseconds)
    10200, 1560, 500, 520,  500, 520,  500, 1540, 500, 1560, 500, 520,
    500,   520,  500, 1540, 500, 520,  520, 1540, 500, 1540, 520, 520,
    500,   500,  520, 1540, 500, 1560, 500, 520,  500, 1540, 500, 51900,
    1020,  1560, 500, 520,  500, 520,  500, 1540, 500, 1560, 500, 520,
    500,   520,  500, 1560, 480, 540,  480, 1560, 500, 1560, 500, 520,
    500,   520,  500, 1540, 500, 1560, 500, 520,  500, 1540, 500, 0};

static const microseconds_t timePlus[] = {
    // ON, OFF (in 10's of microseconds)
    1000, 1560, 640, 380,  500, 520, 500, 1540, 520, 500,  520, 500,
    520,  1540, 500, 520,  500, 520, 500, 1560, 500, 1540, 500, 520,
    500,  1560, 500, 1540, 500, 520, 500, 1560, 500, 1540, 500, 51920,
    1000, 1560, 500, 520,  500, 520, 500, 1560, 480, 540,  500, 520,
    480,  1560, 500, 520,  500, 520, 500, 1560, 500, 1540, 500, 520,
    500,  1560, 500, 1540, 500, 520, 520, 1540, 500, 1540, 500, 0};

static const microseconds_t timeMinus[] = {
    // ON, OFF (in 10's of microseconds)
    1020, 1540, 500, 520, 500, 1560, 500, 1540, 500, 1560, 500, 1560,
    480,  540,  500, 520, 500, 1540, 500, 1560, 580, 440,  500, 520,
    500,  520,  500, 520, 500, 1540, 500, 1560, 500, 520,  580, 51800,
    1160, 1420, 500, 520, 500, 1540, 500, 1560, 500, 1560, 500, 1540,
    500,  520,  500, 520, 500, 1560, 500, 1540, 500, 520,  500, 520,
    500,  520,  500, 520, 500, 1560, 500, 1540, 500, 520,  500, 0};

static const microseconds_t alarmOnOff[] = {
    // ON, OFF (in 10's of microseconds)
    1000, 1560, 500, 520,  500, 1560, 480, 540,  500, 520,  500, 520,
    500,  1540, 500, 520,  500, 520,  500, 1560, 500, 520,  500, 1540,
    500,  1560, 500, 1540, 520, 500,  520, 1540, 500, 1540, 500, 51900,
    1020, 1560, 500, 520,  500, 1540, 500, 520,  500, 520,  500, 520,
    500,  1560, 500, 520,  500, 520,  500, 1540, 500, 540,  620, 1420,
    500,  1560, 500, 1540, 500, 520,  500, 1560, 500, 1540, 500, 0};

static const microseconds_t alarmTime[] = {
    // ON, OFF (in 10's of microseconds)
    1020, 1560, 500, 1560, 500, 1540, 500, 520,  500, 520,  500, 520,
    500,  1560, 500, 520,  500, 520,  500, 520,  500, 520,  500, 1540,
    500,  1560, 500, 1560, 480, 540,  480, 1560, 500, 1540, 500, 51900,
    1020, 1560, 500, 1540, 500, 1560, 500, 520,  500, 520,  500, 520,
    500,  1560, 480, 540,  480, 540,  480, 540,  480, 540,  480, 1560,
    500,  1560, 500, 1540, 500, 520,  500, 1560, 500, 1540, 500, 0};

static const IrSequence onOffSequence(onOff,
                                      sizeof(onOff) / sizeof(microseconds_t));
static const IrSequence timePlusSequence(timePlus, sizeof(timePlus) /
                                                       sizeof(microseconds_t));
static const IrSequence
    timeMinusSequence(timeMinus, sizeof(timeMinus) / sizeof(microseconds_t));
static const IrSequence
    alarmOnOffSequence(alarmOnOff, sizeof(alarmOnOff) / sizeof(microseconds_t));
static const IrSequence
    alarmTimeSequence(alarmTime, sizeof(alarmTime) / sizeof(microseconds_t));
static IrSender *irSender;
dutycycle_t dutyCycle;
enum TimeAdjMode { SET, FIX };
enum AM_PM { AM, PM };

// vars for alarm time
static int hour = 5;
static int minute = 15;
static bool isAM = true;
static int minuteDifference = 0;

// 'GET'-request parameter names
const char *PARAM_MESSAGE = "message";
const char *PARAM_HOUR = "hour";
const char *PARAM_MINUTE = "minute";
const char *PARAM_AMPM = "ampm";
const char *PARAM_REQTYPE = "type";

// prototypes
String processHomePage(const String &);
String processSetTimePage(const String &);
String processFixTimePage(const String &);
void updateInternalAlarmTime(int, int, AM_PM);
void updateMinuteDifference(int, int, AM_PM);

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Error: Not found");
}

void setup() {
  // settings storage setup/load
  alarm_settings.begin("bwrc_sett", true);
  hour = alarm_settings.getInt("hour", 5);
  minute = alarm_settings.getInt("minute", 15);
  isAM = alarm_settings.getBool("isAM", true);
  alarm_settings.end();

  // IR setup
  randomSeed(analogRead(A0));
  irSender = IrSenderPwm::getInstance(true, PIN);
  dutyCycle = static_cast<dutycycle_t>(random(20, 80));

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("WiFi Failed!\n");
    return;
  }
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // start the fs to enable loading of HTML files
  SPIFFS.begin();

  // "set time" form sends GET requests to the base path
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    int h = -1;
    int m = -1;
    AM_PM ampm = AM;

    if (request->hasParam(PARAM_HOUR)) {
      h = request->getParam(PARAM_HOUR)->value().toInt();
    }

    if (request->hasParam(PARAM_MINUTE)) {
      m = request->getParam(PARAM_MINUTE)->value().toInt();
    }

    if (request->hasParam(PARAM_AMPM)) {
      ampm = request->getParam(PARAM_AMPM)->value() == "am" ? AM : PM;
    }

    if (request->hasParam(PARAM_REQTYPE)) {
      if (request->getParam(PARAM_REQTYPE)->value() == "set") {
        updateMinuteDifference(h, m, ampm);
      }
    }

    if (h != -1 && m != -1) {
      updateInternalAlarmTime(h, m, ampm);
    }

    request->send(SPIFFS, "/index.html", String(), false, processHomePage);
  });

  server.on("/onoff", HTTP_GET, [](AsyncWebServerRequest *request) {
    irSender->send(onOffSequence, necFrequency, dutyCycle);
    request->send(SPIFFS, "/index.html", String(), false, processHomePage);
  });

  server.on("/alonoff", HTTP_GET, [](AsyncWebServerRequest *request) {
    irSender->send(alarmOnOffSequence, necFrequency, dutyCycle);
    request->send(SPIFFS, "/index.html", String(), false, processHomePage);
  });

  server.on("/setalarm", HTTP_GET, [](AsyncWebServerRequest *request) {
    // request->send(200, "text/html", generateSetTimePage(SET));
    request->send(SPIFFS, "/set_time.html", String(), false,
                  processSetTimePage);
  });

  server.on("/fixalarm", HTTP_GET, [](AsyncWebServerRequest *request) {
    // request->send(200, "text/html", generateSetTimePage(FIX));
    request->send(SPIFFS, "/set_time.html", String(), false,
                  processFixTimePage);
  });

  server.onNotFound(notFound);

  server.begin();
}

void loop() {
  // always be checking to see whether a new alarm time has been set
  if (minuteDifference) {
    irSender->send(alarmTimeSequence, necFrequency, dutyCycle);
    delay(200);

    while (minuteDifference > 0) {
      irSender->send(timePlusSequence, necFrequency, dutyCycle);
      --minuteDifference;
      delay(200);
    }
    while (minuteDifference < 0) {
      irSender->send(timeMinusSequence, necFrequency, dutyCycle);
      ++minuteDifference;
      delay(200);
    }

    irSender->send(alarmOnOffSequence, necFrequency, dutyCycle);
  }
}

// processing functions handle replacement of template %VARIABLES%
String processHomePage(const String &param) {
  if (param == "ALARM_HOUR_STR") {
    return String(hour);
  } else if (param == "ALARM_MINUTE_STR") {
    String prefix = minute < 10 ? "0" : "";
    return prefix + minute;
  } else if (param == "ALARM_AM_PM_STR") {
    return isAM ? "AM" : "PM";
  }
  return "";
}

String processSetTimePage(const String &param) {
  if (param == "ACTION_STR") {
    return "Set";
  } else if (param == "ACTION_TYPE") {
    return "set";
  }
  return "";
}

String processFixTimePage(const String &param) {
  if (param == "ACTION_STR") {
    return "Fix";
  } else if (param == "ACTION_TYPE") {
    return "fix";
  }
  return "";
}

void updateInternalAlarmTime(int newHour, int newMinute, AM_PM ampm) {
  // update global variables with new values
  hour = newHour;
  minute = newMinute;
  isAM = (ampm == AM);

  // save alarm time setting to non-volatile memory
  alarm_settings.begin("bwrc_sett", false);
  alarm_settings.putInt("hour", hour);
  alarm_settings.putInt("minute", minute);
  alarm_settings.putBool("isAM", isAM);
  alarm_settings.end();
}

void updateMinuteDifference(int newHour, int newMinute, AM_PM ampm) {
  // calculate minute difference between current and desired times
  int hourDiff = newHour - hour;
  int minuteDiff = newMinute - minute;
  int hourOffset = 0;
  AM_PM currAMPM = isAM ? AM : PM;
  if (ampm != currAMPM)
    hourOffset = 12;
  hourDiff += hourOffset;
  minuteDifference = minuteDiff + (hourDiff * 60);

  // make sure we take the shorter path to our destination time
  if (minuteDifference > HALF_DAY_IN_MINUTES) {
    minuteDifference -= HALF_DAY_IN_MINUTES;
  } else if (minuteDifference < (HALF_DAY_IN_MINUTES * -1)) {
    minuteDifference += HALF_DAY_IN_MINUTES;
  }
}
