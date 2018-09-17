#ifndef HUBCONNECTION_HPP
#define HUBCONNECTION_HPP

// Inline directives for template instantiation
#include "HubConnection.h"

template <typename T> 
bool HubConnection::sendReportedProperty(const std::string key, T value)
{
    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root.set(key.c_str(), value);

    char message[MAX_MESSAGE_SIZE];
    root.printTo(message);

    IOTHUB_CLIENT_RESULT result = IoTHubClient_LL_SendReportedState(this->_iotHubClientHandle,
                                                                    (const unsigned char *)message,
                                                                    strlen(message),
                                                                    this->internalReportedStateCallback, NULL);

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

#endif