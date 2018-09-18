#include <FS.h>

#include <WiFiManager.h>      // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoLog.h>

#include "config.h"

// extern reference to global defined in main.cpp
extern char iotConnStr[CONN_STR_MAX_LEN];

bool shouldSaveConfig = false;

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
