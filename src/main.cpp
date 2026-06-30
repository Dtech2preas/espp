#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/dhcp.h>
#include <WiFiAP.h>

extern "C" {
#include "lwip/lwip_napt.h"
void ip_napt_enable(u32_t addr, int enable);
}

#include <WebServer.h>
#include <Update.h>
#include <HTTPClient.h>

// Castle House WiFi
const char* ssid_sta = "Castle House";
const char* password_sta = "12345678";

// DTECH AP
const char* ssid_ap = "DTECH";
const char* password_ap = "12345678";

// AP Gateway IP
IPAddress local_ip(192,168,4,1);
IPAddress gateway(192,168,4,1);
IPAddress subnet(255,255,255,0);

WebServer server(80);

const int MAX_LOG_SIZE = 4096;
String logBuffer = "";

void addLog(String msg) {
  Serial.println(msg);
  if (logBuffer.length() + msg.length() > MAX_LOG_SIZE) {
    int cutIndex = logBuffer.indexOf('\n', msg.length() + 500);
    if (cutIndex == -1) cutIndex = msg.length() + 500;
    logBuffer = logBuffer.substring(cutIndex + 1);
  }
  logBuffer += msg + "\n";
}

bool checkAccess() {
  IPAddress remote = server.client().remoteIP();
  // Only allow access from 192.168.4.x
  if (remote[0] == 192 && remote[1] == 168 && remote[2] == 4) {
    return true;
  }
  server.send(403, "text/plain", "Forbidden: Access restricted to DTECH network only.");
  return false;
}

bool checkInternet() {
  HTTPClient http;
  http.begin("http://clients3.google.com/generate_204");
  int httpCode = http.GET();
  http.end();
  return (httpCode == 204);
}

void handleRoot() {
  if (!checkAccess()) return;

  String html = "<html><head><title>DTECH Admin Panel</title>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>body{font-family:Arial; padding:20px;} .card{border:1px solid #ccc; padding:15px; margin-bottom:15px; border-radius:5px;} </style>";
  html += "</head><body>";
  html += "<h2>DTECH WiFi Repeater</h2>";

  html += "<div class='card'>";
  html += "<h3>Status</h3>";
  html += "<p><b>Upstream WiFi (Castle House):</b> " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "</p>";
  html += "<p><b>DTECH Hotspot:</b> Running</p>";
  html += "<p><b>Connected Devices (DTECH):</b> " + String(WiFi.softAPgetStationNum()) + "</p>";

  html += "<p><b>Internet Access:</b> Checking...</p>"; // Handled via JS below
  html += "<p id='internet_status'><i>Waiting for check...</i></p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Admin Tools</h3>";
  html += "<ul>";
  html += "<li><a href='/logs'>View Console Logs</a></li>";
  html += "<li><a href='/ota'>Upload New Firmware (OTA)</a></li>";
  html += "<li><a href='/speedtest'>Test Internet Speed</a></li>";
  html += "</ul>";
  html += "</div>";

  // JS for internet check
  html += "<script>";
  html += "fetch('/check_internet').then(r=>r.text()).then(t=>{document.getElementById('internet_status').innerHTML = '<b>' + t + '</b>';});";
  html += "</script>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleCheckInternet() {
  if (!checkAccess()) return;
  if (checkInternet()) {
    server.send(200, "text/plain", "Yes (Online)");
  } else {
    server.send(200, "text/plain", "No (Offline)");
  }
}

void handleLogs() {
  if (!checkAccess()) return;
  String html = "<html><head><title>Logs</title><meta http-equiv=\"refresh\" content=\"5\"></head><body>";
  html += "<h2>Console Logs (Auto-refreshing)</h2>";
  html += "<pre style='background:#f4f4f4; padding:10px; border:1px solid #ddd; white-space:pre-wrap;'>";
  html += logBuffer;
  html += "</pre>";
  html += "<br><a href='/'>Back to Dashboard</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSpeedTest() {
  if (!checkAccess()) return;

  addLog("Starting speed test...");

  HTTPClient http;
  // Use a reliable 10MB test file
  http.begin("http://ipv4.download.thinkbroadband.com/10MB.zip");

  unsigned long startTime = millis();
  int httpCode = http.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      int len = http.getSize();
      uint8_t buff[128] = { 0 };
      WiFiClient * stream = http.getStreamPtr();
      int totalBytes = 0;
      unsigned long testStart = millis();

      // Download for max 5 seconds
      while(http.connected() && (len > 0 || len == -1) && (millis() - testStart < 5000)) {
        size_t size = stream->available();
        if(size) {
          int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
          totalBytes += c;
          if(len > 0) {
            len -= c;
          }
        }
      }
      unsigned long duration = millis() - testStart;
      float speedMbps = (totalBytes * 8.0) / (duration / 1000.0) / 1000000.0;

      String res = "Speed test finished. Downloaded " + String(totalBytes) + " bytes in " + String(duration) + " ms.<br>";
      res += "Estimated Speed: <b>" + String(speedMbps, 2) + " Mbps</b>";
      addLog("Speed test result: " + String(speedMbps, 2) + " Mbps");

      server.send(200, "text/html", "<h2>Speed Test</h2><p>" + res + "</p><br><a href='/'>Back</a>");
    } else {
      server.send(200, "text/html", "Error in HTTP request. Code: " + String(httpCode) + "<br><a href='/'>Back</a>");
      addLog("Speed test HTTP error: " + String(httpCode));
    }
  } else {
    server.send(200, "text/html", "Connection failed.<br><a href='/'>Back</a>");
    addLog("Speed test connection failed.");
  }
  http.end();
}

void handleOTAForm() {
  if (!checkAccess()) return;
  String html = "<html><body><h2>Upload New Firmware</h2>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='update'>";
  html += "<input type='submit' value='Update'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  addLog("Starting ESP32 WiFi Repeater...");

  // Connect to Castle House WiFi
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid_sta, password_sta);
  addLog("Connecting to Castle House WiFi...");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    addLog("Connected! STA IP: " + WiFi.localIP().toString());
  } else {
    addLog("Failed to connect to upstream WiFi. Will keep retrying in background.");
  }

  // Start DTECH Access Point
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ssid_ap, password_ap);
  addLog("DTECH AP started! IP: " + WiFi.softAPIP().toString());

  // Enable NAT / Internet forwarding
  u32_t napt_netif_ip = local_ip;
  ip_napt_enable(htonl(napt_netif_ip), 1);
  addLog("Internet sharing enabled.");

  // Setup Web Server routes
  server.on("/", handleRoot);
  server.on("/logs", handleLogs);
  server.on("/speedtest", handleSpeedTest);
  server.on("/check_internet", handleCheckInternet);

  // OTA Routes
  server.on("/ota", HTTP_GET, handleOTAForm);
  server.on("/update", HTTP_POST, []() {
    if (!checkAccess()) return;
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "UPDATE FAILED" : "UPDATE SUCCESS! Rebooting...");
    ESP.restart();
  }, []() {
    if (!checkAccess()) return;
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      addLog("Update start: " + upload.filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        addLog("Update Success: " + String(upload.totalSize) + " bytes.");
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
  addLog("Admin WebServer started on port 80");
}

void loop() {
  server.handleClient();

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      addLog("Upstream connection lost. Reconnecting...");
      WiFi.reconnect();
    }
  }
}