#include "BLEDevice.h"
#include <WiFi.h>
#include <PubSubClient.h>

#include "config.h"

//Boot count used to check if battery status should be read
RTC_DATA_ATTR int bootCount = 0;

//Device count
static int deviceCount = sizeof FLORA_DEVICES / sizeof FLORA_DEVICES[0];

//The remote service we wish to connect to
static BLEUUID serviceUUID("00001204-0000-1000-8000-00805f9b34fb");

//The characteristic of the remote service we are interested in
static BLEUUID uuid_version_battery("00001a02-0000-1000-8000-00805f9b34fb");
static BLEUUID uuid_sensor_data("00001a01-0000-1000-8000-00805f9b34fb");
static BLEUUID uuid_write_mode("00001a00-0000-1000-8000-00805f9b34fb");

//Timeserver: Western European Time
#define NTP_SERVER "de.pool.ntp.org"
#define TZ_INFO "WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00"

//GPIOs
const int ledPin = 2;
const int pumpPin = 4;
const int batteryPin = 36;
const int waterPin = 37;
const int plantPin = 38; //optional
const int solarPin = 39;

TaskHandle_t deepSleepTaskHandle = NULL;

WiFiClient espClient;
PubSubClient client(espClient);

typedef struct floraData {
  float temperature;
  int moisture;
  int light;
  int conductivity;
  int battery;
  bool success;
} floraData;

void connectWifi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setHostname(CLIENT_NAME);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("");
}

void disconnectWifi() {
  WiFi.disconnect(true);
  Serial.println("WiFi disonnected");
}

void connectMqtt() {
  Serial.println("Connecting to MQTT...");
  client.setServer(MQTT_HOST, MQTT_PORT);

  while (!client.connected()) {
    if (!client.connect(CLIENT_NAME, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.print("MQTT connection failed:");
      Serial.print(client.state());
      Serial.println("Retrying...");
      delay(MQTT_RETRY_WAIT);
    }
  }

  Serial.println("MQTT connected");
  Serial.println("");
}

void disconnectMqtt() {
  client.disconnect();
  Serial.println("MQTT disconnected");
}

BLEClient* getFloraClient(BLEAddress floraAddress) {
  BLEClient* floraClient = BLEDevice::createClient();

  if (!floraClient->connect(floraAddress)) {
    Serial.println("- Connection failed, skipping");
    return nullptr;
  }

  Serial.println("- Connection successful");
  return floraClient;
}

BLERemoteService* getFloraService(BLEClient* floraClient) {
  BLERemoteService* floraService = nullptr;

  try {
    floraService = floraClient->getService(serviceUUID);
  }
  catch (...) {
    //Something went wrong
  }
  if (floraService == nullptr) {
    Serial.println("- Failed to find data service");
  }
  else {
    Serial.println("- Found data service");
  }

  return floraService;
}

bool forceFloraServiceDataMode(BLERemoteService* floraService) {
  BLERemoteCharacteristic* floraCharacteristic;

  //Get device mode characteristic, needs to be changed to read data
  Serial.println("- Force device in data mode");
  floraCharacteristic = nullptr;
  try {
    floraCharacteristic = floraService->getCharacteristic(uuid_write_mode);
  }
  catch (...) {
    //Something went wrong
  }
  if (floraCharacteristic == nullptr) {
    Serial.println("-- Failed, skipping device");
    return false;
  }

  //Write the magic data
  uint8_t buf[2] = {0xA0, 0x1F};
  floraCharacteristic->writeValue(buf, 2, true);

  delay(500);
  return true;
}

bool readFloraDataCharacteristic(BLERemoteService* floraService, struct floraData* retData) {
  BLERemoteCharacteristic* floraCharacteristic = nullptr;

  //Get the main device data characteristic
  Serial.println("- Access characteristic from device");
  try {
    floraCharacteristic = floraService->getCharacteristic(uuid_sensor_data);
  }
  catch (...) {
    //Something went wrong
  }
  if (floraCharacteristic == nullptr) {
    Serial.println("-- Failed, skipping device");
    return false;
  }

  //Read characteristic value
  Serial.println("- Read value from characteristic");
  std::string value;
  try{
    value = floraCharacteristic->readValue();
  }
  catch (...) {
    //Something went wrong
    Serial.println("-- Failed, skipping device");
    return false;
  }
  const char *val = value.c_str();

  Serial.print("Hex: ");
  for (int i = 0; i < 16; i++) {
    Serial.print((int)val[i], HEX);
    Serial.print(" ");
  }
  Serial.println(" ");

  int16_t* temp_raw = (int16_t*)val;
  float temperature = (*temp_raw) / ((float)10.0);
  Serial.print("-- Temperature: ");
  Serial.println(temperature);

  int moisture = val[7];
  Serial.print("-- Moisture: ");
  Serial.println(moisture);

  int light = val[3] + val[4] * 256;
  Serial.print("-- Light: ");
  Serial.println(light);

  int conductivity = val[8] + val[9] * 256;
  Serial.print("-- Conductivity: ");
  Serial.println(conductivity);

  if ((temperature > 200) || (temperature < -100)) {
    Serial.println("-- Unreasonable values received, skip publish");
    return false;
  }

  retData->temperature = temperature;
  retData->moisture = moisture;
  retData->light = light;
  retData->conductivity = conductivity;

  return true;
}

bool readFloraBatteryCharacteristic(BLERemoteService* floraService, struct floraData* retData) {
  BLERemoteCharacteristic* floraCharacteristic = nullptr;

  //Get the device battery characteristic
  Serial.println("- Access battery characteristic from device");
  try {
    floraCharacteristic = floraService->getCharacteristic(uuid_version_battery);
  }
  catch (...) {
    //Something went wrong
  }
  if (floraCharacteristic == nullptr) {
    Serial.println("-- Failed, skipping battery level");
    return false;
  }

  //Read characteristic value
  Serial.println("- Read value from characteristic");
  std::string value;
  try{
    value = floraCharacteristic->readValue();
  }
  catch (...) {
    //Something went wrong
    Serial.println("-- Failed, skipping battery level");
    return false;
  }
  const char *val2 = value.c_str();
  int battery = val2[0];

  Serial.print("-- Battery: ");
  Serial.println(battery);
  retData->battery = battery;

  return true;
}

bool processFloraService(BLERemoteService* floraService, bool readBattery, struct floraData* retData) {
  //Set device in data mode
  if (!forceFloraServiceDataMode(floraService)) {
    return false;
  }

  bool dataSuccess = readFloraDataCharacteristic(floraService, retData);

  bool batterySuccess = true;
  if (readBattery) {
    batterySuccess = readFloraBatteryCharacteristic(floraService, retData);
  }

  retData->success = dataSuccess && batterySuccess;
  return retData->success;
}

bool processFloraDevice(BLEAddress floraAddress, bool getBattery, int tryCount, struct floraData* retData) {
  Serial.print("Processing Flora device at ");
  Serial.print(floraAddress.toString().c_str());
  Serial.print(" (try ");
  Serial.print(tryCount);
  Serial.println(")");

  //Connect to flora ble server
  BLEClient* floraClient = getFloraClient(floraAddress);
  if (floraClient == nullptr) {
    return false;
  }

  //Connect data service
  BLERemoteService* floraService = getFloraService(floraClient);
  if (floraService == nullptr) {
    floraClient->disconnect();
    return false;
  }

  //Process devices data
  bool success = processFloraService(floraService, getBattery, retData);

  //Disconnect from device
  floraClient->disconnect();

  return success;
}

void deepSleep() {
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * 60000000ll);
  Serial.println("Go into deepSleep.");
  digitalWrite(pumpPin, LOW);
  delay(100);
  digitalWrite(ledPin, LOW);
  delay(100);
  esp_deep_sleep_start();
}

void delayedDeepSleep(void *parameter) {
  delay(EMERGENCY_DEEPSLEEP * 1000);
  Serial.println("Error, entering emergency deepSleep...");
  deepSleep();
}

void setup() {
  //Enabel status LED
  pinMode (ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  //All action is done when device is woken up
  Serial.begin(115200);
  delay(1000);

  //Check if battery status should be read - based on boot count
  bool readBattery = ((bootCount % BATTERY_INTERVAL) == 0);

  Serial.print("Boot number: ");
  Serial.println(bootCount);

  //Increase boot count
  bootCount++;

  //Create a deepSleep task in case something gets stuck
  xTaskCreate(delayedDeepSleep, "deepSleep", 4096, NULL, 1, &deepSleepTaskHandle);

  Serial.println("Initialize BLE client...");
  BLEDevice::init("");
  BLEDevice::setPower(ESP_PWR_LVL_P7);

  struct floraData* deviceData = (struct floraData*)malloc(deviceCount * sizeof(struct floraData));

  //Process devices
  for (int i=0; i<deviceCount; i++) {
    int tryCount = 0;
    char* deviceMacAddress = FLORA_DEVICES[i];
    BLEAddress floraAddress(deviceMacAddress);

    while (tryCount < RETRY) {
      tryCount++;
      if (processFloraDevice(floraAddress, readBattery, tryCount, &(deviceData[i]))) {
        break;
      }
      delay(1000);
    }
    delay(1500);
  }

  //Connecting wifi and mqtt server
  connectWifi();
  connectMqtt();

  delay(1000);

  //Date and Time
  if(bootCount == 1) {
    //NTP Synchronisieren 10s
    Serial.println("Get NTP Time...");
    struct tm local;
    configTzTime(TZ_INFO, NTP_SERVER);
    getLocalTime(&local, 10000);
  } else {
    //NTP Synchronisieren after deep sleep
    setenv("TZ", TZ_INFO, 1);
    tzset();
  }

  //Show date on Display
  tm local;
  getLocalTime(&local);
  Serial.println(&local, "%a, %d %b %Y Datum: %d.%m.%y  Zeit: %H:%M:%S");
  String lastTime = (&local, "%a, %d %b %Y Datum: %d.%m.%y  Zeit: %H:%M:%S");
  //Read analog inputs
  float batteryValue = analogRead(batteryPin); //1024 = 5.5V
  float solarValue = analogRead(solarPin); //1024 = 5.5V
  float waterValue = analogRead(waterPin); //0 to 4095
  float plantValue = analogRead(plantPin); //0 to 4095 (optional)

  //Calculate voltages
  float batteryVoltage = ((5.5 / 1024) * batteryValue);
  float solarVoltage = ((5.5 / 1024) * solarValue);
  //Calculate values
  float waterLevel = ((waterValue / 4095) * 100);
  float plantLevel = ((plantValue / 4095) * 100);

  //Output voltages
  Serial.print("System Battery: ");
  Serial.print(batteryVoltage);
  Serial.println("V");
  Serial.print("Solar Voltag: ");
  Serial.print(solarVoltage);
  Serial.println("V");
  //Output values
  Serial.print("Water: ");
  Serial.println(waterLevel);
  Serial.print("Plant: ");
  Serial.println(plantLevel);

  //Post deviceData
  String dataTopic = MQTT_BASE_TOPIC + "/deviceData/";
  char buffer[64];

  Serial.print("Publishing data for deviceData");
  Serial.print(" to ");
  Serial.println(dataTopic);

  snprintf(buffer, 64, "true");
  client.publish((dataTopic + "connection").c_str(), buffer);
  snprintf(buffer, 64, "%d", WiFi.RSSI());
  client.publish((dataTopic + "link_quality").c_str(), buffer);
  snprintf(buffer, 64, "%d", millis());
  client.publish((dataTopic + "up_time").c_str(), buffer);
  snprintf(buffer, 64, "%d", lastTime);
  client.publish((dataTopic + "last_time").c_str(), buffer);
  snprintf(buffer, 64, "%f", batteryVoltage);
  client.publish((dataTopic + "system_battery").c_str(), buffer);
  snprintf(buffer, 64, "%f", solarVoltage);
  client.publish((dataTopic + "solar_voltage").c_str(), buffer);
  snprintf(buffer, 64, "%f", waterLevel);
  client.publish((dataTopic + "water_level").c_str(), buffer);
  snprintf(buffer, 64, "%f", plantLevel);
  client.publish((dataTopic + "plant_level").c_str(), buffer);
  //snprintf(buffer, 64, "%s", WiFi.localIP().toString());
  //client.publish((dataTopic + "IP").c_str(), buffer);
  client.publish("PlantSystem/deviceData/IP", String(WiFi.localIP().toString()).c_str());

  delay(2000);

  //Post BaseData
  for (int i=0; i<deviceCount; i++) {
    if (deviceData[i].success) {
      char* deviceMacAddress = FLORA_DEVICES[i];
      String baseTopic = MQTT_BASE_TOPIC + "/" + deviceMacAddress + "/";

      //char buffer[64];

      Serial.print("Publishing data for ");
      Serial.print(deviceMacAddress);
      Serial.print(" to ");
      Serial.println(baseTopic);

      snprintf(buffer, 64, "%f", deviceData[i].temperature);
      client.publish((baseTopic + "temperature").c_str(), buffer);
      snprintf(buffer, 64, "%d", deviceData[i].moisture);
      client.publish((baseTopic + "moisture").c_str(), buffer);
      snprintf(buffer, 64, "%d", deviceData[i].light);
      client.publish((baseTopic + "light").c_str(), buffer);
      snprintf(buffer, 64, "%d", deviceData[i].conductivity);
      client.publish((baseTopic + "conductivity").c_str(), buffer);
      if (readBattery) {
        snprintf(buffer, 64, "%d", deviceData[i].battery);
        client.publish((baseTopic + "battery").c_str(), buffer);
      }
      delay(200);
    }
  }

  //Start water pump if moisture is lower than 40 and waterLevel higer than 20
  int mois = deviceData[0].moisture;
  if(mois <= 50){
    if(waterLevel >= 10){
      String dataTopic = MQTT_BASE_TOPIC + "/deviceData/";
      char buffer[64];
      snprintf(buffer, 64, "true");
      client.publish((dataTopic + "watering").c_str(), buffer);
      snprintf(buffer, 64, "false");
      client.publish((dataTopic + "water_empty").c_str(), buffer);
      delay(200);
      Serial.print("Wasser level: ");
      Serial.println(waterLevel);
      Serial.println("Start water pump for 2 minutes.");
      pinMode (pumpPin, OUTPUT);
      digitalWrite(pumpPin, HIGH);
      delay(120000);
      digitalWrite(pumpPin, LOW);
      Serial.println("Stop water pump");
    } else {
      Serial.println("Water level to low");
      snprintf(buffer, 64, "true");
      client.publish((dataTopic + "water_empty").c_str(), buffer);
      delay(200);
    }
  } else {
    Serial.println("No water needed, moisture to high");
  }
  delay(1000);

  //Disconnect wifi and mqtt
  disconnectWifi();
  disconnectMqtt();

  //Delete emergency deepSleep task
  vTaskDelete(deepSleepTaskHandle);

  //Go to sleep now
  deepSleep();
}

void loop() {
  ///We're not doing anything in the loop, only on device wakeup
  //delay(10000);
  client.loop();
}
