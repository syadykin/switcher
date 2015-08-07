/*
 * Find device by service
 * $ avahi-browse -r _switcher._udp -p -t
 * 
 * Enable over wifi
 * $ echo -ne "\x00\x00\x00" | nc -4u -w0 192.168.78.206 3333
 * 
 * Enable over wifi for one second
 * $ echo -ne "\x00\x01\x00" | nc -4u -w0 192.168.78.206 3333
 * 
 * Enable over wifi for 256 seconds
 * $ echo -ne "\x00\x00\x01" | nc -4u -w0 192.168.78.206 3333
 * 
 * Disable over wifi
 * $ echo -ne "\xFF\x00\x00" | nc -4u -w0 192.168.78.206 3333
 * 
 * Connect using screen
 * $ screen /dev/ttyUSB0 115200
 */


#include <Ticker.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiClient.h>
#include <EEPROM.h>

#define GPIO0 0
#define GPIO2 2

// Local UDP port listen to
#define PORT 3333

#define NAME_LENGTH 16
char name[NAME_LENGTH + 1] = "switch";

#define SSID_LENGTH 32
char ssid[SSID_LENGTH + 1] = "ssid";

#define PASSWORD_LENGTH 64
char password[PASSWORD_LENGTH + 1] = "password";

// 4 is "NAME" literal size here
#define MEMSIZE 4 + NAME_LENGTH + SSID_LENGTH + PASSWORD_LENGTH

// must be 2 minutes buy default?
#define MDNS_TTL 120

WiFiUDP udp;
MDNSResponder mdns;
Ticker blinker;
Ticker announcer;

bool enabled = false;

uint8_t n;
char tmp[MEMSIZE];
bool wasConnected = false;

char cmdBuf[MEMSIZE];
int8_t cmdPos = 0;

bool readConfig() {
  for (n = 0; n < MEMSIZE; n++)
    tmp[n] = EEPROM.read(n);

  if (strstr(&tmp[0], "NAME") != NULL) {
    strncpy(name, &tmp[4], NAME_LENGTH);
    strncpy(ssid, &tmp[4 + NAME_LENGTH], SSID_LENGTH);
    strncpy(password, &tmp[4 + NAME_LENGTH + SSID_LENGTH], PASSWORD_LENGTH);
    return true;
  }
  return false;
}

void printConfig() {
  Serial.print("NAME ");
  Serial.println(name);
  Serial.print("SSID ");
  Serial.println(ssid);
  Serial.print("PASS ");
  Serial.println(password);
}

void writeConfig() {
  strcpy(&tmp[0], "NAME");
  strcpy(&tmp[4], name);
  strcpy(&tmp[4 + NAME_LENGTH], ssid);
  strcpy(&tmp[4 + NAME_LENGTH + SSID_LENGTH], password);
  for (n = 0; n < MEMSIZE; n++)
    EEPROM.write(n, tmp[n]);
  EEPROM.commit();
  Serial.println("OK");
}

bool state = false;
void blink() {
  Serial.print(".");
  digitalWrite(GPIO0, state);
  state = !state;
}

void announce() {
  mdns.update();
}

char b;
bool nl;

void readSerial(uint8_t size) {
  for (n = 0; n < size; n++) {

    // buffer overflow — reset command and wait for \n

    if (cmdPos >= MEMSIZE) cmdPos = -1;
    b = Serial.read();
    // local echo
    // Serial.print(b);
    nl = b == '\r' || b == '\n';
    
    if (nl && cmdPos != -1) {
      cmdBuf[cmdPos] = 0;
      // parse string and run commands

      if (!strncmp(cmdBuf, "NAME ", 5) && strlen(cmdBuf) > 5) {
        strcpy(name, &cmdBuf[5]);
      } else if (!strncmp(cmdBuf, "SSID ", 5) && strlen(cmdBuf) > 5) {
        strcpy(ssid, &cmdBuf[5]);
      } else if (!strncmp(cmdBuf, "PASS ", 5)) {
        strcpy(password, &cmdBuf[5]);
      } else if (!strcmp(cmdBuf, "PASS")) {
        password[0] = 0;
      } else if (!strcmp(cmdBuf, "RESET")) {
        ESP.restart();
      } else if (!strcmp(cmdBuf, "WRITE")) {
        writeConfig();
      } else if (!strcmp(cmdBuf, "READ")) {
        readConfig();
      } else if (!strcmp(cmdBuf, "PRINT")) {
        printConfig();
      } else if (!strcmp(cmdBuf, "ON")) {
        toggle(true, 0);
      } else if (!strcmp(cmdBuf, "OFF")) {
        toggle(false, 0);
      } else if (!strcmp(cmdBuf, "STATUS")) {
        status();
      } else {
        Serial.println("ERR unknown command");
        Serial.println("# Available commands are:");
        Serial.println("# 'ON' enable switch");
        Serial.println("# 'OFF' disable switch");
        Serial.println("# 'STATUS' prints module status");
        Serial.println("# 'NAME <name>' set zeroconf host name");
        Serial.println("# 'SSID <ssid>' set SSID AP name connect to");
        Serial.println("# 'PASS' clear wifi password");
        Serial.println("# 'PASS <pass>' set wifi password");
        Serial.println("# 'READ' read config from EEPROM");
        Serial.println("# 'WRITE' write config to EEPROM");
        Serial.println("# 'PRINT' print config");
        Serial.println("# 'RESET' module reboot");
      }

      cmdBuf[0] = 0;
      cmdPos = 0;
    } else if (nl && cmdPos == -1) {
      // looong command completed, start to fill buffer again
      cmdPos = 0;
    } else if (cmdPos != -1) {
      cmdBuf[cmdPos++] = b;
    }
  }
}

void status() {
  if (enabled) {
    Serial.println("ON");
  } else {
    Serial.println("OFF");
  }
}

uint64_t time;
void toggle(bool value, uint16_t tm) {
  if (tm != 0) time = millis() + tm * 1000;
  else time = 0;
  enabled = value;
  // indication LED
  digitalWrite(GPIO0, enabled);
  digitalWrite(GPIO2, !enabled);
  // reset timer for further use
  if (!enabled) time = 0;
  status();
}

//uint16_t fh;
//Ticker heaper;
//void heap() {
//  fh = ESP.getFreeHeap();
//  Serial.print("HEAP ");
//  Serial.println(fh);
//}

void setup() {
  // set to off both GPIO
  pinMode(GPIO0, OUTPUT);
  digitalWrite(GPIO0, LOW);
  pinMode(GPIO2, OUTPUT);
  digitalWrite(GPIO2, HIGH);

  EEPROM.begin(MEMSIZE + 1);
  Serial.begin(115200);

  // read config from EEPROM
  readConfig();

  toggle(false, 0);

//  heaper.attach(60, heap);
}

int read;
bool connecting = false;

void loop() {
  // WiFi is disconnected or connecting
  if (WiFi.status() != WL_CONNECTED) {
    // not yet connection attempt made
    if (!connecting) {
      if (strlen(password) == 0) WiFi.begin(ssid);
      else WiFi.begin(ssid, password);
  
      // stop all udp we have
      udp.stopAll();
  
      // detach mdns announce
      announcer.detach();
  
      // attach status indicator
      blinker.attach(0.25, blink);
  
      connecting = true;
    }

  // WiFi is connected but status is still connecting
  // means we need run dependent services
  } else if (connecting) {
    // start listening
    udp.begin(PORT);

    if (!mdns.begin(name, WiFi.localIP(), MDNS_TTL)) {
      Serial.println("ERR cannot start mDNS");
    } else {
      mdns.addService("switcher", "udp", PORT);
      announce();
      announcer.attach(MDNS_TTL, announce);
    }

    blinker.detach();
    connecting = false;

  // WiFi is connected and services up and running
  } else {
    read = udp.parsePacket();
    if (read == 3) {
      udp.read(tmp, read);
      if (tmp[0] == 0x00) {
        toggle(true, tmp[1] | tmp[2] << 8);
      } else if (tmp[0] == 0xFF) {
        toggle(false, 0);
      }
    }
    if (read) udp.flush();
  }

  read = Serial.available();
  if (read != 0) readSerial(read);
  if (time != 0 && time < millis()) toggle(false, 0);
}

