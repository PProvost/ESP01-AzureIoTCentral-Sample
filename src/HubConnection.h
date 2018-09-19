#ifndef HUBCONNECTION_H
#define HUBCONNECTION_H

#include <string>
#include <map>
#include <memory>

#include <Time.h>

#include <sdk/iothub_client_ll.h>
#include <sdk/iothub_message.h>

#define MAX_LEN_CONN_STR 512
#define MAX_MESSAGE_SIZE 1024

// Callback types
typedef std::function<std::string(std::string, void *)> MethodCallbackFunctionType;
typedef std::function<void(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason)> ConnectionStatusCallbackFunctionType;
typedef std::function<bool(std::string,std::string,void*)> DesiredPropertyCallbackFunctionType;

class HubConnection
{
  public:
    HubConnection();

    bool setup(std::string connectionString);
    void loop();

    bool sendMeasurements(std::map<std::string, double> inputMap, time_t timestamp = 0);
    bool registerDeviceMethod(std::string methodName, MethodCallbackFunctionType callback, void *context = NULL);
    bool registerConnectionStatusCallback(ConnectionStatusCallbackFunctionType func);
    bool registerDesiredPropertyCallback(std::string propName, DesiredPropertyCallbackFunctionType callback, void* context=NULL);

    template <typename T> 
    bool sendReportedProperty(const std::string key, T value);


    /*
    bool SendEvent(std::string name, std::string value, time_t timestamp=0);
    bool SendStateTransition(std::string name, std::string value, time_t timestamp=0);
    */

    // For backward compatibility if you want to use direct SDK "LL" functions
    IOTHUB_CLIENT_LL_HANDLE getHubClientHandle();

  protected:
    IOTHUB_CLIENT_RESULT internalSendReportedProperty(const char* buffer, size_t length);
    static int internalDeviceDirectMethodCallback(const char *method_name, const unsigned char *payload, size_t size, unsigned char **response, size_t *response_size, void *context);
    static void internalTelemetryConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context);
    static void internalConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void *userContextCallback);
    static void internalReportedStateCallback(int status_code, void *userContextCallback);
    static void internalDesiredPropertiesCallback(DEVICE_TWIN_UPDATE_STATE update_state, const unsigned char *payLoad, size_t size, void *userContextCallback);

  private:
    time_t _lastTokenTime;
    IOTHUB_CLIENT_LL_HANDLE _iotHubClientHandle;
    std::string _connectionString;
    ConnectionStatusCallbackFunctionType _connectionStatusCallback;

    std::map<std::string, std::pair<MethodCallbackFunctionType, void *>> _methodCallbackMap;
    std::map<std::string, std::pair<DesiredPropertyCallbackFunctionType, void *>> _desiredPropCallbackMap;
};

#include "HubConnection.hpp"

#endif