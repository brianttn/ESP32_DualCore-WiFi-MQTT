#include "WiFi.h"
#include <WiFiMulti.h>      // 用來「搜尋並連線：多組AP」
#include <PubSubClient.h>

WiFiMulti wifiMulti;        // 建立：WiFiMulti物件

/* - - - - - - - -     MultiCore：Core0, Core1     - - - - - - - - */
TaskHandle_t core0Task;     // 宣告任務：core0Task
TaskHandle_t core1Task;     // 宣告任務：core1Task

/* - - - - - -     Led & Buzzer Toggle Parameters     - - - - - - */
#define bltInLED 2              // 內建LED(GPIO2)
#define ledToggleTime 50000     // LED Toggle Time(500ms)


/* = = = = = = = = = = = =     WiFi & MQTT Settings     = = = = = = = = = = = = */
typedef struct{
    uint8_t wifiOffLine     :1;     // WiFi斷線Flag
    uint8_t mqttOffLine     :1;     // MQTT斷線Flag
    uint8_t blank			:6;
}wifiMqtt_t;
volatile wifiMqtt_t WiFiMqttFlags = {0, 0, 0};      // 宣告並初始化

/* - - - - - - - - - - - -     WiFi     - - - - - - - - - - - - */
// SSID, Password
const char* loginArr[][2] = {
    {SSID1, Password1},
    {SSID2, Password2},
    {SSID3, Password3}
};

/* - - - - - - - - - - - -     MQTT     - - - - - - - - - - - - */
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// MQTT Server(Broker) IP
const char* MQTTServer = "IP Address";
// clientID：用戶端唯一識別碼。如果同時有多個ESP32裝置，方便識別
const char *clientID = "MC1_1_1";
char mcPubTopic[] = "MainController/MC1_1_1";
char mcSubTopic[] = "rpiCmd/MC1_1_1";


/* = = = = = = = = = =     Timer0 Interrupt : Generating Time Base     = = = = = = = = = = */
hw_timer_t *timerPtr = NULL;        // Create a 「pointer：hardware timer」

// Handle with the 「shared variables」
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

typedef struct tFlag{
    uint8_t ledToggleF      :1;
    uint8_t ledToggleAck    :1;
    uint8_t blank           :6;
}timeFlag_t;
volatile timeFlag_t timeFlags = {0, 0, 0};      // 宣告並初始化

typedef struct tCnt{
    uint32_t ledToggleCnt;      // Byte3_0
}timeCnt_t;
volatile timeCnt_t timeCnts = {0};


/* = = = = = = = = = = = = = = = =     Subroutines     = = = = = = = = = = = = = = = = */
/* - - - - - - - -     Core0 Task : Reconnect WiFi & MQTT     - - - - - - - - */
void Task_reConWiFiMQTT(void * pvParameters )
{
    for(;;)
    {
        // 若「WiFi斷線」或「MQTT斷線」
        if(WiFiMqttFlags.wifiOffLine==1 || WiFiMqttFlags.mqttOffLine==1){
            if(WiFiMqttFlags.wifiOffLine==1)
            	Serial.println("WiFi離線，重新連線!!");
            if(WiFiMqttFlags.mqttOffLine == 1)
            	Serial.println("MQTT離線，重新連線!!");

            delay(500);        // Waiting for stable status

            setupScanConWiFi();
	        connectMQTT();
        }
        else		// 若「WiFi與MQTT皆保持連線」，core0Task休息
        {
            delay(1);   // 不可省略
        }
    }
}

/* - - - - - - - - - - - -     Core1 Task : Main Loop     - - - - - - - - - - - - */
void Task_mainLoop(void * pvParameters )
{
    for(;;)
    {
        if(WiFi.status() != WL_CONNECTED)       // WiFi：斷線
            WiFiMqttFlags.wifiOffLine = 1;      // 設定「WiFi斷線」Flag

        if(!mqttClient.connected())             // MQTT：斷線
            WiFiMqttFlags.mqttOffLine = 1;      // 設定「MQTT斷線」Flag

        mqttClient.loop();

        if(timeFlags.ledToggleAck==1){
            timeFlags.ledToggleAck = 0;       // 清除計數達成Flag

            if(digitalRead(bltInLED) == LOW)
                digitalWrite(bltInLED, HIGH);
            else
                digitalWrite(bltInLED, LOW);

            timeFlags.ledToggleF = 1;         // 「重新開始」ledToggle「計數」
        }
    }
}

/* - - - - - - - -      Timer0 ISR(interrupt service routine)     - - - - - - - - */
void IRAM_ATTR timer0ISR(){
    portENTER_CRITICAL_ISR(&timerMux);

    if(timeFlags.ledToggleF==1){
        if(timeCnts.ledToggleCnt >= ledToggleTime){
            timeFlags.ledToggleAck = 1;

            timeFlags.ledToggleF = 0;
            timeCnts.ledToggleCnt = 0;
        }
        else
            timeCnts.ledToggleCnt++;
    }
    else
        timeCnts.ledToggleCnt = 0;

    portEXIT_CRITICAL_ISR(&timerMux);
}

/* - - - - - - - - - - - -      Timer0 Setting     - - - - - - - - - - - - */
void timer0IntInit(){
    // The clock frequency is 80 MHz, by using a pre-scaler value of 80.
    // Interrupt period: 1 uSec
    // Interrupt frequency: 1 MHz
    // 參數設定  timer:timer0, prescaler:80, true:counting "up" mode
    timerPtr = timerBegin(0, 80, true);        // Timer interrupt every "1us"

    // timer0ISR() will be called when the timer 「expires」 and a timer 「interrupt is generated」.
    // Because the 「edge」 mode is set to true in timerAttachInterrupt function, interrupts will be accepted on 「edge of the clock」.
    timerAttachInterrupt(timerPtr, &timer0ISR, true);      // 記得給定ISR的「位址」

    // In 「timerAlarmWrite」 function the timer 「alarm value」 is set to 「10us」 and 「auto repeat」 is 「set to be true」.
    // The ISR will be called every 「10us」.
    timerAlarmWrite(timerPtr, 10, true);

    timerAlarmEnable(timerPtr);     // Enable timer0
}

/* - - - - - - - - - - - -      WiFi Connection     - - - - - - - - - - - - */
void wifiConProc(){
    for(int i=0; i<sizeof(loginArr)/sizeof(loginArr[0]); i++){
        wifiMulti.addAP(loginArr[i][0], loginArr[i][1]);
    }

	Serial.print("使用核心編號：");
	Serial.println(xPortGetCoreID());           //xPortGetCoreID()顯示執行核心編號

    Serial.println("Connecting WiFi...");

    if(wifiMulti.run() == WL_CONNECTED){
        WiFiMqttFlags.wifiOffLine = 0;          //連線成功，清除「WiFi斷線」Flag
    	digitalWrite(bltInLED, HIGH);           // 連線成功，內建LED：亮

        Serial.println("");
        Serial.print("Successful connected to Access Point: ");
        Serial.println(WiFi.SSID());
        Serial.println("WiFi connected");
        Serial.print("IP address: ");           // 顯示：取得的「IP位址」
        Serial.println(WiFi.localIP());
		Serial.print("ESP32 MAC Address = ");
		Serial.println(WiFi.macAddress());
		Serial.println();
    }
}

/* - - - - - - - - - -      WiFi Scan & Call Connection     - - - - - - - - - - */
void setupScanConWiFi(){
    /* - - - - - -   WiFi Setup   - - - - - - */
    WiFi.mode(WIFI_STA);	// Set WiFi to 「Station」 mode
    WiFi.disconnect();
    delay(200);

    /* - - - - - -   WiFi Scan   - - - - - - */
    uint8_t n = WiFi.scanNetworks();		// The number of networks found

    if(n == 0)
        Serial.println("No networks found.");
    else
    {
        Serial.print(n);
        Serial.println(" networks have been found.");

        for(int i=0; i<n; i++)		// Print SSID and RSSI for each network found
        {
        	Serial.printf("%d: ", i+1);
            Serial.print(WiFi.SSID(i));
            Serial.print(" (");
            Serial.print(WiFi.RSSI(i));
            Serial.print(")");
            // 有加密的網路在後面加「*」號，沒加密的加「一個空格」
            Serial.print((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
            // 顯示：AP 的 MAC Address
			Serial.print("  MAC = ");
            Serial.println(WiFi.BSSIDstr(i));

            delay(10);
        }

        wifiConProc();
    }
}

/* - - - - - - - - - - - -      MQTT Connection     - - - - - - - - - - - - */
void connectMQTT(){
    Serial.println("Attempting MQTT connection ...");

    if(!mqttClient.connect(clientID))       // 連接到「用戶端唯一識別碼」指定的這個ESP32裝置
    {
        Serial.print("Failed. Client state = ");
        Serial.println(mqttClient.state());
        Serial.println("Try link MQTT again.");
    }
    else
    {
    	WiFiMqttFlags.mqttOffLine = 0;      //連線成功，清除「MQTT斷線」Flag
		Serial.printf("MQTTClient connected\n\n");
	    mqttClient.subscribe(mcSubTopic);   // Subscribe
    }
}

/* - - - - - - - - - -      Receive MQTT JSON Data     - - - - - - - - - - */
void receivedCallback(char* topic, byte* payload, unsigned int payloadLen){
    String payloadBuf;

    for(int i=0; i<payloadLen; i++){
        payloadBuf += (char)payload[i];
    }

    Serial.print("topic: ");
    Serial.println(topic);

    Serial.print("payload: ");
    Serial.println(payloadBuf);
}

/* - - - - - - - - -      Execution Environment Setting     - - - - - - - - - */
void setup()
{
    Serial.begin(115200);

    pinMode(bltInLED, OUTPUT);
    digitalWrite(bltInLED, LOW);

    timer0IntInit();

    setupScanConWiFi();

    /* - - - - - -   MQTT Setting   - - - - - - */
    mqttClient.setServer(MQTTServer, 1883);
    mqttClient.setCallback(receivedCallback);
    connectMQTT();

    /* - - - - - -   在核心0啟動core0Task   - - - - - - */
    xTaskCreatePinnedToCore(
        Task_reConWiFiMQTT,     // 任務實際對應的Function
        "core0Task",            // 任務名稱
        4096,                   // 所需堆疊空間(常用10000)
        NULL,                   // 輸入值:無
        1,                      // 優先權：0代表最優先執行，1次之，以此類推
        &core0Task,             // 對應的任務handle變數(變數位址)
        0                       // 指定執行核心編號(0、1、tskNO_AFFINITY：系統指定)
    );

    /* - - - - - -   在核心1啟動core1Task   - - - - - - */
    xTaskCreatePinnedToCore(
        Task_mainLoop,      // 任務實際對應的Function
        "core1Task",        // 任務名稱
        10000,              // 所需堆疊空間(常用10000)
        NULL,               // 輸入值:無
        1,                  // 2個Task的優先權「不可同時設為0」，「可同時設為1」
        &core1Task,         // 對應的任務handle變數(變數位址)
        1                   // 指定執行核心編號(0、1、tskNO_AFFINITY：系統指定)
    );

    /* - - - - - -   設定相關應用Flags   - - - - - - */
    timeFlags.ledToggleF = 1;   // 「開始」ledToggle「計數Flag」
}

void loop(){}
