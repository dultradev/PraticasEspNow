#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>

// --- Definições de Pinos ---
#define PINO_LED_VERMELHO 16 
#define PINO_LED_VERDE    15 

// Sensor Ultrassônico 
#define PINO_TRIG 6
#define PINO_ECHO 5

// Sensor DHT22
#define PINO_DHT 4           
#define TIPO_DHT DHT22

// Sensor de Presença PIR HW-416-B
#define PINO_PIR 7

// --- Parâmetros Físicos do Tanque ---
const float DISTANCIA_TANQUE_VAZIO = 100.0; 
const float DISTANCIA_TANQUE_CHEIO = 10.0;  

// --- Configuração ESP-NOW ---
// Endereço MAC do ESP32 de exibição (destino)
uint8_t macDestino[] = {0x80, 0xB5, 0x4E, 0xC5, 0x8A, 0xE0};

// Estrutura do pacote de dados atualizada para floats
typedef struct struct_pacote {
  float nivel;
  float temp;
  float umidade;
  int presenca;
  unsigned long timestamp;
} struct_pacote;

struct_pacote pacoteDados;
DHT dht(PINO_DHT, TIPO_DHT);

// Variáveis de controle globais para o status de envio
volatile bool respostaRecebida = false;
volatile bool envioSucesso = false;

// Função de Callback executada ao enviar dados (CORRIGIDA AQUI)
void aoEnviarDados(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  respostaRecebida = true;
  envioSucesso = (status == ESP_NOW_SEND_SUCCESS);
}

// Função auxiliar para piscar um LED sem quebrar o estado definitivo dele
void piscarLED(int pino, int estadoAtual) {
  digitalWrite(pino, !estadoAtual);
  delay(150);
  digitalWrite(pino, estadoAtual);
}

void setup() {
  Serial.begin(115200);

  // Configuração dos pinos
  pinMode(PINO_LED_VERMELHO, OUTPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);
  pinMode(PINO_PIR, INPUT);

  dht.begin();

  // Garante os LEDs inicialmente apagados
  digitalWrite(PINO_LED_VERDE, LOW);
  digitalWrite(PINO_LED_VERMELHO, LOW);

  // 1. Configura o Wi-Fi obrigatoriamente no modo Station
  WiFi.mode(WIFI_STA);
  Serial.print("Modo Wi-Fi Station ativado. MAC do dispositivo: ");
  Serial.println(WiFi.macAddress());

  // 2. Inicializa o ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar o ESP-NOW!");
    return;
  }

  // 3. Registra o callback de envio para monitorar o sucesso/falha
  esp_now_register_send_cb(aoEnviarDados);

  // 4. Registra o dispositivo par (Peer) de destino
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, macDestino, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Falha ao adicionar o dispositivo de destino (peer)!");
    return;
  }

  Serial.println("Sistema Pronto e ESP-NOW Configurado.");
  delay(2000);
}

void loop() {
  Serial.println("\n--- Atualização dos Sensores ---");

  // ==========================================
  // 1. LEITURA DO ULTRASSÔNICO (Nível)
  // ==========================================
  digitalWrite(PINO_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PINO_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_TRIG, LOW);

  long duracao = pulseIn(PINO_ECHO, HIGH);
  float distancia_cm = (duracao * 0.0343) / 2.0;

  float nivel_porcentagem = ((DISTANCIA_TANQUE_VAZIO - distancia_cm) / (DISTANCIA_TANQUE_VAZIO - DISTANCIA_TANQUE_CHEIO)) * 100.0;
  if (nivel_porcentagem < 0) nivel_porcentagem = 0.0;
  if (nivel_porcentagem > 100) nivel_porcentagem = 100.0;

  // Exibe com 2 casas decimais
  Serial.printf("Nível do tanque: %.2f%%\n", nivel_porcentagem);

  // Definição das condições padrão de segurança do tanque
  int estado_led_verde = LOW;
  int estado_led_vermelho = LOW;
  bool alerta_critico = false;

  if (nivel_porcentagem < 20.0) {
    estado_led_vermelho = HIGH;
    estado_led_verde = LOW;
    Serial.println("Alerta! Nível de tinta baixo.");
    alerta_critico = true;
  } else {
    estado_led_vermelho = LOW;
    estado_led_verde = HIGH;
  }

  // Aplica o estado estático inicial baseado no tanque
  digitalWrite(PINO_LED_VERDE, estado_led_verde);
  digitalWrite(PINO_LED_VERMELHO, estado_led_vermelho);

  // ==========================================
  // 2. LEITURA DO DHT22 (Temp/Umid)
  // ==========================================
  float umidade = dht.readHumidity();
  float temperatura = dht.readTemperature();

  if (isnan(umidade) || isnan(temperatura)) {
    Serial.println("Erro ao ler o sensor DHT22!");
    umidade = 0.0; // Atribui valor seguro para o pacote em caso de falha
    temperatura = 0.0;
  } else {
    // Exibe com 2 casas decimais
    Serial.printf("Temperatura: %.2f ºC | Umidade: %.2f%%\n", temperatura, umidade);
  }

  // ==========================================
  // 3. LEITURA DO PIR (Presença)
  // ==========================================
  int estado_PIR = digitalRead(PINO_PIR);
  if (estado_PIR == HIGH) {
    Serial.println("Presença detectada");
  } else {
    Serial.println("Sem presença");
  }

  // ==========================================
  // 4. MONTAGEM E ENVIO DO PACOTE VIA ESP-NOW
  // ==========================================
  // Agora os valores são armazenados como float sem conversão para (int)
  pacoteDados.nivel = nivel_porcentagem;
  pacoteDados.temp = temperatura;
  pacoteDados.umidade = umidade;
  pacoteDados.presenca = (estado_PIR == HIGH) ? 1 : 0;
  pacoteDados.timestamp = millis();

  respostaRecebida = false; // Reseta a flag para aguardar a confirmação atual

  // Envia o pacote
  esp_err_t resultadoEnvio = esp_now_send(macDestino, (uint8_t *) &pacoteDados, sizeof(pacoteDados));
  
  if (resultadoEnvio == ESP_OK) {
    // Aguarda confirmação de recebimento do hardware (com timeout de 100ms)
    unsigned long tempoEspera = millis();
    while (!respostaRecebida && (millis() - tempoEspera < 100)) {
      delay(1);
    }

    if (envioSucesso) {
      // Exibe os valores no formato textual solicitado com 2 casas decimais (%.2f)
      Serial.printf("Pacote enviado com sucesso: {nível=%.2f%%, temp=%.2f°C, umidade=%.2f%%, luz=680, presença=%d}\n", 
                    pacoteDados.nivel, pacoteDados.temp, pacoteDados.umidade, pacoteDados.presenca);
      
      // Pisca o LED Verde para indicar sucesso
      piscarLED(PINO_LED_VERDE, estado_led_verde);
    } else {
      Serial.println("Falha no envio ESPNOW");
      piscarLED(PINO_LED_VERMELHO, estado_led_vermelho);
    }
  } else {
    Serial.println("Falha no envio ESPNOW");
    piscarLED(PINO_LED_VERMELHO, estado_led_vermelho);
  }

  // ==========================================
  // 5. STATUS GERAL DO SISTEMA (Chão de Fábrica)
  // ==========================================
  if (alerta_critico) {
    Serial.println("Estado do sistema: Alerta – verificar tanque de tinta");
  } else {
    Serial.println("Estado do sistema: Operação normal");
  }

  Serial.println("--------------------------------");
  
  delay(2000); 
}
