#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "esp_camera.h"

// Configurações de WiFi
const char* ssid = "SATC IOT";           // Nome da rede WiFi
const char* password = "IOT2024@#";   // Senha da rede WiFi

// Configurações do Telegram Bot
#define BOTtoken "7242482319:AAGzrE_ISUd2i5bZZW-z6z1JLS8cV4vduog"         // Token do Bot do Telegram
#define CHAT_ID "7061355747"            // ID do Chat autorizado

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

// Pinos para câmera ESP32-CAM (AI-Thinker)
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// Pinos do sistema
const int pirPin = 13;        // Pino do sensor PIR para detecção de movimento
const int buzzerPin = 12;     // Pino do buzzer para alarme
#define FLASH_LED_PIN 4       // Pino do flash da câmera

// Variáveis de estado do sistema
bool systemActive = false;    // Indica se o sistema está ativado
bool alarmTriggered = false;  // Indica se o alarme foi disparado
unsigned long alarmStartTime = 0; // Tempo em que o alarme foi iniciado
const unsigned long ALARM_DURATION = 10000; // Duração do alarme em milissegundos
bool sendPhoto = false;       // Flag para envio de foto ao detectar movimento

// Configuração para requisições do bot
int botRequestDelay = 1000;   // Tempo entre verificações de mensagens do bot
unsigned long lastTimeBotRan; // Armazena o último momento em que o bot foi consultado

// Função para parar o alarme
void stopAlarm() {
  digitalWrite(buzzerPin, LOW);    // Desliga o buzzer
  alarmTriggered = false;         // Reseta o estado do alarme
  Serial.println("Alarme parado");
}

// Configurações da câmera
void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Configuração de qualidade da câmera
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;  // Resolução maior
    config.jpeg_quality = 10;           // Qualidade da imagem
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA; // Resolução menor
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Inicializa a câmera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Erro ao inicializar a câmera: 0x%x\n", err);
    ESP.restart(); // Reinicia se a inicialização falhar
  }
  Serial.println("Câmera inicializada com sucesso!");
}

// Função para enviar uma foto pelo Telegram
String sendPhotoTelegram() {
  const char* server = "api.telegram.org";
  String response = "";

  camera_fb_t* fb = esp_camera_fb_get(); // Captura uma imagem da câmera
  if (!fb) {
    Serial.println("Erro ao capturar imagem");
    return "Erro ao capturar imagem";
  }

  if (client.connect(server, 443)) {
    String head = "--RandomBoundary\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + String(CHAT_ID) +
                  "\r\n--RandomBoundary\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"image.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--RandomBoundary--\r\n";

    uint16_t imageLen = fb->len;
    uint16_t totalLen = imageLen + head.length() + tail.length();

    client.println("POST /bot" + String(BOTtoken) + "/sendPhoto HTTP/1.1");
    client.println("Host: " + String(server));
    client.println("Content-Type: multipart/form-data; boundary=RandomBoundary");
    client.println("Content-Length: " + String(totalLen));
    client.println();
    client.print(head);

    client.write(fb->buf, fb->len);
    client.print(tail);

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        response += c;
      }
    }
    client.stop();
    esp_camera_fb_return(fb); // Libera a memória da imagem
    Serial.println("Foto enviada!");
  } else {
    response = "Erro na conexão com o Telegram";
    Serial.println(response);
  }
  return response;
}

// Função para tratar mensagens recebidas pelo bot
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);

    if (chat_id != CHAT_ID) { // Verifica se o chat é autorizado
      bot.sendMessage(chat_id, "Usuário não autorizado", "");
      continue;
    }

    String text = bot.messages[i].text;
    if (text == "/start") {
      String welcome = "Bem-vindo! Comandos disponíveis:\n\n";
      welcome += "/activate - Ativar sistema\n";
      welcome += "/deactivate - Desativar sistema\n";
      welcome += "/photo - Capturar foto\n";
      welcome += "/status - Ver status do sistema\n";
      bot.sendMessage(chat_id, welcome, "");
    } else if (text == "/activate") {
      systemActive = true;
      bot.sendMessage(chat_id, "Sistema ativado", "");
    } else if (text == "/deactivate") {
      systemActive = false;
      stopAlarm();
      bot.sendMessage(chat_id, "Sistema desativado", "");
    } else if (text == "/photo") {
      sendPhotoTelegram();
    } else if (text == "/status") {
      String status = "Status:\n";
      status += systemActive ? "Sistema ATIVADO\n" : "Sistema DESATIVADO\n";
      status += alarmTriggered ? "Alarme TOCANDO" : "Alarme inativo";
      bot.sendMessage(chat_id, status, "");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(pirPin, INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(FLASH_LED_PIN, OUTPUT);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando ao WiFi...");
  }
  Serial.println("WiFi conectado!");
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  initCamera(); // Inicializa a câmera
}

void loop() {
  // Verifica mensagens do Telegram
  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages) {
      handleNewMessages(numNewMessages);
    }
    lastTimeBotRan = millis();
  }

  // Monitora o sensor PIR
  if (systemActive && digitalRead(pirPin) == HIGH && !alarmTriggered) {
    bot.sendMessage(CHAT_ID, "Movimento detectado!", "");
    sendPhotoTelegram();
    alarmTriggered = true;
    alarmStartTime = millis();
    digitalWrite(buzzerPin, HIGH);
  }

  // Desliga o alarme após o tempo definido
  if (alarmTriggered && (millis() - alarmStartTime >= ALARM_DURATION)) {
    stopAlarm();
  }
}