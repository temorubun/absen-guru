#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <time.h>

// Konfigurasi WiFi
const char* ssid = "NARWADAN";
const char* password = "happyhouse2010";
const char* hostname = "fingerprint-esp32"; // Hostname untuk mDNS

// Konfigurasi Supabase
const String supabase_url = "https://zntmnajpxgusovqroneh.supabase.co";
const String supabase_api_key = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InpudG1uYWpweGd1c292cXJvbmVoIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NDQ4NzM4NjYsImV4cCI6MjA2MDQ0OTg2Nn0.2cOGbm_aOp9wxkwhRc56yv0fRtVakUmhMMC8_-nhw1A"; // Disingkat untuk keamanan

// Konfigurasi NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // GMT+7 dalam detik
const int daylightOffset_sec = 0;

// Inisialisasi serial dan fingerprint
HardwareSerial mySerial(2);  // UART2: TX = 17, RX = 16
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// Variabel global
String mode = "absensi";
int idPendaftaran = -1;
bool sedangMendaftar = false;
int jumlahPercobaan = 0;
const int MAX_PERCOBAAN = 3;

// Create servers
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Forward declarations
void kirimAbsensiKeWeb(int fingerprint_id);
int scanSidikJari();
bool daftarSidikJari(int id);
uint8_t getFingerprintEnroll(int id);
void sendSerialUpdate(String message);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      }
      break;
  }
}

void sendSerialUpdate(String message) {
  Serial.println(message);
  webSocket.broadcastTXT(message);
}

void handleGetIP() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json");
  
  DynamicJsonDocument doc(200);
  doc["ip"] = WiFi.localIP().toString();
  doc["wsPort"] = 81;
  doc["hostname"] = hostname;
  doc["status"] = "success";
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handlePostRequest() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json");
  sendSerialUpdate("Received a request");
  
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    sendSerialUpdate("Data diterima dari web: " + body);
    
    idPendaftaran = body.substring(body.indexOf(":") + 1).toInt();
    mode = "daftar";
    sedangMendaftar = true;
    jumlahPercobaan = 0;
    sendSerialUpdate("Mode pendaftaran diaktifkan, ID: " + String(idPendaftaran));
    
    daftarSidikJari(idPendaftaran);
    
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Pendaftaran berhasil untuk ID: " + String(idPendaftaran) + "\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Bad Request\"}");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Koneksi WiFi
  WiFi.begin(ssid, password);
  sendSerialUpdate("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  sendSerialUpdate("WiFi terhubung");
  
  // Setup mDNS
  if (MDNS.begin(hostname)) {
    sendSerialUpdate("mDNS started. Device can be reached at: " + String(hostname) + ".local");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  } else {
    sendSerialUpdate("Error setting up mDNS");
  }
  
  // Tampilkan IP Address
  sendSerialUpdate("IP Address: " + WiFi.localIP().toString());

  // Konfigurasi waktu
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Koneksi fingerprint
  mySerial.begin(57600, SERIAL_8N1, 16, 17); // RX=16, TX=17
  finger.begin(57600);

  if (finger.verifyPassword()) {
    sendSerialUpdate("Sensor fingerprint berhasil terdeteksi");
  } else {
    sendSerialUpdate("Sensor fingerprint tidak ditemukan. Periksa koneksi!");
    while (1);
  }

  // Setup routes with CORS
  server.on("/ip", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
    server.send(200);
  });
  
  server.on("/ip", HTTP_GET, handleGetIP);
  
  server.on("/daftar", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
    server.send(200);
  });
  
  server.on("/daftar", HTTP_POST, handlePostRequest);
  
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  sendSerialUpdate("HTTP Server Started");
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  sendSerialUpdate("WebSocket Server Started");
}

void loop() {
  server.handleClient(); // Handle incoming client requests
  webSocket.loop();

  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    Serial.println("Data diterima dari web: " + input);

    if (input.startsWith("DAFTAR:")) {
      idPendaftaran = input.substring(7).toInt();
      mode = "daftar";
      sedangMendaftar = true;
      jumlahPercobaan = 0;
      Serial.println("Mode pendaftaran diaktifkan, ID: " + String(idPendaftaran));
    }
  }

  if (mode == "daftar" && sedangMendaftar) {
    Serial.println("Memulai pendaftaran sidik jari untuk ID: " + String(idPendaftaran));
    if (daftarSidikJari(idPendaftaran)) {
      sedangMendaftar = false;
      mode = "absensi";
    } else if (jumlahPercobaan >= MAX_PERCOBAAN) {
      Serial.println("Gagal mendaftar setelah " + String(MAX_PERCOBAAN) + " percobaan");
      sedangMendaftar = false;
      mode = "absensi";
    }
  }

  if (mode == "absensi") {
    int idTerbaca = scanSidikJari();
    if (idTerbaca > 0) {
      Serial.println("ID Terdeteksi: " + String(idTerbaca));
      kirimAbsensiKeWeb(idTerbaca);
    }
  }
}

void kirimAbsensiKeWeb(int fingerprint_id) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terhubung. Gagal mengirim data.");
    return;
  }

  // Dapatkan waktu saat ini
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    Serial.println("Gagal mendapatkan waktu");
    return;
  }

  char waktu[25];
  strftime(waktu, sizeof(waktu), "%Y-%m-%d %H:%M:%S", &timeinfo);

  HTTPClient http;
  
  // Pertama, dapatkan staff_id berdasarkan fingerprint_id
  String staffEndpoint = supabase_url + "/rest/v1/staff?fingerprint_id=eq." + String(fingerprint_id) + "&select=id";
  http.begin(staffEndpoint);
  http.addHeader("apikey", supabase_api_key);
  http.addHeader("Authorization", "Bearer " + supabase_api_key);
  
  int httpCode = http.GET();
  String staff_id = "";
  
  if (httpCode > 0) {
    String payload = http.getString();
    // Parse response untuk mendapatkan staff_id
    if (payload.length() > 2) { // minimal response valid: [{}]
      DynamicJsonDocument doc(200);
      deserializeJson(doc, payload);
      if (doc[0]["id"]) {
        staff_id = doc[0]["id"].as<String>();
      }
    }
  }
  
  http.end();

  // Jika staff_id ditemukan, kirim data absensi
  if (staff_id.length() > 0) {
    String endpoint = supabase_url + "/rest/v1/absensi";
    http.begin(endpoint);
    http.addHeader("apikey", supabase_api_key);
    http.addHeader("Authorization", "Bearer " + supabase_api_key);
    http.addHeader("Content-Type", "application/json");
    
    String jsonData = "{\"fingerprint_id\":" + String(fingerprint_id) + 
                     ",\"staff_id\":\"" + staff_id + "\"" +
                     ",\"waktu_masuk\":\"" + String(waktu) + "\"}";
    
    httpCode = http.POST(jsonData);
    
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println("Respon Supabase:");
      Serial.println(payload);
      sendSerialUpdate("Absensi berhasil dicatat untuk ID: " + String(fingerprint_id));
    } else {
      Serial.println("HTTP Request gagal, kode: " + String(httpCode));
      sendSerialUpdate("Gagal mencatat absensi untuk ID: " + String(fingerprint_id));
    }
  } else {
    Serial.println("Staff dengan fingerprint ID " + String(fingerprint_id) + " tidak ditemukan");
    sendSerialUpdate("Staff dengan fingerprint ID " + String(fingerprint_id) + " tidak ditemukan");
  }

  http.end();
}

int scanSidikJari() {
  if (finger.getImage() != FINGERPRINT_OK) return -1;
  if (finger.image2Tz() != FINGERPRINT_OK) return -1;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) return -1;
  return finger.fingerID;
}

bool daftarSidikJari(int id) {
  while (jumlahPercobaan < MAX_PERCOBAAN && !getFingerprintEnroll(id)) {
    jumlahPercobaan++;
    sendSerialUpdate("Percobaan ke-" + String(jumlahPercobaan) + " dari " + String(MAX_PERCOBAAN));
    delay(1000);
  }
  return jumlahPercobaan < MAX_PERCOBAAN;
}

uint8_t getFingerprintEnroll(int id) {
  int p = -1;
  sendSerialUpdate("Menunggu sidik jari pertama...");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) {
      delay(100);
      continue;
    }
  }

  if (finger.image2Tz(1) != FINGERPRINT_OK) return false;

  sendSerialUpdate("Lepaskan jari...");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER);

  sendSerialUpdate("Letakkan jari yang sama lagi...");
  while (finger.getImage() != FINGERPRINT_OK);
  if (finger.image2Tz(2) != FINGERPRINT_OK) return false;

  if (finger.createModel() != FINGERPRINT_OK) {
    sendSerialUpdate("Gagal membuat model. Ulangi proses.");
    return false;
  }

  if (finger.storeModel(id) == FINGERPRINT_OK) {
    sendSerialUpdate("Sidik jari berhasil disimpan di ID: " + String(id));
    mode = "absensi";
    sedangMendaftar = false;
    return true;
  } else {
    sendSerialUpdate("Gagal menyimpan model ke sensor.");
    return false;
  }
}
