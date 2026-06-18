#define BUTTON_PIN 3  
#define EXT_LED_PIN 1 

unsigned long ultimoTempoDebounce = 0;  
const unsigned long tempoEspera = 50;   

int ultimoEstadoBotao = HIGH;  
int estadoBotaoEstabilizado = HIGH; 

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(EXT_LED_PIN, OUTPUT);
  digitalWrite(EXT_LED_PIN, LOW);
}

void loop() {
  int leituraAtual = digitalRead(BUTTON_PIN);

  if (leituraAtual != ultimoEstadoBotao) {
    ultimoTempoDebounce = millis();
  }

  if ((millis() - ultimoTempoDebounce) > tempoEspera) {
    if (leituraAtual != estadoBotaoEstabilizado) {
      estadoBotaoEstabilizado = leituraAtual;

      if (estadoBotaoEstabilizado == LOW) {
        digitalWrite(EXT_LED_PIN, HIGH);  
        Serial.println("[EVENTO] Botão PRESSIONADO -> LED LIGADO");
      } else {
        digitalWrite(EXT_LED_PIN, LOW);   
        Serial.println("[EVENTO] Botão SOLTO -> LED DESLIGADO");
      }
    }
  }

  ultimoEstadoBotao = leituraAtual;
}
