#include <WiFi.h>
#include <WebServer.h>

// Configurações do Access Point Wi-Fi
const char* SSID     = "Semaforo_Inteligente_ESP32";
const char* PASSWORD = "12345678";

// Definição de Pinos do Semáforo e Sensores
#define LDR_PIN 34           // Entrada analógica do LDR
#define BUTTON_PEDESTRE 12   // Botão de solicitação de travessia (Interrupção)

#define LED_VERDE 14         // LED Verde dos carros
#define LED_AMARELO 2        // LED Amarelo dos carros (ou Built-in)
#define LED_VERMELHO 4       // LED Vermelho dos carros

WebServer server(80);

// Variáveis voláteis para controle da Interrupção
volatile bool pedestreSolicitou = false;
volatile unsigned long ultimaInterrupcao = 0;
const unsigned long TEMPO_DEBOUNCE = 150; // Janela maior para botões de pedestre

int valorLdrAtual = 0;

// Rotina de Serviço de Interrupção (ISR) para o botão de pedestre
void IRAM_ATTR isrBotaoPedestre() {
  unsigned long tempoAtual = millis();
  if (tempoAtual - ultimaInterrupcao > TEMPO_DEBOUNCE) {
    pedestreSolicitou = true; // Seta a flag para o loop processar
    ultimaInterrupcao = tempoAtual;
  }
}

// Interface Web de Telemetria
const char* HTML_MONITOR = R"rawhtml(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Semáforo Inteligente - ESP32</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: Arial, sans-serif; background: #0f1117; color: #e2e8f0;
           display: flex; justify-content: center; align-items: center; min-height: 100vh; }
    .card { background: #1a1d27; border: 1px solid #2e3347; border-radius: 12px; padding: 2rem; width: 360px; text-align: center; }
    h1 { color: #10b981; font-size: 1.2rem; margin-bottom: 1.5rem; }
    .ldr-box { background: #22263a; border: 1px solid #2e3347; border-radius: 8px; padding: 1.5rem; margin-bottom: 1rem; }
    .ldr-value { font-size: 3rem; font-family: monospace; color: #f59e0b; font-weight: 700; }
    .status { font-size: 0.9rem; color: #94a3b8; margin-top: 10px; }
  </style>
</head>
<body>
<div class="card">
  <h1>Semáforo Urbano Inteligente</h1>
  <div class="ldr-box">
    <div class="ldr-value" id="ldrVal">----</div>
    <div class="status" id="ldrStatus">Lendo ambiente...</div>
  </div>
  <p style="font-size:0.75rem; color:#64748b;">Monitoramento de Iluminação Pública</p>
</div>
<script>
setInterval(async function() {
  try {
    var resp = await fetch("/data");
    var data = await resp.json();
    document.getElementById("ldrVal").textContent = data.ldr;
    document.getElementById("ldrStatus").textContent = data.ldr < 1500 ? "Modo Noturno (Alerta)" : "Modo Diurno (Ativo)";
  } catch(e) { document.getElementById("ldrVal").textContent = "ERR"; }
}, 1000);
</script>
</body>
</html>
)rawhtml";

void handleData() {
  String json = "{\"ldr\":" + String(valorLdrAtual) + "}";
  server.send(200, "application/json", json);
}

void handleRoot() { server.send(200, "text/html", HTML_MONITOR); }

void setup() {
  Serial.begin(115200);
  
  pinMode(LDR_PIN, INPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARELO, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  
  pinMode(BUTTON_PEDESTRE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PEDESTRE), isrBotaoPedestre, FALLING);
  
  WiFi.softAP(SSID, PASSWORD);
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Semáforo pronto. IP: ");
  Serial.println(WiFi.softAPIP());
}

// Variáveis para temporização não-bloqueante das fases do semáforo diurno
unsigned long tempoFaseSemaforo = 0;
unsigned long tempoUltimoPiscaNoturno = 0;
int faseAtual = 0; // 0=Verde, 1=Amarelo, 2=Vermelho
bool estadoPisca = false;

void loop() {
  server.handleClient();
  valorLdrAtual = analogRead(LDR_PIN);
  
  // ── MODO NOTURNO (LDR < 1500): Piscar amarelo a cada 1 segundo ──
  if (valorLdrAtual < 1500) {
    // Apaga os outros sinais por segurança
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, LOW);
    
    if (millis() - tempoUltimoPiscaNoturno >= 1000) {
      estadoPisca = !estadoPisca;
      digitalWrite(LED_AMARELO, estadoPisca ? HIGH : LOW);
      tempoUltimoPiscaNoturno = millis();
    }
    // Zera solicitações de pedestre à noite (ou ignora conforme regra da cidade)
    pedestreSolicitou = false; 
  }
  
  // ── MODO DIURNO (LDR >= 1500): Ciclo normal com Interrupção Ativa ──
  else {
    // PRIORIDADE MÁXIMA DO MODO DIURNO: Pedestre apertou o botão
    if (pedestreSolicitou) {
      Serial.println("[PEDESTRE] Botão pressionado! Forçando fechamento do sinal...");
      
      // Se estava no verde, passa imediatamente para o amarelo
      if (faseAtual == 0) {
        digitalWrite(LED_VERDE, LOW);
        digitalWrite(LED_AMARELO, HIGH);
        delay(2000); // Tempo seguro do amarelo
      }
      
      // Abre o sinal vermelho para os carros (travessia do pedestre) por 5 segundos
      digitalWrite(LED_AMARELO, LOW);
      digitalWrite(LED_VERMELHO, HIGH);
      delay(5000); 
      digitalWrite(LED_VERMELHO, LOW);
      
      // Reseta e volta para o verde padrão
      pedestreSolicitou = false;
      faseAtual = 0;
      tempoFaseSemaforo = millis();
    }
    
    // Máquina de estados do ciclo normal (Sem travar o processador para a web funcionar)
    switch(faseAtual) {
      case 0: // Verde ativo (duração normal: 7 segundos)
        digitalWrite(LED_VERDE, HIGH);
        digitalWrite(LED_AMARELO, LOW);
        digitalWrite(LED_VERMELHO, LOW);
        if (millis() - tempoFaseSemaforo >= 7000) {
          faseAtual = 1;
          tempoFaseSemaforo = millis();
        }
        break;
        
      case 1: // Amarelo ativo (duração: 3 segundos)
        digitalWrite(LED_VERDE, LOW);
        digitalWrite(LED_AMARELO, HIGH);
        digitalWrite(LED_VERMELHO, LOW);
        if (millis() - tempoFaseSemaforo >= 3000) {
          faseAtual = 2;
          tempoFaseSemaforo = millis();
        }
        break;
        
      case 2: // Vermelho ativo (duração: 5 segundos)
        digitalWrite(LED_VERDE, LOW);
        digitalWrite(LED_AMARELO, LOW);
        digitalWrite(LED_VERMELHO, HIGH);
        if (millis() - tempoFaseSemaforo >= 5000) {
          faseAtual = 0;
          tempoFaseSemaforo = millis();
        }
        break;
    }
  }
}
