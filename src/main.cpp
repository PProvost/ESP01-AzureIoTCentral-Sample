#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <Time.h>

#include <sdk/iothub_client.h>
#include <sdk/iothub_message.h>
#include <azure_c_shared_utility/platform.h>
#include <azure_c_shared_utility/threadapi.h>
#include <azure_c_shared_utility/macro_utils.h>
#include <sdk/iothubtransportmqtt.h>
#include <sdk/iothub_client_options.h>
#include <AzureIoTProtocol_MQTT.h>

#include <SimpleTimer.h>      // Lightweight scheduling of tasks
#include <ArduinoJson.h>      // JSON Parsing for Arduino
#include <DNSServer.h>        // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h> // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>      // https://github.com/tzapu/WiFiManager WiFi Configuration Magic

// Uncomment to disable all logging. May significantly reduce sketch size.
// #define DISABLE_LOGGING
#include <ArduinoLog.h>

#include "config.h"

// Times before 2010 (1970 + 40 years) are invalid
#define MIN_EPOCH 40 * 365 * 24 * 3600

// Max message size
#define MESSAGE_MAX_LEN 1024

// Max length of Connection String
#define CONN_STR_MAX_LEN    512

// Used for random simluated telemetry values
const double minTemperature = -20.0;
const double minPressure = 80.0;

// Globals
StaticJsonBuffer<MESSAGE_MAX_LEN> jsonBuffer;
SimpleTimer timer;
static char msgText[MESSAGE_MAX_LEN];
static IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle = NULL;
static IOTHUB_MESSAGE_HANDLE messageHandle = NULL;
static bool loopActive = true;
static time_t lastTokenTime;
static char iotConnStr[CONN_STR_MAX_LEN];
bool shouldSaveConfig = false;

void InitSerial()
{
    // Start serial and initialize stdout
    Serial.begin(115200);
    Serial.setDebugOutput(true);
}

void InitWifi()
{
    if (SPIFFS.begin())
    {
        Log.trace("Mounted file system" CR);
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
            connstrFile.write((uint8_t *)iotConnStr, 256);
        }

        connstrFile.close();
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

void ConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void *userContextCallback)
{
    Log.trace("**CONNECTION STATUS CALLBACK** - %s" CR, ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS_REASON, reason));
}

void DesiredPropertiesCallback(DEVICE_TWIN_UPDATE_STATE update_state, const unsigned char *payLoad, size_t size, void *userContextCallback)
{
    Log.trace("**DESIRED PROP** - %d" CR, update_state);

    // The following sample code shows how to parse the incoming twin change
    // using the ArduinoJson library.
    //
    // See https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-device-twins#device-twins

    /*
    jsonBuffer.clear();
    JsonObject &root = jsonBuffer.parseObject(payLoad);
    if (!root.success())
    {
        Serial.printf("parseTwinMessage: Parse %s failed.\r\n", payLoad);
        return;
    }

    if (root["desired"]["interval"].success())
    {
       sleepInterval = root["desired"]["interval"];
    }
    else if (root.containsKey("interval"))
    {
       sleepInterval = root["interval"];
    }

    // TODO: Send the reported property as acknowledgement

    */
}

int DeviceDirectMethodCallback(const char *method_name, const unsigned char *payload, size_t size, unsigned char **response, size_t *response_size, void *userContextCallback)
{
    // NOTE: The malloc below seems like it should leak, but it doesn't because the IoT SDK will free the memory.
    //       In my experimenting, a static buffer caused a runtime exception. Also, there is nothing in the docs
    //       that indicates what the return value from this callback should be. I'm assuming that it is an HTTP
    //       response code for now and returning 200.

    Log.trace("**METHOD CALL** - %s" CR, method_name);
    const char *resp = "{}"; // NOTE: For Central, the return response MUST be valid JSON.
    *response_size = strlen(resp);
    *response = (unsigned char *)malloc(*response_size);
    (void)memcpy(*response, resp, *response_size);
    return 200;
}

void TelemetryConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *userContextCallback)
{
    // You can use this for retry logic or anything else that would want to react to the success
    // or failure of the message.
    //
    // IMPORTANT! You should clean up the allocated message once you've confirmed that it was sent successfully.

    Log.trace("Telemetry Confirmation Receieved: %s" CR, ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));
    if (result == IOTHUB_CLIENT_CONFIRMATION_OK && messageHandle != NULL)
    {
        IoTHubMessage_Destroy(messageHandle);
        messageHandle = NULL;
    }
}

void ReportedStateCallback(int status_code, void *userContextCallback)
{
    // You can use this for retry logic or anything else that would want to react to the success
    // or failure of the message.
}

void SendTelemetry()
{
    double temperature = minTemperature + (rand() % 10);
    double pressure = minPressure + (rand() % 20);
    sprintf_s(msgText, sizeof(msgText), "{\"temp\":%.2f,\"pressure\":%.2f}", temperature, pressure);
    if ((messageHandle = IoTHubMessage_CreateFromString(msgText)) == NULL)
        Log.error("ERROR: iotHubMessageHandle is NULL!" CR);
    else
    {
        if (IoTHubClient_LL_SendEventAsync(iotHubClientHandle, messageHandle, TelemetryConfirmationCallback, NULL) != IOTHUB_CLIENT_OK)
            Log.error("ERROR: IoTHubClient_LL_SendEventAsync..........FAILED!" CR);
        else
            Log.trace("IoTHubClient_LL_SendEventAsync accepted message for transmission to IoT Hub." CR);
    }
}

void SendReportedProperty(const char *payload)
{
    IOTHUB_CLIENT_RESULT result = IoTHubClient_LL_SendReportedState(iotHubClientHandle,
                                                                    (const unsigned char *)payload,
                                                                    strlen(payload),
                                                                    ReportedStateCallback, NULL);

    if (result != IOTHUB_CLIENT_OK)
        Log.error("Failure sending reported property!!!" CR);
    else
        Log.trace("IoTHubClient::sendReportedProperty COMPLETED" CR);
}

void SendAllReportedProperties()
{
    sprintf_s(msgText, sizeof(msgText), "{\"wifi_ap_name\":\"%s\"}", WiFi.SSID().c_str());
    SendReportedProperty(msgText);
}

void CheckHubConnection(bool force = false)
{
    time_t current = time(NULL);

    if (force || (current > lastTokenTime + 3000)) // force refresh after 50m (3000 sec)
    {
        IoTHubClient_LL_Destroy(iotHubClientHandle);

        if ((iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(iotConnStr, MQTT_Protocol)) == NULL)
        {
            Log.fatal("ERROR: iotHubClientHandle is NULL!" CR);
            loopActive = false;
            return;
        }

        lastTokenTime = current; //time(NULL);
    }

#ifdef SDK_LOG_TRACING
    bool traceOn = true;
    IoTHubClient_LL_SetOption(iotHubClientHandle, OPTION_LOG_TRACE, &traceOn);
#endif

    // If you want to change the SDK retry policy, uncomment the following line and set the parameters as you see fit
    // IoTHubClient_LL_SetRetryPolicy(iotHubClientHandle, IOTHUB_CLIENT_RETRY_EXPONENTIAL_BACKOFF, 1200);
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

    if (platform_init() != 0)
    {
        Log.fatal("Failed to initialize the platform." CR);
        loopActive = false;
        return;
    }

    CheckHubConnection(true);
    if (!loopActive)
        return;

    Log.notice("== Enabling Device Method Callback ==" CR);
    if (IoTHubClient_LL_SetDeviceMethodCallback(iotHubClientHandle, DeviceDirectMethodCallback, NULL) != IOTHUB_CLIENT_OK)
    {
        Log.error("IoTHubClient_LL_SetDeviceMethodCallback..........FAILED!" CR);
        return;
    }

    Log.notice("== Enabling Device Twin Callback ==" CR);
    if (IoTHubClient_LL_SetDeviceTwinCallback(iotHubClientHandle, DesiredPropertiesCallback, NULL) != IOTHUB_CLIENT_OK)
    {
        Log.error("IoTHubClient_LL_SetDeviceTwinCallback..........FAILED!" CR);
        return;
    }

    Log.notice("== Enabling Connection Status Callback ==" CR);
    if (IoTHubClient_LL_SetConnectionStatusCallback(iotHubClientHandle, ConnectionStatusCallback, NULL) != IOTHUB_CLIENT_OK)
    {
        Log.error("IoTHubClient_LL_SetConnectionStatusCallback..............FAILED!" CR);
        return;
    }

    Log.notice("== Enabling Reported Properties every 5 min ==");
    timer.setInterval(5 * 60 * 1000, SendAllReportedProperties); // Every 5 mins

    Log.notice("== Enabling Telemetry every 5 sec ==" CR);
    timer.setInterval(5 * 1000, SendTelemetry);

#ifdef ENABLE_FREE_HEAP_LOGGING
    Log.notice("== Enabling Free Heap Dump event 10 sec ==" CR);
    timer.setInterval(10 * 1000, DumpFreeHeap); // Every 10 seconds
#endif

    Log.trace("setup() complete" CR);
    Log.trace("loopActive=%T" CR, loopActive);
}

void loop()
{
    if (loopActive)
    {
        CheckHubConnection();
        timer.run();
        IoTHubClient_LL_DoWork(iotHubClientHandle);
        delay(100);
    }
}
