// Minimal OpenTurbine Cluster Protocol companion for an ESP32.
// Copy ../OTCClusterClient.h beside this sketch or add the examples directory
// to the Arduino include path before compiling.

#include "OTCClusterClient.h"

static constexpr int ECU_RX_PIN = 16;  // companion receives ECU cluster TX
static constexpr int ECU_TX_PIN = 17;  // optional: companion sends to ECU RX
static constexpr uint32_t ECU_BAUD = 115200;

HardwareSerial EcuSerial(2);
OTCClusterClient otc;

void setup() {
  Serial.begin(115200);
  EcuSerial.begin(ECU_BAUD, SERIAL_8N1, ECU_RX_PIN, ECU_TX_PIN);
  otc.begin(EcuSerial);
  otc.requestSchema();
}

void loop() {
  otc.update();

  static unsigned long lastPrintMs = 0;
  if (otc.dataReceived && millis() - lastPrintMs >= 250) {
    lastPrintMs = millis();
    Serial.printf("mode=%s", otc.modeText());
    if (otc.hasN1)  Serial.printf(" N1=%.0f rpm", otc.n1Rpm);
    if (otc.hasN2)  Serial.printf(" N2=%.0f rpm", otc.n2Rpm);
    if (otc.hasTot) Serial.printf(" TOT=%.1f C", otc.totC);
    if (otc.hasTit) Serial.printf(" TIT=%.1f C", otc.titC);
    if (otc.hasOil) Serial.printf(" oil=%.2f bar", otc.oilBar);
    Serial.printf(" status=%s\n", otc.statusText);
  }

  static bool lossWasShown = false;
  const bool lost = otc.signalLost();
  if (lost && !lossWasShown) Serial.println("NO DATA FROM ECU");
  lossWasShown = lost;
}

