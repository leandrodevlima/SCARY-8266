# SCARY-8266

Multitool sub-GHz caseiro em **ESP8266 (NodeMCU)** com módulo **CC1101**, display **ST7735** colorido e 3 botões. Firmware próprio em Arduino para **capturar e retransmitir** sinais de rádio de código fixo (OOK) na faixa sub-GHz, salvar na memória interna com nomes e trabalhar em modo RAW.

> ⚠️ **Uso educacional.** Projeto feito para estudo de RF e para operar **nos seus próprios equipamentos** (portão, campainha, tomada etc.). Não use para acessar sistemas de terceiros sem autorização. Controles de **código rolante (rolling code)** não são replicáveis por design — isso é esperado.

## Recursos

- **Receber** — captura um sinal e mostra protocolo, valor (chave), bits e pulso.
- **Retransmitir** — reenvia o último sinal capturado (na frequência em que foi capturado).
- **Frequência** — seleção entre 300, 315, 433.92, 868.30 e 915 MHz (padrão 433.92).
- **Salvos** — grava o sinal capturado com um **nome** (lista pronta), lista os salvos e permite **Enviar** ou **Apagar** (com confirmação). Persistência em **LittleFS** (sobrevive ao desligar).
- **Modo RAW** — captura/replay por temporização bruta (bit-bang no GDO0), para sinais que o decodificador não reconhece.
- **Sobre** — informações do firmware.

Navegação: **CIMA/BAIXO** movem, **OK** confirma, **OK longo** volta.

## Hardware

- ESP8266 — placa NodeMCU
- Módulo CC1101 (SPI) + antena para a faixa usada (ex.: fio reto ~17,3 cm para 433 MHz)
- Display TFT ST7735 (SPI)
- 3 botões (OK / CIMA / BAIXO)

## Pinout (ligações)

### Display ST7735
| Display | ESP8266 (D / GPIO) |
|---|---|
| CS  | D3 / GPIO0 |
| DC  | D4 / GPIO2 |
| RES | D0 / GPIO16 |
| SCL (SCLK) | D5 / GPIO14 |
| SDA (MOSI) | D7 / GPIO13 |
| BLK | 3V3 |

### CC1101
| CC1101 | ESP8266 (D / GPIO) |
|---|---|
| SCK  | D5 / GPIO14 |
| MOSI | D7 / GPIO13 |
| MISO | D6 / GPIO12 |
| CSN  | D1 / GPIO5 |
| GDO0 | D2 / GPIO4 |
| ANT  | antena (não vai no ESP) |
| VCC  | 3V3 |
| GND  | GND |

> O display e o CC1101 compartilham o barramento SPI (SCLK/MOSI).

### Botões
| Botão | ESP8266 (D / GPIO) | Observação |
|---|---|---|
| OK    | D8 / GPIO15 | ativo-alto |
| CIMA  | RX / GPIO3  | pull-up interno |
| BAIXO | A0          | analógico (limiar 500) |

## Compilar (Arduino IDE)

1. Instale o core **ESP8266** (Gerenciador de Placas).
2. Bibliotecas necessárias:
   - `Adafruit GFX`
   - `Adafruit ST7735 and ST7789`
   - `ELECHOUSE CC1101 (SmartRC-CC1101-Driver-Lib)`
   - `RCSwitch`
   - `LittleFS` (já vem com o core ESP8266)
3. Em **Ferramentas**:
   - **MMU**: `16KB cache + 48KB IRAM`
   - **Flash Size**: uma opção **com sistema de arquivos** (ex.: `4MB (FS:2MB ...)`) — necessário para os salvos em LittleFS.
4. Selecione a porta COM e grave.

## Frequências e nomes prontos

- Frequências: `300.00, 315.00, 433.92, 868.30, 915.00` MHz (padrão `433.92`).
- Nomes prontos para salvar: `Portao, Portao 2, Carro, Casa, Tomada, Campainha`.

## Estrutura

```
SCARY-8266.ino   # firmware principal (versão final)
LICENSE          # MIT
README.md
```

## Créditos

Construído sobre as bibliotecas Adafruit GFX/ST7735, ELECHOUSE SmartRC-CC1101-Driver-Lib e RCSwitch.

## Licença

[MIT](LICENSE) © 2026 Leandro Barbosa de Lima
