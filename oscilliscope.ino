#include <arduinoFFT.h>
#include <math.h>
#include "esp_timer.h"   // Using esp_timer API
#include <SevSeg.h>      // Seven segment display library

// DAC and ADC pins:
const int dacPin = 25;
const int adcPin = 34;

// Sampling parameters:
double sampleRate = 2048;      // DAC and ADC sample rate in Hz

// Sine table parameters:
#define MAX_TABLE_SIZE 12000
int tableSize;                    // Actual table size for current frequency
uint8_t sineTable[MAX_TABLE_SIZE];
volatile int sineIndex = 0;       // Updated in timer callback

// Frequency control:
int currentOutputFrequency = 10;   // Start at 50 Hz

// FFT parameters:
const int samples = 2048;         // Number of samples for FFT
double vReal[samples], vImag[samples];
ArduinoFFT<double> fft(vReal, vImag, samples, sampleRate);

// Timer for DAC update:
esp_timer_handle_t periodic_timer;
uint64_t period_us;  // Timer period in microseconds

// Button pins (using internal pull-ups; pressed = LOW)
const int buttonUpPin = 32;    // Increase frequency
const int buttonDownPin = 33;  // Decrease frequency

// New LED pins for the RGB LED:
const int blueLedPin = 26;     // Blue component
const int redLedPin = 22;      // Red component

// Create SevSeg object for the 7-seg display:
SevSeg sevseg;

// Timer callback: continuously updates the DAC output using the sine table.
void IRAM_ATTR dacTimerCallback(void* arg) {
  dacWrite(dacPin, sineTable[sineIndex]);
  sineIndex++;
  if (sineIndex >= tableSize) {
    sineIndex = 0;
  }
}

// Helper function to update the sine table for a given frequency.
void updateSineTable(int freq) {
  // Calculate table size = sampleRate / frequency
  tableSize = (int)(sampleRate / freq);
  // Precompute sine values scaled for dacWrite (0–255)
  for (int i = 0; i < tableSize; i++) {
    float angle = 2 * PI * i / tableSize;
    sineTable[i] = (uint8_t)((sin(angle) + 1.0) * 127.5);
  }
  sineIndex = 0;  // Reset index when table is updated
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Allow time for Serial Monitor to open
  
  // Set up button pins with internal pull-ups:
  pinMode(buttonUpPin, INPUT_PULLUP);
  pinMode(buttonDownPin, INPUT_PULLUP);

  pinMode(blueLedPin, OUTPUT);
pinMode(redLedPin, OUTPUT);
  
  // Set initial frequency and update the sine table:
  currentOutputFrequency = 10;
  updateSineTable(currentOutputFrequency);
  
  // Create the periodic timer using esp_timer.
  const esp_timer_create_args_t timer_args = {
    .callback = &dacTimerCallback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,  // Run in a dedicated timer task
    .name = "dacTimer"
  };
  esp_timer_create(&timer_args, &periodic_timer);
  
  // Timer period based on sampleRate (microseconds per DAC update)
  period_us = (uint64_t)(1000000.0 / sampleRate);
  esp_timer_start_periodic(periodic_timer, period_us);

  // --- SevSeg Display Setup ---
  // We'll use 4 digits with these ESP32 pins (avoiding pins 26, 25, 33, 32, 34):
  byte numDigits = 4;
  // Digit (common cathode) pins:
  byte digitPins[] = {2, 4, 27, 14};
  // Segment pins in order: A, B, C, D, E, F, G, DP:
  byte segmentPins[] = {5, 12, 13, 15, 18, 19, 21, 23};
  bool resistorsOnSegments = true;    // true if your module has built-in resistors
  bool updateWithDelays = false;      // Non-blocking refresh recommended on ESP32
  bool leadingZeros = false;          // No leading zeros
  bool disableDecPoint = false;       // Allow decimal point if desired
  sevseg.begin(COMMON_CATHODE, numDigits, digitPins, segmentPins, 
               resistorsOnSegments, updateWithDelays, leadingZeros, disableDecPoint);
  sevseg.setBrightness(90);           // Brightness 0–100
}

void loop() {
  // --- Check buttons to adjust frequency ---
  if(digitalRead(buttonUpPin) == LOW) {
    delay(50); // Simple debounce
    currentOutputFrequency += 10;
    if(currentOutputFrequency > 500) { // Upper limit
      currentOutputFrequency = 500;
    }


    digitalWrite(redLedPin, HIGH);
    delay(100);
    digitalWrite(redLedPin, LOW);
    
    esp_timer_stop(periodic_timer);
    updateSineTable(currentOutputFrequency);
    esp_timer_start_periodic(periodic_timer, period_us);
    Serial.print("Frequency increased to: ");
    Serial.print(currentOutputFrequency);
    Serial.println(" Hz");
    delay(300); // Prevent rapid repeated changes
  }
  
  if(digitalRead(buttonDownPin) == LOW) {
    delay(50); // Simple debounce
    currentOutputFrequency -= 10;
    if(currentOutputFrequency < 1) { // Lower limit
      currentOutputFrequency = 1;
    }

 
    digitalWrite(blueLedPin, HIGH);
    delay(100);
    digitalWrite(blueLedPin, LOW);
    
    esp_timer_stop(periodic_timer);
    updateSineTable(currentOutputFrequency);
    esp_timer_start_periodic(periodic_timer, period_us);
    Serial.print("Frequency decreased to: ");
    Serial.print(currentOutputFrequency);
    Serial.println(" Hz");
    delay(300); // Prevent rapid repeated changes
  }
  
  // --- Capture ADC data from adcPin ---
  for (int i = 0; i < samples; i++) {
    vReal[i] = analogRead(adcPin) * (3.3 / 4095.0);  // Convert ADC reading to volts
    vImag[i] = 0;
    delayMicroseconds((int)(1000000.0 / sampleRate));
  }
  
  // --- Apply FFT on the sampled data ---
  fft.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  fft.compute(FFT_FORWARD);
  fft.complexToMagnitude();
  
  // --- Find the dominant frequency (ignoring DC) ---
  int peakBin = 0;
  double maxMagnitude = 0;
  for (int i = 1; i < samples / 2; i++) {
    if (vReal[i] > maxMagnitude) {
      maxMagnitude = vReal[i];
      peakBin = i;
    }
  }
  double detectedFrequency = peakBin * (sampleRate / samples);
  
  // Scale the detected frequency (as in your original code)
  double scaledFrequency = detectedFrequency * 0.9;
  int displayFrequency = round(scaledFrequency);
  
  Serial.print("Output: ");
  Serial.print(currentOutputFrequency);
  Serial.print(" Hz, Detected: ");
  Serial.print(scaledFrequency, 2);
  Serial.println(" Hz");
  
  // --- Update the seven-segment display ---
  // Display the detected frequency as an integer with no leading zeros.
  //sevseg.setNumber(displayFrequency, 0);
  sevseg.setNumber(currentOutputFrequency, 0);

  
  // Refresh the display continuously for 1 second to maintain multiplexing.
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {
    sevseg.refreshDisplay();
  }
}
