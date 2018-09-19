#include <sdk/iothub_client.h>
#include <sdk/iothub_message.h>
#include <azure_c_shared_utility/platform.h>
#include <azure_c_shared_utility/threadapi.h>
#include <azure_c_shared_utility/macro_utils.h>
#include <sdk/iothubtransportmqtt.h>
#include <sdk/iothub_client_options.h>
#include <AzureIoTProtocol_MQTT.h>
#include <ArduinoLog.h>
#include <ArduinoJson.h>

#include "IoTC_Connection.h"

#define HTTP_CODE_OK                200
#define HTTP_CODE_PARTIAL_CONTENT   206
#define HTTP_CODE_NOT_IMPLEMENTED   501

typedef std::map<std::string, std::pair<MethodCallbackFunctionType, void *>> CallbackMapType;

// Ctor - default
IoTC_Connection::IoTC_Connection() : _iotHubClientHandle(NULL)
{
    // memset(this->connectionString, '\0', MAX_LEN_CONN_STR);
}

IOTHUB_CLIENT_LL_HANDLE IoTC_Connection::getHubClientHandle()
{
    return this->_iotHubClientHandle;
}

bool IoTC_Connection::setup(const std::string connectionString)
{
    if (platform_init() != 0)
    {
        Log.fatal("Failed to initialize the platform." CR);
        return false;
    }

    this->_connectionString = connectionString;

    if ((this->_iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(this->_connectionString.c_str(), MQTT_Protocol)) == NULL)
    {
        Log.fatal("ERROR: iotHubClientHandle is NULL!" CR);
        return false;
    }

    Log.trace("IoTHubClient client connected successfully" CR);

#ifdef SDK_LOG_TRACING
    bool traceOn = true;
    IoTHubClient_LL_SetOption(iotHubClientHandle, OPTION_LOG_TRACE, &traceOn);
#endif

    Log.notice("== Enabling Device Method Callback ==" CR);
    if (IoTHubClient_LL_SetDeviceMethodCallback(this->_iotHubClientHandle, IoTC_Connection::internalDeviceDirectMethodCallback, this) != IOTHUB_CLIENT_OK)
    {
        Log.error("IoTHubClient_LL_SetDeviceMethodCallback..........FAILED!" CR);
        return false;
    }

    Log.notice("== Enabling Connection Status Callback ==" CR);
    if (IoTHubClient_LL_SetConnectionStatusCallback(this->_iotHubClientHandle, IoTC_Connection::internalConnectionStatusCallback, this) != IOTHUB_CLIENT_OK)
    {
        Log.error("IoTHubClient_LL_SetConnectionStatusCallback..........FAILED!" CR);
        return false;
    }

    Log.notice("== Enabling Device Twin Callback ==" CR);
    if (IoTHubClient_LL_SetDeviceTwinCallback(this->_iotHubClientHandle, IoTC_Connection::internalDesiredPropertiesCallback, this) != IOTHUB_CLIENT_OK)
    {
        Log.error("IoTHubClient_LL_SetDeviceTwinCallback..........FAILED!" CR);
        return false;
    }

    // If you want to change the SDK retry policy, uncomment the following line and set the parameters as you see fit
    // IoTHubClient_LL_SetRetryPolicy(iotHubClientHandle, IOTHUB_CLIENT_RETRY_EXPONENTIAL_BACKOFF, 1200);

    return true;
}

void IoTC_Connection::loop()
{
    IoTHubClient_LL_DoWork(this->_iotHubClientHandle);
}

bool IoTC_Connection::sendTelemetry(const char* jsonPayload, time_t timestamp)
{
    IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(jsonPayload);
    if (messageHandle == NULL)
    {
        Log.error("Failed to create message handle" CR);
        return false;
    }
    Log.trace("Message handle created: %s" CR, jsonPayload);

    IOTHUB_CLIENT_RESULT result = IoTHubClient_LL_SendEventAsync(this->_iotHubClientHandle, messageHandle, internalTelemetryConfirmationCallback, messageHandle);
    if (result != IOTHUB_CLIENT_OK)
    {
        Log.error("ERROR: IoTHubClient_LL_SendEventAsync FAILED! - %s" CR, ENUM_TO_STRING(IOTHUB_CLIENT_RESULT, result));
        IoTHubMessage_Destroy(messageHandle);
        return false;
    }

    Log.trace("IoTHubClient_LL_SendEventAsync accepted message for transmission to IoT Hub." CR);
    return true;
}

bool IoTC_Connection::sendStateChange(std::string key, std::string value, time_t timestamp)
{
    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root[key.c_str()] = value.c_str();

    char message[MAX_MESSAGE_SIZE];
    root.printTo(message);

    return this->sendTelemetry(message, timestamp);
}

bool IoTC_Connection::sendEvent(std::string key, std::string value, time_t timestamp)
{
    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root[key.c_str()] = value.c_str();

    char message[MAX_MESSAGE_SIZE];
    root.printTo(message);

    return this->sendTelemetry(message, timestamp);
}

bool IoTC_Connection::sendMeasurementJson(std::string key, std::string valueJson, time_t timestamp)
{
    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root[key.c_str()] = RawJson(valueJson.c_str());

    char message[MAX_MESSAGE_SIZE];
    root.printTo(message);

    return this->sendTelemetry(message, timestamp);
}

bool IoTC_Connection::sendMeasurement(std::string key, double value, time_t timestamp)
{
    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root[key.c_str()] = value;

    char message[MAX_MESSAGE_SIZE];
    root.printTo(message);

    return this->sendTelemetry(message, timestamp);
}

bool IoTC_Connection::sendMeasurements(std::map<std::string, double> inputMap, time_t timestamp)
{
    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    for (auto const &element : inputMap)
        root.set(element.first.c_str(), element.second);

    char message[MAX_MESSAGE_SIZE];
    root.printTo(message);

    return this->sendTelemetry(message, timestamp);
}

bool IoTC_Connection::sendReportedPropertyJson(const std::string key, const std::string valueJson)
{
    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root[key.c_str()] = RawJson(valueJson.c_str());

    char message[MAX_MESSAGE_SIZE];
    root.printTo(message);

    auto result = this->internalSendReportedProperty(message, strlen(message));

    if (result != IOTHUB_CLIENT_OK)
    {
        Log.error("Failure sending reported property!!!" CR);
        return false;
    }
    else
    {
        Log.trace("IoTHubClient::sendReportedProperty COMPLETED" CR);
        return true;
    }
}

bool IoTC_Connection::registerDeviceMethod(std::string methodName, MethodCallbackFunctionType callback, void *context)
{
    this->_methodCallbackMap[methodName] = std::pair<MethodCallbackFunctionType, void *>(callback, context);
    return true;
}

bool IoTC_Connection::registerDesiredPropertyCallback(std::string propName, DesiredPropertyCallbackFunctionType callback, void *context)
{
    this->_desiredPropCallbackMap[propName] = std::pair<DesiredPropertyCallbackFunctionType, void *>(callback, context);
    return true;
}

bool IoTC_Connection::registerConnectionStatusCallback(ConnectionStatusCallbackFunctionType func)
{
    _connectionStatusCallback = func;
    return true;
}

//
// Internal protected helper methods
IOTHUB_CLIENT_RESULT IoTC_Connection::internalSendReportedProperty(const char *buffer, size_t length)
{
    Log.trace("internalSendReportedProperty: %s", buffer);
    return IoTHubClient_LL_SendReportedState(this->_iotHubClientHandle,
                                             (const unsigned char *)buffer,
                                             length,
                                             this->internalReportedStateCallback, NULL);
}

//
// Static callback functions
//

void IoTC_Connection::internalConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void *context)
{
    Log.trace("Connection Status Received: %s - %s" CR, ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS, result), ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS_REASON, reason));
    IoTC_Connection *IoTC_Connection = static_cast<IoTC_Connection *>(context);
    if (IoTC_Connection->_connectionStatusCallback)
        IoTC_Connection->_connectionStatusCallback(result, reason);
}

void IoTC_Connection::internalTelemetryConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context)
{
    Log.trace("Telemetry Confirmation Receieved: %s" CR, ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));
    auto handle = static_cast<IOTHUB_MESSAGE_HANDLE>(context);
    if (handle != NULL)
        IoTHubMessage_Destroy(handle);
}

int IoTC_Connection::internalDeviceDirectMethodCallback(const char *method_name, const unsigned char *payload, size_t size, unsigned char **response, size_t *response_size, void *context)
{
    Log.trace("**METHOD CALL** - %s" CR, method_name);

    IoTC_Connection *IoTC_Connection = static_cast<IoTC_Connection *>(context);
    std::string method = method_name;
    CallbackMapType &map = IoTC_Connection->_methodCallbackMap;

    CallbackMapType::iterator it = map.find(method);
    if (it == map.end()) // key exists, call the user function
        return HTTP_CODE_NOT_IMPLEMENTED;

    auto p = it->second;
    MethodCallbackFunctionType f = p.first;
    void *userContext = p.second;
    auto resp = f(method, userContext);

    // NOTE: The malloc below seems like it should leak, but it doesn't because the IoT SDK will free the memory.
    //       In my experimenting, a static buffer caused a runtime exception.
    *response_size = resp.length();
    *response = (unsigned char *)malloc(*response_size);
    (void)memcpy(*response, resp.c_str(), *response_size);
    return HTTP_CODE_OK;
}

void IoTC_Connection::internalReportedStateCallback(int status_code, void *userContextCallback)
{
    // You can use this for retry logic or anything else that would want to react to the success
    // or failure of the message.
}

void IoTC_Connection::internalDesiredPropertiesCallback(DEVICE_TWIN_UPDATE_STATE update_state, const unsigned char *payLoad, size_t size, void *context)
{

    /*
    enum DEVICE_TWIN_UPDATE_STATE {
        DEVICE_TWIN_UPDATE_COMPLETE
        DEVICE_TWIN_UPDATE_PARTIAL
    }
    */

    /*
    A partial update will look like this (for one writable property):
        {
            "fan-speed": {
                "value": 55
            },
            "$version": 5
        }
    Or this (for more than one):
        {
            "fan-speed": {
                "value": 55
            },
            "light-switch": {
                "value": false
            },
            "$version": 5
        }

    A full update will look like this (for one writable property):
        {
            "desired": {
                "fan-speed": {
                "value": 50
                },
                "$version": 3
            },
            "reported": {
                "wifi_ap_name": "Provost-Mesh",
                "$version": 2031
            }
        }
    Or this (for more than one):
        {
            "desired": {
                "fan-speed": {
                    "value": 50
                },
                "light-switch": {
                    "value": "on"
                }
                "$version": 3
            },
            "reported": {
                "wifi_ap_name": "Provost-Mesh",
                "$version": 2031
            }
        }

    For each property accepted by the device, you must acknowledge it like this:
        {
            "fan-speed": {
                "value": { the value you landed on },
                "statusCode": { http status code? },
                "status": { "completed" | "pending" | "failed" },
                "desiredVersion": { whatever you were given in the update }
            }
        }

    Note that if you are pending, you should report what you're at now (when available), and later follow it up
    with the final value when done.
    */

    Log.trace("**DESIRED PROP RECEIVED** (%s)" CR, ENUM_TO_STRING(DEVICE_TWIN_UPDATE_STATE, update_state));

    IoTC_Connection *IoTC_Connection = static_cast<IoTC_Connection *>(context);

    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;

    JsonObject &root = jsonBuffer.parseObject(payLoad);

    char buffer[1024];
    root.prettyPrintTo(buffer);

    // TODO: Move this to a static function?
    auto Parse = [IoTC_Connection](const JsonObject &it) {
        int version = it["$version"];
        for (const auto &kv : it)
        {
            auto key = kv.key;

            // bail out if it starts with a $ sign
            if (key[0] == '$')
                continue; 

            auto val = kv.value["value"].as<char *>();
            auto &map = IoTC_Connection->_desiredPropCallbackMap;

            StaticJsonBuffer<MAX_MESSAGE_SIZE> messageJson;
            JsonObject &root = messageJson.createObject();
            auto &propObj = root.createNestedObject(key);
            propObj["value"] = val;
            propObj["desiredVersion"] = version;

            if (map.find(key) == map.end())
            {
                Log.warning("Unregistered desired property received: %s: %s" CR, key, val);
                propObj["statusCode"] = HTTP_CODE_NOT_IMPLEMENTED;
                propObj["message"] = "Setting not supported on this device";
                propObj["status"] = "failed";
            }
            else
            {
                Log.trace("Registered desired property received: %s: %s" CR, key, val);
                DesiredPropertyCallbackFunctionType f = map[key].first;
                void *userContext = map[key].second;
                bool result = f(key, val, userContext);

                propObj["statusCode"] = (int)((result == true) ? HTTP_CODE_OK : HTTP_CODE_PARTIAL_CONTENT);
                propObj["status"] = (const char *)((result == true) ? "completed" : "failed");
            }
            
            char buffer[MAX_MESSAGE_SIZE];
            root.printTo(buffer, MAX_MESSAGE_SIZE);
            int length = strlen(buffer);
            IoTC_Connection->internalSendReportedProperty(buffer, length);
        }
    };

    // Unwrap if it is a full twin update
    JsonVariant target = root;
    if (root.containsKey("desired"))
        target = root["desired"];

    Parse(target.as<JsonObject>());
}