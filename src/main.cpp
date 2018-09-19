#include <Arduino.h>
#include <Time.h>

#include <SimpleTimer.h>      // Lightweight scheduling of tasks
#include <ArduinoJson.h>      // JSON Parsing for Arduino
#include <DNSServer.h>        // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h> // Local WebServer used to serve the configuration portal
#include <ArduinoLog.h>

#include "HubConnection.h"
#include "Wifi.h"
#include "config.h"

// Epoch Time - Times before 2010 (1970 + 40 years) are invalid
#define MIN_EPOCH 40 * 365 * 24 * 3600

// Used for random simluated telemetry values
const double minTemperature = -20.0;
const double minPressure = 80.0;

// Globals
char iotConnStr[CONN_STR_MAX_LEN];
static SimpleTimer timer;
static bool loopActive = false;

// Objectify
HubConnection hubConnection;

void InitSerial()
{
    // Start serial and initialize stdout
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    delay(1000); // Wait for serial
}


void InitTime()
{
    time_t epochTime;

    Log.notice("Configuring NTP..." CR);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    while (true)
    {
        epochTime = time(NULL);

        if (epochTime < MIN_EPOCH)
        {
            Log.warning("Fetching NTP epoch time failed! Waiting 2 seconds to retry." CR);
            delay(2000);
        }
        else
        {
            Log.notice("Fetched NTP epoch time is: %ld" CR, epochTime);
            break;
        }

        delay(10);
    }
}

void SendTelemetry()
{
    double temperature = minTemperature + (rand() % 10);
    double pressure = minPressure + (rand() % 20);

    std::map<std::string, double> telemetryMap;
    telemetryMap["temp"] = temperature;
    telemetryMap["pressure"] = pressure;

    hubConnection.sendMeasurements(telemetryMap);
}

void SendReportedProperty_SSID()
{
    if (hubConnection.sendReportedProperty("wifi_ap_name", WiFi.SSID().c_str()) == false)
        Log.error("Failure sending reported property!!!" CR);
    else
        Log.trace("hubConnection.sendReportedProperty COMPLETED" CR);
}

#ifdef ENABLE_FREE_HEAP_LOGGING
void DumpFreeHeap()
{
    Log.trace("Free Heap: %d" CR, ESP.getFreeHeap());
}
#endif

void setup()
{
    InitSerial();
    Log.begin(LOG_LEVEL, &Serial, true);
    Log.notice("setup() starting" CR);

    InitWifi();
    InitTime();

    if (!hubConnection.setup(iotConnStr))
    {
        return;
    }

    auto rebootCallback = [](std::string name, void *context) -> std::string {
        Log.notice("Device method \"reboot\" called." CR);

        // Note: IoT Central expects the return payload to be valid JSON
        return "{}";
    };

    Log.notice("== Enabling Device Method Callback ==" CR);
    if (hubConnection.registerDeviceMethod("reboot", rebootCallback) == false)
    {
        Log.error("Register device method FAILED!" CR);
        return;
    }

    auto connectionStatusCallback = [](IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason) {
        Log.notice("Connection status callback: %s - %s" CR, ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS, result), ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS_REASON, reason));
    };

    Log.notice("== Enabling Connection Status Callback ==" CR);
    if (hubConnection.registerConnectionStatusCallback(connectionStatusCallback) == false)
    {
        Log.error("hubConnection.registerConnectionStatusCallback FAILED!" CR);
        return;
    }

    auto desiredPropCallback = [](std::string name, std::string val, void* context)->bool{
        Log.notice("Desired property '%s' updated: %s" CR, name.c_str(), val.c_str());
        return true;
    };

    Log.notice("== Enabling Device Twin Callback ==" CR);
    if (hubConnection.registerDesiredPropertyCallback("fan-speed", desiredPropCallback) == false)
    {
        Log.error("hubConnection.registerDesiredPropertyCallback FAILED!" CR);
        return;
    }

    Log.notice("== Enabling Reported Property (SSID) every 5 min ==" CR);
    timer.setInterval(5 * 60 * 1000, SendReportedProperty_SSID); // Every 5 mins

    Log.notice("== Enabling Telemetry every 5 sec ==" CR);
    timer.setInterval(5 * 1000, SendTelemetry);

#ifdef ENABLE_FREE_HEAP_LOGGING
    Log.notice("== Enabling Free Heap Dump event 10 sec ==" CR);
    timer.setInterval(10 * 1000, DumpFreeHeap); // Every 10 seconds
#endif

    // If we got here, activate the loop()
    loopActive = true;
    Log.trace("setup() completes successfully" CR);
}

void loop()
{
    if (loopActive)
    {
        timer.run();
        hubConnection.loop();
        delay(100);
    }
}
