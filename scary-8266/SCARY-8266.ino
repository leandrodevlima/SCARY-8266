/*
 * SCARY-8266  —  ETAPA 6 (v2): Salvos com NOMES + menus de opcoes
 * Alvo: ESP8266 (NodeMCU)
 *
 * Fluxo novo:
 *   Receber -> captura -> OK -> escolhe NOME (lista) -> OK salva na flash.
 *   Salvos  -> lista pelos nomes -> OK -> [Enviar][Apagar][Voltar].
 *   Apagar  -> confirma [Nao][Sim].
 *   Navegacao: CIMA/BAIXO move, OK confirma, OK longo volta.
 *
 * ====== LIGACOES (iguais) ======
 *  DISPLAY: SCL D5(14) SDA D7(13) RES D0(16) DC D4(2) CS D3(0) BLK 3V3
 *  CC1101 : SCK D5(14) MOSI D7(13) MISO D6(12) CSN D1(5) GDO0 D2(4) + antena
 *  BOTOES : OK D8(15 ativo-alto)  CIMA RX(3 pullup)  BAIXO A0(analog)
 *
 * ====== PLACA ======
 *  Ferramentas -> MMU -> "16KB cache + 48KB IRAM"
 *  Ferramentas -> Flash Size -> opcao COM FS (ex.: "4MB (FS:2MB ...)")
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <LittleFS.h>
#include <string.h>

// ---------- Display ----------
#define TFT_CS 0
#define TFT_DC 2
#define TFT_RST 16
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ---------- CC1101 ----------
#define CC_SCK 14
#define CC_MISO 12
#define CC_MOSI 13
#define CC_CSN 5
#define CC_GDO0 4
RCSwitch mySwitch = RCSwitch();

// ---------- Botoes ----------
#define BTN_OK 15
#define BTN_UP 3
#define BTN_DOWN A0
#define A0_LIMIAR 500
#define OK_LONGO_MS 600

// ---------- Frequencias ----------
const float FREQS[] = {300.00, 315.00, 433.92, 868.30, 915.00};
const int   N_FREQS = sizeof(FREQS) / sizeof(FREQS[0]);
int freqIdx = 2;

// ---------- Nomes prontos ----------
const char* NOMES[] = {"Portao", "Portao 2", "Carro", "Casa", "Tomada", "Campainha"};
const int   N_NOMES = sizeof(NOMES) / sizeof(NOMES[0]);
int nomeSel = 0;

// ---------- Cores ----------
#define C_FUNDO  ST77XX_BLACK
#define C_TITULO ST77XX_CYAN
#define C_TEXTO  ST77XX_WHITE
#define C_SEL_BG ST77XX_BLUE
#define C_VALOR  ST77XX_GREEN
#define C_AVISO  ST77XX_YELLOW

// ---------- Estados ----------
enum Tela { MENU, RECEBER, NOMEAR, FREQUENCIA, SALVOS, SALVO_DET, APAGAR_CONF, RAWMODE, SOBRE };
Tela tela = MENU;
const char* ITENS[] = {"Receber", "Retransmitir", "Frequencia", "Salvos", "Modo RAW", "Sobre"};
const int   N_ITENS = 6;
int menuSel = 0;

// ---------- Sinal atual ----------
bool          temSinal   = false;
unsigned long sinalValor = 0;
unsigned int  sinalBits  = 0;
int           sinalProto = 0;
int           sinalPulso = 0;
float         sinalFreq  = 433.92;

// ---------- Salvos (flash) ----------
#define MAX_SALVOS 6
struct Sinal { char nome[12]; unsigned long valor; unsigned int bits; int proto; int pulso; float freq; };
Sinal salvos[MAX_SALVOS];
int nSalvos = 0;
int salvoSel = 0;   // selecao na lista
int detSel   = 0;   // opcao no detalhe (0=Enviar 1=Apagar 2=Voltar)
int apagarSel = 0;  // 0=Nao 1=Sim

// ---------- Buffer RAW ----------
#define RAW_MAX 512
#define RAW_MIN 16
#define RAW_GAP_US 15000UL
#define RAW_TIMEOUT_MS 5000UL
volatile unsigned int  rawTimings[RAW_MAX];
volatile unsigned int  rawCount = 0;
volatile unsigned long lastEdge = 0;
unsigned int rawLen = 0;
bool  rawCapturado = false;
float sinalFreqRaw = 433.92;

void IRAM_ATTR rawISR() {
  unsigned long now = micros();
  unsigned int dur = (unsigned int)(now - lastEdge);
  lastEdge = now;
  if (rawCount < RAW_MAX) rawTimings[rawCount++] = dur;
}

// ================= Flash =================
void carregarSalvos() {
  nSalvos = 0;
  if (!LittleFS.exists("/sinais.txt")) return;
  File f = LittleFS.open("/sinais.txt", "r");
  if (!f) return;
  while (f.available() && nSalvos < MAX_SALVOS) {
    String ln = f.readStringUntil('\n');
    ln.trim();
    if (ln.length() < 7) continue;
    // formato: nome;valor;bits;proto;pulso;freq
    int i0 = ln.indexOf(';');
    int i1 = ln.indexOf(';', i0 + 1);
    int i2 = ln.indexOf(';', i1 + 1);
    int i3 = ln.indexOf(';', i2 + 1);
    int i4 = ln.indexOf(';', i3 + 1);
    if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0 || i4 < 0) continue;
    Sinal s;
    strncpy(s.nome, ln.substring(0, i0).c_str(), sizeof(s.nome) - 1);
    s.nome[sizeof(s.nome) - 1] = 0;
    s.valor = (unsigned long) ln.substring(i0 + 1, i1).toInt();
    s.bits  = ln.substring(i1 + 1, i2).toInt();
    s.proto = ln.substring(i2 + 1, i3).toInt();
    s.pulso = ln.substring(i3 + 1, i4).toInt();
    s.freq  = ln.substring(i4 + 1).toFloat();
    salvos[nSalvos++] = s;
  }
  f.close();
  Serial.print(F("Salvos carregados: ")); Serial.println(nSalvos);
}

void gravarSalvos() {
  File f = LittleFS.open("/sinais.txt", "w");
  if (!f) { Serial.println(F("ERRO ao gravar")); return; }
  for (int i = 0; i < nSalvos; i++) {
    f.print(salvos[i].nome);  f.print(';');
    f.print(salvos[i].valor); f.print(';');
    f.print(salvos[i].bits);  f.print(';');
    f.print(salvos[i].proto); f.print(';');
    f.print(salvos[i].pulso); f.print(';');
    f.println(salvos[i].freq, 2);
  }
  f.close();
}

bool salvarComNome(const char* nome) {
  if (!temSinal || sinalValor == 0 || nSalvos >= MAX_SALVOS) return false;
  Sinal s;
  strncpy(s.nome, nome, sizeof(s.nome) - 1);
  s.nome[sizeof(s.nome) - 1] = 0;
  s.valor = sinalValor; s.bits = sinalBits; s.proto = sinalProto;
  s.pulso = sinalPulso; s.freq = sinalFreq;
  salvos[nSalvos++] = s;
  gravarSalvos();
  return true;
}

void apagarSalvo(int idx) {
  if (idx < 0 || idx >= nSalvos) return;
  for (int i = idx; i < nSalvos - 1; i++) salvos[i] = salvos[i + 1];
  nSalvos--;
  gravarSalvos();
}

// ================= Botoes =================
bool lerOK()   { return digitalRead(BTN_OK) == HIGH; }
bool lerUP()   { return digitalRead(BTN_UP) == LOW; }
bool lerDOWN() { return analogRead(BTN_DOWN) > A0_LIMIAR; }

int lerBotoes() {
  static bool okAnt = false, upAnt = false, dnAnt = false;
  static unsigned long okT0 = 0;
  int ev = 0;
  bool up = lerUP();
  if (up && !upAnt) { ev = 1; delay(25); }
  upAnt = up;
  bool dn = lerDOWN();
  if (dn && !dnAnt) { ev = 2; delay(25); }
  dnAnt = dn;
  bool ok = lerOK();
  if (ok && !okAnt) okT0 = millis();
  if (!ok && okAnt) { ev = (millis() - okT0 >= OK_LONGO_MS) ? 4 : 3; delay(25); }
  okAnt = ok;
  return ev;
}

// ================= Desenho =================
void cabecalho(const char* t) {
  tft.fillScreen(C_FUNDO);
  tft.setTextWrap(false);
  tft.setTextColor(C_TITULO);
  tft.setTextSize(2);
  tft.setCursor(4, 2);
  tft.print(t);
  tft.drawFastHLine(0, 20, tft.width(), ST77XX_MAGENTA);
}

void rodape(const char* txt) {
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 72);
  tft.print(txt);
}

// desenha uma lista de opcoes com selecao (compacta p/ caber ate 6 itens + rodape)
void desenhaLista(const char* titulo, const char* const* opts, int n, int sel, const char* rod) {
  cabecalho(titulo);
  tft.setTextSize(1);
  for (int i = 0; i < n; i++) {
    int y = 22 + i * 8;
    if (i == sel) { tft.fillRect(0, y - 1, tft.width(), 8, C_SEL_BG); tft.setTextColor(ST77XX_WHITE); }
    else tft.setTextColor(C_TEXTO);
    tft.setCursor(6, y);
    tft.print(opts[i]);
  }
  rodape(rod);
}

void desenhaMenu() { desenhaLista("SCARY", ITENS, N_ITENS, menuSel, "OK seleciona"); }

void telaReceber() {
  cabecalho("RX");
  tft.setTextSize(1);
  tft.setTextColor(C_AVISO); tft.setCursor(4, 26);
  tft.print("Escutando "); tft.print(FREQS[freqIdx], 2); tft.print(" MHz");
  tft.setTextColor(C_TEXTO); tft.setCursor(4, 40); tft.print("Aperte um controle...");
  rodape("OK longo = voltar");
}

void mostraSinal() {
  cabecalho("RX");
  tft.setTextSize(1);
  tft.setTextColor(C_TEXTO); tft.setCursor(4, 26); tft.print("Valor:");
  tft.setTextColor(C_VALOR); tft.setCursor(50, 26); tft.print(sinalValor);
  tft.setTextColor(C_TEXTO); tft.setCursor(4, 38); tft.print("Bits:");
  tft.setTextColor(C_VALOR); tft.setCursor(50, 38); tft.print(sinalBits);
  tft.setTextColor(C_AVISO); tft.setCursor(4, 54); tft.print("OK = salvar com nome");
  rodape("OK longo = voltar");
}

void telaNomear() { desenhaLista("NOME", NOMES, N_NOMES, nomeSel, "OK salva / longo cancela"); }

void telaFrequencia() {
  cabecalho("FREQ");
  tft.setTextSize(1);
  tft.setTextColor(C_TEXTO); tft.setCursor(4, 28); tft.print("Frequencia atual:");
  tft.setTextSize(2);
  tft.setTextColor(C_VALOR); tft.setCursor(4, 42); tft.print(FREQS[freqIdx], 2);
  rodape("CIMA/BAIXO muda  OK=ok");
}

void telaSalvos() {
  if (nSalvos == 0) {
    cabecalho("SALVOS"); tft.setTextSize(1);
    tft.setTextColor(C_AVISO); tft.setCursor(4, 34); tft.print("Nenhum salvo");
    rodape("OK = voltar");
    return;
  }
  cabecalho("SALVOS");
  tft.setTextSize(1);
  for (int i = 0; i < nSalvos; i++) {
    int y = 22 + i * 8;
    if (i == salvoSel) { tft.fillRect(0, y - 1, tft.width(), 8, C_SEL_BG); tft.setTextColor(ST77XX_WHITE); }
    else tft.setTextColor(C_TEXTO);
    tft.setCursor(6, y);
    tft.print(i + 1); tft.print(": "); tft.print(salvos[i].nome);
  }
  rodape("OK abre  longo volta");
}

const char* OPC_DET[] = {"Enviar", "Apagar", "Voltar"};
void telaSalvoDet() {
  cabecalho("SINAL");
  tft.setTextSize(1);
  tft.setTextColor(C_VALOR); tft.setCursor(4, 24); tft.print(salvos[salvoSel].nome);
  tft.setTextColor(C_TEXTO); tft.setCursor(4, 34);
  tft.print(salvos[salvoSel].valor); tft.print(" @");
  tft.print(salvos[salvoSel].freq, 2);
  for (int i = 0; i < 3; i++) {
    int y = 46 + i * 9;
    if (i == detSel) { tft.fillRect(0, y - 1, tft.width(), 9, C_SEL_BG); tft.setTextColor(ST77XX_WHITE); }
    else tft.setTextColor(C_AVISO);
    tft.setCursor(6, y);
    tft.print(OPC_DET[i]);
  }
}

const char* OPC_SN[] = {"Nao", "Sim"};
void telaApagarConf() {
  cabecalho("APAGAR");
  tft.setTextSize(1);
  tft.setTextColor(C_TEXTO); tft.setCursor(4, 26);
  tft.print("Apagar '"); tft.print(salvos[salvoSel].nome); tft.print("'?");
  for (int i = 0; i < 2; i++) {
    int y = 42 + i * 11;
    if (i == apagarSel) { tft.fillRect(0, y - 1, tft.width(), 11, C_SEL_BG); tft.setTextColor(ST77XX_WHITE); }
    else tft.setTextColor(C_AVISO);
    tft.setCursor(6, y);
    tft.print(OPC_SN[i]);
  }
}

void telaSobre() {
  cabecalho("SOBRE");
  tft.setTextSize(1);
  tft.setTextColor(C_TEXTO);
  tft.setCursor(4, 28); tft.print("SCARY-8266 v1.2");
  tft.setCursor(4, 40); tft.print("ESP8266 + CC1101");
  rodape("OK = voltar");
}

void telaRawInicio() {
  cabecalho("RAW");
  tft.setTextSize(1);
  tft.setTextColor(C_TEXTO);
  tft.setCursor(4, 26); tft.print("Captura bruta (RAW)");
  tft.setCursor(4, 38); tft.print("p/ sinais nao decod.");
  tft.setTextColor(C_AVISO); tft.setCursor(4, 54); tft.print("OK = capturar");
  rodape("OK longo = voltar");
}

void aviso(const char* txt, uint16_t cor) {
  cabecalho("INFO"); tft.setTextSize(1);
  tft.setTextColor(cor); tft.setCursor(4, 40); tft.print(txt);
  delay(900);
}

// ================= RF =================
void aplicaFreq() { ELECHOUSE_cc1101.setMHZ(FREQS[freqIdx]); ELECHOUSE_cc1101.SetRx(); }

void enviarSinal(unsigned long valor, unsigned int bits, int proto, int pulso, float freq) {
  if (valor == 0) return;
  cabecalho("TX"); tft.setTextSize(1); tft.setTextColor(C_AVISO);
  tft.setCursor(4, 40); tft.print("Enviando "); tft.print(freq, 2); tft.print(" MHz");

  mySwitch.disableReceive();
  ELECHOUSE_cc1101.setMHZ(freq);
  ELECHOUSE_cc1101.SetTx();
  delay(5);
  mySwitch.enableTransmit(CC_GDO0);
  mySwitch.setProtocol(proto);
  mySwitch.setPulseLength(pulso);
  mySwitch.setRepeatTransmit(10);
  mySwitch.send(valor, bits);
  mySwitch.disableTransmit();

  pinMode(CC_GDO0, INPUT);
  ELECHOUSE_cc1101.setMHZ(FREQS[freqIdx]);
  ELECHOUSE_cc1101.SetRx();
  mySwitch.enableReceive(CC_GDO0);

  tft.setTextColor(C_VALOR); tft.setCursor(4, 54); tft.print("OK!");
  delay(800);
}

void retransmitirAtual() {
  if (!temSinal || sinalValor == 0) { aviso("Nenhum sinal", ST77XX_RED); return; }
  enviarSinal(sinalValor, sinalBits, sinalProto, sinalPulso, sinalFreq);
}

// ---- RAW ----
void capturarRaw() {
  cabecalho("RAW"); tft.setTextSize(1);
  tft.setTextColor(C_AVISO); tft.setCursor(4, 34); tft.print("Capturando...");
  tft.setTextColor(ST77XX_WHITE); tft.setCursor(4, 48); tft.print("Aperte o controle");

  mySwitch.disableReceive();
  ELECHOUSE_cc1101.setMHZ(FREQS[freqIdx]);
  ELECHOUSE_cc1101.SetRx();
  sinalFreqRaw = FREQS[freqIdx];

  rawCount = 0;
  lastEdge = micros();
  pinMode(CC_GDO0, INPUT);
  attachInterrupt(digitalPinToInterrupt(CC_GDO0), rawISR, CHANGE);
  unsigned long t0 = millis();
  while (true) {
    if (rawCount >= RAW_MIN && (micros() - lastEdge) > RAW_GAP_US) break;
    if (rawCount >= RAW_MAX) break;
    if (millis() - t0 > RAW_TIMEOUT_MS) break;
    yield();
  }
  detachInterrupt(digitalPinToInterrupt(CC_GDO0));

  rawLen = rawCount;
  rawCapturado = (rawLen >= RAW_MIN);
  ELECHOUSE_cc1101.SetRx();
  mySwitch.enableReceive(CC_GDO0);

  cabecalho("RAW"); tft.setTextSize(1);
  if (rawCapturado) {
    tft.setTextColor(C_VALOR); tft.setCursor(4, 26); tft.print("Capturado!");
    tft.setTextColor(C_TEXTO); tft.setCursor(4, 40); tft.print("Transicoes: "); tft.print(rawLen);
    tft.setTextColor(C_AVISO); tft.setCursor(4, 54); tft.print("CIMA = enviar");
    rodape("OK = voltar");
    Serial.print(F("RAW capturado, transicoes=")); Serial.println(rawLen);
  } else {
    tft.setTextColor(ST77XX_RED); tft.setCursor(4, 34); tft.print("Nada capturado");
    rodape("OK = voltar");
  }
}

void enviarRaw() {
  if (!rawCapturado || rawLen < RAW_MIN) return;
  cabecalho("RAW TX"); tft.setTextSize(1);
  tft.setTextColor(C_AVISO); tft.setCursor(4, 40); tft.print("Enviando RAW...");

  mySwitch.disableReceive();
  ELECHOUSE_cc1101.setMHZ(sinalFreqRaw);
  ELECHOUSE_cc1101.SetTx();
  pinMode(CC_GDO0, OUTPUT);
  for (int r = 0; r < 5; r++) {
    ESP.wdtFeed();
    bool nivel = true;
    for (unsigned int i = 1; i < rawLen; i++) {
      unsigned int d = rawTimings[i];
      if (d > 25000) d = 25000;
      digitalWrite(CC_GDO0, nivel ? HIGH : LOW);
      delayMicroseconds(d);
      nivel = !nivel;
    }
    digitalWrite(CC_GDO0, LOW);
    delay(20);
  }
  pinMode(CC_GDO0, INPUT);
  ELECHOUSE_cc1101.setMHZ(FREQS[freqIdx]);
  ELECHOUSE_cc1101.SetRx();
  mySwitch.enableReceive(CC_GDO0);

  Serial.println(F("RAW retransmitido."));
  tft.setTextColor(C_VALOR); tft.setCursor(4, 54); tft.print("OK!");
  delay(700);
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n[SCARY-8266] Etapa 6 v2 - salvos com nomes"));

  pinMode(BTN_OK, INPUT);
  pinMode(BTN_UP, INPUT_PULLUP);

  if (LittleFS.begin()) { Serial.println(F("LittleFS OK")); carregarSalvos(); }
  else Serial.println(F("LittleFS FALHOU - verifique Flash Size com FS"));

  tft.initR(INITR_MINI160x80);
  tft.invertDisplay(false);
  tft.setRotation(1);

  ELECHOUSE_cc1101.setSpiPin(CC_SCK, CC_MISO, CC_MOSI, CC_CSN);
  ELECHOUSE_cc1101.setGDO0(CC_GDO0);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(FREQS[freqIdx]);
  ELECHOUSE_cc1101.SetRx();
  mySwitch.enableReceive(CC_GDO0);

  desenhaMenu();
}

// ================= Loop =================
void loop() {
  int ev = lerBotoes();

  switch (tela) {

    case MENU:
      if (ev == 1) { menuSel = (menuSel - 1 + N_ITENS) % N_ITENS; desenhaMenu(); }
      if (ev == 2) { menuSel = (menuSel + 1) % N_ITENS; desenhaMenu(); }
      if (ev == 3) {
        switch (menuSel) {
          case 0: tela = RECEBER; telaReceber(); break;
          case 1: retransmitirAtual(); desenhaMenu(); break;
          case 2: tela = FREQUENCIA; telaFrequencia(); break;
          case 3: tela = SALVOS; salvoSel = 0; telaSalvos(); break;
          case 4: tela = RAWMODE; rawCapturado = false; telaRawInicio(); break;
          case 5: tela = SOBRE; telaSobre(); break;
        }
      }
      break;

    case RECEBER:
      if (mySwitch.available()) {
        unsigned long v = mySwitch.getReceivedValue();
        if (v != 0) {
          sinalValor = v; sinalBits = mySwitch.getReceivedBitlength();
          sinalProto = mySwitch.getReceivedProtocol();
          sinalPulso = mySwitch.getReceivedDelay();
          sinalFreq  = FREQS[freqIdx];
          temSinal   = true;
          Serial.print(F("Capturado valor=")); Serial.println(sinalValor);
          mostraSinal();
        }
        mySwitch.resetAvailable();
      }
      if (ev == 3 && temSinal) { tela = NOMEAR; nomeSel = 0; telaNomear(); }  // OK -> nomear
      if (ev == 4 || (ev == 3 && !temSinal)) { tela = MENU; desenhaMenu(); }  // OK longo / sem sinal -> volta
      break;

    case NOMEAR:
      if (ev == 1) { nomeSel = (nomeSel - 1 + N_NOMES) % N_NOMES; telaNomear(); }
      if (ev == 2) { nomeSel = (nomeSel + 1) % N_NOMES; telaNomear(); }
      if (ev == 3) {                                   // OK confirma nome -> salva
        if (salvarComNome(NOMES[nomeSel])) aviso("Salvo!", C_VALOR);
        else aviso("Cheio ou sem sinal", ST77XX_RED);
        tela = MENU; desenhaMenu();
      }
      if (ev == 4) { tela = RECEBER; mostraSinal(); }  // cancela
      break;

    case FREQUENCIA:
      if (ev == 1) { freqIdx = (freqIdx + 1) % N_FREQS;           aplicaFreq(); telaFrequencia(); }
      if (ev == 2) { freqIdx = (freqIdx - 1 + N_FREQS) % N_FREQS; aplicaFreq(); telaFrequencia(); }
      if (ev == 3 || ev == 4) { tela = MENU; desenhaMenu(); }
      break;

    case SALVOS:
      if (nSalvos > 0) {
        if (ev == 1) { salvoSel = (salvoSel - 1 + nSalvos) % nSalvos; telaSalvos(); }
        if (ev == 2) { salvoSel = (salvoSel + 1) % nSalvos; telaSalvos(); }
        if (ev == 3) { tela = SALVO_DET; detSel = 0; telaSalvoDet(); }
      }
      if (ev == 4 || (ev == 3 && nSalvos == 0)) { tela = MENU; desenhaMenu(); }
      break;

    case SALVO_DET:
      if (ev == 1) { detSel = (detSel - 1 + 3) % 3; telaSalvoDet(); }
      if (ev == 2) { detSel = (detSel + 1) % 3; telaSalvoDet(); }
      if (ev == 3) {
        if (detSel == 0) {                             // Enviar
          Sinal s = salvos[salvoSel];
          enviarSinal(s.valor, s.bits, s.proto, s.pulso, s.freq);
          telaSalvoDet();
        } else if (detSel == 1) {                      // Apagar -> confirma
          tela = APAGAR_CONF; apagarSel = 0; telaApagarConf();
        } else {                                       // Voltar
          tela = SALVOS; telaSalvos();
        }
      }
      if (ev == 4) { tela = SALVOS; telaSalvos(); }
      break;

    case APAGAR_CONF:
      if (ev == 1 || ev == 2) { apagarSel = 1 - apagarSel; telaApagarConf(); }
      if (ev == 3) {
        if (apagarSel == 1) {                          // Sim
          apagarSalvo(salvoSel);
          if (salvoSel >= nSalvos) salvoSel = (nSalvos > 0) ? nSalvos - 1 : 0;
          aviso("Apagado", C_AVISO);
          tela = SALVOS; telaSalvos();
        } else {                                       // Nao
          tela = SALVO_DET; telaSalvoDet();
        }
      }
      if (ev == 4) { tela = SALVO_DET; telaSalvoDet(); }
      break;

    case RAWMODE:
      if (!rawCapturado) {
        if (ev == 3) capturarRaw();
        if (ev == 4) { tela = MENU; desenhaMenu(); }
      } else {
        if (ev == 1) enviarRaw();
        if (ev == 3 || ev == 4) { tela = MENU; rawCapturado = false; desenhaMenu(); }
      }
      break;

    case SOBRE:
      if (ev == 3 || ev == 4) { tela = MENU; desenhaMenu(); }
      break;

    default:
      tela = MENU; desenhaMenu();
      break;
  }
}
