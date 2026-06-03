// =============================================================================
// ESP32_Recepção — Monitoramento
// Recebe dados via ESP-NOW + exibe na matriz de LEDs + envia para AWS DynamoDB
// O bloco original da matriz foi preservado integralmente.
// =============================================================================

// ── Bibliotecas originais ────────────────────────────────────────────────────
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// ── Bibliotecas adicionadas para a AWS ──────────────────────────────────────
#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>

// ==========================================
// CONFIGURAÇÃO DA MATRIZ (original)
// ==========================================
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   4
#define PIN_CS        10
#define PIN_DATA      11
#define PIN_CLK       12

MD_Parola matriz = MD_Parola(HARDWARE_TYPE, PIN_DATA, PIN_CLK, PIN_CS, MAX_DEVICES);

// ==========================================
// PINOS (original)
// ==========================================
#define PIN_LED_VD    16
#define PIN_LED_VM    17

// ==========================================
// STRUCT ESP-NOW (original — não alterar)
// ==========================================
typedef struct struct_mensagem {
    float nivel;
    float temperatura;
    float umidade;
    int   luminosidade;
    int   presenca;
} struct_mensagem;

struct_mensagem dadosRecebidos;

// ==========================================
// VARIÁVEIS ORIGINAIS (não alteradas)
// ==========================================
unsigned long lastRxMillis   = 0;
const unsigned long TIMEOUT_MS = 5000;
unsigned long lastScreenChange = 0;
int    telaAtual       = 1;
String textoExibicao   = "AGUARDANDO";

// ==========================================
// VARIÁVEIS ADICIONADAS PARA AWS
// ==========================================
volatile bool dadosNovos = false;   // sinaliza novo pacote ESP-NOW para o loop()
unsigned long msgID      = 1;       // ID incremental para o DynamoDB

WiFiClientSecure net;
PubSubClient     client(net);

// ==========================================
// FUNÇÕES AWS (adicionadas)
// ==========================================

// Gera timestamp ISO 8601: "YYYY-MM-DDTHH:MM:SSZ"
String getTimestamp() {
  time_t now = time(nullptr);
  struct tm* t = gmtime(&now);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", t);
  return String(buf);
}

// Trata mensagens recebidas pelo tópico de comandos AWS
void messageHandler(char* topic, byte* payload, unsigned int length) {
  Serial.print("[AWS] Mensagem recebida: ");
  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload);
  const char* message = doc["message"];
  if (message) Serial.println(message);
}

// Conecta Wi-Fi + NTP + AWS IoT Core
void connectAWS() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[REDE] Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[REDE] Wi-Fi conectado!");

  // NTP — necessário para o timestamp funcionar
  configTime(0, 0, "pool.ntp.org");
  Serial.print("[REDE] Sincronizando NTP");
  while (time(nullptr) < 1000000000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[REDE] Horário sincronizado!");

  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  client.setServer(AWS_IOT_ENDPOINT, 8883);
  client.setCallback(messageHandler);

  Serial.print("[AWS] Conectando ao IoT Core");
  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(500);
  }

  if (client.connected()) {
    client.subscribe("esp32grm/ww/sub");
    Serial.println("\n[AWS] Conectado!");
  }
}

void reconnectMQTT() {
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    if (client.connect(THINGNAME)) {
      client.subscribe("esp32grm/ww/sub");
      Serial.println("[AWS] Reconectado.");
    }
  }
}

// Publica os dados no DynamoDB via MQTT
// Mapeia os campos da struct para os nomes das colunas do banco
void publishMessage(struct_mensagem dados) {
  if (!client.connected()) return;

  StaticJsonDocument<256> doc;
  doc["ID"]            = String(msgID++);
  doc["timestamp"]     = getTimestamp();
  doc["nivel_tinta"]   = dados.nivel;        // struct: nivel    → banco: nivel_tinta
  doc["temperatura_c"] = dados.temperatura;  // struct: temperatura → banco: temperatura_c
  doc["umidade_pct"]   = dados.umidade;      // struct: umidade  → banco: umidade_pct
  doc["luminosidade"]  = dados.luminosidade;
  doc["presenca"]      = dados.presenca;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  client.publish("esp32grm/ww/pub", jsonBuffer);
  Serial.printf("[AWS] Dados enviados → ID: %lu | Nível: %.1f%%\n", msgID - 1, dados.nivel);
}

// ==========================================
// CALLBACK ESP-NOW
// Alterado: parse JSON no lugar do memcpy da struct.
// A struct dadosRecebidos é mantida — só muda como é preenchida.
// O ESP32_Percepção deve enviar JSON com os campos:
//   nivel_tinta, temperatura_c, umidade_pct, luminosidade, presenca
// ==========================================
void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
  lastRxMillis = millis();

  // Parse do JSON recebido via ESP-NOW
  StaticJsonDocument<256> jsonRx;
  DeserializationError erro = deserializeJson(jsonRx, incomingData, len);

  if (erro) {
    Serial.print("[ESP-NOW] Erro ao parsear JSON: ");
    Serial.println(erro.c_str());
    return;
  }

  // Preenche a struct com os valores do JSON
  // Os campos do JSON (nomes do banco) são mapeados para os campos da struct (nomes da matriz)
  dadosRecebidos.nivel        = jsonRx["nivel_tinta"]   | 0.0f;
  dadosRecebidos.temperatura  = jsonRx["temperatura_c"] | 0.0f;
  dadosRecebidos.umidade      = jsonRx["umidade_pct"]   | 0.0f;
  dadosRecebidos.luminosidade = jsonRx["luminosidade"]  | 0;
  dadosRecebidos.presenca     = jsonRx["presenca"]      | 0;

  dadosNovos = true; // sinaliza para o loop() publicar na AWS

  digitalWrite(PIN_LED_VD, HIGH);
  digitalWrite(PIN_LED_VM, LOW);

  Serial.print("RX: nivel=");  Serial.print((int)dadosRecebidos.nivel);       Serial.print("% ");
  Serial.print("temp=");       Serial.print((int)dadosRecebidos.temperatura); Serial.print("C ");
  Serial.print("umd=");        Serial.print((int)dadosRecebidos.umidade);     Serial.print("% ");
  Serial.print("lux=");        Serial.print(dadosRecebidos.luminosidade);     Serial.print(" ");
  Serial.print("prs=");        Serial.print(dadosRecebidos.presenca);
  Serial.println(" ts=2026-05-27T16:00:00Z");
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_VD, OUTPUT);
  pinMode(PIN_LED_VM, OUTPUT);
  digitalWrite(PIN_LED_VD, LOW);
  digitalWrite(PIN_LED_VM, HIGH);

  matriz.begin();
  matriz.setIntensity(5);
  matriz.displayClear();
  matriz.displayText("INICIANDO", PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);

  // ── Adicionado: conecta Wi-Fi + AWS antes de iniciar o ESP-NOW ───────────
  // O Wi-Fi precisa estar conectado primeiro para fixar o canal correto,
  // que o ESP32_Percepção também deve usar.
  connectAWS();
  // ─────────────────────────────────────────────────────────────────────────

  // Bloco original do ESP-NOW (não alterado)
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

// ==========================================
// LOOP (original + bloco AWS adicionado no início)
// ==========================================
void loop() {

  // ── Adicionado: mantém MQTT vivo e publica dados novos ───────────────────
  if (!client.connected()) reconnectMQTT();
  client.loop();

  if (dadosNovos) {
    dadosNovos = false;
    publishMessage(dadosRecebidos);
  }
  // ─────────────────────────────────────────────────────────────────────────

  // Bloco original da matriz (não alterado)
  if (matriz.displayAnimate()) {
    matriz.displayReset();
  }

  unsigned long currentMillis = millis();

  if (currentMillis - lastRxMillis > TIMEOUT_MS) {
    digitalWrite(PIN_LED_VM, HIGH);
    digitalWrite(PIN_LED_VD, LOW);

    if (textoExibicao != "SEM DADOS") {
      textoExibicao = "SEM DADOS";
      matriz.displayText(textoExibicao.c_str(), PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
      Serial.println("LED VERMELHO ON - timeout de comunicação");
    }
    return;
  }

  if (currentMillis - lastScreenChange >= 2000) {
    lastScreenChange = currentMillis;

    switch (telaAtual) {
      case 1:
        textoExibicao = "NVL " + String((int)dadosRecebidos.nivel) + "%";
        Serial.println("Tela -> NVL");
        telaAtual = 2;
        break;
      case 2:
        textoExibicao = "TMP " + String((int)dadosRecebidos.temperatura) + "C";
        Serial.println("Tela -> TMP");
        telaAtual = 3;
        break;
      case 3:
        textoExibicao = "UMD " + String((int)dadosRecebidos.umidade) + "%";
        Serial.println("Tela -> UMD");
        telaAtual = 4;
        break;
      case 4:
        textoExibicao = "LUX " + String(dadosRecebidos.luminosidade);
        Serial.println("Tela -> LUX");
        telaAtual = 5;
        break;
      case 5:
        textoExibicao = "PRS " + String(dadosRecebidos.presenca == 1 ? "ON" : "OFF");
        Serial.println("Tela -> PRS");
        telaAtual = 1;
        break;
    }
    matriz.displayText(textoExibicao.c_str(), PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  }
}
