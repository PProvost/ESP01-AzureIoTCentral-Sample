#include <ESP8266HTTPClient.h>

#include <sdk/iothub_client.h>
#include <sdk/iothub_message.h>
#include <azure_c_shared_utility/platform.h>
#include <azure_c_shared_utility/threadapi.h>
#include <azure_c_shared_utility/macro_utils.h>
#include <sdk/iothubtransportmqtt.h>
#include <sdk/iothub_client_options.h>
#include <AzureIoTProtocol_MQTT.h>

#define ARDUINOJSON_ENABLE_PROGMEM 1

#include <ArduinoLog.h>
#include <ArduinoJson.h>

#include "HubConnection.h"

typedef std::map<std::string, std::pair<MethodCallbackFunctionType, void *>> CallbackMapType;

// Ctor - default
HubConnection::HubConnection() : _iotHubClientHandle(NULL)
{
    // memset(this->connectionString, '\0', MAX_LEN_CONN_STR);
}

IOTHUB_CLIENT_LL_HANDLE HubConnection::getHubClientHandle()
{
    return this->_iotHubClientHandle;
}

bool HubConnection::setup(const std::string connectionString)
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
    if (IoTHubClient_LL_SetDeviceMethodCallback(this->_iotHubClientHandle, HubConnection::internalDeviceDirectMethodCallback, this) != IOTHUB_CLIENT_OK)
    {
        Log.error("IoTHubClient_LL_SetDeviceMethodCallback..........FAILED!" CR);
        return false;
    }

    Log.notice("== Enabling Connection Status Callback ==" CR);
    if (IoTHubClient_LL_SetConnectionStatusCallback(this->_iotHubClientHandle, HubConnection::internalConnectionStatusCallback, this) != IOTHUB_CLIENT_OK)
    {
        Log.error("IoTHubClient_LL_SetConnectionStatusCallback..........FAILED!" CR);
        return false;
    }

    Log.notice("== Enabling Device Twin Callback ==" CR);
    if (IoTHubClient_LL_SetDeviceTwinCallback(this->_iotHubClientHandle, HubConnection::internalDesiredPropertiesCallback, this) != IOTHUB_CLIENT_OK)
    {
        Log.error("IoTHubClient_LL_SetDeviceTwinCallback..........FAILED!" CR);
        return false;
    }

    // If you want to change the SDK retry policy, uncomment the following line and set the parameters as you see fit
    // IoTHubClient_LL_SetRetryPolicy(iotHubClientHandle, IOTHUB_CLIENT_RETRY_EXPONENTIAL_BACKOFF, 1200);

    return true;
}

void HubConnection::loop()
{
    IoTHubClient_LL_DoWork(this->_iotHubClientHandle);
}

bool HubConnection::sendMeasurements(std::map<std::string, double> inputMap, time_t timestamp)
{
    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    for (auto const &element : inputMap)
    {
        root.set(element.first.c_str(), element.second);
    }

    char message[MAX_MESSAGE_SIZE];
    root.printTo(message);

    IOTHUB_MESSAGE_HANDLE handle = IoTHubMessage_CreateFromString(message);
    if (handle == NULL)
    {
        Log.error("Failed to create message handle" CR);
        return false;
    }
    Log.trace("Message handle created: %s" CR, message);

    IOTHUB_CLIENT_RESULT result = IoTHubClient_LL_SendEventAsync(this->_iotHubClientHandle, handle, internalTelemetryConfirmationCallback, handle);
    if (result != IOTHUB_CLIENT_OK)
    {
        Log.error("ERROR: IoTHubClient_LL_SendEventAsync FAILED! - %s" CR, ENUM_TO_STRING(IOTHUB_CLIENT_RESULT, result));
        IoTHubMessage_Destroy(handle);
        return false;
    }

    Log.trace("IoTHubClient_LL_SendEventAsync accepted message for transmission to IoT Hub." CR);
    return true;
}

bool HubConnection::registerDeviceMethod(std::string methodName, MethodCallbackFunctionType callback, void *context)
{
    this->_methodCallbackMap[methodName] = std::pair<MethodCallbackFunctionType, void *>(callback, context);
    return true;
}

bool HubConnection::registerDesiredPropertyCallback(std::string propName, DesiredPropertyCallbackFunctionType callback, void *context)
{
    this->_desiredPropCallbackMap[propName] = std::pair<DesiredPropertyCallbackFunctionType, void *>(callback, context);
    return true;
}

bool HubConnection::registerConnectionStatusCallback(ConnectionStatusCallbackFunctionType func)
{
    _connectionStatusCallback = func;
    return true;
}

//
// Static callback functions
//

void HubConnection::internalConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void *context)
{
    Log.trace("Connection Status Received: %s - %s" CR, ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS, result), ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS_REASON, reason));
    HubConnection *hubConnection = static_cast<HubConnection *>(context);
    if (hubConnection->_connectionStatusCallback)
        hubConnection->_connectionStatusCallback(result, reason);
}

void HubConnection::internalTelemetryConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context)
{
    Log.trace("Telemetry Confirmation Receieved: %s" CR, ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));
    auto handle = static_cast<IOTHUB_MESSAGE_HANDLE>(context);
    if (handle != NULL)
        IoTHubMessage_Destroy(handle);
}

int HubConnection::internalDeviceDirectMethodCallback(const char *method_name, const unsigned char *payload, size_t size, unsigned char **response, size_t *response_size, void *context)
{
    Log.trace("**METHOD CALL** - %s" CR, method_name);

    HubConnection *hubConnection = static_cast<HubConnection *>(context);
    std::string method = method_name;
    CallbackMapType& map = hubConnection->_methodCallbackMap;

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

void HubConnection::internalReportedStateCallback(int status_code, void *userContextCallback)
{
    // You can use this for retry logic or anything else that would want to react to the success
    // or failure of the message.
}

void HubConnection::internalDesiredPropertiesCallback(DEVICE_TWIN_UPDATE_STATE update_state, const unsigned char *payLoad, size_t size, void *context)
{
    Log.trace("**DESIRED PROP RECEIVED**" CR);

    HubConnection* hubConnection = static_cast<HubConnection *>(context);

    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payLoad);

    char buffer[1024];
    root.prettyPrintTo(buffer);
    Log.trace("Desired Prop:\r\n%s" CR, buffer);

    if (root["desired"])
    {
        const JsonObject& desired = root["desired"];
        for (const auto& kv : desired)
        {
            auto key = kv.key;
            // auto val = kv.value.as<char*>();
            auto val = kv.value["value"].as<char*>();
            Log.trace("\tKey/Val: %s/%s" CR, key, val);
            auto& map = hubConnection->_desiredPropCallbackMap;
            if (map.find(key) != map.end())
            {
                Log.trace("\tFOUND!" CR);
                DesiredPropertyCallbackFunctionType f = map[key].first;
                void* userContext = map[key].second;
                f(key, val, userContext);                
            }
            else
                Log.trace("\tNOT Found!" CR);
        }
    }    
}