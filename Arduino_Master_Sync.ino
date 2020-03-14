#include <Adafruit_SSD1306.h>
#include <TimerOne.h>
#include <EEPROM.h>
#include <Encoder.h>

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

#define LED_PIN1 7
#define SYNC_OUTPUT_PIN 6 // Audio sync digital pin
#define SYNC_OUTPUT_PIN2 8 // 2nd audio sync pin

Encoder myEnc(2, 3); // rotary encoder pin 2,3
const int button_rotary = 5; // rotary encoder button
const int button_start = 4; // push button

volatile int blinkCount = 0;
volatile int blinkCount2 = 0;
volatile int AudioSyncCount = 0;
volatile int AudioSyncCount2 = 0;

#define MIDI_START 0xFA
#define MIDI_STOP 0xFC
#define MIDI_TIMING_CLOCK 0xF8
#define CLOCKS_PER_BEAT 24
#define AUDIO_SYNC 12
#define AUDIO_SYNC2 12

#define MINIMUM_BPM 20
#define MAXIMUM_BPM 300

#define BLINK_TIME 4 // LED blink time

long intervalMicroSeconds;
int bpm;
int audio_sync2;

boolean playing = false;
boolean sync_editing = false;
boolean display_update = false;

void setup(void) {
  Serial.begin(31250); // MIDI output init

  bpm = EEPROMReadInt(0);
  if (bpm > MAXIMUM_BPM || bpm < MINIMUM_BPM) {
    bpm = 120;
  }
  audio_sync2 = EEPROMReadInt(3);
  if (audio_sync2 > 64 || audio_sync2 < 2) {
    audio_sync2 = 12;
  }
   
  Timer1.initialize(intervalMicroSeconds);
  Timer1.setPeriod(60L * 1000 * 1000 / bpm / CLOCKS_PER_BEAT);
  Timer1.attachInterrupt(sendClockPulse);  

  pinMode(button_start,INPUT_PULLUP);
  pinMode(button_rotary,INPUT_PULLUP);
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);  
  display.setTextSize(4);
  display.setCursor(0,0);
  display.print(bpm);
  display.display();
}

void EEPROMWriteInt(int p_address, int p_value)
     {
     byte lowByte = ((p_value >> 0) & 0xFF);
     byte highByte = ((p_value >> 8) & 0xFF);

     EEPROM.write(p_address, lowByte);
     EEPROM.write(p_address + 1, highByte);
     }

unsigned int EEPROMReadInt(int p_address)
     {
     byte lowByte = EEPROM.read(p_address);
     byte highByte = EEPROM.read(p_address + 1);

     return ((lowByte << 0) & 0xFF) + ((highByte << 8) & 0xFF00);
}

void bpm_display() { 
  updateBpm();
  EEPROMWriteInt(0,bpm);  
  //display.clearDisplay();
  display.setTextSize(4);
  display.setCursor(0,0);  
  display.setTextColor(WHITE, BLACK);
  display.print("     ");
  display.setCursor(0,0);
  display.print(bpm);
  display.display();
  display_update = false;
}

void sync_display() {
  EEPROMWriteInt(3,audio_sync2);
  
  int sync_current;
  sync_current = audio_sync2 - 12;  
  
  if (sync_current < 0) {    
    sync_current = abs(sync_current);
  } else if (sync_current > 0) {
    sync_current = -sync_current;
  }
    
  display.setTextSize(4);
  display.setCursor(0,0);
  display.setTextColor(WHITE, BLACK);
  display.print("     ");  
  display.setCursor(0,0);
  display.print(sync_current);
  display.display();
}

void startOrStop() {
  if (!playing) {
    Serial.write(MIDI_START);
  } else {
    all_off();
    Serial.write(MIDI_STOP);
  }
  playing = !playing;
}

int oldPosition;

void loop(void) {
  byte i = 0;
  byte p = 0;
  
  if (digitalRead(button_start)== LOW) {
    startOrStop();
    delay(500);
  }
  if (digitalRead(button_rotary)== LOW) {
    p = 1;
    delay(500);    
  }
  
  int newPosition = (myEnc.read()/4);
  if (newPosition != oldPosition) {    
    if (oldPosition < newPosition) {
      i = 2;
    } else if (oldPosition > newPosition) {
      i = 1;
    }
    oldPosition = newPosition;
  }
  
  if (!sync_editing) {      
      if (i == 2) {
        bpm++;
        if (bpm > MAXIMUM_BPM) {
          bpm = MAXIMUM_BPM;
        }
        bpm_display();          
      } else if (i == 1) {
        bpm--;
        if (bpm < MINIMUM_BPM) {
          bpm = MINIMUM_BPM;
        }
        bpm_display();
      } else if (p == 1) {
        //rotary.resetPush();
        sync_display();
        sync_editing = true;
      }
  } else  { // 2nd jack audio sync speed
      if (p == 1) {      
        bpm_display();
        sync_editing = false;
      } else if (i == 1) {      
        audio_sync2++;
        if (audio_sync2 > 64) { audio_sync2 = 64; }
        sync_display();
      } else if (i == 2) {
        audio_sync2--;
        if (audio_sync2 < 2) { audio_sync2 = 2; }
        sync_display();
      }      
  }
}

void all_off() {
  digitalWrite(SYNC_OUTPUT_PIN, LOW);
  digitalWrite(SYNC_OUTPUT_PIN2, LOW);
  digitalWrite(LED_PIN1, LOW);
}

void sendClockPulse() {  
  
  Serial.write(MIDI_TIMING_CLOCK); // sending midi clock
  
  if (playing) {  
  
  blinkCount = (blinkCount + 1) % CLOCKS_PER_BEAT;
  blinkCount2 = (blinkCount2 + 1) % (CLOCKS_PER_BEAT / 2);
  AudioSyncCount = (AudioSyncCount + 1) % AUDIO_SYNC;
  AudioSyncCount2 = (AudioSyncCount2 + 1) % audio_sync2;

  if (AudioSyncCount == 0) {
      digitalWrite(SYNC_OUTPUT_PIN, HIGH); 
  } else {        
    if (AudioSyncCount == 1) {     
      digitalWrite(SYNC_OUTPUT_PIN, LOW);
    }
  }  

  if (AudioSyncCount2 == 0) {
      digitalWrite(SYNC_OUTPUT_PIN2, HIGH);
  } else {        
    if (AudioSyncCount2 == 1) {
      digitalWrite(SYNC_OUTPUT_PIN2, LOW);
    }
  }
  
  if (blinkCount == 0) {
      digitalWrite(LED_PIN1, HIGH);      
  } else {
     if (blinkCount == BLINK_TIME) {
       digitalWrite(LED_PIN1, LOW);      
     }
  }
  } // if playing
}

void updateBpm() { 
  long interval = 60L * 1000 * 1000 / bpm / CLOCKS_PER_BEAT;  
  Timer1.setPeriod(interval);
}
