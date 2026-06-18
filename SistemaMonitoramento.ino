#include <WiFi.h>
#include <WebServer.h>

// Configurações do Access Point Wi-Fi (Igual ao seu anterior)
const char* SSID     = "Monitor_SOS_ESP32";
const char* PASSWORD = "12345678";

// Definição de Pinos (Altere conforme sua montagem física)
#define LDR_PIN 34         // Entrada analógica do ADC para o sensor LDR
#define BUTTON_SOS_PIN 12  // Entrada digital para o botão SOS (com interrupção)
#define LED_YELLOW 2       // Pino do LED de alerta/noturno (ou Built-in se compatível)
#define LED_RED 4          // Pino do LED vermelho de emergência SOS

WebServer server(80);

// Variáveis de controle de estado globais e voláteis (Modificadas na ISR)
volatile bool sosAtivo = false;
volatile unsigned long ultimaInterrupcao = 0;
const unsigned long TEMPO_DEBOUNCE = 50; // Filtro de 50 milissegundos para o botão

// Variável para armazenar a última leitura estável do LDR
int valorLdrAtual = 0;

// Rotina de Serviço de Interrupção (ISR) residente na memória rápida IRAM
void IRAM_ATTR isrBotaoSOS() {
  unsigned long tempoAtual = millis();
  
  // Tratamento de Debounce por Software
  if (tempoAtual - ultimaInterrupcao > TEMPO_DEBOUNCE) {
    sosAtivo = true; // Sinaliza a ocorrência imediata do evento crítico
    ultimaInterrupcao = tempoAtual;
  }
}

// Interface Web minimalista e moderna em HTML que atualiza os dados via Fetch API
const char* HTML_MONITOR = R"rawhtml(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Sistema de Monitoramento Inteligente</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: Arial, sans-serif; background: #0f1117; color: #e2e8f0;
           display: flex; justify-content: center; align-items: center; min-height: 100vh; }
    .card { background: #1a1d27; border: 1px solid #2e3347; border-radius: 12px; padding: 2rem; width: 360px; text-align: center; }
    h1 { color: #3b82f6; font-size: 1.2rem; margin-bottom: 1.5rem; }
    label { font-size: 0.8rem; color: #94a3b8; display: block; margin-bottom: 10px; }
    .ldr-box { background: #22263a; border: 1px solid #2e3347; border-radius: 8px; padding: 1.5rem; margin-bottom: 1rem; }
    .ldr-value { font-size: 3rem; font-family: monospace; color: #f59e0b; font-weight: 700; }
    .status { font-size: 0.9rem; color: #94a3b8; margin-top: 10px; }
    .indicator { display: inline-block; width: 12px; height: 12px; border-radius: 50%; background: #22c55e; margin-right: 6px; }
    .indicator.alert { background: #ef4444; box-shadow: 0 0 8px #ef4444; }
  </style>
</head>
<body>
<div class="card">
  <h1>Monitoramento Urbano & Alerta</h1>
  <label>Telemetria em Tempo Real (Sensor LDR)</label>
  <div class="ldr-box">
    <div class="ldr-value" id="ldrVal">----</div>
    <div class="status" id="ldrStatus">Calculando luminosidade...</div>
  </div>
  <p style="font-size:0.75rem; color:#64748b;">Frequência de Atualização: >= 1Hz</p>
</div>

<script>
// Realiza requisições a cada 1 segundo (1Hz) para obter a leitura do ADC
setInterval(async function() {
  try {
    var resp = await fetch("/data");
    var data = await resp.json();
    document.getElementById("ldrVal").textContent = data.ldr;
    
    if(data.ldr < 1500) {
      document.getElementById("ldrStatus").innerHTML = "<span class='indicator alert'></span>Modo Noturno Ativo";
    } else {
      document.getElementById("ldrStatus").innerHTML = "<span class='indicator'></span>Luminosidade Normal";
    }
  } catch(e) {
    document.getElementById("ldrVal").textContent = "ERR";
  }
}, 1000);
</script>
</body>
</html>
)rawhtml";

// Envia o JSON com as informações obtidas pelo ADC
void handleData() {
  String json = "{";
  json += "\"ldr\":" + String(valorLdrAtual);
  json += "}";
  server.send(200, "application/json", json);
}

void handleRoot()     { server.send(200, "text/html", HTML_MONITOR); }
void handleNotFound() { server.send(404, "text/plain", "Página não encontrada"); }

void setup() {
  Serial.begin(115200);
  
  // Configuração dos Pinos
  pinMode(LDR_PIN, INPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  
  // Configuração do botão com resistor interno de Pull-up
  pinMode(BUTTON_SOS_PIN, INPUT_PULLUP);
  
  // Atrela a Interrupção de hardware ao pino do botão na borda de descida (FALLING)
  attachInterrupt(digitalPinToInterrupt(BUTTON_SOS_PIN), isrBotaoSOS, FALLING);
  
  // Inicialização do Wi-Fi como Ponto de Acesso (AP)
  WiFi.softAP(SSID, PASSWORD);
  Serial.println("Rede Wi-Fi Criada!");
  Serial.print("Endereço IP do Servidor: ");
  Serial.println(WiFi.softAPIP());
  
  // Configuração das rotas do Servidor Web
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.onNotFound(handleNotFound);
  server.begin();
}

unsigned long tempoUltimoPisca = 0;
bool estadoAmarelo = false;

void loop() {
  // Executa o tratamento das requisições HTTP do Servidor Web
  server.handleClient();
  
  // Realiza a leitura contínua (Polling) do LDR para atualização da telemetria
  valorLdrAtual = analogRead(LDR_PIN);
  
  // MÁQUINA DE ESTADOS E CONTROLE DE PRIORIDADES
  
  // PRIORIDADE MÁXIMA (NÍVEL 1): Botão SOS acionado de forma assíncrona por Interrupção
  if (sosAtivo) {
    Serial.println("[ALERTA] Botão SOS acionado! Entrando em prioridade máxima.");
    
    // Desliga qualquer sinalização anterior do modo noturno
    digitalWrite(LED_YELLOW, LOW); 
    
    // Mantém o LED Vermelho fixo por exatamente 3 segundos, conforme o requisito
    digitalWrite(LED_RED, HIGH);
    delay(3000); 
    digitalWrite(LED_RED, LOW);
    
    // Reseta a flag para limpar o estado de interrupção tratada
    sosAtivo = false;
    Serial.println("[SISTEMA] Alerta SOS encerrado. Retornando ao fluxo de fundo.");
  }
  // PRIORIDADE MÉDIA (NÍVEL 2): Condição de Baixa Luminosidade (Modo Noturno)
  else if (valorLdrAtual < 1500) { // 1500 é um limiar padrão ajustável de escuridão
    // Piscar o LED amarelo a cada 2 segundos de forma não-bloqueante usando millis()
    if (millis() - tempoUltimoPisca >= 2000) {
      estadoAmarelo = !estadoAmarelo;
      digitalWrite(LED_YELLOW, estadoAmarelo ? HIGH : LOW);
      tempoUltimoPisca = millis();
    }
  }
  // PRIORIDADE MÍNIMA (NÍVEL 3): Luminosidade está regular/dia
  else {
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
  }
}
