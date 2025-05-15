// sistemaSeguridad.ino
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DHT.h>

// Pines
#define PIR_PIN 13
#define SWITCH_PIN 27
#define GAS_PIN 34
#define DHT_PIN 4
#define BUZZER_PIN 26
#define LED_PIN 25
#define FAN_PIN 23

// WiFi
const char* ssid = "MEGACABLE-949F";
const char* password = "p353rdDW";

// Web server
AsyncWebServer server(80);

// DHT
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// Variables del sistema
bool alarmaActiva = false;
bool alarmaSonando = false;
bool alarmaYaApagada = true;
String motivoAlarma = ""; 
String modoAlarma = "ninguno";  // total, noche, ninguno
int umbralGas = 600;
int umbralHumedad = 70;
unsigned long lastBlinkTime = 0;
const unsigned long blinkInterval = 200; // 200 ms entre cambios
bool estadoSirena = false;

bool puertaAbiertaAlDisparo = false;
bool pirActivoAlDisparo = false;
bool gasAltoAlDisparo = false;
bool humedadAltaAlDisparo = false;

unsigned long tiempoDeArme = 0;
const unsigned long tiempoGracia = 5000; // 5 segundos de gracia tras armar

// Últimos valores leídos
int gasValor = 0;
float humedad = 0;
bool pirActivo = false;
bool puertaAbierta = false;

// Timers con millis
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 2000;

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(GAS_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);

  dht.begin();

  // WiFi y LittleFS
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando...");
  }
  Serial.println("WiFi conectado");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());

  if (!LittleFS.begin()) {
    Serial.println("Error al montar LittleFS");
    return;
  }

  // Rutas web
  server.serveStatic("/", LittleFS, "/index.html").setDefaultFile("index.html");
  server.serveStatic("/style.css", LittleFS, "/style.css");
  server.serveStatic("/config.html", LittleFS, "/config.html");

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Estado del sistema
  server.on("/estado", HTTP_GET, [](AsyncWebServerRequest *request){
  DynamicJsonDocument doc(1024); // aumenta tamaño por seguridad

  // Estado actual
  doc["alarma"] = alarmaActiva;
  doc["alarmaSonando"] = alarmaSonando;
  doc["modo"] = modoAlarma;
  doc["gas"] = gasValor;
  doc["humedad"] = humedad;
  doc["pir"] = pirActivo;
  doc["puerta"] = puertaAbierta;
  doc["umbralGas"] = umbralGas;
  doc["umbralHumedad"] = umbralHumedad;

  float temperatura = dht.readTemperature();
  if (isnan(temperatura)) temperatura = 0.0;
  doc["temperatura"] = temperatura;

  // Datos al momento del disparo
  doc["motivo"] = motivoAlarma;
  doc["puertaDisparo"] = puertaAbiertaAlDisparo;
  doc["pirDisparo"] = pirActivoAlDisparo;
  doc["gasDisparo"] = gasAltoAlDisparo;
  doc["humedadDisparo"] = humedadAltaAlDisparo;

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
  });

  // Cambiar parámetros
  server.on("/config", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, data);
    umbralGas = doc["umbral_gas"];
    umbralHumedad = doc["umbral_humedad"];
    Serial.print("Nuevo umbral de gas: "); Serial.println(umbralGas);
    Serial.print("Nuevo umbral de humedad: "); Serial.println(umbralHumedad);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  // Botón de alternar alarma
  server.on("/toggle-alarma", HTTP_POST, [](AsyncWebServerRequest *request){
    alarmaActiva = !alarmaActiva;
    if (!alarmaActiva) modoAlarma = "ninguno";
    Serial.print("Alarma activada: "); Serial.println(alarmaActiva);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  // Nueva ruta: armar total
  server.on("/armar-total", HTTP_POST, [](AsyncWebServerRequest *request){
    alarmaActiva = true;
    modoAlarma = "total";
    Serial.println(">>> Alarma ARMADA en modo TOTAL");
    request->send(200, "application/json", "{\"ok\":true}");
  });

  // Nueva ruta: armar noche
  server.on("/armar-noche", HTTP_POST, [](AsyncWebServerRequest *request){
    alarmaActiva = true;
    modoAlarma = "noche";
    Serial.println(">>> Alarma ARMADA en modo NOCHE");
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/desarmar-alarma", HTTP_POST, [](AsyncWebServerRequest *request){
    alarmaActiva = false;
    modoAlarma = "";
    Serial.println(">>> Alarma DESARMADA");
    request->send(200, "application/json", "{\"ok\":true}");
  });
  server.begin();
}
void armarAlarma(String modo) {
  alarmaActiva = true;
  modoAlarma = modo;
  alarmaSonando = false;
  motivoAlarma = "";
  tiempoDeArme = millis();  // Guardar el tiempo en que se armó
}

void desarmarAlarma() {
  alarmaActiva = false;
  alarmaSonando = false;
  motivoAlarma = "";
  modoAlarma = "";
}
void activarAlarma() {
  alarmaSonando = true;
  lastBlinkTime = millis();
  estadoSirena = false; // o true para empezar con sonido
  // Guardar estados y motivos aquí o en el loop
}
void loop() {
  if (millis() - lastSensorRead >= sensorInterval) {
    lastSensorRead = millis();

    // Leer sensores
    pirActivo = digitalRead(PIR_PIN);
    puertaAbierta = !digitalRead(SWITCH_PIN);
    gasValor = analogRead(GAS_PIN);
    humedad = dht.readHumidity();
    if (isnan(humedad)) {
      Serial.println("Error al leer la humedad");
      humedad = 0;
    }

    // Ventilador
    bool ventiladorActivo = (gasValor > umbralGas || humedad > umbralHumedad);
    digitalWrite(FAN_PIN, ventiladorActivo);

    // Evaluar disparo solo si la alarma está activa y no está sonando
    if (alarmaActiva && !alarmaSonando) {
      // Esperar tiempo de gracia para evitar activación inmediata al armar
      if (millis() - tiempoDeArme > tiempoGracia) {
        bool disparo = false;
        if (modoAlarma == "total") {
          disparo = pirActivo || puertaAbierta || gasValor > umbralGas || humedad > umbralHumedad;
        } else if (modoAlarma == "noche") {
          disparo = puertaAbierta || gasValor > umbralGas || humedad > umbralHumedad; // sin PIR
        }

        if (disparo) {
          alarmaSonando = true;

          // Guardar motivos
          motivoAlarma = "";
          if (pirActivo) motivoAlarma += "Movimiento detectado (PIR). ";
          if (puertaAbierta) motivoAlarma += "Puerta abierta. ";
          if (gasValor > umbralGas) motivoAlarma += "Gas alto. ";
          if (humedad > umbralHumedad) motivoAlarma += "Humedad alta. ";

          // Guardar estados reales al momento del disparo
          puertaAbiertaAlDisparo = puertaAbierta;
          pirActivoAlDisparo = pirActivo;
          gasAltoAlDisparo = gasValor > umbralGas;
          humedadAltaAlDisparo = humedad > umbralHumedad;

          Serial.println(">>> ¡Alarma ACTIVADA! Razón: " + motivoAlarma);
        }
      }
    }

    // Si la alarma está sonando, mantener el parpadeo y sonido intermitente
    if (alarmaSonando) {
      if (millis() - lastBlinkTime >= blinkInterval) {
        lastBlinkTime = millis();
        estadoSirena = !estadoSirena;

        if (estadoSirena) {
          tone(BUZZER_PIN, 1000);
          digitalWrite(LED_PIN, HIGH);
        } else {
          noTone(BUZZER_PIN);
          digitalWrite(LED_PIN, LOW);
        }
      }
    }

    // Apagar la alarma solo si fue desactivada manualmente
    if (!alarmaActiva && alarmaSonando) {
      alarmaSonando = false;
      motivoAlarma = "";
      noTone(BUZZER_PIN);
      digitalWrite(LED_PIN, LOW);
      Serial.println(">>> Alarma DESARMADA y apagada");
    }
  }
}