#ifndef LOGGER_H_
#define LOGGER_H_

#include "Arduino.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Syslog.h>   //https://github.com/arcao/Syslog
#include "settings.h"

class CoffeeLogger {
  private:
    WiFiUDP udpClient;
    Syslog syslog;
  
  public:
    CoffeeLogger();

    void log(String msg);
    void log(uint16_t pri, String msg);
    String print2digits(int number);
    String print10digits(unsigned long number);
    String printCredit(int credit); 
};
#endif

