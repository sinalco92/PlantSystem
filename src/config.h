//Array of different xiaomi flora MAC addresses
char* FLORA_DEVICES[] = {
    "XXXXXXXXXXXX" //C4:7C:8D:61:37:19
};

//deepSleep in minutes
#define TIME_TO_SLEEP 120
//Emergency deepSleep in seconds
#define EMERGENCY_DEEPSLEEP 180
//How often should the battery from the Flora Sensor be read - in run count
#define BATTERY_INTERVAL 6
//How often should a device be retried in a run when something fails
#define RETRY 3

//WiFi settings
const char*   CLIENT_NAME     = "PlantSystem";
const char*   WIFI_SSID       = "XXXXXXXXXXXXXXXXX";
const char*   WIFI_PASSWORD   = "XXXXXXXXXXXXXXXXX";

//MQTT settings
const char*   MQTT_HOST       = "192.168.178.XXX";
const int     MQTT_PORT       = 1883;
const char*   MQTT_CLIENTID   = "PlantSystem";
const char*   MQTT_USERNAME   = "XXXXXXXX";
const char*   MQTT_PASSWORD   = "XXXXXXXX";
const String  MQTT_BASE_TOPIC = "PlantSystem";
const int     MQTT_RETRY_WAIT = 5000;
