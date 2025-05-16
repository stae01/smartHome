#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <HTTPClient.h>

// Pines
#define PIR_PIN 13
#define SWITCH_PIN 27
#define GAS_PIN 34
#define DHT_PIN 4
#define BUZZER_PIN 26
#define LED_PIN 25
#define FAN_PIN 23

// Configuración API
const char* serverUrl = "http://192.168.1.71:5000/api/eventos";

// Wi-Fi
const char* ssid = "INFINITUM7B45";
const char* password = "UuPdwNRxL8";

// WebServer síncrono
WebServer server(80);

// DHT
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// Estado y lógica
bool alarmaActiva = false;
bool alarmaSonando = false;
String modoAlarma = "ninguno";  // total, noche
String motivoAlarma = "";

// Umbrales y tiempos
int umbralGas = 600;
int umbralHumedad = 70;
unsigned long tiempoDeArme = 0;
const unsigned long tiempoGracia = 5000;
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 2000;
unsigned long lastBlinkTime = 0;
const unsigned long blinkInterval = 200;

// Lecturas
int gasValor = 0;
float humedad = 0;
bool pirActivo = false;
bool puertaAbierta = false;

// Prototipo
void enviarEvento();

// — HTTP Handlers

void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) {
    server.send(500, "text/plain", "No index");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

void handleEstado() {
  DynamicJsonDocument doc(512);
  doc["alarma"] = alarmaActiva;
  doc["alarmaSonando"] = alarmaSonando;
  doc["modo"] = modoAlarma;
  doc["gas"] = gasValor;
  doc["humedad"] = humedad;
  doc["pir"] = pirActivo;
  doc["puerta"] = puertaAbierta;
  doc["umbralGas"] = umbralGas;
  doc["umbralHumedad"] = umbralHumedad;
  float t = dht.readTemperature();
  doc["temperatura"] = isnan(t) ? 0.0 : t;
  doc["motivo"] = motivoAlarma;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleToggle() {
  alarmaActiva = !alarmaActiva;
  if (!alarmaActiva) alarmaSonando = false;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleArmarT() {
  alarmaActiva = true;
  modoAlarma = "total";
  tiempoDeArme = millis();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleArmarN() {
  alarmaActiva = true;
  modoAlarma = "noche";
  tiempoDeArme = millis();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleDesarmar() {
  alarmaActiva = false;
  alarmaSonando = false;
  server.send(200, "application/json", "{\"ok\":true}");
}

// — setup y loop

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  dht.begin();

  // Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado IP: " + WiFi.localIP().toString());

  // LittleFS
  if (!LittleFS.begin()) {
    Serial.println("FS error");
    return;
  }

  // Rutas Web
  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/estado", HTTP_GET, handleEstado);
  server.on("/toggle-alarma", HTTP_POST, handleToggle);
  server.on("/armar-total", HTTP_POST, handleArmarT);
  server.on("/armar-noche", HTTP_POST, handleArmarN);
  server.on("/desarmar-alarma", HTTP_POST, handleDesarmar);
  server.begin();
}

void loop() {
  server.handleClient();

  // Leer sensores
  if (millis() - lastSensorRead < sensorInterval) return;
  lastSensorRead = millis();
  pirActivo = digitalRead(PIR_PIN);
  puertaAbierta = !digitalRead(SWITCH_PIN);
  gasValor = analogRead(GAS_PIN);
  humedad = dht.readHumidity();
  if (isnan(humedad)) humedad = 0;

  // Ventilador
  digitalWrite(FAN_PIN, (gasValor > umbralGas || humedad > umbralHumedad));

  // Evaluar disparo
  if (alarmaActiva && !alarmaSonando
      && millis() - tiempoDeArme > tiempoGracia) {
    bool disparo = false;
    if (modoAlarma == "total") {
      disparo = pirActivo || puertaAbierta || gasValor > umbralGas || humedad > umbralHumedad;
    } else {
      disparo = puertaAbierta || gasValor > umbralGas || humedad > umbralHumedad;
    }
    if (disparo) {
      alarmaSonando = true;
      motivoAlarma = "";
      if (pirActivo) motivoAlarma += "Movimiento. ";
      if (puertaAbierta) motivoAlarma += "Puerta. ";
      if (gasValor > umbralGas) motivoAlarma += "Gas. ";
      if (humedad > umbralHumedad) motivoAlarma += "Humedad. ";
      Serial.println(">>> Alarma: " + motivoAlarma);
      enviarEvento();
    }
  }

  // Parpadeo y sonido
  if (alarmaSonando) {
    if (millis() - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = millis();
      static bool estado = false;
      estado = !estado;
      digitalWrite(LED_PIN, estado);
      estado ? tone(BUZZER_PIN, 1000) : noTone(BUZZER_PIN);
    }
  }

  // Apagar
  if (!alarmaActiva && alarmaSonando) {
    alarmaSonando = false;
    noTone(BUZZER_PIN);
    digitalWrite(LED_PIN, LOW);
    Serial.println(">>> Alarma apagada");
  }
}

void enviarEvento() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<256> doc;
  doc["humedad"] = humedad;
  doc["gas"] = gasValor;
  doc["movimiento"] = pirActivo;
  doc["puerta"] = puertaAbierta;
  doc["alerta"] = alarmaSonando;
  doc["modo_alarma"] = modoAlarma;
  doc["motivo"] = motivoAlarma;
  String payload;
  serializeJson(doc, payload);
  int code = http.POST(payload);
  Serial.printf("POST código: %d\n", code);
  http.end();
}
