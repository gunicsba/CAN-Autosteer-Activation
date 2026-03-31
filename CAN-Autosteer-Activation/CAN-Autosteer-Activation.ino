/*
 * @Description: 
        CAN Test
        Two devices burning this program simultaneously can 
    observe each other sending information on the serial port: '1,2,3,4,5,6,7,8'.
    (ESP32 Arduino IDE Library Version: arduino_esp32_v3.0.1)
 * @version: V1.0.0
 * @Author: LILYGO_L
 * @Date: 2024-01-18 18:55:08
 * @LastEditors: LILYGO_L
 * @LastEditTime: 2024-06-24 10:09:19
 * @License: GPL 3.0
 */
#include <Arduino.h>
#include "driver/twai.h"

// -------------------------------------------------------------
// SLCAN (Serial Line CAN) Configuration
// -------------------------------------------------------------
boolean slcan_enabled = false;  // Start closed, require 'O' command
boolean slcan_timestamp = false;
static uint8_t hexval[17] = "0123456789ABCDEF";
int CANBUSSPEED = 250000;       // Default baud rate

// -------------------------------------------------------------
// Brand Configuration (K-Bus engage support only)
// -------------------------------------------------------------
// Only these brands have K-Bus engage messages:
// 1=Deutz (Valtra/MF group), 2=CaseIH/NH, 3=Fendt, 5=FendtOne
// 255 = All supported brands
int Brand = 2;                // Default: all supported brands
boolean addressClaim = false;   // Address claim disabled by default
uint8_t CANBUS_ModuleID = 0x1C; // Default module ID

// -------------------------------------------------------------
// Engage Output Configuration
// -------------------------------------------------------------
#define OUTPUT_HOLD_MS 1000      // Minimum output hold time (ms)
unsigned long engageHoldUntil = 0;  // Time until output can go low
boolean engageState = false;    // Current engage state

#define engageLED 18

// CAN
#define CAN_TX 10
#define CAN_RX 11

// Forward declarations
void CAN_Reinit_With_Speed(int speed);

// -------------------------------------------------------------
// SLCAN Functions
// -------------------------------------------------------------

void slcan_ack()
{
    Serial.write('\r');
}

void slcan_nack()
{
    Serial.write('\a');
}

// Set engage output HIGH with minimum hold time
void setEngageOutput(boolean state)
{
    Serial.println("Engaged!!");
    if (state) {
        engageState = true;
        engageHoldUntil = millis() + OUTPUT_HOLD_MS;
        digitalWrite(engageLED, HIGH);
    } else {
        engageState = false;
        // Don't immediately go LOW - let the hold timer handle it
    }
}

// Get module ID for brand (K-Bus supported brands only)
uint8_t getModuleIDForBrand(int brand)
{
    switch (brand) {
        case 1: return 0x1C;  // Deutz
        case 2: return 0xAA;  // CaseIH/NH
        case 3: return 0x2C;  // Fendt
        case 5: return 0x2C;  // FendtOne
        default: return 0x1C;
    }
}

// Get address claim ID for brand (K-Bus supported brands only)
uint32_t getClaimIDForBrand(int brand)
{
    switch (brand) {
        case 1: return 0x18EEFF1C;  // Deutz
        case 2: return 0x18EEFFAA;  // CaseIH/NH
        case 3: return 0x18EEFF2C;  // Fendt
        case 5: return 0x18EEFF2C;  // FendtOne
        default: return 0x18EEFF1C;
    }
}

// Send J1939 address claim message
void sendAddressClaim()
{
    if (!addressClaim || Brand == 255) return;
    
    twai_message_t claimMsg;
    memset(&claimMsg, 0, sizeof(claimMsg));
    claimMsg.identifier = getClaimIDForBrand(Brand);
    claimMsg.extd = 1;
    claimMsg.data_length_code = 8;
    claimMsg.data[0] = 0x00;
    claimMsg.data[1] = 0x00;
    claimMsg.data[2] = 0xC0;
    claimMsg.data[3] = 0x0C;
    claimMsg.data[4] = 0x00;
    claimMsg.data[5] = 0x17;
    claimMsg.data[6] = 0x02;
    claimMsg.data[7] = 0x20;
    
    twai_transmit(&claimMsg, pdMS_TO_TICKS(100));
}

// Set brand and optionally claim address
void setBrand(int newBrand, boolean claim)
{
    Brand = newBrand;
    addressClaim = claim;
    
    // Only K-Bus supported brands: 1, 2, 3, 5
    if (Brand == 1 || Brand == 2 || Brand == 3 || Brand == 5) {
        CANBUS_ModuleID = getModuleIDForBrand(Brand);
        // FendtOne uses 500k
        if (Brand == 5 && CANBUSSPEED != 500000) {
            CANBUSSPEED = 500000;
            CAN_Reinit_With_Speed(CANBUSSPEED);
        }
        if (addressClaim) {
            sendAddressClaim();
        }
    }
}

// Reinitialize CAN with new baud rate
void CAN_Reinit_With_Speed(int speed)
{
    twai_stop();
    twai_driver_uninstall();
    
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config;
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    
    switch (speed) {
        case 50000:
            t_config = TWAI_TIMING_CONFIG_50KBITS();
            break;
        case 100000:
            t_config = TWAI_TIMING_CONFIG_100KBITS();
            break;
        case 125000:
            t_config = TWAI_TIMING_CONFIG_125KBITS();
            break;
        case 250000:
            t_config = TWAI_TIMING_CONFIG_250KBITS();
            break;
        case 500000:
            t_config = TWAI_TIMING_CONFIG_500KBITS();
            break;
        case 1000000:
            t_config = TWAI_TIMING_CONFIG_1MBITS();
            break;
        default:
            t_config = TWAI_TIMING_CONFIG_250KBITS();
            break;
    }
    
    twai_driver_install(&g_config, &t_config, &f_config);
    
    if (slcan_enabled) {
        twai_start();
    }
    
    uint32_t alerts_to_enable = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS |
                                TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS |
                                TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_DATA |
                                TWAI_ALERT_RX_QUEUE_FULL;
    twai_reconfigure_alerts(alerts_to_enable, NULL);
}

// Send CAN message from SLCAN command
void slcan_send_canmsg(char *buf, boolean rtrFlag) {
    if (!slcan_enabled) return;
    
    twai_message_t outMsg;
    memset(&outMsg, 0, sizeof(outMsg));
    
    int msg_id = 0;
    int msg_len = 0;
    int candata = 0;
    
    if (buf[0] == 't' || buf[0] == 'r') {
        // Standard frame: tIIILDD...
        sscanf(&buf[1], "%03x", &msg_id);
        outMsg.extd = 0;
        sscanf(&buf[4], "%01x", &msg_len);
        for (int i = 0; i < msg_len; i++) {
            sscanf(&buf[5 + (i*2)], "%02x", &candata);
            outMsg.data[i] = candata;
        }
    } else {
        // Extended frame: TIIIIIIIILDD...
        sscanf(&buf[1], "%08x", &msg_id);
        outMsg.extd = 1;
        sscanf(&buf[9], "%01x", &msg_len);
        for (int i = 0; i < msg_len; i++) {
            sscanf(&buf[10 + (i*2)], "%02x", &candata);
            outMsg.data[i] = candata;
        }
    }
    
    outMsg.rtr = rtrFlag ? 1 : 0;
    outMsg.identifier = msg_id;
    outMsg.data_length_code = msg_len;
    
    twai_transmit(&outMsg, pdMS_TO_TICKS(100));
}

// LAWICEL SLCAN Protocol Parser
void pars_slcancmd(char *buf)
{
    switch (buf[0]) {
        case 'O':               // OPEN CAN
            slcan_enabled = true;
            twai_start();
            slcan_ack();
            break;
        case 'C':               // CLOSE CAN
            slcan_enabled = false;
            twai_stop();
            slcan_ack();
            break;
        case 't':               // send std frame
            slcan_send_canmsg(buf, false);
            slcan_ack();
            break;
        case 'T':               // send ext frame
            slcan_send_canmsg(buf, false);
            slcan_ack();
            break;
        case 'r':               // send std rtr frame
            slcan_send_canmsg(buf, true);
            slcan_ack();
            break;
        case 'R':               // send ext rtr frame
            slcan_send_canmsg(buf, true);
            slcan_ack();
            break;
        case 'Z':               // ENABLE TIMESTAMPS
            switch (buf[1]) {
                case '0':
                    slcan_timestamp = false;
                    slcan_ack();
                    break;
                case '1':
                    slcan_timestamp = true;
                    slcan_ack();
                    break;
                default:
                    slcan_nack();
                    break;
            }
            break;
        case 'M':               // set ACCEPTANCE CODE
            slcan_ack();
            break;
        case 'm':               // set ACCEPTANCE MASK
            slcan_ack();
            break;
        case 's':               // CUSTOM CAN bit-rate
            slcan_nack();
            break;
        case 'S':               // CAN bit-rate
            switch (buf[1]) {
                case '0': // 10k - N/A
                case '1': // 20k - N/A
                    slcan_nack();
                    break;
                case '2': // 50k
                    CANBUSSPEED = 50000;
                    CAN_Reinit_With_Speed(CANBUSSPEED);
                    slcan_ack();
                    break;
                case '3': // 100k
                    CANBUSSPEED = 100000;
                    CAN_Reinit_With_Speed(CANBUSSPEED);
                    slcan_ack();
                    break;
                case '4': // 125k
                    CANBUSSPEED = 125000;
                    CAN_Reinit_With_Speed(CANBUSSPEED);
                    slcan_ack();
                    break;
                case '5': // 250k
                    CANBUSSPEED = 250000;
                    CAN_Reinit_With_Speed(CANBUSSPEED);
                    slcan_ack();
                    break;
                case '6': // 500k
                    CANBUSSPEED = 500000;
                    CAN_Reinit_With_Speed(CANBUSSPEED);
                    slcan_ack();
                    break;
                case '7': // 800k - N/A
                    slcan_nack();
                    break;
                case '8': // 1000k
                    CANBUSSPEED = 1000000;
                    CAN_Reinit_With_Speed(CANBUSSPEED);
                    slcan_ack();
                    break;
                default:
                    slcan_nack();
                    break;
            }
            break;
        case 'F':               // STATUS FLAGS
            Serial.print("F00");
            slcan_ack();
            break;
        case 'V':               // VERSION NUMBER
            Serial.print("V1013");
            slcan_ack();
            break;
        case 'N':               // SERIAL NUMBER
            Serial.print("NESP32");
            slcan_ack();
            break;
        case 'h':               // HELP
            Serial.println();
            Serial.println(F("ESP32 SLCAN - AgOpenGPS"));
            Serial.println();
            Serial.println(F("O     = Open CAN"));
            Serial.println(F("C     = Close CAN"));
            Serial.println(F("t     = Send std frame"));
            Serial.println(F("T     = Send ext frame"));
            Serial.println(F("r     = Send std RTR frame"));
            Serial.println(F("R     = Send ext RTR frame"));
            Serial.println(F("Z0/Z1 = Timestamp Off/On"));
            Serial.println(F("S2    = 50k"));
            Serial.println(F("S3    = 100k"));
            Serial.println(F("S4    = 125k"));
            Serial.println(F("S5    = 250k"));
            Serial.println(F("S6    = 500k"));
            Serial.println(F("S8    = 1000k"));
            Serial.println(F("F     = Status flags"));
            Serial.println(F("V     = Version"));
            Serial.println(F("N     = Serial number"));
            Serial.println(F("--- Extended Commands ---"));
            Serial.println(F("B1    = Deutz"));
            Serial.println(F("B2    = CaseIH/NH"));
            Serial.println(F("B3    = Fendt"));
            Serial.println(F("B5    = FendtOne (auto 500k)"));
            Serial.println(F("B1C   = Brand with address claim"));
            Serial.println(F("BA    = All K-Bus brands (default)"));
            Serial.print(F("Speed: ")); Serial.print(CANBUSSPEED); Serial.println(F(" bps"));
            Serial.print(F("SLCAN: ")); Serial.println(slcan_enabled ? F("ON") : F("OFF"));
            Serial.print(F("Brand: ")); 
            if (Brand == 255) Serial.println(F("ALL"));
            else { Serial.print(Brand); Serial.println(addressClaim ? F(" (claim)") : F("")); }
            slcan_nack();
            break;
        case 'B':               // BRAND SELECTION (extended command)
            if (buf[1] == 'A' || buf[1] == 'a') {
                // All brands mode
                Brand = 255;
                addressClaim = false;
                slcan_ack();
            } else if (buf[1] == '1' || buf[1] == '2' || buf[1] == '3' || buf[1] == '5') {
                // Only brands with K-Bus engage support
                int newBrand = buf[1] - '0';
                boolean claim = (buf[2] == 'C' || buf[2] == 'c');
                setBrand(newBrand, claim);
                slcan_ack();
            } else {
                slcan_nack();
            }
            break;
        default:
            slcan_nack();
            break;
    }
}

// Read serial input and dispatch SLCAN commands
void xfer_tty2can()
{
    static char cmdbuf[32];
    static int cmdidx = 0;
    int ser_length;
    
    if ((ser_length = Serial.available()) > 0)
    {
        for (int i = 0; i < ser_length; i++) {
            char val = Serial.read();
            
            // Skip newlines (some tools send \r\n)
            if (val == '\n') continue;
            
            cmdbuf[cmdidx++] = val;
            if (cmdidx == 32)
            {
                slcan_nack();
                cmdidx = 0;
            } else if (val == '\r')
            {
                cmdbuf[cmdidx-1] = '\0';  // Replace \r with null terminator
                if (cmdidx > 1) {  // Only parse if there's actual content
                    pars_slcancmd(cmdbuf);
                }
                cmdidx = 0;
            }
        }
    }
}

// Output CAN message in SLCAN format
void xfer_can2tty(twai_message_t &inMsg)
{
    if (!slcan_enabled) return;
    
    String command = "";
    long time_now = 0;
    
    if (inMsg.extd) {
        // Extended frame
        if (inMsg.rtr) {
            command = command + "R";
        } else {
            command = command + "T";
        }
        command = command + char(hexval[(inMsg.identifier >> 28) & 1]);
        command = command + char(hexval[(inMsg.identifier >> 24) & 15]);
        command = command + char(hexval[(inMsg.identifier >> 20) & 15]);
        command = command + char(hexval[(inMsg.identifier >> 16) & 15]);
        command = command + char(hexval[(inMsg.identifier >> 12) & 15]);
        command = command + char(hexval[(inMsg.identifier >> 8) & 15]);
        command = command + char(hexval[(inMsg.identifier >> 4) & 15]);
        command = command + char(hexval[inMsg.identifier & 15]);
        command = command + char(hexval[inMsg.data_length_code]);
    } else {
        // Standard frame
        if (inMsg.rtr) {
            command = command + "r";
        } else {
            command = command + "t";
        }
        command = command + char(hexval[(inMsg.identifier >> 8) & 15]);
        command = command + char(hexval[(inMsg.identifier >> 4) & 15]);
        command = command + char(hexval[inMsg.identifier & 15]);
        command = command + char(hexval[inMsg.data_length_code]);
    }
    
    for (int i = 0; i < inMsg.data_length_code; i++) {
        command = command + char(hexval[inMsg.data[i] >> 4]);
        command = command + char(hexval[inMsg.data[i] & 15]);
    }
    
    if (slcan_timestamp) {
        time_now = millis() % 60000;
        command = command + char(hexval[(time_now >> 12) & 15]);
        command = command + char(hexval[(time_now >> 8) & 15]);
        command = command + char(hexval[(time_now >> 4) & 15]);
        command = command + char(hexval[time_now & 15]);
    }
    
    command = command + '\r';
    Serial.print(command);
}

// -------------------------------------------------------------
// CAN Driver Functions
// -------------------------------------------------------------

void CAN_Drive_Initialization()
{
    Serial.print("Starting CAN Drive");
    // Initialize configuration structures using macro initializers
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    // Accept all CAN messages for SLCAN functionality
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Install TWAI driver (don't start - wait for 'O' command)
    twai_driver_install(&g_config, &t_config, &f_config);

    uint32_t alerts_to_enable = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS |
                                TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS |
                                TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_DATA |
                                TWAI_ALERT_RX_QUEUE_FULL;
    twai_reconfigure_alerts(alerts_to_enable, NULL);

        Serial.print("Started CAN Drive");
}

// Check for K-Bus engage messages (only brands that support it)
// From CAN_All_Brands.ino K_Receive() function
void checkEngageMessage(twai_message_t &msg)
{
    boolean engaged = false;
    uint32_t id = msg.identifier;
    
    // Brand 1: Deutz (K-Bus engage) - from Valtra/MF group
    if (Brand == 1 || Brand == 255) {
        if (id == 0x18FF5806) {
            if (msg.data[4] == 0x01) {
                engaged = true;
            }
        }
    }
    
    // Brand 2: CaseIH/New Holland (K-Bus engage)
    if (Brand == 2 || Brand == 255) {
        if (id == 0x14FF7706) {
            if ((msg.data[0] == 130 && msg.data[1] == 1) ||
                (msg.data[0] == 178 && msg.data[1] == 4)) {
                engaged = true;
            }
        }
    }
    
    // Brand 3: Fendt armrest buttons (K-Bus, standard frame 0x613)
    if (Brand == 3 || Brand == 255) {
        if (id == 0x613) {
            //{0x15, 0x22, 0x06, 0xCA, 0x80, 0x01, 0x00, 0x00} ;    //  press little go
            if (msg.data[0] == 0x15 && msg.data[1] == 0x22 
                && msg.data[2] == 0x06 && msg.data[3] == 0xCA 
                && msg.data[4] == 0x80 && msg.data[5] == 0x01) {
                    engaged = true;
            }
        }
    }
    
    // Brand 5: FendtOne (K-Bus engage)
    if (Brand == 5 || Brand == 255) {
        if (id == 0xCFFD899) {
            if (msg.data[3] == 0xF6) {
                engaged = true;
            }
        }
    }
    
    setEngageOutput(engaged);

}

void setup()
{
    Serial.begin(921600);  // Standard SLCAN baud rate
    delay(100);
    pinMode(engageLED, OUTPUT);
    digitalWrite(engageLED, LOW);

    CAN_Drive_Initialization();
    Serial.print("Started!!  ");
    Serial.println(Brand);
    
    // Delay startup messages to not interfere with SLCAN init
    delay(100);
}

void loop()
{
    
    // Check CAN alerts (non-blocking)
    uint32_t alerts_triggered;
    twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(1));
    
    // Get bus status info
    twai_status_info_t twai_status_info;
    twai_get_status_info(&twai_status_info);

    // Handle bus errors and recovery
    if (twai_status_info.state == TWAI_STATE_BUS_OFF) {
        twai_initiate_recovery();
    } else if (twai_status_info.state == TWAI_STATE_STOPPED && slcan_enabled) {
        twai_start();
    }

    // Process received CAN messages
    if (alerts_triggered & TWAI_ALERT_RX_DATA)
    {
        twai_message_t rx_buf;
        while (twai_receive(&rx_buf, 0) == ESP_OK)
        {
            // Check for engage messages (all brands)
            checkEngageMessage(rx_buf);
        }
    }
    
    // Handle output hold timer - keep output HIGH for minimum duration
    if (!engageState && millis() >= engageHoldUntil) {
        digitalWrite(engageLED, LOW);
    }
}