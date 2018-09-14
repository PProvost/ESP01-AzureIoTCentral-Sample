# ESP01 Azure IoT Central Sample

A basic ESP8266/ESP01 sample for Azure IoT Central using the IoT Hub C-SDK

Author: Peter Provost (https://github.com/PProvost)

## Dependencies

* If you are using Platformio, the dependencies should be automatically resolved
  and downloaded using the libdeps directive in platformio.ini
* If you are not using Platformio, or if it doesn't work for you, here are the
  dependencies and links
    * AzureIoTHub - https://platformio.org/lib/show/480/AzureIoTHub
    * AzureIoTProtocol_MQTT - https://platformio.org/lib/show/1279/AzureIoTProtocol_MQTT
    * AzureIoTUtility - https://platformio.org/lib/show/1277/AzureIoTUtility
    * SimpleTimer - https://platformio.org/lib/show/419/SimpleTimer
    * ArduinoLog - https://platformio.org/lib/show/1532/ArduinoLog
    * ArduinoJson - https://platformio.org/lib/show/64/ArduinoJson
    * WifiManager - https://platformio.org/lib/show/567/WifiManager

## Notes

* This is not an official sample for Azure IoT Hub or Azure IoT Central. It is the 
  result of my personal exploration and experimentation with the ESP01 chip, the
  IoT Hub SDK and IoT Central.
* I attempted to show the main device capabilities that are avilable in IoT Central,
  but I didn't cover everything the IoT Hub SDK can do.
* This project was created using Platformio & VSCode and probably won't compile
  without modification in the Arduino IDE. But it shouldn't be too hard to figure
  out.
