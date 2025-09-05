// PROF Áudio
// Sonômetro 01 //// data de conclusão 05/09/2025 /// 
// Aferido com RadioShack Sound Level Meter : Response > SLOW  e Weighting > A
// Web > HTTPS://192.168.100.119  e https://io.adafruit.com/Paulo_Ferreira/overview
// ESP32 Dev Module >> SPH0645 >> TM1637 >> WS2812 LED 5050 RGB 8x8 Módulo de matriz LED de 64 bits para Arduino
// Desenvolvido no FabLab Senai-RJ  Jacarepaguá

#include <WiFi.h>
#include <WebServer.h>
#include "AdafruitIO_WiFi.h"
#include <driver/i2s.h>
#include <math.h>
#include <TM1637Display.h>
#include <FastLED.h>
#include <driver/adc.h>

// ---- Mesma rede WiFi do ESP32 ----
#define WIFI_SSID    "Fique Fora 2"
#define WIFI_PASS    "Paulo@61"

// ---- Coloque suas credenciais Adafruit ----
#define IO_USERNAME  "Paulo_Ferreira"
#define IO_KEY       "001b1bc31bb6481a99c8c1dd7f142329"

// Objeto do Adafruit IO
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *feedDecibeis = io.feed("Teste Sonômetro 01");  
AdafruitIO_Feed *feedBateria  = io.feed("Nivel_Bateria"); // feed extra para bateria

WebServer server(80);

// === PINOS DO SPH0645 ===
#define I2S_WS   15  
#define I2S_SD   32  
#define I2S_SCK  14  

// === CONFIGURAÇÃO I2S ===
#define SAMPLE_RATE   48000
#define I2S_READ_LEN  1024
#define I2S_PORT      I2S_NUM_0

// Média móvel
#define SMOOTHING 0.2f
float smoothed_dB = 0;

// === TM1637 ===
#define CLK 25
#define DIO 26
TM1637Display display(CLK, DIO);

// === FASTLED ===
#define LED_PIN   5
#define NUM_LEDS  64
CRGB leds[NUM_LEDS];

// === BUFFER PARA 2H ===
#define MAX_SAMPLES 7200
float history[MAX_SAMPLES];
int history_index = 0;
int history_count = 0;

// === Temporização não bloqueante ===
unsigned long previousMillis = 0;
const long interval = 5000; // Intervalo de 4 segundos para leitura emm Dashboard adafruit_IO

// === Filtro biquad IIR da Curva A ===
struct Biquad {
  float a0, a1, a2;
  float b1, b2;
  float z1, z2;
};

Biquad aFilter = {
  0.169994948147430, 0.339989896294861, 0.169994948147430,
  -1.69065929318251, 0.73248077421585,
  0, 0
};

float applyBiquad(Biquad &f, float x) {
    float y = f.a0 * x + f.z1;
    f.z1 = f.a1 * x - f.b1 * y + f.z2;
    f.z2 = f.a2 * x - f.b2 * y;
    return y;
}

// === MONITORAR BATERIA ===
#define BATTERY_PIN 34
#define VREF 2.1        
#define MAX_ADC 4095.0  

#define R1 100000.0     
#define R2 100000.0     

float readBatteryVoltage() {
  // Usando o novo driver ADC para evitar conflitos
  long sum = - 0;
  const int samples = 10;

  for (int i = 0; i < samples; i++) {
    sum += adc1_get_raw(ADC1_CHANNEL_6); // GPIO34 = ADC1_CH6
    delay(2);
  }
////// Ajuste da leitura do valor da tensão da bateria em porcentagem %%% para o IOadafruit
  float adcVal = (float)sum / samples;
  float v_adc = (((((adcVal / MAX_ADC) * VREF) - 0.2) * 100 ) / VREF);
  float v_bat = v_adc * ((R1 + R2) / R2);
  return v_bat;
}

// === I2S ===
void i2s_install() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = I2S_READ_LEN,
        .use_apll = true,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

void i2s_setpin() {
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };
    i2s_set_pin(I2S_PORT, &pin_config);
}

// === MÉDIA DO HISTÓRICO ===
float getHistoryMean() {
    if (history_count == 0) return 0;
    double sum = 0;
    for (int i = 0; i < history_count; i++) sum += history[i];
    return sum / history_count;
}

// === WEBPAGE === http://192.168.100.119
void handleRoot() {
    float media = getHistoryMean();
    float vbat = readBatteryVoltage();
    String color;
    if (smoothed_dB < 55) color = "green";
    else if (smoothed_dB < 65) color = "orange";
    else color = "red";
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='5'>";
    html += "<title>Decibelímetro ESP32</title></head>";
    html += "<body style='font-family: Arial; text-align: center; background-color:black; color:white;'>";
    html += "<h1>Unidade Neonatal</h1>";
    html += "<h2>Leitura Sonômetro</h2>";
    html += "<p><b>Nível Sonoro (dB(A)):</b><br>";
    html += "<span style='font-size:2em; color:" + color + ";'>" + String(smoothed_dB, 2) + " dB</span></p>";
    html += "<p><b>Média (2h):</b><br>";
    html += "<span style='font-size:2em;'>" + String(media, 2) + " dB</span></p>";
    html += "<p><b>Bateria:</b><br>";
    html += "<span style='font-size:2em;'>" + String(vbat, 2) + "  %</span></p>";
    html += "<p><b>Uptime:</b> " + String(millis() / 1000) + " segundos</p>";
    html += "<p><b>Status WiFi:</b> " + String(WiFi.RSSI()) + " dBm</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

// === VERIFICA CONEXÃO WiFi ===
void checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi desconectado! Reconectando...");
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        delay(1000);
    }
}

void setup() {
    Serial.begin(115200);
    
    // Configuração do ADC primeiro para evitar conflitos
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11); // GPIO34 = ADC1_CH6
    
    // Configuração I2S
    i2s_install();
    i2s_setpin();
    i2s_zero_dma_buffer(I2S_PORT);
    
    // Configuração do display
    display.setBrightness(2);
    display.clear();
    
    // Configuração dos LEDs
    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(10);
    FastLED.clear();
    FastLED.show();
    
    // Conexão com Adafruit IO
    Serial.println("Conectando ao Adafruit IO...");
    io.connect();
    
    while(io.status() < AIO_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("Conectado ao Adafruit IO!");
    
    // Servidor web
    server.on("/", handleRoot);
    server.begin();
    Serial.println("Servidor web iniciado!");
    
    Serial.println("Sistema iniciado com sucesso!");
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Executa tarefas essenciais sem bloqueio
    io.run();  
    server.handleClient();
    checkWiFi(); // Verifica conexão WiFi
    
    // Verifica se passaram 2 segundos desde a última leitura
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        
        // Leitura de áudio
        int32_t samples[I2S_READ_LEN];
        size_t bytes_read;
        i2s_read(I2S_PORT, (char*)samples, I2S_READ_LEN * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        int samples_read = bytes_read / sizeof(int32_t);

        // Cálculo do RMS
        double mean = 0;
        for(int i = 0; i < samples_read; i++) mean += samples[i];
        mean /= samples_read;

        double sum_squares = 0;
        for(int i = 0; i < samples_read; i++){
            float s = (samples[i] - mean) * (1.0f / 2147483648.0f); // Multiplicação mais eficiente
            s = applyBiquad(aFilter, s);
            sum_squares += s * s;
        }
        double rms = sqrt(sum_squares / samples_read);

        // Cálculo de dB
        float dB = 20.0 * log10(rms + 1e-9f) + 94;

        // Suavização
        smoothed_dB = (SMOOTHING * dB) + ((1.0 - SMOOTHING) * smoothed_dB);
        
        // Proteção contra valores inválidos
        if (isnan(smoothed_dB) || isinf(smoothed_dB)) {
            smoothed_dB = 0;
        }

        // Histórico
        history[history_index] = smoothed_dB;
        history_index = (history_index + 1) % MAX_SAMPLES;
        if (history_count < MAX_SAMPLES) history_count++;

        // Display
        Serial.printf("dB(A): %.2f\n", smoothed_dB);
        static float last_display_dB = -100;
        if (fabs(smoothed_dB - last_display_dB) > 0.1) {
            int valor = (int)(smoothed_dB * 100);  
            display.showNumberDecEx(valor, 0b01000000, true);
            last_display_dB = smoothed_dB;
        }

        // LEDs
        CRGB cor;
        if (smoothed_dB < 55) cor = CRGB::Green;
        else if (smoothed_dB < 65) cor = CRGB::Yellow;
        else cor = CRGB::Red;

        for(int i=0; i<NUM_LEDS; i++) leds[i] = cor;
        FastLED.show();

        // Envia dados para o Adafruit IO
        if (!feedDecibeis->save(smoothed_dB)) {
            Serial.println("Erro ao enviar decibéis para Adafruit IO!");
        }

        // Envia nível de bateria
        float vbat = readBatteryVoltage();
        if (!feedBateria->save(vbat)) {
            Serial.println("Erro ao enviar bateria para Adafruit IO!");
        }
    }
    
    // Pequena pausa para dar tempo a outras tarefas
    delay(250);
}