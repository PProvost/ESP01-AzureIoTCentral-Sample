#ifndef IOTC_CONNECTION_HPP
#define IOTC_CONNECTION_HPP

// Inline directives for template instantiation
#include "IoTC_Connection.h"

template <typename T> 
bool IoTC_Connection::sendReportedProperty(const std::string key, T value)
{
    StaticJsonBuffer<MAX_MESSAGE_SIZE> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root.set(key.c_str(), value);

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

#endif // IOTC_CONNECTION_HPP