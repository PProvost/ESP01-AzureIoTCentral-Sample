#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <Time.h>

/*
#include <sdk/iothub_client.h>
#include <sdk/iothub_message.h>
#include <azure_c_shared_utility/platform.h>
#include <azure_c_shared_utility/threadapi.h>
#include <azure_c_shared_utility/macro_utils.h>
#include <sdk/iothubtransportmqtt.h>
#include <sdk/iothub_client_options.h>
#include <AzureIoTProtocol_MQTT.h>
*/

#include <SimpleTimer.h>      // Lightweight scheduling of tasks
#include <ArduinoJson.h>      // JSON Parsing for Arduino
#include <DNSServer.h>        // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h> // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>      // https://github.com/tzapu/WiFiManager WiFi Configuration Magic

// Uncomment to disable all logging. May significantly reduce sketch size.
// #define DISABLE_LOGGING
#include <ArduinoLog.h>

#include "HubConnection.h"
#include "config.h"

// Times before 2010 (1970 + 40 years) are invalid
#define MIN_EPOCH 40 * 365 * 24 * 3600

// Max message size
#define MESSAGE_MAX_LEN 1024

// Max length of Connection String
#define CONN_STR_MAX_LEN 512

// Used for random simluated telemetry values
const double minTemperature = -20.0;
const double minPressure = 80.0;

// Globals
StaticJsonBuffer<MESSAGE_MAX_LEN> jsonBuffer;
SimpleTimer timer;
static bool loopActive = false;
static char iotConnStr[CONN_STR_MAX_LEN];
bool shouldSaveConfig = false;

// Objectify
HubConnection hubConnection;

void InitSerial()
{
    delay(1000);
    // Start serial and initialize stdout
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    delay(1000); // Wait for serial
}

void InitWifi()
{
    if (SPIFFS.begin())
    {
        Log.trace("Mounted file system" CR);

        Dir dir = SPIFFS.openDir("/");
        Serial.println("**SPIFFS Dir");
        while (dir.next())
        {
            Serial.println(dir.fileName());
        }

        if (SPIFFS.exists("/connstr.txt"))
        {
            //file exists, reading and loading
            Log.trace("Reading connection string file" CR);
            File connstrFile = SPIFFS.open("/connstr.txt", "r");
            if (connstrFile)
            {
                Log.trace("opened connection string file" CR);
                size_t size = connstrFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                connstrFile.readBytes(buf.get(), size);
                strcpy(iotConnStr, buf.get());

                Log.trace("Connection string loaded from file: %s" CR, iotConnStr);
            }
        }
        else
        {
            Log.warning("Connection string file does not exist" CR);
        }
    }
    else
    {
        Log.trace("failed to mount FS");
    }

    WiFiManagerParameter custom_connstr("iotConnStr", "IoT Connection String", iotConnStr, CONN_STR_MAX_LEN);

    WiFiManager wifiManager;
    wifiManager.setDebugOutput(ENABLE_WIFI_MANAGER_LOGGING);
    wifiManager.setSaveConfigCallback([]() { shouldSaveConfig = true; });
    wifiManager.addParameter(&custom_connstr);

    if (RESET_CONFIG)
    {
        Log.notice("Resetting all saved config" CR);
        wifiManager.resetSettings();
        SPIFFS.format();
    }

    wifiManager.autoConnect();

    strcpy(iotConnStr, custom_connstr.getValue());

    if (shouldSaveConfig)
    {
        Log.trace("Saving config file" CR);
        File connstrFile = SPIFFS.open("/connstr.txt", "w");
        if (!connstrFile)
        {
            Log.trace("Failed to open config file for writing" CR);
        }
        else
        {
            Log.trace("Writing config file" CR);
            connstrFile.write((uint8_t *)iotConnStr, CONN_STR_MAX_LEN);
            connstrFile.print(iotConnStr);
        }

        connstrFile.close();

        Dir dir = SPIFFS.openDir("/");
        Serial.println("**SPIFFS Dir");
        while (dir.next())
        {
            Serial.println(dir.fileName());
        }
    }
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

    Log.notice("== Enabling Device Twin Callback ==" CR);
    if (hubConnection.registerDesiredPropertyCallback("fan-speed", [](std::string name, std::string val, void* context){
        Log.notice("Desired property 'fan-speed' updated: %s" CR, val.c_str());
    }) == false)
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
