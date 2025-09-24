// WS2812 LED Ring mit ESP-NOW für ESP32-PICO-D4 (LoLin D32)
// KORRIGIERTE VERSION - Node-ID Problem behoben, 22 Sept 2025
// https://claude.ai/public/artifacts/50918975-1c64-46c3-9313-9da90cd87a2d 

#include <FastLED.h>
#include <esp_now.h>
#include <WiFi.h>
#include "esp_efuse.h"

// LED Ring Konfiguration
#define LED_PIN     13 // 23       
#define NUM_LEDS    8 //1      
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

// ESP-NOW Variablen
#define MAX_PEERS 10
struct PeerDevice {
  uint8_t mac[6];
  unsigned long lastSeen;
  bool active;
};

PeerDevice knownPeers[MAX_PEERS];
int activePeerCount = 0;
unsigned long lastPeerScan = 0;
const unsigned long PEER_SCAN_INTERVAL = 2000;  // 2 statt 3 Sekunden
const unsigned long PEER_TIMEOUT = 10000;       

// Pulsing Parameter
float brightness = 0;
int myNodeId = -1; // GEÄNDERT: Initialisierung mit -1 statt 0
bool espnowInitialized = false;

// Broadcast Message Structure
typedef struct {
  char deviceType[20];
  unsigned long timestamp;
  int nodeId;
} BroadcastMessage;

BroadcastMessage myMessage;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-PICO-D4 LED Ring Start ===");
  
  // GEÄNDERT: WiFi nur EINMAL initialisieren
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500); // Kurz warten für WiFi Stabilisierung
  
  // GEÄNDERT: Node-ID SOFORT generieren
  generateNodeId();
  
  // Sicherstellen, dass Node-ID korrekt gesetzt wurde
  if (myNodeId <= 0) {
    Serial.println("⚠️  FEHLER: Node-ID nicht korrekt generiert!");
    // Fallback: Einfache Berechnung
    uint64_t mac = ESP.getEfuseMac();
    myNodeId = (mac & 0xFFFF);
    if (myNodeId == 0) myNodeId = 12345; // Absoluter Fallback
    Serial.println("Fallback Node-ID: " + String(myNodeId));
  }
  
  // LED Ring initialisieren
  Serial.println("Initialisiere LED Ring...");
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(100);
  
  // Test: Alle LEDs kurz einschalten
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
  delay(500);
  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show();
  delay(500);
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  delay(500);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  Serial.println("LED Test abgeschlossen");
  
  // GEÄNDERT: MAC nur einmal ausgeben
  Serial.println("MAC Adresse: " + WiFi.macAddress());
  
  // ESP-NOW Setup
  Serial.println("Initialisiere ESP-NOW...");
  esp_err_t result = esp_now_init();
  if (result != ESP_OK) {
    Serial.println("ESP-NOW Init Fehler: " + String(result));
    espnowInitialized = false;
  } else {
    Serial.println("ESP-NOW erfolgreich initialisiert");
    espnowInitialized = true;
    
    // Callbacks registrieren
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceived);
    
    // GEÄNDERT: Message vorbereiten mit Verifikation
    strcpy(myMessage.deviceType, "LED_RING_D32");
    myMessage.nodeId = myNodeId;
    
    Serial.println("Message Node-ID gesetzt auf: " + String(myMessage.nodeId));
  }
  
  // Peer Array initialisieren
  for(int i = 0; i < MAX_PEERS; i++) {
    knownPeers[i].active = false;
  }
  
  // GEÄNDERT: Finale Verifikation
  Serial.println("=== SETUP ABGESCHLOSSEN ===");
  Serial.println("Finale Node-ID: " + String(myNodeId));
  Serial.println("Message Node-ID: " + String(myMessage.nodeId));
  Serial.println("=============================\n");
}

// VERBESSERTE Node-ID Generierung mit mehr Debug-Info
void generateNodeId() {
    Serial.println("=== GENERIERE NODE-ID ===");
    
    // MAC-Adresse auslesen
    uint8_t mac[6];
    WiFi.macAddress(mac);
    
    // MAC-Adresse anzeigen
    Serial.print("MAC Adresse (Bytes): ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) Serial.print(":");
        if (mac[i] < 16) Serial.print("0");
        Serial.print(mac[i], HEX);
    }
    Serial.println();
    
    // Mehrere Berechnungsmethoden für Robustheit
    int nodeId_A = (mac[4] << 8) | mac[5];  // Standard
    int nodeId_B = (mac[3] << 8) | mac[4];  // Alternative 1
    int nodeId_C = (mac[2] << 8) | mac[5];  // Alternative 2
    
    Serial.println("Berechnung A (Bytes 4+5): " + String(nodeId_A));
    Serial.println("Berechnung B (Bytes 3+4): " + String(nodeId_B));  
    Serial.println("Berechnung C (Bytes 2+5): " + String(nodeId_C));
    
    // Beste nicht-null Methode wählen
    if (nodeId_A != 0) {
        myNodeId = nodeId_A;
        Serial.println(">>> Gewählt: Methode A");
    } else if (nodeId_B != 0) {
        myNodeId = nodeId_B;
        Serial.println(">>> Gewählt: Methode B");
    } else if (nodeId_C != 0) {
        myNodeId = nodeId_C;
        Serial.println(">>> Gewählt: Methode C");
    } else {
        myNodeId = 9999; // Fallback
        Serial.println(">>> Gewählt: Fallback");
    }
    
    Serial.println("Node-ID FINAL: " + String(myNodeId));
    Serial.println("Node-ID (hex): 0x" + String(myNodeId, HEX));
    Serial.println("=== NODE-ID SETUP ABGESCHLOSSEN ===");
}

void loop() {
  // GEÄNDERT: Debug-Check für Node-ID Stabilität hinzugefügt
  static int lastNodeId = myNodeId;
  if (lastNodeId != myNodeId) {
    Serial.println("⚠️  Node-ID GEÄNDERT: " + String(lastNodeId) + " → " + String(myNodeId));
    lastNodeId = myNodeId;
  }
  
  // ESP-NOW Peer Discovery
  if(espnowInitialized && (millis() - lastPeerScan > PEER_SCAN_INTERVAL)) {
    // GEÄNDERT: Verifikation vor Broadcast
    if (myNodeId <= 0) {
      Serial.println("⚠️  Node-ID ist 0 oder negativ! Überspringe Broadcast.");
    } else {
      broadcastPresence();
    }
    updateActivePeers();
    lastPeerScan = millis();
  }
  
  // LED Effekt basierend auf Peer-Anzahl
  selectEffectByPeers();
  
  // Debug Info alle 5 Sekunden
  static unsigned long lastDebug = 0;
  if(millis() - lastDebug > 5000) {
    printStatus();
    lastDebug = millis();
  }
  
  delay(50);
}

// === ESP-NOW FUNKTIONEN ===

void broadcastPresence() {
  if(!espnowInitialized) return;
  
  // GEÄNDERT: Node-ID in Message aktualisieren (Sicherheitsmaßnahme)
  myMessage.nodeId = myNodeId;
  myMessage.timestamp = millis();
  
  // Broadcast Adresse
  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  
  // Prüfen ob Broadcast-Peer existiert
  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    esp_err_t result = esp_now_add_peer(&peerInfo);
    if (result != ESP_OK) {
      Serial.println("Broadcast-Peer Fehler: " + String(result));
      return;
    } else {
      Serial.println("Broadcast-Peer hinzugefügt");
    }
  }
  
  // Message senden
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&myMessage, sizeof(myMessage));
  if (result == ESP_OK) {
    Serial.print("✓ Broadcast (Node " + String(myNodeId) + ") ");
  } else {
    Serial.print("✗ Send Fehler: " + String(result) + " ");
  }
}

// GEÄNDERT: Korrekte Callback-Signatur für neuere ESP32-Versionen
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("OK");
  } else {
    Serial.println("FAIL");
  }
}

void onDataReceived(const esp_now_recv_info* recv_info, const uint8_t *incomingData, int len) {
  if (len != sizeof(BroadcastMessage)) {
    Serial.println("Ungültige Message-Größe empfangen");
    return;
  }
  
  BroadcastMessage message;
  memcpy(&message, incomingData, sizeof(message));
  
  // GEÄNDERT: Mehr Debug-Info
  Serial.print("📡 Empfangen von Node " + String(message.nodeId));
  Serial.println(" (Typ: " + String(message.deviceType) + ")");
  
  // Nur LED Ring Devices beachten und nicht eigene Messages
  if(strcmp(message.deviceType, "LED_RING_D32") == 0 && message.nodeId != myNodeId) {
    updatePeerList(recv_info->src_addr);
    Serial.println("✅ Gültiger Peer erkannt: Node " + String(message.nodeId));
  } else {
    Serial.println("❌ Ignoriert (eigene Node oder anderer Typ)");
  }
}

void updatePeerList(const uint8_t* mac) {
  unsigned long currentTime = millis();
  
  // Prüfen ob Peer bereits bekannt
  for(int i = 0; i < MAX_PEERS; i++) {
    if(knownPeers[i].active && memcmp(knownPeers[i].mac, mac, 6) == 0) {
      knownPeers[i].lastSeen = currentTime;
      return;
    }
  }
  
  // Neuen Peer hinzufügen
  for(int i = 0; i < MAX_PEERS; i++) {
    if(!knownPeers[i].active) {
      memcpy(knownPeers[i].mac, mac, 6);
      knownPeers[i].lastSeen = currentTime;
      knownPeers[i].active = true;
      
      Serial.print("🆕 Neuer Peer: ");
      for(int j = 0; j < 6; j++) {
        Serial.printf("%02X", mac[j]);
        if(j < 5) Serial.print(":");
      }
      Serial.println();
      break;
    }
  }
}

void updateActivePeers() {
  int oldCount = activePeerCount;
  activePeerCount = 0;
  unsigned long currentTime = millis();
  
  for(int i = 0; i < MAX_PEERS; i++) {
    if(knownPeers[i].active) {
      if(currentTime - knownPeers[i].lastSeen > PEER_TIMEOUT) {
        knownPeers[i].active = false;
        Serial.println("Peer Timeout");
      } else {
        activePeerCount++;
      }
    }
  }
  
  // GEÄNDERT: Meldung nur bei Änderung
  if(oldCount != activePeerCount) {
    Serial.println("👥 Aktive Peers: " + String(oldCount) + " → " + String(activePeerCount));
  }
}

// === LED EFFEKTE ===

void selectEffectByPeers() {
  switch(activePeerCount) {
    case 0:  // Allein - Blaues Pulsieren
      simplePulse(CRGB::Blue);
      break;
    case 1:  // 1 Peer - Grünes Pulsieren
      simplePulse(CRGB::Green);
      break;
    case 2:  // 2 Peers - Rotes Pulsieren
      simplePulse(CRGB::Red);
      break;
    case 3:  // 3 Peers - Gelbes Pulsieren
      simplePulse(CRGB::Yellow);
      break;
    default: // 4+ Peers - Weißes Pulsieren
      simplePulse(CRGB::White);
      break;
  }
}

void simplePulse(CRGB color) {
  brightness = (sin(millis() * 0.003) + 1.0) * 0.5;
  
  CRGB scaledColor = color;
  scaledColor.nscale8((int)(brightness * 255));
  
  fill_solid(leds, NUM_LEDS, scaledColor);
  FastLED.show();
}

void printStatus() {
  Serial.println("\n=== STATUS ===");
  Serial.println("Node ID: " + String(myNodeId));
  Serial.println("Message Node ID: " + String(myMessage.nodeId));
  Serial.println("ESP-NOW: " + String(espnowInitialized ? "OK" : "FEHLER"));
  Serial.println("Aktive Peers: " + String(activePeerCount));
  Serial.println("Freier Heap: " + String(ESP.getFreeHeap()));
  Serial.println("==============\n");
}