# ArducamPico

Program do przechwytywania obrazu z kamery Arducam HM01B0 na Raspberry Pi Pico i wysyłania strumienia wideo przez USB-Serial.

## Jak to działa

1. **Inicjalizacja** — konfiguracja kamery przez I2C, uruchomienie strumieniowania 30 FPS w trybie QVGA
2. **Przechwytywanie** — DMA zapisuje piksele do bufora w tle (CPU nie czeka)
3. **Transmisja** — po zakończeniu DMA, Pico wysyła nagłówek synchronizacji + dane klatki przez Serial
4. **Pipeline** — natychmiast po wysyłce zlecane jest nowe przechwycenie (double buffering)

## Wymagania sprzętowe

| Komponent | Opis |
|-----------|------|
| Raspberry Pi Pico | RP2040, USB Full Speed |
| Arducam HM01B0 | Moduł kamery z sensorem 324x244 QVGA |
| Okablowanie | Połączenie GPIO wg poniższej tabeli |

## Mapowanie pinów GPIO

| GPIO | Funkcja | Opis |
|------|---------|------|
| 4 | I2C SDA | Dane konfiguracyjne sensora |
| 5 | I2C SCL | Zegar I2C |
| 6 | D0 | Linia danych (1-bit magistrali) |
| 14 | PCLK | Zegar pikseli |
| 16 | VSYNC | Synchronizacja pionowa (ramka) |
| — | MCLK | Oscylator wewnętrzny modułu Arducam |

## Wymagania programowe

- **Arduino IDE** z obsługą Raspberry Pi Pico
- **Biblioteka** `PicoHM01B0` — do zainstalowania przez Library Manager lub z repozytorium

## Protokół komunikacji

Pico wysyła strumień danych przez USB-Serial (115200 baud) w formacie:

```
[Nagłówek 2B] [Klatka 79056B] [Nagłówek 2B] [Klatka 79056B] ...
```

| Pole | Rozmiar | Wartość | Opis |
|------|---------|---------|------|
| Nagłówek | 2 bajty | `0x55 0xAA` | Znacznik początku ramki |
| Klatka | 79 056 bajtów | 324 × 244 | Obraz w skali szarości (1 bajt/piksel) |

### Odbiór po stronie Pythona

```python
import serial

ser = serial.Serial('/dev/ttyACM0', 115200)  # lub COMx na Windows
HEADER = b'\x55\xaa'
FRAME_SIZE = 324 * 244  # 79056

while True:
    # Synchronizacja — szukaj nagłówka
    while ser.read(2) != HEADER:
        pass

    # Odczytaj dane klatki
    frame_data = ser.read(FRAME_SIZE)

    # Konwersja do numpy array (opcjonalnie)
    import numpy as np
    frame = np.frombuffer(frame_data, dtype=np.uint8).reshape((244, 324))
```

## Parametry kamery

| Parametr | Wartość | Uwagi |
|----------|---------|-------|
| Rozdzielczość | 324 × 244 (QVGA) | Padding 4px w stosunku do 320×240 |
| FPS | 30 | Płynna praca, ~2.37 MB/s |
| Magistrala danych | 1-bit | Wystarczająca dla QVGA |
| Binning 2×2 | Wyłączony | Zachowuje pełną rozdzielczość |
| Orientacja | Normalna | Bez flipów |

## Znane ograniczenia

- Przepustowość USB Full Speed (~12 Mbps) ogranicza maksymalne FPS przy wyższych rozdzielczościach
- Padding 4px w QVGA — odbiornik musi wiedzieć o 324×244, nie 320×240
- Brak korekcji kolorów — sensor zwraca surowe dane w skali szarości
