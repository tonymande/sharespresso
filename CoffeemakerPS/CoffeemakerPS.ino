/* 
 Sharespresso by Thank-The-Maker.org
 
 is an Arduino-based RFID payment system for coffeemakers with toptronic logic unit, as Jura 
 Impressa S95 and others without modifying the coffeemaker itself. 

 Based on Oliver Krohns famous Coffeemaker-Payment-System 
 at https://github.com/oliverk71/Coffeemaker-Payment-System

 and 

 Sharespresso by c't/Peter Siering
 at https://www.heise.de/ct/artikel/Sharespresso-NFC-Bezahlsystem-fuer-Kaffeevollautomaten-3058350.html
 
 Hardware used: 
  - SparkFun ESP8266 Thing - Dev Board
  - AZDelivery 3er Set 128 x 64 Pixel 0,96 Zoll OLED Display
  - pn532/mfrc522 rfid card reader (13.56MHz), 
  - HC-05 bluetooth, male/female jumper wires (optional: ethernet shield, buzzer, button)
 
 The code is provided 'as is', without any guarantuee. Use at your own risk! 
*/

// needed for conditional includes to work, don't ask why ;-)
char trivialfix;

#ifndef LOGGER_H_
  #include "logging.h";
#endif
#ifndef SETTINGS_H_
 #include "settings.h"
#endif
#ifndef EEPROMCONFIG_H_
 #include "eepromconfig.h";
#endif
#include "juragigax8.h";
#include "mqtt.h";
#include "nfcreader.h";
#include "ble.h";
#include "otaupdate.h";


// options to include into project
#define DEBUG 1 // some more logging
// coffemaker model
//#define X7 1 // x7/saphira
#define S95 1

#include <Wire.h>
#include <SPI.h>

// product codes send by coffeemakers "?PA<x>\r\n", just <x>
#if defined(S95)
char products[] = "EFABJIG";
#endif
#if defined(X7)
char products[] = "ABCHDEKJFG";
#endif

// general variables (used in loop)
boolean buttonPress = false;
String BTstring=""; // contains what is received via bluetooth (from app or other bt client)
unsigned long actTime; // timer for RFID etc
unsigned long buttonTime; // timer for button press 
boolean override = false;  // to override payment system by the voice-control/button-press app
unsigned long RFIDcard = 0;
int price=0;
String last_product="";
pricelist_t pricelist;
cardlist_t cardlist;

Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

OledDisplay oled;
MqttService mqttService;
Buzzer buzzer;
NfcReader nfcReader(nfc, oled, buzzer);
JuraGigaX8 coffeemaker(oled, buzzer);
BleConnection bleConnection;
CoffeeLogger logger;
EEPROMConfig eepromConfig;
OTAUpdate update(oled);

void setup() {
#if defined(SERLOG) || defined(DEBUG)
  Serial.begin(9600);
#endif
#if defined(DEBUG)
  logger.serlog("number of products: " + String(sizeof(products)));
#endif
  logger.serlog("initializing OLED");
  oled.initOled();
  
  oled.message_print(F("sharespresso"), F("starting up"), 0);
  coffeemaker.initCoffeemaker();         // start serial communication at 9600bps

  logger.serlog(F("initializing bluetooth module"));
  bleConnection.initBle();

  // initialized rfid lib
#if defined(DEBUG)
  logger.serlog(F("initializing rfid reader"));
#endif
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
#if defined(DEBUG)
    logger.serlog(F("didn't find PN53x board"));
#endif
  }
  // configure board to read RFID tags and cards
  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(0xfe);

  logger.serlog("reading pricelist from EEPROM");
  pricelist = eepromConfig.readPricelist();
  for(int i=0; i<10; i++) {
    String message = "price for product[" + String(i) + "]: " + String(pricelist.prices[i]);
      logger.serlog(message);
  }

  cardlist = eepromConfig.readCards();
  for(int i=0; i<MAX_CARDS; i++) {
    String message = "registered card[" + String(i) + "]: " + String(cardlist.cards[i].card);
      logger.serlog(message);
  }

  mqttService.setup_wifi();
  mqttService.initMqtt(mqttCallback);
  oled.message_print(F("Ready to brew"), F(""), 2000);
  // activate coffemaker connection and inkasso mode
//  myCoffeemaker.listen();
  coffeemaker.inkasso_on();
}

void loop() {  
    mqttService.loopMqtt();
 
  // Check if there is a bluetooth connection and command
  // handle serial and bluetooth input
  BTstring = bleConnection.readCommand();

  while( Serial.available() ){  
    BTstring += String(char(Serial.read()));
    delay(7);  
  }
  BTstring.trim();
  
  if (BTstring.length() > 0){
    executeCommand(BTstring);   
  }          

  // Get key pressed on coffeemaker
  String message = coffeemaker.fromCoffeemaker();   // gets answers from coffeemaker 
  if (message.length() > 0){
    logger.serlog( message);
    if (message.charAt(0) == '?' && message.charAt(1) == 'P'){     // message starts with '?P' ?
      buttonPress = true;
      buttonTime = millis();
      int product = 255;
      for (int i = 0; i < sizeof(products); i++) {
        if (message.charAt(3) == products[i]) {
          product = i;
          break;
        }
      }
      if ( product != 255) {
        String productname;
          switch (product) {
#if defined(S95)
            case 0: productname = F("Small cup"); break;
            case 1: productname = F("2 small cups"); break;
            case 2: productname = F("Large cup"); break;
            case 3: productname = F("2 large cups"); break;
            case 4: productname = F("Steam 2"); break;
            case 5: productname = F("Steam 1"); break;
            case 6: productname = F("Extra large cup"); break;
#endif
#if defined(X7)
            case 0: productname = F("Cappuccino"); break;
            case 1: productname = F("Espresso"); break;
            case 2: productname = F("Espresso dopio"); break;
            case 3: productname = F("Milchkaffee"); break;
            case 4: productname = F("Kaffee"); break;
            case 5: productname = F("Kaffee gross"); break;
            case 6: productname = F("Dampf links"); break;
            case 7: productname = F("Dampf rechts"); break;
            case 8: productname = F("Portion Milch"); break;
            case 9: productname = F("Caffee Latte"); break;
#endif
          }
        price = pricelist.prices[product];
        last_product= String(message.charAt( 3))+ "/"+ String(product)+ " ";
        oled.message_print(productname, logger.printCredit(price), 0);
      } 
      else {
        oled.message_print(F("Error unknown"), F("product"), 2000);
        buttonPress = false;
      }
      // boss mode, he does not pay
      if (override == true){
        price = 0;
      }
    }
  }
  // User has five seconds to pay
  if (buttonPress == true) {
    if (millis()-buttonTime > 5000){  
      buttonPress = false;
      price = 0;
      last_product = "";
      oled.message_clear();
    }
  }
  if (buttonPress == true && override == true){
    coffeemaker.toCoffeemaker("?ok\r\n");
    buttonPress == false;
    override == false;
  }
  // RFID Identification      
  RFIDcard = 0;  
  actTime = millis(); 
  do {
    RFIDcard = nfcReader.nfcidread();
    if (RFIDcard == MASTERCARD) {
      coffeemaker.servicetoggle();
      delay(60);
      RFIDcard= 0;
    }
    if (RFIDcard != 0) {
      oled.message_clear();
      break; 
    }           
  } 
  while ( (millis()-actTime) < 60 );  

  if (RFIDcard != 0){
    int k = MAX_CARDS;
    for(int i=0;i<MAX_CARDS;i++){         
      if (((RFIDcard) == (cardlist.cards[i].card)) && (RFIDcard != 0 )){
        k = i;
        int credit = eepromConfig.readCredit(k*6+4);
        if(buttonPress == true){                 // button pressed on coffeemaker?
           if ((credit - price) > 0) {
            oled.message_print(logger.print10digits(RFIDcard), logger.printCredit(credit), 0);
            eepromConfig.updateCredit(k*6+4, ( credit- price));
            coffeemaker.toCoffeemaker("?ok\r\n");            // prepare coffee
            buttonPress= false;
            price= 0;
            last_product= "";
          } 
          else {
            buzzer.beep(2);
            oled.message_print(logger.printCredit(credit), F("Not enough"), 2000); 
          }
        } 
        else {                                // if no button was pressed on coffeemaker / check credit
          oled.message_print(logger.printCredit(credit), F("Remaining credit"), 2000);
        }
        i = MAX_CARDS;      // leave loop (after card has been identified)
      }      
    }
    if (k == MAX_CARDS){ 
      k=0; 
      buzzer.beep(2);
      oled.message_print(String(logger.print10digits(RFIDcard)),F("card unknown!"),2000);
    }           
  }
}

void executeCommand(String command) {
    // BT: Start registering new cards until 10 s no valid, unregistered card
#if defined(DEBUG)
    logger.serlog(command);
#endif
    if( command == "RRR" ){          
      actTime = millis();
      buzzer.beep(1);
      oled.message_print(F("Registering"),F("new cards"),0);
      mqttService.publish("Registering new cards");
      nfcReader.registernewcards();
      mqttService.publish("Registering ended");
      oled.message_clear();
    }
    // BT: Send RFID card numbers to app    
    if(command == "LLL"){  // 'L' for 'list' sends RFID card numbers to app   
      String cards = "";
      for(int i=0;i<MAX_CARDS;i++){
        unsigned long card=cardlist.cards[i].card;
#ifdef BLE_ENABLED
        bleConnection.getSerial().print(logger.print10digits(card)); 
#endif
        if(card > 0) cards += logger.print10digits(card);
        if (i < (MAX_CARDS-1)) {
#ifdef BLE_ENABLED
         bleConnection.getSerial().write(',');  // write comma after card number if not last
#endif
          if(card > 0) cards += ",";
        }
      }
      mqttService.publish("CARDS:" + cards);
    }
    // BT: Delete a card and referring credit   
    if(command.startsWith("DDD") == true){
      command.remove(0,3); // removes "DDD" and leaves the index
      int i = command.toInt();
      i--; // list picker index (app) starts at 1, while RFIDcards array starts at 0
      unsigned long card= cardlist.cards[i].card;
      int credit= eepromConfig.readCredit(i*6+4);      
      oled.message_print(logger.print10digits(card), F("deleting"), 2000);    
      eepromConfig.deleteCard(i*6, 0);
      eepromConfig.deleteCredit(i*6+2, 0);
      buzzer.beep(1);
      mqttService.publish("Deleted card: " + String(card));
    }    
    // BT: Charge a card    
    if((command.startsWith("CCC") == true) ){
      char a1 = command.charAt(3);  // 3 and 4 => card list picker index (from app)
      char a2 = command.charAt(4);
      char a3 = command.charAt(5);  // 5 and 6 => value to charge
      char a4 = command.charAt(6);    
      command = String(a1)+String(a2); 
      int i = command.toInt();    // index of card
      command = String(a3)+String(a4);
      int j = command.toInt();   // value to charge
      j *= 100;
      i--; // list picker index (app) starts at 1, while RFIDcards array starts at 0  
      int credit= eepromConfig.readCredit(i*6+4);
      credit+= j;
      eepromConfig.updateCredit(i*6+4, credit);
      buzzer.beep(1);
      unsigned long card=cardlist.cards[i].card;
      oled.message_print(logger.print10digits(card),"+"+logger.printCredit(j),2000);    
       mqttService.publish("Charged card " + String(card) + ", new credit: " + logger.printCredit(j));
    } 
    // BT: Receives (updated) price list from app.  
    if(command.startsWith("CHA") == true){
      int k = 3;
      pricelist_t pricelist;
      for (int i = 0; i < 11;i++){  
        String tempString = "";
        do {
          tempString += command.charAt(k);
          k++;
        } while (command.charAt(k) != ','); 
        int j = tempString.toInt();
        Serial.println(i*2+PRICELIST_ADDRESS_OFFSET);
        if(i!=10) {
          pricelist.prices[i] = j;
        } else {
          pricelist.defaultCredit = j;
        }
        k++;
      }
      eepromConfig.updatePricelist(pricelist);
      buzzer.beep(1);
      oled.message_print(F("Pricelist"), F("updated!"), 2000);
      mqttService.publish("Updated pricelist on device");
    }
    // BT: Sends price list to app. Product 1 to 10 (0-9), prices divided by commas plus standard value for new cards
    if(command.startsWith("REA") == true){
      // delay(100); // testweise      
      for (int i = 0; i < 11; i++) {
        price = pricelist.prices[i];
#ifdef BLE_ENABLED
        bleConnection.getSerial().print(int(price/100));
        bleConnection.getSerial().print('.');
        if ((price%100) < 10){
          bleConnection.getSerial().print('0');
        }
        bleConnection.getSerial().print(price%100);
        if (i < 10) bleConnection.getSerial().write(',');
#endif
      }
      mqttService.publish("Received pricelist from device");
    } 

    if(command.startsWith("UPDATE") == true) {
      mqttService.publish("Firmwareupdate requested");
      update.startUpdate();
    }

    if(command == "?M3"){
      coffeemaker.inkasso_on();
      mqttService.publish("Inkasso-mode turned ON");
    }
    if(command == "?M1"){
      coffeemaker.inkasso_off();  
      mqttService.publish("Inkasso-mode turned OFF");
   }
    if(command == "FA:04"){        // small cup ordered via app
      coffeemaker.toCoffeemaker("FA:04\r\n"); 
      override = true;
    }
    if(command == "FA:06"){        // large cup ordered via app
      coffeemaker.toCoffeemaker("FA:06\r\n");  
      override = true;
    }
    if(command == "FA:0C"){        // extra large cup ordered via app
      coffeemaker.toCoffeemaker("FA:0C\r\n");  
      override = true;
    } 
 }


// On ESP822 platform this method has to stay in main sketch. This is because of
// callback signature "std::function<void(char*, uint8_t*, unsigned int)> callback"
// in PubSubClient for ESP8266
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String command  = "";
  for (int i = 0; i < length; i++) {
    command += (char)payload[i];
  }
  Serial.print(command);
  executeCommand(command);
  Serial.println();
}
