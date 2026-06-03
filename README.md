# 🏭 Sistema IoT de Monitoramento de Tanque de Tinta

> Projeto acadêmico desenvolvido no SENAI CIMATEC — Práticas Integradas: Camada de Serviço  
> Dois ESP32 se comunicando via ESP-NOW, com envio de dados para a AWS e dashboard em tempo real.

---

## 🎯 Objetivo do Projeto

Simular um sistema IoT de chão de fábrica para monitoramento do processo de pintura de blocos de madeira. O sistema coleta dados de sensores em tempo real, transmite entre dois ESP32 via ESP-NOW, persiste os dados na nuvem AWS e exibe tudo em um dashboard web acessível publicamente.

---

## 🏗️ Arquitetura Completa

```
┌─────────────────────────────┐        ┌─────────────────────────────┐
│      ESP32_Percepção        │        │      ESP32_Recepção          │
│      (Chão de Fábrica)      │        │      (Monitoramento)         │
│                             │        │                              │
│  Sensores:                  │        │  Exibição:                   │
│  • Ultrassônico → nível     │        │  • Matriz de LEDs            │
│  • DHT22 → temp/umidade     │ ESP-NOW│  • LED Verde: dados OK       │
│  • PIR → presença           │ ──────▶│  • LED Vermelho: timeout     │
│                             │ Struct │                              │
│  LEDs:                      │        │  AWS (MQTT/TLS):             │
│  • Verde: nível ≥ 20%       │        │  • Conecta ao IoT Core       │
│  • Vermelho: nível < 20%    │        │  • Publica JSON a cada 2s    │
└─────────────────────────────┘        └──────────────┬───────────────┘
                                                       │ MQTT TLS
                                                       ▼
                                        ┌──────────────────────────┐
                                        │     AWS IoT Core         │
                                        └──────────────┬───────────┘
                                                       │ Rule
                                                       ▼
                                        ┌──────────────────────────┐
                                        │    AWS Lambda            │
                                        └──────────────┬───────────┘
                                                       │ PutItem
                                                       ▼
                                        ┌──────────────────────────┐
                                        │    AWS DynamoDB          │
                                        └──────────────┬───────────┘
                                                       │ Scan
                                                       ▼
                                        ┌──────────────────────────┐
                                        │    AWS Lambda (leitura)  │
                                        │  + Function URL (pública)│
                                        └──────────────┬───────────┘
                                                       │ HTTP GET
                                                       ▼
                                        ┌──────────────────────────┐
                                        │    Dashboard (S3)        │
                                        │  Atualiza a cada 2s      │
                                        └──────────────────────────┘
```

---

## 📦 Componentes de Hardware

### ESP32_Percepção (Chão de Fábrica)
| Componente | Função | Pino |
|---|---|---|
| Sensor Ultrassônico HC-SR04 | Mede distância até a tinta → calcula nível (%) | TRIG: 6 / ECHO: 5 |
| Sensor DHT22 | Temperatura (°C) e Umidade (%) | 4 |
| Sensor de Presença PIR | Detecta movimento próximo ao tanque | 7 |
| LED Verde | Nível do tanque ≥ 20% (operação normal) | 15 |
| LED Vermelho | Nível do tanque < 20% (alerta) | 16 |

### ESP32_Recepção (Monitoramento)
| Componente | Função | Pino |
|---|---|---|
| Matriz de LEDs MAX7219 (4x) | Exibe leituras ciclicamente (NVL, TMP, UMD, PRS) | CS: 10 / DATA: 11 / CLK: 12 |
| LED Verde | Dados ESP-NOW recebidos com sucesso | 16 |
| LED Vermelho | Timeout — sem dados por mais de 5s | 17 |

---

## 🗂️ Estrutura de Arquivos

```
/
├── esp32_percepcao/
│   └── esp32_percepcao.ino       # Firmware do ESP32 Chão de Fábrica
│
├── esp32_recepcao/
│   ├── esp32_recepcao_final.ino  # Firmware do ESP32 de Monitoramento
│   └── secrets.h                 # Credenciais Wi-Fi e certificados AWS (não versionar!)
│
├── aws/
│   ├── lambda_insere.py          # Lambda: recebe do IoT Core e grava no DynamoDB
│   └── lambda_leitura.py         # Lambda: lê do DynamoDB e serve para o dashboard
│
├── dashboard/
│   └── index.html            # Interface web (hospedada no S3)
│
└── README.md
```

> ⚠️ **Nunca versione o `secrets.h`** — ele contém certificados e senhas. Adicione ao `.gitignore`.

---

## ⚙️ Configuração do Hardware

### Cálculo do Nível de Tinta
O sensor ultrassônico mede a distância até a superfície da tinta. O nível é calculado por:

```
nível (%) = (1 - distância_medida / altura_máxima_do_tanque) × 100
```

Configure as constantes no `esp32_percepcao.ino` conforme o tanque físico:
```cpp
const float DISTANCIA_TANQUE_VAZIO = 100.0; // distância (cm) quando tanque vazio
const float DISTANCIA_TANQUE_CHEIO = 10.0;  // distância (cm) quando tanque cheio
```

### Comunicação ESP-NOW
Os dois ESP32 se comunicam via **ESP-NOW** usando uma struct binária. A struct deve ser **idêntica** nos dois dispositivos:

```cpp
typedef struct struct_mensagem {
    float         nivel;        // nível do tanque (%)
    float         temperatura;  // temperatura (°C)
    float         umidade;      // umidade (%)
    int           presenca;     // 0 = ausente / 1 = detectada
    unsigned long timestamp_ms; // millis() do momento do envio
} struct_mensagem;
```

Para a comunicação funcionar, **ambos devem estar no mesmo canal Wi-Fi**. Após subir o `esp32_recepcao_final.ino`, o canal é impresso no Serial Monitor:
```
[REDE] Canal Wi-Fi: 6 — configure WIFI_CHANNEL com esse valor no ESP32_Percepção!
```

Use esse valor na linha do `esp32_percepcao.ino`:
```cpp
peerInfo.channel = 6; // substitua pelo canal impresso acima
```

---

## 🛠️ Configuração do Ambiente de Desenvolvimento

### Arduino IDE — Bibliotecas Necessárias
Instale via **Library Manager** (`Sketch > Include Library > Manage Libraries`):

| Biblioteca | Autor | Para qual ESP32 |
|---|---|---|
| DHT sensor library | Adafruit | Percepção |
| ESP32Servo | Kevin Harrington | Percepção |
| MD_Parola | majicDesigns | Recepção |
| MD_MAX72XX | majicDesigns | Recepção |
| PubSubClient | Nick O'Leary | Recepção |
| ArduinoJson | Benoit Blanchon | Recepção |
| WiFiClientSecure | (ESP32 core) | Recepção |

### secrets.h
Dentro do arquivo `secrets.h` dentro da pasta `esp32_recepcao/` está um tamplate que deve ser alterado com seus dados.

Os certificados são baixados ao criar o **Thing** no AWS IoT Core.

---

## ☁️ Configuração da AWS

### 1. DynamoDB — Criar Tabela
- **Nome:** `pode ser qualquer nome`
- **Partition Key:** `ID` (Number)
- Demais atributos são criados automaticamente pelo Lambda

### 2. AWS IoT Core — Criar Thing e Certificados
1. IoT Core → Manage → Things → Create single thing
2. Nome: `pode ser qualquer nome`
3. Gere e baixe os certificados (`.crt`, `.key`, Root CA)
4. Crie uma **Policy** e anexe ao certificado:
```json
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Action": ["iot:Connect", "iot:Publish", "iot:Subscribe", "iot:Receive"],
    "Resource": "*"
  }]
}
```

### 3. Lambda de Inserção — `pode ser qualquer nome`
Cria ou atualiza o item no DynamoDB quando o ESP32 publica no tópico MQTT.

**Código:** ver `aws/lambda_insere.py`

**IoT Core Rule** que aciona a Lambda:
```sql
SELECT * FROM 'INSIRA O NOME DO SEU TÓPICO AQUI'
```

**JSON recebido do ESP32:**
```json
{
  "ID"          : "42",
  "timestamp"   : "2025-09-02T14:35:00Z",
  "nivel_tinta" : 75.3,
  "temperatura_c": 24.1,
  "umidade_pct" : 55.0,
  "presenca"    : 1
}
```

### 4. Lambda de Leitura — `pode ser qualquer nome`
Lê os dados do DynamoDB e serve para o dashboard via HTTP.

**Código:** ver `aws/lambda_leitura.py`

Após criar a função:
- Configuration → Function URL → Auth type: **NONE**
- CORS: Allow origin `*`, Allow methods `GET`
- Copie a URL gerada (será usada no dashboard)

### 5. Dashboard — Hospedar no S3
1. Crie um bucket S3 com hospedagem estática habilitada
2. Desmarque "Block all public access"
3. Adicione a bucket policy de acesso público
4. Faça upload do `dashboard/dashboard.html`
5. Substitua no HTML a variável `API_URL` pela Function URL da Lambda de leitura:
```javascript
const API_URL = "https://SEU_ID.lambda-url.us-east-1.on.aws/";
```
6. A URL pública do dashboard será:
```
http://SEU_BUCKET.s3-website-us-east-1.amazonaws.com
```

---

## 🚀 Como Rodar o Projeto

### ESP32_Percepção
1. Abra `esp32_percepcao/esp32_percepcao.ino` no Arduino IDE
2. Ajuste `DISTANCIA_TANQUE_VAZIO` e `DISTANCIA_TANQUE_CHEIO` conforme o tanque físico
3. Cole o MAC do ESP32_Recepção em `macDestino[]`
4. Grave no ESP32 do chão de fábrica

### ESP32_Recepção
1. Abra `esp32_recepcao/esp32_recepcao_final.ino` no Arduino IDE
2. Certifique-se de que o `secrets.h` está na mesma pasta com as credenciais corretas
3. Grave no ESP32 de monitoramento
4. Abra o Serial Monitor (115200 baud) e anote o canal Wi-Fi impresso
5. Use esse canal no `peerInfo.channel` do ESP32_Percepção

### Verificando o Funcionamento
| Indicador | Significa |
|---|---|
| LED Verde aceso no receptor | ESP-NOW funcionando, dados chegando |
| LED Vermelho aceso no receptor | Timeout — sem dados há mais de 5s |
| Serial Monitor: `[AWS] Enviado ID:X` | Dado publicado no IoT Core com sucesso |
| Registro aparece no DynamoDB | Lambda de inserção funcionando |
| Dashboard atualiza | Pipeline completo funcionando |

---

## 📊 Dados Monitorados

| Campo | Tipo | Descrição |
|---|---|---|
| `ID` | Number | Identificador único incremental |
| `timestamp` | String | Data e hora ISO 8601 (gerado pelo ESP32_Recepção via NTP) |
| `nivel_tinta` | Number | Nível do tanque em % (0 a 100) |
| `temperatura_c` | Number | Temperatura ambiente em °C |
| `umidade_pct` | Number | Umidade relativa em % |
| `presenca` | Number | Presença detectada: 1 = sim / 0 = não |

---

## 👥 Equipe

Equipe ESP32 Emissor -> Marcos Menezes, Rian Dultra e Gabriel Queiroz

Equipe ESP32 Receptor -> Caio Schneider, Flávio Fox e Henrique Rapadura

SENAI CIMATEC, 2026.

---

## 📚 Referências

- [AWS IoT Core Documentation](https://docs.aws.amazon.com/iot/)
- [AWS Lambda Documentation](https://docs.aws.amazon.com/lambda/)
- [Espressif ESP-NOW Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
- [Random Nerd Tutorials — ESP32](https://randomnerdtutorials.com/)