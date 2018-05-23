// rf95_client.cpp
//
// Example program showing how to use RH_RF95 on Raspberry Pi
// Uses the bcm2835 library to access the GPIO pins to drive the RFM95 module
// Requires bcm2835 library to be already installed
// http://www.airspayce.com/mikem/bcm2835/
// Use the Makefile in this directory:
// cd example/raspi/rf95
// make
// sudo ./rf95_client
//
// Contributed by Charles-Henri Hallard based on sample RH_NRF24 by Mike Poublon

#include "mqtt/client.h"
#include <bcm2835.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <RH_RF69.h>
#include <RH_RF95.h>

// define hardware used change to fit your need
// Uncomment the board you have, if not listed
// uncommment custom board and set wiring tin custom section

// LoRasPi board
// see https://github.com/hallard/LoRasPI
// #define BOARD_LORASPI

// iC880A and LinkLab Lora Gateway Shield (if RF module plugged into)
// see https://github.com/ch2i/iC880A-Raspberry-PI
//#define BOARD_IC880A_PLATE

// Raspberri PI Lora Gateway for multiple modules
// see https://github.com/hallard/RPI-Lora-Gateway
//#define BOARD_PI_LORA_GATEWAY

// Dragino Raspberry PI hat
// see https://github.com/dragino/Lora
//#define BOARD_DRAGINO_PIHAT

// DycodeX LoRa Raspberry PI hat
// =========================================
// see https://github.com/dycodex/LoRa-Raspberry-Pi-Hat
// #define BOARD_DYCODEX

// DycodeX LoRa Raspberry PI hat V2
// =========================================
#define BOARD_DYCODEX_V2

// Now we include RasPi_Boards.h so this will expose defined
// constants with CS/IRQ/RESET/on board LED pins definition
#include "./RasPiBoards.h"

// Our RFM95 Configuration
#define RF_FREQUENCY 868.10
#define RF_GATEWAY_ID 1
#define RF_NODE_ID 10

// Create an instance of a driver
RH_RF95 rf95(RF_CS_PIN, RF_IRQ_PIN);
// RH_RF95 rf95(RF_CS_PIN);

// Flag for Ctrl-C
volatile sig_atomic_t force_exit = false;

const std::string SERVER_ADDRESS("tcp://localhost:1883");
const std::string CLIENT_ID{"transmitter"};
const std::string TOPIC{"/controlling-drone"};

const std::string user =
    "bbff39d0d3066758ffe55666762b3c8b150295b848cb6c871b79f2fff36c79fb";
const std::string password =
    "50acea3098359517297e08040dc6bfc371d044190be6527c1ac29e078cbe8313";

const int QOS = 1;

using namespace std;
using namespace std::chrono;

// --------------------------------------------------------------------------
// Simple function to manually reconect a client.

bool try_reconnect(mqtt::client &cli) {
  constexpr int N_ATTEMPT = 30;

  for (int i = 0; i < N_ATTEMPT && !cli.is_connected(); ++i) {
    try {
      cli.reconnect();
      return true;
    } catch (const mqtt::exception &) {
      this_thread::sleep_for(seconds(1));
    }
  }
  return false;
}

void sig_handler(int sig) {
  printf("\n%s Break received, exiting!...\n", __BASEFILE__);
  force_exit = true;
  std::exit(0);
}

// Main Function
int main(int argc, const char *argv[]) {
  static unsigned long last_millis;
  static unsigned long led_blink = 0;

  signal(SIGINT, sig_handler);
  printf("%s\n", __BASEFILE__);

  if (!bcm2835_init()) {
    fprintf(stderr, "%s bcm2835_init() Failed\n\n", __BASEFILE__);
    return 1;
  }

  printf("RF95 CS=GPIO%d", RF_CS_PIN);

#ifdef RF_LED_PIN
  pinMode(RF_LED_PIN, OUTPUT);
  digitalWrite(RF_LED_PIN, HIGH);
#endif

#ifdef RF_IRQ_PIN
  printf(", IRQ=GPIO%d", RF_IRQ_PIN);
  // IRQ Pin input/pull down
  pinMode(RF_IRQ_PIN, INPUT);
  bcm2835_gpio_set_pud(RF_IRQ_PIN, BCM2835_GPIO_PUD_DOWN);
#endif

#ifdef RF_RST_PIN
  printf(", RST=GPIO%d", RF_RST_PIN);
  // Pulse a reset on module
  pinMode(RF_RST_PIN, OUTPUT);
  digitalWrite(RF_RST_PIN, LOW);
  bcm2835_delay(150);
  digitalWrite(RF_RST_PIN, HIGH);
  bcm2835_delay(100);
#endif

#ifdef RF_LED_PIN
  printf(", LED=GPIO%d", RF_LED_PIN);
  digitalWrite(RF_LED_PIN, LOW);
#endif

  if (!rf95.init()) {
    fprintf(stderr, "\nRF95 module init failed, Please verify wiring/module\n");
  } else {
    printf("\nRF95 module seen OK!\r\n");

    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    connOpts.set_user_name(user.c_str());
    connOpts.set_password(password.c_str());

    mqtt::client cli(SERVER_ADDRESS, CLIENT_ID);

#ifdef RF_IRQ_PIN
    // Since we may check IRQ line with bcm_2835 Rising edge detection
    // In case radio already have a packet, IRQ is high and will never
    // go to low so never fire again
    // Except if we clear IRQ flags and discard one if any by checking
    rf95.available();

    // Now we can enable Rising edge detection
    bcm2835_gpio_ren(RF_IRQ_PIN);
#endif

    // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf =
    // 128chips/symbol, CRC on

    // The default transmitter power is 13dBm, using PA_BOOST.
    // If you are using RFM95/96/97/98 modules which uses the PA_BOOST
    // transmitter pin, then you can set transmitter powers from 5 to 23 dBm:
    // rf95.setTxPower(23, false);
    // If you are using Modtronix inAir4 or inAir9,or any other module which
    // uses the transmitter RFO pins and not the PA_BOOST pins then you can
    // configure the power transmitter power for -1 to 14 dBm and with useRFO
    // true. Failure to do that will result in extremely low transmit powers.
    // rf95.setTxPower(14, true);

    rf95.setTxPower(23, false);

    // You can optionally require this module to wait until Channel Activity
    // Detection shows no activity on the channel before transmitting by setting
    // the CAD timeout to non-zero:
    // rf95.setCADTimeout(10000);

    // Adjust Frequency
    rf95.setFrequency(RF_FREQUENCY);

    // This is our Node ID
    rf95.setThisAddress(RF_NODE_ID);
    rf95.setHeaderFrom(RF_NODE_ID);

    // Where we're sending packet
    rf95.setHeaderTo(RF_GATEWAY_ID);

    printf("RF95 node #%d init OK @ %3.2fMHz\n", RF_NODE_ID, RF_FREQUENCY);

    last_millis = millis();

    try {
      cout << "Connecting to the MQTT server..." << flush;
      cli.connect(connOpts);
      cli.subscribe(TOPIC, QOS);
      cout << "OK\n" << endl;

      // Consume messages

      while (!force_exit) {
        auto msg = cli.consume_message();

        // printf( "millis()=%ld last=%ld diff=%ld\n", millis() , last_millis,
        // millis() - last_millis );

        if (!msg) {
          if (!cli.is_connected()) {
            cout << "Lost connection. Attempting reconnect" << endl;
            if (try_reconnect(cli)) {
              cli.subscribe(TOPIC, QOS);
              cout << "Reconnected" << endl;
              continue;
            } else {
              cout << "Reconnect failed." << endl;
              break;
            }
          } else
            break;
        }

        // Send every 5 seconds
        // if (millis() - last_millis > 5000) {
        //   last_millis = millis();

        // }

#ifdef RF_LED_PIN
        led_blink = millis();
        digitalWrite(RF_LED_PIN, HIGH);
#endif
        vector<uint8_t> dataVector(msg->to_string().begin(), msg->to_string().end());
        uint8_t *data = &dataVector[0];
        uint8_t len = (uint8_t)strlen(msg->to_string().c_str());

        printf("Sending %02d bytes to node #%d => ", len, RF_GATEWAY_ID);
        printbuffer(data, len);
        printf("\n");
        rf95.send(data, len);
        rf95.waitPacketSent();

        cout << msg->get_topic() << ": " << msg->to_string() << endl;

        // Begin the main body of code

#ifdef RF_LED_PIN
        // Led blink timer expiration ?
        if (led_blink && millis() - led_blink > 200) {
          led_blink = 0;
          digitalWrite(RF_LED_PIN, LOW);
        }
#endif

        // Let OS doing other tasks
        // Since we do nothing until each 5 sec
        bcm2835_delay(100);
      }
      cout << "\nDisconnecting from the MQTT server..." << flush;
      cli.disconnect();
      cout << "OK" << endl;
    } catch (const mqtt::exception &exc) {
      cerr << exc.what() << endl;
      return 1;
    }
  }

#ifdef RF_LED_PIN
  digitalWrite(RF_LED_PIN, LOW);
#endif
  printf("\n%s Ending\n", __BASEFILE__);
  bcm2835_close();
  return 0;
}
