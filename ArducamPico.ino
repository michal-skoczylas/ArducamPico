#include <Arduino.h>
#include <PicoHM01B0.h>

PicoHM01B0 camera;

// Rozdzielczość QVGA: matryca HM01B0 w tym trybie przesyła 324x244 zamiast standardowych 320x240.
// Dodatkowe 4 kolumny i 4 wiersze to padding wymagany przez kontroler sensora — nie zawierają użytecznych danych.
#define FRAME_WIDTH 324
#define FRAME_HEIGHT 244

// Bufor na jedną klatkę obrazu (79 056 bajtów).
// Wyrównanie do 4 bajtów (__attribute__((aligned(4))) jest wymagane przez DMA na RP2040 —
// bez tego transfer może się zakończyć błędnym odczytem lub zawieszeniem.
uint8_t frame_buffer[FRAME_WIDTH * FRAME_HEIGHT] __attribute__((aligned(4)));

// Wzorzec bajtów wysyłany przed każdą klatką, służący jako punkt synchronizacji.
// Odbiornik (skrypt Python) szuka sekwencji 0x55 0xAA w strumieniu danych,
// aby zidentyfikować początek ramki i poprawnie zdekodować obraz.
uint8_t header[2] = {0x55, 0xAA};

void setup()
{
  // Komunikacja szeregowa USB — przepustowość 115200 baud jest wystarczająca
  // dla 30 FPS × 79056 bajtów ≈ 2.37 MB/s (USB Full Speed na Pico osiąga ~12 Mbps).
  Serial.begin(115200);

  // Konfiguracja pinów GPIO zgodna z schematem modułu Arducam HM01B0.
  // Każdy pin ma przypisaną konkretną rolę w magistrali kamery:
  PicoHM01B0_config config;
  config.i2c_dat_gpio = 4;   // I2C SDA — dane konfiguracyjne sensora (rejetry)
  config.i2c_clk_gpio = 5;   // I2C SCL — zegar I2C
  config.vsync_gpio = 16;    // VSYNC — sygnał pionowej synchronizacji (ramka)
  config.d0_gpio = 6;        // D0 — linia danych (1-bitowy tryb magistrali)
  config.pclk_gpio = 14;     // PCLK — zegar pikseli (oprogramowanie próbek)

  // MCLK = -1 oznacza użycie wewnętrznego oscylatora modułu Arducam.
  // Alternatywnie można podać numer GPIO, jeśli chcemy generować zegar z Pico.
  config.mclk_gpio = -1;

  config.bus_4bit = false;       // Tryb 1-bitowy (wystarczający dla QVGA 30 FPS)
  config.flip_horizontal = false; // Bez lustrzanego odbicia
  config.flip_vertical = false;   // Bez obrotu o 180°

  // Inicjalizacja kamery — nawiązuje połączenie I2C i konfiguruje sensor.
  // Zwraca false jeśli nie uda się zainicjalizować (np. zły adres I2C, brak zasilania).
  if (!camera.begin(config))
  {
    Serial.println("Blad polaczenia z kamera!");
    while (1)
      delay(100); // Zatrzymaj program — bez kamery dalsze działanie nie ma sensu
  }

  // Uruchomienie strumieniowania obrazu.
  // Parametry: start_streaming(frame_rate, binning_2x2, qvga_mode)
  //   - 30.0 FPS — płynna praca, kompromis między jakością a przepustowością
  //   - false — bez binningu 2x2 (zachowuje pełną rozdzielczość)
  //   - true  — tryb QVGA (324x244) zamiast pełnej rozdzielczości sensora
  camera.start_streaming(30.0, false, true);

  // Inicjalizacja pierwszego przechwycenia DMA "w tle".
  // DMA zacznie zapisywać piksele do frame_buffer bez blokowania CPU.
  camera.start_capture(frame_buffer);
}

void loop()
{
  // Krok 1: Blokuj do momentu, aż DMA zakończy zapis całej klatki.
  // To jedyny moment, gdy CPU czeka — reszta operacji dzieje się równolegle.
  camera.wait_for_frame();

  // Krok 2: Wyślij nagłówek synchronizacji (0x55 0xAA) do odbiornika Python.
  // Odbiornik szuka tego wzorca, aby wiedzieć, gdzie zaczyna się nowa ramka.
  Serial.write(header, 2);

  // Krok 3: Wyślij cały bufor klatki przez USB-Serial.
  // 324 × 244 = 79 056 bajtów — to jest właściwy obraz (z paddingiem).
  Serial.write(frame_buffer, FRAME_WIDTH * FRAME_HEIGHT);

  // Krok 4: Natychmiast zleć DMA pobranie kolejnej klatki.
  // To jest kluczowe dla wydajności: kamera ładuje nową ramkę do pamięci,
  // podczas gdy CPU właśnie wysyła poprzednią przez Serial.
  // Taki pipeline "double buffering" eliminuje przestoje.
  camera.start_capture(frame_buffer);
}