class MyDevice : Something
{
    IoTProperty<string> DeviceId;
    IoTProperty<double> Temperature;
    IoTMethod<void(void>) Reboot;


    MyDevice() :
        DeviceId(false /* writable */),
        Temperature(false /* writable */),
    {
    }
}