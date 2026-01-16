#define TINY_GSM_MODEM_SIM800
#define SerialMon Serial
#define SerialAT Serial1
#define TINY_GSM_DEBUG SerialMon
#define GSM_PIN ""

#include <TinyGsmClient.h>

// --- USER CONFIGURATION ---
#define ADMIN_NUMBER ""  // REPLACE WITH YOUR NUMBER
#define SMS_COOLDOWN 180000            // 3 Minutes (180000ms) between alert SMS to prevent spam

// --- PINS ---
#define MODEM_TX 26
#define MODEM_RX 27
#define MODEM_RST 25
#define MQ2_PIN 34    // Analog Pin for Smoke
#define FLAME_PIN 35  // Digital Pin for Fire (0 = Fire detected usually)
#define LED_PIN 2     // Onboard LED for status

// --- THRESHOLDS ---
const int SMOKE_THRESHOLD = 2500;  // Adjust based on your MQ2 calibration (0-4095)

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

TinyGsmClient client(modem);

// Variables
String received_message = "";
String sender_number = "";
bool isReceivingMessage = false;

// Timer Variables for non-blocking alerts
unsigned long lastFireSmsTime = 0;
unsigned long lastSmokeSmsTime = 0;

// Function Prototypes
String extractPhoneNumber(String response);
void handleSms(String number, String msg);
void checkSensors();

void setup() {
  SerialMon.begin(115200);
  delay(100);

  // Sensor Setup
  pinMode(MQ2_PIN, INPUT);
  pinMode(FLAME_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Modem Setup
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, LOW);
  delay(100);
  digitalWrite(MODEM_RST, HIGH);
  delay(3000);

  SerialMon.println("Wait ...");
  SerialAT.begin(115200, SERIAL_8N1, MODEM_TX, MODEM_RX);
  delay(3000);
  SerialMon.println("Initializing modem ...");
  modem.restart();
  delay(3000);
  modem.init();
  if (GSM_PIN && modem.getSimStatus() != 3) {
    modem.simUnlock(GSM_PIN);
  }
  String modemInfo = modem.getModemInfo();
  SerialMon.print("Modem Info: ");
  SerialMon.println(modemInfo);
  SerialMon.print("Waiting for network...");
  if (!modem.waitForNetwork()) {
    SerialMon.println(" fail");
    delay(10000);  // wait 10s, for connected to network successfully
    return;
  }
  SerialMon.println(" success");
  if (modem.isNetworkConnected()) {
    DBG("Network connected");
  }
  String imei = modem.getIMEI();
  SerialMon.print("IMEI: ");
  SerialMon.println(imei);
  String operatorName = modem.getOperator();
  SerialMon.print("Operator: ");
  SerialMon.println(operatorName);
  int signalQuality = modem.getSignalQuality();  // Signal quality (0–31, 99 = not known)
  SerialMon.print("Signal Quality (0-31): ");
  SerialMon.println(signalQuality);
  SerialMon.println("Enabling network time synchronization...");

  // Sync Time for timestamps
  modem.sendAT("+CLTS=1");
  modem.sendAT("&W");

  // SMS Mode
  modem.sendAT("+CMGF=1");
  delay(1000);
  SerialAT.print("AT+CNMI=2,2,0,0,0\r");
  delay(1000);

  // Notify Boot
  SerialMon.println("System Online. Sending Boot SMS...");
  modem.sendSMS(ADMIN_NUMBER, "IoT System Online: Fire & Gas Monitoring Active.");
}

void loop() {
  // 1. Check for Incoming SMS
  while (SerialAT.available()) {
    String response = SerialAT.readStringUntil('\n');
    response.trim();
    if (response.startsWith("+CMT: ")) {
      sender_number = extractPhoneNumber(response);
      isReceivingMessage = true;
    } else if (isReceivingMessage) {
      received_message = response;
      isReceivingMessage = false;
      received_message.trim();
      received_message.toLowerCase();
      handleSms(sender_number, received_message);
    }
  }

  // 2. Check Sensors
  checkSensors();
}

void checkSensors() {
  int smokeValue = analogRead(MQ2_PIN);
  int flameValue = digitalRead(FLAME_PIN);  // Usually LOW (0) means Fire detected for these modules

  // --- FIRE DETECTION LOGIC ---
  if (flameValue == LOW) {  // Fire Detected
    digitalWrite(LED_PIN, HIGH);
    SerialMon.println("CRITICAL: FIRE DETECTED!");

    // Check if we can send SMS (Cooldown logic)
    if (millis() - lastFireSmsTime > SMS_COOLDOWN || lastFireSmsTime == 0) {
      String alertMsg = "URGENT ALERT: Fire Detected! Sensor Value: " + String(flameValue);
      modem.sendSMS(ADMIN_NUMBER, alertMsg);
      SerialMon.println("Fire SMS Sent.");
      lastFireSmsTime = millis();
    }
  }

  // --- SMOKE DETECTION LOGIC ---
  else if (smokeValue > SMOKE_THRESHOLD) {
    digitalWrite(LED_PIN, HIGH);
    SerialMon.print("WARNING: High Gas/Smoke Level: ");
    SerialMon.println(smokeValue);

    if (millis() - lastSmokeSmsTime > SMS_COOLDOWN || lastSmokeSmsTime == 0) {
      String alertMsg = "WARNING: Smoke/Gas Leak Detected! Level: " + String(smokeValue);
      modem.sendSMS(ADMIN_NUMBER, alertMsg);
      SerialMon.println("Smoke SMS Sent.");
      lastSmokeSmsTime = millis();
    }
  }

  // --- NORMAL STATE ---
  else {
    digitalWrite(LED_PIN, LOW);
  }
}

void handleSms(String number, String msg) {
  SerialMon.print("SMS From: ");
  SerialMon.println(number);
  SerialMon.print("Message: ");
  SerialMon.println(msg);

  if (msg == "status") {
    // Read current sensor values for the report
    int currentSmoke = analogRead(MQ2_PIN);
    int currentFlame = digitalRead(FLAME_PIN);
    int signal = modem.getSignalQuality();
    String operatorName = modem.getOperator();

    String statusMsg = "--- SYSTEM REPORT ---\n";
    statusMsg += "Power: ON\n";
    statusMsg += "Signal: " + String(signal) + "%\n";
    statusMsg += "Smoke Level: " + String(currentSmoke) + " (Limit: " + String(SMOKE_THRESHOLD) + ")\n";
    statusMsg += "Fire Sensor: " + String(currentFlame == LOW ? "DETECTED!" : "Safe") + "\n";

    modem.sendSMS(number, statusMsg);
    SerialMon.println("Status Report Sent.");
  } else {
    modem.sendSMS(number, "Unknown Command. Send 'status' to get sensor data.");
  }
}

String extractPhoneNumber(String response) {
  int startIndex = response.indexOf("\"") + 1;
  int endIndex = response.indexOf("\",", startIndex);
  return response.substring(startIndex, endIndex);
}