#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ESP32Encoder.h>
#include <SPI.h>
#include <SD.h>
#include <Audio.h>
#include "Adafruit_MAX1704X.h"

#define SD_CS   10
#define SD_MOSI 11
#define SD_MISO 12
#define SD_SCK  13
#define I2S_LRC  1 
#define I2S_DOUT 2 
#define I2S_BCLK 3  
#define MY_SDA_PIN 5
#define MY_SCL_PIN 6

unsigned long lastDebounceTime = 0; // for debouncing

// for sd card
bool sdInitialized = false;

const int MAX_SONGS = 200;
String songNames[MAX_SONGS]; // stores individual songs
int songCount = 0;

const int MAX_PLAYLISTS = 50;
String playlistNames[MAX_PLAYLISTS]; // stores playlists
int playlistCount = 0;

const int MAX_PLAYLIST_SONGS = 200;
String playlistSongs[MAX_PLAYLIST_SONGS]; // stores songs of a certain playlist
int playlistSongCount = 0;



// for OLED
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);



bool screenNeedsUpdate = true;

int selectedItem = 0;
int topVisibleItem = 0;

int scrollPosition = 0; //scrolling text

unsigned long lastScrollTime = 0;

const int scrollDelay = 250;

// for audio 

Audio audio;

int shuffleDeck[200]; // for shuffling songs in a playlist
int shuffleIndex = 0;

volatile bool nextSongRequested = false;


const unsigned char playpauseskip [] PROGMEM = {
	// 'play/pause/skip', 34x7px
	0x11, 0x21, 0x14, 0x22, 0x02, 0x99, 0x61, 0x14, 0x66, 0x02, 0xdd, 0xe1, 0x14, 0xee, 0x02, 0xff, 
	0xe1, 0x15, 0xfe, 0x03, 0xdd, 0xe1, 0x14, 0xee, 0x02, 0x99, 0x61, 0x14, 0x66, 0x02, 0x11, 0x21, 
	0x14, 0x22, 0x02
};

const unsigned char shuffleicon [] PROGMEM = {
	// 'shuffle', 8x7px
	0x40, 0xe3, 0x54, 0x08, 0x54, 0xe3, 0x40
};

const unsigned char repeaticon [] PROGMEM = {
	// 'repeaticon', 9x7px
	0x7c, 0x00, 0x02, 0x00, 0x82, 0x00, 0xc2, 0x01, 0xa2, 0x02, 0x82, 0x00, 0x7c, 0x00
};

// for battery gauge

Adafruit_MAX17048 maxlipo;

unsigned long lastBatteryCheck = 0;
int batteryPercentage = 100;
bool isCharging = false;

// predefined functions

//drawing functions
void drawMenu();
void drawAllSongs();
void drawPlaylists();
void drawSettings();
void drawAbout();
void drawNowPlaying();
void drawCurrentScreen();
void drawBootScreen();

// sd card/audio functions

void scanSongs();
void loadPlaylists();
void loadPlaylistSongs(String playlistName);
void startPlaylist(String playlistName);
void playSong(String songName);
void playNextSong();
void playPreviousSong();
void stopSong();
void audio_info(const char *info);
void audio_eof_mp3(const char *info);
void listDir(fs::FS &fs, const char * dirname, uint8_t levels);

//misc functions
int getCurrentScreenItemCount();
void setCurrentFont();
String removeExtension(String name);
String fitToScreen(String name);
String formatSongName(String name);
String getScrollingText(String text);
String formatTime(uint32_t totalSeconds);
void buildShuffleDeck(int totalTracks);
void updateBatteryLevel();
void displayBatteryPercentage();

const char* settingsItems[] ={
  "Brightness",
  "Volume",
  "Font",
  "Shuffle",
  "Repeat"
};

const char* aboutItems[] ={
  "mp3 player",
  "VERSION 1.0",
  "JOSHUA SCALLO",
  "               :)"
};


enum Screen {
  MENU,
  ALLSONGS,
  PLAYLISTS,
  SETTINGS,
  ABOUT,
  NOWPLAYING
};

Screen currentScreen = MENU;

// for encoder
ESP32Encoder encoder;

const int CLK = 8;
const int DT  = 7;
const int SW  = 9;

bool buttonPressed = false;
bool longPressHandled = false;
unsigned long buttonPressTime = 0;
const unsigned long LONG_PRESS_TIME = 1000; // milliseconds

//for settings
int volume = 50;
bool editingVolume = false;
int brightness = 5;
bool editingBrightness = false;
int font = 1;
bool editingFont = false;
bool shuffle = true;
bool editingShuffle = false;
bool repeatSong = false;
bool repeatPlaylist = false;
int repeatMode = 0; // 0: OFF, 1: ALL, 2: ONE
bool editingRepeat = false;


// for now playing tab

String currentSong = "";
String currentPlaylist = "";
bool playingFromPlaylist = false;
int currentSongIndex = 0;
int currentPlaylistIndex = 0;
bool isPlaying = false;
bool editingNowPlayingVolume = false;
bool isPaused = false;



//__________________________________________________________________________________________________________setup

void setup() {

  Serial.begin(115200);

  Wire.begin(MY_SDA_PIN, MY_SCL_PIN);
  Wire.setClock(400000);

  // for battery gauge
  
  if (!maxlipo.begin()) {
    Serial.println("Could NOT find MAX17048 fuel gauge!");
  } else {
    Serial.println("MAX17048 fuel gauge found!");
  }

  maxlipo.reset();
  delay(100); // Give it a tiny moment to reboot
  maxlipo.quickStart();


  // OLED
  
  u8g2.begin();
  u8g2.setContrast(map(brightness, 1, 10, 0, 255));

  // boot screen
  drawBootScreen();
  delay(2000);

  // encoder
  pinMode(SW, INPUT_PULLUP);
  ESP32Encoder::useInternalWeakPullResistors = puType::none;
  encoder.attachSingleEdge(DT, CLK);
  encoder.setFilter(1023);
  encoder.setCount(0);

  // sd card
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, SPI, 16000000)) { 
  Serial.println("SD Card Mount Failed!");
  return;
  }

  Serial.println("SD SUCCESS");
  sdInitialized = true;
  scanSongs();
  loadPlaylists();
  // printing size and files on the card
  float cardSizeGB = (float)SD.cardSize() / (1024.0 * 1024.0 * 1024.0);

  Serial.print("Card size: ");
  Serial.print(cardSizeGB, 2);
  Serial.println(" GB");

  Serial.println("Files on card:");
  listDir(SD, "/", 1);

  // for audio
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(map(volume, 0, 100, 0, 21));

  randomSeed(micros()); // for shuffle functionality

}





//__________________________________________________________________________________________________________loop

void loop() {
  audio.loop();

  updateBatteryLevel();

  static unsigned long songEndTimer = 0;

  // If the user clicked play (isPlaying == true), but the DAC has stopped streaming:
  if (isPlaying && !isPaused && !audio.isRunning()) {
    if (songEndTimer == 0) {
      songEndTimer = millis(); // start a short debounce timer
    } 
    // Wait 300ms to make sure it's an actual track end (not a brief startup buffer pause)
    else if (millis() - songEndTimer > 300) {
      songEndTimer = 0;
      Serial.println("Track finished naturally! Loading next song...");
      playNextSong(); 
    }
  } else {
    songEndTimer = 0; // Reset timer while track is actively playing
  }

  // rotation detection
  long position = encoder.getCount();
  int oldSelection = selectedItem;
  
  if (currentScreen == SETTINGS && selectedItem == 0 && editingBrightness) {

    if (position > 0) {
      brightness++;
      brightness = constrain(brightness, 1, 10);
      u8g2.setContrast(map(brightness, 1, 10, 0, 255));
      encoder.setCount(0);
      screenNeedsUpdate = true;
    }

    else if (position < 0) {
      brightness--;
      brightness = constrain(brightness, 1, 10);
      u8g2.setContrast(map(brightness, 1, 10, 0, 255));
      encoder.setCount(0);
      screenNeedsUpdate = true;
    }
  }
  else if (currentScreen == SETTINGS && selectedItem == 1 && editingVolume) {

    if (position > 0) {
      volume = constrain(volume + 1, 0, 100);
      audio.setVolume(map(volume, 0, 100, 0, 21));
      encoder.setCount(0);
      screenNeedsUpdate = true;
    }

    else if (position < 0) {
      volume = constrain(volume - 1, 0, 100);
      audio.setVolume(map(volume, 0, 100, 0, 21));
      encoder.setCount(0);
      screenNeedsUpdate = true;
    }
  }
  else if (currentScreen == SETTINGS && selectedItem == 2 && editingFont) {

    if (position > 0) {
      font++;
      font = constrain(font, 1, 5);
      encoder.setCount(0);
      screenNeedsUpdate = true;
    }

    else if (position < 0) {
      font--;
      font = constrain(font, 1, 5);
      encoder.setCount(0);
      screenNeedsUpdate = true;
    }
  }
  else if (currentScreen == SETTINGS && selectedItem == 3 && editingShuffle) {

    if (position > 0) {
      shuffle = true;
      encoder.setCount(0);
      screenNeedsUpdate = true;
    }

    else if (position < 0) {
      shuffle = false;
      encoder.setCount(0);
      screenNeedsUpdate = true;
    }
  }
  else if (currentScreen == SETTINGS && selectedItem == 4 && editingRepeat) {
    if (position > 0) {
      repeatMode++;
      if (repeatMode > 2) repeatMode = 0; // wrap around to OFF
      encoder.setCount(0);
      screenNeedsUpdate = true;
    } 
    else if (position < 0) {
      repeatMode--;
      if (repeatMode < 0) repeatMode = 2; // wrap around to ONE
      encoder.setCount(0);
      screenNeedsUpdate = true;
    }
    
    // Sync UI state to your underlying logic booleans
    repeatPlaylist = (repeatMode == 1);
    repeatSong = (repeatMode == 2);
  }
  else if (currentScreen == NOWPLAYING && selectedItem == 3 && editingNowPlayingVolume) {
  if (position > 0) {
    volume = constrain(volume + 1, 0, 100);
    audio.setVolume(map(volume, 0, 100, 0, 21));
    encoder.setCount(0);
    screenNeedsUpdate = true;
  } else if (position < 0) {
    volume = constrain(volume - 1, 0, 100);
    audio.setVolume(map(volume, 0, 100, 0, 21));
    encoder.setCount(0);
    screenNeedsUpdate = true;
  }
}
  else {
    if (position > 0) {

      selectedItem--;
      encoder.setCount(0);
      Serial.print("Selected: ");
      Serial.println(selectedItem);

    }

    else if (position < 0) {

      selectedItem++;
      encoder.setCount(0);
      Serial.print("Selected: ");
      Serial.println(selectedItem);

    }

  }

  selectedItem = constrain(selectedItem, 0, getCurrentScreenItemCount() - 1);

  if (selectedItem < topVisibleItem) {
    topVisibleItem = selectedItem;
  }

  if (selectedItem >= topVisibleItem + 4) {
    topVisibleItem = selectedItem - 3;
  }

  if (selectedItem != oldSelection) {
    screenNeedsUpdate = true;
  }
  
  // general button inital press
  if (!buttonPressed && digitalRead(SW) == LOW && (millis() - lastDebounceTime > 250)) {

    buttonPressed = true;
    longPressHandled = false;
    buttonPressTime = millis();

  }

  // button in held position
  if (buttonPressed && !longPressHandled && millis() - buttonPressTime >= LONG_PRESS_TIME && digitalRead(SW) == LOW) {
    
    currentScreen = MENU;
    selectedItem = 0;
    topVisibleItem = 0;
    screenNeedsUpdate = true;
    longPressHandled = true;

    //exiting when editing a setting
    editingBrightness = false;
    editingVolume = false;
    editingFont = false;
    editingShuffle = false;
    editingRepeat = false;

    // exiting now playing
    editingNowPlayingVolume = false; 
    
  
  }

  // button released
  if(buttonPressed && digitalRead(SW) == HIGH) {
    if (millis() - lastDebounceTime > 50) { // Non-blocking debounce
      buttonPressed = false;
      lastDebounceTime = millis();


      if (!longPressHandled) {

        if (currentScreen == MENU) {
        
          if (isPlaying) {

            switch (selectedItem) {

              case 0:
                currentScreen = NOWPLAYING;
                break;

              case 1:
                currentScreen = ALLSONGS;
                break;

              case 2:
                currentScreen = PLAYLISTS;
                break;

              case 3:
                currentScreen = SETTINGS;
                break;

              case 4:
                currentScreen = ABOUT;
                break;
            }
            selectedItem = 0;
            topVisibleItem = 0;
            screenNeedsUpdate = true;
          }
          else {

            switch (selectedItem) {

              case 0:
                currentScreen = ALLSONGS;
                break;

              case 1:
                currentScreen = PLAYLISTS;
                break;

              case 2:
                currentScreen = SETTINGS;
                break;

              case 3:
                currentScreen = ABOUT;
                break;
            }
            selectedItem = 0;
            topVisibleItem = 0;
            screenNeedsUpdate = true;
          }
        }

        else if (currentScreen == ALLSONGS) {

          currentPlaylist = "";
          playingFromPlaylist = false;
          currentSongIndex = selectedItem;

          if (shuffle) {
            buildShuffleDeck(songCount);
    
            // swap chosen song to the front of the deck without deleting any other song
            for (int i = 0; i < songCount; i++) {
              if (shuffleDeck[i] == currentSongIndex) {
                shuffleDeck[i] = shuffleDeck[0];
                shuffleDeck[0] = currentSongIndex;
                break;
              }
            }
          }

          playSong(songNames[selectedItem]);

          currentScreen = NOWPLAYING;

          selectedItem = 0;
          topVisibleItem = 0;

          screenNeedsUpdate = true;

        }
        else if (currentScreen == PLAYLISTS) {

          currentPlaylist = playlistNames[selectedItem];

          startPlaylist(currentPlaylist);

          currentScreen = NOWPLAYING;

          selectedItem = 0;
          topVisibleItem = 0;

          screenNeedsUpdate = true;

        }
        else if (currentScreen == SETTINGS) {

          switch (selectedItem){
          
            case 0:

              editingVolume = false;
              editingFont = false;
              editingShuffle = false;
              editingRepeat = false;
              editingBrightness = !editingBrightness;
              screenNeedsUpdate = true;
              break;

            case 1:

              editingBrightness = false;
              editingFont = false;
              editingShuffle = false;
              editingRepeat = false;
              editingVolume = !editingVolume;
              screenNeedsUpdate = true;
              break;

            case 2:

              editingBrightness = false;
              editingVolume = false;
              editingShuffle = false;
              editingRepeat = false;
              editingFont = !editingFont;
              screenNeedsUpdate = true;
              break;

            case 3:

              editingBrightness = false;
              editingVolume = false;
              editingFont = false;
              editingRepeat = false;
              editingShuffle = !editingShuffle;
              screenNeedsUpdate = true;
              break;

            case 4:

              editingBrightness = false;
              editingVolume = false;
              editingFont = false;
              editingShuffle = false;
              editingRepeat = !editingRepeat;
              screenNeedsUpdate = true;
              break;
          }
        }
        else if (currentScreen == NOWPLAYING) {

          if (selectedItem == 3) { // volume option clicked
            editingNowPlayingVolume = !editingNowPlayingVolume;
            screenNeedsUpdate = true;
          } 
          else if (!editingNowPlayingVolume) {
            switch (selectedItem) {
              case 0: // rewind / previous
                playPreviousSong();
                break;

              case 1: // play/pause toggle
                audio.pauseResume();
                isPaused = !isPaused;
                screenNeedsUpdate = true;
                break;

              case 2: // skip
                playNextSong();
                break;
            }
          }
        }
      }
    }
  }
  if (currentScreen == NOWPLAYING) {
      
    String displayName = removeExtension(currentSong);

    if (millis() - lastScrollTime > scrollDelay) {

      scrollPosition++;

      if (scrollPosition >= displayName.length()+5) {
        scrollPosition = 0;
      }

      lastScrollTime = millis();
      screenNeedsUpdate = true;

    }
  }
  // update screen
  if (screenNeedsUpdate) {
    drawCurrentScreen();
    screenNeedsUpdate = false;
  }

  

}

//__________________________________________________________________________________________________________functions



// drawing functions

void drawMenu() {

  u8g2.clearBuffer();

  setCurrentFont();
  u8g2.drawStr(0, 12, "MENU");
  u8g2.drawLine(0, 14, 128, 14);

  displayBatteryPercentage();

  const char* displayedMenuItems[5];

  int itemCount = 0;

  if (isPlaying) {
    displayedMenuItems[itemCount++] = "Now Playing";
  }

  displayedMenuItems[itemCount++] = "All Songs";
  displayedMenuItems[itemCount++] = "Playlists";
  displayedMenuItems[itemCount++] = "Settings";
  displayedMenuItems[itemCount++] = "About";

  for (int i = 0; i < 4; i++) {

    int itemIndex = topVisibleItem + i;

    if (itemIndex >= itemCount) {
      break;
    }

    if (itemIndex == selectedItem) {
      u8g2.drawStr(0, 24 + i * 12, ">");
    }

    u8g2.drawStr(10, 24 + i * 12, displayedMenuItems[itemIndex]);
  }

  u8g2.sendBuffer();

}

void drawAllSongs() {

  u8g2.clearBuffer();

   // for continuous audio data

  setCurrentFont();
  u8g2.drawStr(0, 12, "ALL SONGS");
  u8g2.drawLine(0, 14, 128, 14);

  displayBatteryPercentage();

  for (int i = 0; i < 4; i++) {
    int itemIndex = topVisibleItem + i;

    if (itemIndex >= getCurrentScreenItemCount()) {
      break;
    }

    String displayName = formatSongName(songNames[itemIndex]);
  
    if (itemIndex == selectedItem) {
      u8g2.drawStr(0, 24 + i * 12, ">");
    }

    u8g2.drawStr(10, 24 + i * 12, displayName.c_str());

  }

  u8g2.sendBuffer();

}

void drawPlaylists() {

  u8g2.clearBuffer();

  setCurrentFont();
  u8g2.drawStr(0, 12, "PLAYLISTS");
  u8g2.drawLine(0, 14, 128, 14);

  displayBatteryPercentage();

  for (int i = 0; i < 4; i++) {
    int itemIndex = topVisibleItem + i;

    if (itemIndex >= getCurrentScreenItemCount()) {
      break;
    }

    String displayName = formatSongName(playlistNames[itemIndex]);

    if (itemIndex == selectedItem) {
      u8g2.drawStr(0, 24 + i * 12, ">");
    }

    u8g2.drawStr(10, 24 + i * 12, displayName.c_str());

  }

  u8g2.sendBuffer();

}

void drawSettings() {

  u8g2.clearBuffer();

  setCurrentFont();
  u8g2.drawStr(0, 12, "SETTINGS");
  u8g2.drawLine(0, 14, 128, 14);

  displayBatteryPercentage();

  char brightnessString[4];
  sprintf(brightnessString, "%d", brightness);
  char volumeString[4];
  sprintf(volumeString, "%d", volume);
  char fontString[4];
  sprintf(fontString, "%d", font);
  
  for (int i = 0; i < 4; i++) {
    int itemIndex = topVisibleItem + i;

    if (itemIndex >= getCurrentScreenItemCount()) {
      break;
    }

    if (itemIndex == selectedItem) {
      u8g2.drawStr(0, 24 + i * 12, ">");
    }

    u8g2.drawStr(10, 24 + i * 12, settingsItems[itemIndex]);

    if(itemIndex == 0) {
      u8g2.drawStr(90, 24 + i * 12, brightnessString);

      if (editingBrightness) {
        u8g2.drawFrame(85, 14+i*12, 25, 12);
      }
    }
    if (itemIndex == 1) {
      u8g2.drawStr(90, 24 + i * 12, volumeString);

      if (editingVolume) {
        u8g2.drawFrame(85, 14+i*12, 25, 12);
      }
    }
    if(itemIndex == 2) {
      u8g2.drawStr(90, 24 + i * 12, fontString);

      if (editingFont) {
        u8g2.drawFrame(85, 14+i*12, 25, 12);
      }
    }
    if (itemIndex == 3) {

      if (shuffle) {
          u8g2.drawStr(90, 24 + i * 12, "ON");
      } 
      else {
          u8g2.drawStr(90, 24 + i * 12, "OFF");
      }

      if (editingShuffle) {
          u8g2.drawFrame(85, 14 + i * 12, 25, 12);
      }
    }
    if (itemIndex == 4) {
      if (repeatMode == 0) {
        u8g2.drawStr(90, 24 + i * 12, "OFF");
      } else if (repeatMode == 1) {
        u8g2.drawStr(90, 24 + i * 12, "ALL");
      } else if (repeatMode == 2) {
        u8g2.drawStr(90, 24 + i * 12, "ONE");
      }
      
      if (editingRepeat) {
        u8g2.drawFrame(85, 14 + i * 12, 25, 12);
      }
    }
  }

  u8g2.sendBuffer();

}

void drawAbout() {

  u8g2.clearBuffer();

  setCurrentFont();
  u8g2.drawStr(0, 12, "ABOUT");
  u8g2.drawLine(0, 14, 128, 14);

  displayBatteryPercentage();

  for (int i = 0; i < 4; i++) {
    u8g2.drawStr(10, 28 + i * 12, aboutItems[i]);
  }

  u8g2.sendBuffer();

}

void drawNowPlaying() {

  u8g2.clearBuffer();

  setCurrentFont();
  u8g2.drawStr(0, 12, "NOW PLAYING");
  u8g2.drawLine(0, 14, 128, 14);

  displayBatteryPercentage();

  u8g2.drawStr(78, 59, "vol:");
  char volumeString[4];
  sprintf(volumeString, "%d", volume);
  u8g2.drawStr(103, 60, volumeString);


  uint32_t currentSec = audio.getAudioCurrentTime();  // Elapsed time
  uint32_t totalSec   = audio.getAudioFileDuration(); // Total track duration

  String timeString = formatTime(currentSec) + "|" + formatTime(totalSec);

  if (playingFromPlaylist) {
    String displayedPlaylist = getScrollingText(removeExtension(currentPlaylist));
    u8g2.drawStr(2, 26, "playlist: ");
    u8g2.drawStr(56, 26, displayedPlaylist.c_str());
  }
  
  String displayedSong = getScrollingText(removeExtension(currentSong));
  u8g2.drawStr(2, 38, displayedSong.c_str());
  u8g2.drawStr(2, 49, timeString.c_str());
  u8g2.drawXBMP(3, 52, 34, 7, playpauseskip);

  switch (selectedItem) {
    case 0: 
      // Box around Rewind (|<<)
      u8g2.drawFrame(1, 51, 13, 9);
      break;

    case 1: 
      // Box around Play/Pause (>||)
      u8g2.drawFrame(14, 51, 12, 9);
      break;

    case 2: 
      // Box around Skip (>>|)
      u8g2.drawFrame(26, 51, 13, 9);
      break;

    case 3: 
      // Box around Volume (vol: XX)
      u8g2.drawLine(78, 61, 98, 61);
      
      // Adds a double frame when actively turning the knob to change volume
      if (editingNowPlayingVolume) { 
        u8g2.drawFrame(100, 51, 22, 11);
      }
      break;
  }

  if (shuffle) {
    u8g2.drawXBMP(70, 5, 8, 7, shuffleicon);
  }

  if(repeatSong || repeatPlaylist) {
    u8g2.drawXBMP(81, 5, 10, 7, repeaticon);
  }

  u8g2.sendBuffer();
  
}

void drawCurrentScreen() {

    switch(currentScreen) {

        case MENU:
          drawMenu();
          break;

        case ALLSONGS:
          drawAllSongs();
          break;

        case PLAYLISTS:
          drawPlaylists();
          break;
        
        case SETTINGS:
          drawSettings();
          break;

        case ABOUT:
          drawAbout();
          break;
        
        case NOWPLAYING:
          drawNowPlaying();
          break;
    }

}

void drawBootScreen() {

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tf);

  u8g2.drawStr(25, 20, "MP3 PLAYER");
  u8g2.drawStr(15, 36, "Version 1.0");
  u8g2.drawStr(30, 52, "Loading...");

  u8g2.sendBuffer();
}

//__________________________________________________sd card/audio functions

void scanSongs() {
  songCount = 0;
  File songsFolder = SD.open("/");

  if(!songsFolder){
    Serial.println("Songs folder missing!");
    return;
  }

  while (true) {
    File file = songsFolder.openNextFile();
    if (!file) break;

    if (!file.isDirectory()) {
      String fileName = String(file.name());

      if (fileName.startsWith("/")) {
        fileName = fileName.substring(1); // Strips leading slash
      }

      // IGNORE HIDDEN SYSTEM FILES (._ files) & FILTER MP3s
      if (!fileName.startsWith(".") && fileName.endsWith(".mp3")) {
        if (songCount < MAX_SONGS) {
          songNames[songCount] = fileName;
          songCount++;
        }
      }
    }
    file.close();
  }
  songsFolder.close();
}

void loadPlaylists() {
  playlistCount = 0;
  File playlistFolder = SD.open("/");

  if(!playlistFolder){
    Serial.println("Playlists folder missing!");
    return;
  }

  while(true) {
    File file = playlistFolder.openNextFile();
    if(!file) break;

    if(!file.isDirectory()) {
      String fileName = String(file.name());

      if (fileName.startsWith("/")) {
        fileName = fileName.substring(1);
      }

      // IGNORE HIDDEN FILES & FILTER TXTs
      if (!fileName.startsWith(".") && fileName.endsWith(".txt")) {
        if (playlistCount < MAX_PLAYLISTS) {
          playlistNames[playlistCount] = fileName;
          playlistCount++;
        }
      }
    }
    file.close();
  }
  playlistFolder.close();
}

void loadPlaylistSongs(String playlistName) {
  playlistSongCount = 0;
  String filePath = "/" + playlistName;

  File playlistFile = SD.open(filePath.c_str());
  if (!playlistFile) {
    Serial.println("Playlist failed to open!");
    return;
  }

  while (playlistFile.available()) {
    String line = playlistFile.readStringUntil('\n');
    line.trim();

    if (line.length() > 0 && playlistSongCount < MAX_PLAYLIST_SONGS) {
      playlistSongs[playlistSongCount] = line;
      playlistSongCount++;
    }
  }

  playlistFile.close();
}

void startPlaylist(String playlistName) {
  loadPlaylistSongs(playlistName);

  if (playlistSongCount == 0) {
    Serial.println("Playlist is empty or file missing!");
    return;
  }

  playingFromPlaylist = true;

  if (shuffle) {
    buildShuffleDeck(playlistSongCount);
    currentPlaylistIndex = shuffleDeck[0]; // Draw first song from shuffled deck
  } else {
    currentPlaylistIndex = 0;
  }

  playSong(playlistSongs[currentPlaylistIndex]);
}

void playSong(String songName){
  audio.stopSong();

  Serial.print("--- BUTTON CLICKED! PLAYING: ");
  Serial.println(songName);

  currentSong = songName;
  scrollPosition = 0;
  lastScrollTime = millis();
  isPlaying = true;
  isPaused = false;
  
  screenNeedsUpdate = true;

  String filePath = "/" + songName;
  audio.connecttoFS(SD, filePath.c_str());

}

void playNextSong() {

  static unsigned long lastSkipTime = 0;
  if (millis() - lastSkipTime < 500) return; // prevent skipping faster than 500ms
  lastSkipTime = millis();
  // single song on repeat
  if (repeatSong) {
    playSong(currentSong);
    return;
  }

  // playing from a playlist
  if (playingFromPlaylist) {
    if (playlistSongCount == 0) return;

    if (shuffle && playlistSongCount > 1) {
      shuffleIndex++;
      
      // deck finished
      if (shuffleIndex >= playlistSongCount) {
        if (repeatPlaylist) {
          buildShuffleDeck(playlistSongCount); // reshuffle for next pass
        } 
        else {
          isPlaying = false; // stop audio at end of playlist
          audio.stopSong();
          screenNeedsUpdate = true;
          return;
        }
      }
      currentPlaylistIndex = shuffleDeck[shuffleIndex];

    } 
    else { // sequential playback
      currentPlaylistIndex++;
      if (currentPlaylistIndex >= playlistSongCount) {
        if (repeatPlaylist) {
          currentPlaylistIndex = 0;
        } 
        else {
          isPlaying = false;
          audio.stopSong();
          screenNeedsUpdate = true;
          return;
        }
      }
    }

    playSong(playlistSongs[currentPlaylistIndex]);

  } 
  // playing from all songs
  else {
    if (songCount == 0) return;

    if (shuffle && songCount > 1) {
      shuffleIndex++;

      // deck finished
      if (shuffleIndex >= songCount) {
        if (repeatPlaylist) {
          buildShuffleDeck(songCount); // reshuffle for another pass
        } 
        else {
          isPlaying = false; // stops audio when all songs finish
          return;
        }
      }
      currentSongIndex = shuffleDeck[shuffleIndex];
    }
    else { // sequential playback for all songs
      currentSongIndex++;
      if (currentSongIndex >= songCount) {
        if (repeatPlaylist) {
          currentSongIndex = 0;
        } 
        else {
          isPlaying = false;
          return;
        }
      }
    }
    playSong(songNames[currentSongIndex]);
  }
}

void playPreviousSong() {
  // if song has been playing for more than 3 sec, then restart song
  if (audio.getAudioCurrentTime() > 3) {
    playSong(currentSong);
    return;
  }

  if (playingFromPlaylist) {
    if (playlistSongCount == 0) return;

    if (shuffle && playlistSongCount > 1) {
      if (shuffleIndex > 0) {
        shuffleIndex--;
      } else {
        shuffleIndex = playlistSongCount - 1; // Wrap to end of deck
      }
      currentPlaylistIndex = shuffleDeck[shuffleIndex];
    } else {
      if (currentPlaylistIndex > 0) {
        currentPlaylistIndex--;
      } else {
        currentPlaylistIndex = playlistSongCount - 1; // Wrap around
      }
    }
    playSong(playlistSongs[currentPlaylistIndex]);

  } else { // All Songs mode
    if (songCount == 0) return;

    if (shuffle && songCount > 1) {
      if (shuffleIndex > 0) {
        shuffleIndex--;
      } else {
        shuffleIndex = songCount - 1; // Wrap to end of deck
      }
      currentSongIndex = shuffleDeck[shuffleIndex];
    } else {
      if (currentSongIndex > 0) {
        currentSongIndex--;
      } else {
        currentSongIndex = songCount - 1; // Wrap around
      }
    }
    playSong(songNames[currentSongIndex]);
  }
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    Serial.println("Failed to open directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

String removeExtension(String name) {

  if(name.indexOf('.') != -1){
    name.remove(name.lastIndexOf('.'));
  }

  return name;

}

String fitToScreen(String name) {

  if (name.length() > 16) {

    name = name.substring(0,13);
    name += "...";
  }

  return name;

}

String formatSongName(String name) {

  name = removeExtension(name);

  name = fitToScreen(name);
  
  return name;

}


//______________________________________________misc functions

void setCurrentFont() {

  switch (font) {

    case 1:
      u8g2.setFont(u8g2_font_6x12_tf);
      break;

    case 2:
      u8g2.setFont(u8g2_font_5x8_tf);
      break;

    case 3:
      u8g2.setFont(u8g2_font_t0_12_tf);
      break;

    case 4:
      u8g2.setFont(u8g2_font_7x13_tf);
      break;

    case 5:
      u8g2.setFont(u8g2_font_haxrcorp4089_tr);
      break;
  }
}

int getCurrentScreenItemCount(){
  switch (currentScreen) {

    case MENU:
      if (isPlaying) {
        return 5;
      }
      else {
        return 4;
      }

    case ALLSONGS:
      return songCount;

    case PLAYLISTS:
      return playlistCount;

    case SETTINGS:
      return sizeof(settingsItems) / sizeof(settingsItems[0]);

    case ABOUT:
      return 1;

    case NOWPLAYING:
      return 4;

    default:
      return 0;
  }
}

String getScrollingText(String text){

  if(text.length() <= 20){
    return text;
  }

  String padded = text + "     " + text;

  return padded.substring(scrollPosition, scrollPosition + 20);

}

String formatTime(uint32_t totalSeconds) {
  uint32_t minutes = totalSeconds / 60;
  uint32_t seconds = totalSeconds % 60;
  
  char timeBuffer[10];
  // formats as M:SS 
  snprintf(timeBuffer, sizeof(timeBuffer), "%u:%02u", minutes, seconds); 
  
  return String(timeBuffer);
}

void buildShuffleDeck(int totalTracks) {

  for (int i = 0; i < totalTracks; i++) {
    shuffleDeck[i] = i;
  }

  // fisher-yates algorithm
  for (int i = totalTracks - 1; i > 0; i--) {
    int j = random(0, i + 1);
    int temp = shuffleDeck[i];
    shuffleDeck[i] = shuffleDeck[j];
    shuffleDeck[j] = temp;
  }

  shuffleIndex = 0;
}

void updateBatteryLevel() {
  if (millis() - lastBatteryCheck > 10000 || lastBatteryCheck == 0) {
    
    float percent = maxlipo.cellPercent();
    
    batteryPercentage = constrain((int)percent, 0, 100);
    
    float rate = maxlipo.chargeRate();
    if (rate > 0.5) {
      isCharging = true;
    } else {
      isCharging = false;
    }

    lastBatteryCheck = millis();
    screenNeedsUpdate = true; 
  }
}

void displayBatteryPercentage() {
  char batteryString[6];
  sprintf(batteryString, "%d%%", batteryPercentage);
  u8g2.drawStr(103, 11, batteryString);
  if (isCharging) {
    u8g2.drawStr(97, 12, "+");
  }
}
