/*
 * Arduino Midi Master Clock + FX v0.2
 * MIDI master clock/sync/divider for MIDI instruments, Pocket Operators and Korg Volca
 * and Live Effects for Volca Sample (or Drum)
 * by Eunjae Im https://ejlabs.net/arduino-midi-master-clock
 *
 * Required library
 *    TimerOne https://playground.arduino.cc/Code/Timer1
 *    Encoder https://www.pjrc.com/teensy/td_libs_Encoder.html
 *    MIDI https://github.com/FortySevenEffects/arduino_midi_library
 *    Adafruit SSD1306 https://github.com/adafruit/Adafruit_SSD1306
 *******************************************************************************
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *******************************************************************************
 */

#include <Adafruit_SSD1306.h>
#include <TimerOne.h>
#include <EEPROM.h>
#include <Encoder.h>
#include <MIDI.h>

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

#define LED_PIN1 13 // LED onboard (tempo)
#define SYNC_OUTPUT_PIN 6 // Audio Sync Digital Pin
#define SYNC_OUTPUT_PIN2 8 // 2nd Audio Sync Pin
#define BUTTON_START 4 // Start/Stop Push Button
#define BUTTON_ROTARY 5 // Rotary Encoder Button

/* Live Effect Button */
#define BUTTON_FX1 9  // stutter #1
#define BUTTON_FX2 10 // stutter #2
#define BUTTON_FX3 11 // stutter #3
#define BUTTON_FX4 12 // random trigger
#define BUTTON_FX5 7  // pattern restart
#define BUTTON_FX6 2  // pitch up
#define BUTTON_FX7 3  // pitch down

// Volca Sample
int level_cc_num = 7, pitch_cc_num = 44, channel_number = 10, default_pitch = 64;
// Volca Drum
//int level_cc_num = 19, pitch_cc_num = 28, channel_number = 5, default_pitch = 0;

int pitch = default_pitch,
    pitch_change_speed = 3, // pitch up/down speed value    
    pitch_min = 1, // pitch effect min value
    pitch_max = 125; // pitch effect max value

#define CLOCKS_PER_BEAT 24 // MIDI Clock Ticks
#define AUDIO_SYNC 12 // Audio Sync Ticks
#define AUDIO_SYNC2 12 // 2nd Audio Sync Ticks

#define MINIMUM_BPM 20
#define MAXIMUM_BPM 300

#define BLINK_TIME 4 // LED blink time

volatile int  blinkCount = 0,
              blinkCount2 = 0,
              AudioSyncCount = 0,
              AudioSyncCount2 = 0,
              fx_sync_beat = 6,
              fx_sync_start = 0;

long  intervalMicroSeconds,
      bpm,
      audio_sync2;

boolean playing = false,
        sync_editing = false,
        fx1_sent = false,
        fx2_sent = false,
        fx1 = false,
        fx2 = false,
        fx3 = false,
        fx4 = false,
        fx5 = false;

Encoder myEnc(2, 3); // Rotary Encoder Pin 2,3 

MIDI_CREATE_DEFAULT_INSTANCE();

void setup(void) {
  MIDI.begin(); // MIDI init
  MIDI.turnThruOff();

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

  pinMode(BUTTON_START,INPUT_PULLUP);
  pinMode(BUTTON_ROTARY,INPUT_PULLUP);
  pinMode(BUTTON_FX1,INPUT_PULLUP);
  pinMode(BUTTON_FX2,INPUT_PULLUP);
  pinMode(BUTTON_FX3,INPUT_PULLUP);
  pinMode(BUTTON_FX4,INPUT_PULLUP);
  pinMode(BUTTON_FX5,INPUT_PULLUP);
  pinMode(BUTTON_FX6,INPUT_PULLUP);
  pinMode(BUTTON_FX7,INPUT_PULLUP);
  
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
  display.setTextSize(4);
  display.setCursor(0,0);  
  display.setTextColor(WHITE, BLACK);
  display.print("     ");
  display.setCursor(0,0);
  display.print(bpm);
  display.display();  
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
    MIDI.sendRealTime(midi::Start);
  } else {
    all_off();
    MIDI.sendRealTime(midi::Stop);
  }
  playing = !playing;
}

int oldPosition;

void loop(void) {
  byte i = 0;
  byte p = 0;
  
  if (digitalRead(BUTTON_START) == LOW) {
    startOrStop();
    delay(300); // ugly but just make life easier, no need to check debounce
  } else if (digitalRead(BUTTON_ROTARY) == LOW) {    
    p = 1;
    delay(200);
  }

  effect_button(); // call effect button check
  
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

void all_off() { // make sure all sync, led pin stat to low
  digitalWrite(SYNC_OUTPUT_PIN, LOW);
  digitalWrite(SYNC_OUTPUT_PIN2, LOW);
  digitalWrite(LED_PIN1, LOW);
}

void effect_button() {
  if (digitalRead(BUTTON_FX1) == LOW) { // stutter 1
    fx1 = true;    
  } else if (digitalRead(BUTTON_FX2) == LOW) { // stutter 2
    fx1 = true;
    fx_sync_beat = 12;
  } else if (digitalRead(BUTTON_FX3) == LOW) { // stutter 3
    fx1 = true;
    fx_sync_beat = 16;
  } else if (digitalRead(BUTTON_FX4) == LOW) {  // Random trigger
    fx2 = true;   
  } else if (digitalRead(BUTTON_FX5) == LOW) { // restart    
    fx3 = true;    
  } else if (digitalRead(BUTTON_FX6) == LOW) { // pitch up
    fx4 = true;
  } else if (digitalRead(BUTTON_FX7) == LOW) { // pitch up
    fx5 = true;
  } else {
    fx_sync_beat = 6;
    fx1 = false;
    fx2 = false;
    fx3 = false;
    fx4 = false;
    fx5 = false;
  }
}

void sending_fx1(int vol) { // level change effect
  int x = 1;  
  while (x <= channel_number)
    {     
      MIDI.sendControlChange(level_cc_num,vol,x);
      x++;
    }    
}

void sending_fx2() { // random trigger
  int channel = random(1,channel_number + 1);
  MIDI.sendControlChange(level_cc_num, 125, channel);
  MIDI.sendNoteOn(60, 125, channel);
}

void pitch_fx(int value) { // pitch up/down effect
  int x = 1;  
  while (x <= channel_number)
    {     
      MIDI.sendControlChange(pitch_cc_num,value,x); // 44 = PITCH EG INT
      x++;
    }
}

void sendClockPulse() {  

  MIDI.sendRealTime(midi::Clock); // sending midi clock

  fx_sync_start = (fx_sync_start + 1) % fx_sync_beat;
  
  if (fx_sync_start == 1 && fx1 == true && fx1_sent == false) { // stutter 1,2,3
      sending_fx1(0);
      fx1_sent = true;
  } else if (fx_sync_start == 1 && fx1_sent == true) {
      sending_fx1(125);
      fx1_sent = false;            
  }  

  if (fx_sync_start == 0 && fx2 == true) { // random trigger
      /*
      if (!fx2_sent) { 
        sending_fx1(0); // mute all channel once
      }
      */
      MIDI.sendRealTime(midi::Stop);
      sending_fx2();
      fx2_sent = true;
  } else if (fx_sync_start == 0 && fx2_sent == true && fx2 == false) {
      //sending_fx1(125); // reset all channel level to 125
      MIDI.sendRealTime(midi::Continue);
      fx2_sent = false;
  }

  if (fx_sync_start == 0 && fx3 == true) { // pattern restart
      MIDI.sendRealTime(midi::Start);      
  }

  if (fx4 == true) { // pitch up
      if (pitch < pitch_max) {
          pitch = pitch + pitch_change_speed;
          pitch_fx(pitch);
      }
  } else if (fx5 == true) { // pitch down
      if (pitch > pitch_min) {
          pitch = pitch - pitch_change_speed;
          pitch_fx(pitch);
      }
  } else if (fx4 == false && fx5 == false && pitch != default_pitch) { // pitch reset
      pitch = default_pitch;
      pitch_fx(pitch);
  }
  
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

void updateBpm() { // update BPM function (on the fly)
  long interval = 60L * 1000 * 1000 / bpm / CLOCKS_PER_BEAT;  
  Timer1.setPeriod(interval);
}
