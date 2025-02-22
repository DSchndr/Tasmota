/*
  xnrg_29_modbus.ino - Generic Modbus energy meter support for Tasmota

  Copyright (C) 2022  Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_ENERGY_SENSOR
#ifdef USE_MODBUS_ENERGY
/*********************************************************************************************\
 * Generic Modbus energy meter - experimental (but works on my SDM230)
 *
 * Using a rule file called modbus allows to easy configure modbus energy monitor devices.
 * See examples below
 *
 * Works:
 * rule3 on file#modbus do {"name":"SDM230","baud":2400,"config":8N1","address":1,"function":4,"voltage":0,"current":6,"active_power":12,"apparent_power":18,"reactive_power":24,"power_factor":30,"frequency":70,"import_active_energy":342} endon
 *
 * Test set:
 * rule3 on file#modbus do {"name":"SDM230 test1","baud":2400,"config":8N1","address":1,"function":4,"voltage":[0,0,0],"current":[6,6,6],"active_power":[12,12,12],"apparent_power":[18,18,18],"reactive_power":[24,24,24],"power_factor":[30,30,30],"frequency":[70,70,70],"import_active_energy":[342,342,342]} endon
 * rule3 on file#modbus do {"name":"SDM230 test2","baud":2400,"config":8N1","address":1,"function":4,"voltage":[0,0,0],"current":[6,6,6],"active_power":[12,12,12],"apparent_power":[18,18,18],"reactive_power":[24,24,24],"power_factor":[30,30,30],"frequency":70,"import_active_energy":[342,342,342]} endon
\*********************************************************************************************/

#define XNRG_29                  29

#define ENERGY_MODBUS_SPEED      9600    // default Modbus baudrate
#define ENERGY_MODBUS_CONFIG     TS_SERIAL_8N1
#define ENERGY_MODBUS_ADDR       1       // default Modbus device_address

enum EnergyModbusRegisters { NRG_MBS_VOLTAGE,
                             NRG_MBS_CURRENT,
                             NRG_MBS_ACTIVE_POWER,
                             NRG_MBS_APPARENT_POWER,
                             NRG_MBS_REACTIVE_POWER,
                             NRG_MBS_POWER_FACTOR,
                             NRG_MBS_FREQUENCY,
                             NRG_MBS_IMPORT_ACTIVE_ENERGY,
                             NRG_MBS_EXPORT_ACTIVE_ENERGY,
                             NRG_MBS_MAX_REGS };

const char kEnergyModbusValues[] PROGMEM = "voltage|"
                                           "current|"
                                           "active_power|"
                                           "apparent_power|"
                                           "reactive_power|"
                                           "power_factor|"
                                           "frequency|"
                                           "import_active_energy|"
                                           "export_active_energy";

#include <TasmotaModbus.h>
TasmotaModbus *EnergyModbus;

struct NRGMODBUS {
/*
  uint16_t voltage[ENERGY_MAX_PHASES];
  uint16_t current[ENERGY_MAX_PHASES];
  uint16_t active_power[ENERGY_MAX_PHASES];
  uint16_t apparent_power[ENERGY_MAX_PHASES];
  uint16_t reactive_power[ENERGY_MAX_PHASES];
  uint16_t power_factor[ENERGY_MAX_PHASES];
  uint16_t frequency[ENERGY_MAX_PHASES];
  uint16_t import_active[ENERGY_MAX_PHASES];
  uint16_t export_active[ENERGY_MAX_PHASES];
*/
  uint16_t register_address[NRG_MBS_MAX_REGS][ENERGY_MAX_PHASES];

  uint32_t serial_bps;
  uint32_t serial_config;
  uint8_t device_address;
  uint8_t function;

  uint8_t phase;
  uint8_t state;
  uint8_t retry;
} *NrgModbus = nullptr;

/*********************************************************************************************/

void EnergyModbusLoop(void) {
  bool data_ready = EnergyModbus->ReceiveReady();

  if (data_ready) {
    uint8_t buffer[14];  // At least 5 + (2 * 2) = 9

    uint32_t error = EnergyModbus->ReceiveBuffer(buffer, 2);

    AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("NRG: Modbus register %d, phase %d, rcvd %*_H"),
      NrgModbus->state, NrgModbus->phase, EnergyModbus->ReceiveCount(), buffer);

    if (error) {
      /* Return codes from TasmotaModbus.h:
      * 0 = No error
      * 1 = Illegal Function,
      * 2 = Illegal Data Address,
      * 3 = Illegal Data Value,
      * 4 = Slave Error
      * 5 = Acknowledge but not finished (no error)
      * 6 = Slave Busy
      * 7 = Not enough minimal data received
      * 8 = Memory Parity error
      * 9 = Crc error
      * 10 = Gateway Path Unavailable
      * 11 = Gateway Target device failed to respond
      * 12 = Wrong number of registers
      * 13 = Register data not specified
      * 14 = To many registers
      */
      AddLog(LOG_LEVEL_DEBUG, PSTR("NRG: Modbus error %d"), error);
    } else {
      Energy.data_valid[NrgModbus->phase] = 0;

      //  0  1  2  3  4  5  6  7  8
      // SA FC BC Fh Fl Sh Sl Cl Ch
      // 01 04 04 43 66 33 34 1B 38 = 230.2 Volt
      float value;
      ((uint8_t*)&value)[3] = buffer[3];   // Get float values
      ((uint8_t*)&value)[2] = buffer[4];
      ((uint8_t*)&value)[1] = buffer[5];
      ((uint8_t*)&value)[0] = buffer[6];

      switch(NrgModbus->state) {
        case NRG_MBS_VOLTAGE:
          Energy.voltage[NrgModbus->phase] = value;          // 230.2 V
          break;
        case NRG_MBS_CURRENT:
          Energy.current[NrgModbus->phase]  = value;         // 1.260 A
          break;
        case NRG_MBS_ACTIVE_POWER:
          Energy.active_power[NrgModbus->phase] = value;     // -196.3 W
          break;
        case NRG_MBS_APPARENT_POWER:
          Energy.apparent_power[NrgModbus->phase] = value;   // 223.4 VA
          break;
        case NRG_MBS_REACTIVE_POWER:
          Energy.reactive_power[NrgModbus->phase] = value;   // 92.2
          break;
        case NRG_MBS_POWER_FACTOR:
          Energy.power_factor[NrgModbus->phase] = value;     // -0.91
          break;
        case NRG_MBS_FREQUENCY:
          Energy.frequency[NrgModbus->phase] = value;        // 50.0 Hz
          break;
        case NRG_MBS_IMPORT_ACTIVE_ENERGY:
          Energy.import_active[NrgModbus->phase] = value;    // 6.216 kWh => used in EnergyUpdateTotal()
          break;
        case NRG_MBS_EXPORT_ACTIVE_ENERGY:
          Energy.export_active[NrgModbus->phase] = value;    // 478.492 kWh
          break;
      }

      do {
        NrgModbus->phase++;
        if (NrgModbus->phase == Energy.phase_count) {
          NrgModbus->phase = 0;
          NrgModbus->state++;
          if (NrgModbus->state == NRG_MBS_MAX_REGS) {
            NrgModbus->state = 0;
            NrgModbus->phase = 0;
            EnergyUpdateTotal();                 // update every cycle after all registers have been read
            break;
          }
        }
      } while (NrgModbus->register_address[NrgModbus->state][NrgModbus->phase] == 1);
    }
  } // end data ready

  if (0 == NrgModbus->retry || data_ready) {
    NrgModbus->retry = 5;
    EnergyModbus->Send(NrgModbus->device_address, NrgModbus->function, NrgModbus->register_address[NrgModbus->state][NrgModbus->phase], 2);
  } else {
    NrgModbus->retry--;
  }
}

bool EnergyModbusReadRegisters(void) {
#ifdef USE_RULES
  String modbus = RuleLoadFile("MODBUS");
  if (!modbus.length()) { return false; }
//    AddLog(LOG_LEVEL_DEBUG, PSTR("NRG: File '%s'"), modbus.c_str());

//  rule3 on file#modbus do {"name":"SDM230","baud":2400,"config":8N1","address":1,"function":4,"voltage":0,"current":6,"active_power":12,"apparent_power":18,"reactive_power":24,"power_factor":30,"frequency":70,"import_active_energy":342} endon
//  rule3 on file#modbus do {"name":"SDM230 test1","baud":2400,"config":8N1","address":1,"function":4,"voltage":[0,0,0],"current":[6,6,6],"active_power":[12,12,12],"apparent_power":[18,18,18],"reactive_power":[24,24,24],"power_factor":[30,30,30],"frequency":[70,70,70],"import_active_energy":[342,342,342]} endon
//  rule3 on file#modbus do {"name":"SDM230 test2","baud":2400,"config":8N1","address":1,"function":4,"voltage":[0,0,0],"current":[6,6,6],"active_power":[12,12,12],"apparent_power":[18,18,18],"reactive_power":[24,24,24],"power_factor":[30,30,30],"frequency":70,"import_active_energy":[342,342,342]} endon
//  rule3 on file#modbus do {"name":"SDM230 test2","baud":2400,"config":8N1","address":1,"function":4,"voltage":0,"current":[6,6,6],"active_power":[12,12,12],"apparent_power":[18,18,18],"reactive_power":[24,24,24],"power_factor":[30,30,30],"frequency":70,"import_active_energy":[342,342,342]} endon

  const char* json = modbus.c_str();
  uint32_t len = strlen(json) +1;
  if (len < 7) { return false; }

  char json_buffer[len];
  memcpy(json_buffer, json, len);                // Keep original safe
  JsonParser parser(json_buffer);
  JsonParserObject root = parser.getRootObject();
  if (!root) { return false; }

  NrgModbus = (NRGMODBUS *)calloc(sizeof(struct NRGMODBUS), 1);
  if (NrgModbus == nullptr) { return false; }

  // Init defaults
  NrgModbus->serial_bps = ENERGY_MODBUS_SPEED;
  NrgModbus->serial_config = ENERGY_MODBUS_CONFIG;
  NrgModbus->device_address = ENERGY_MODBUS_ADDR;
  NrgModbus->function = 0x04;
  for (uint32_t i = 0; i < 9; i++) {
    for (uint32_t j = 0; j < ENERGY_MAX_PHASES; j++) {
      NrgModbus->register_address[i][j] = 1;     // Not used
    }
  }

  JsonParserToken val;
  val = root[PSTR("baud")];
  if (val) {
    NrgModbus->serial_bps = val.getInt();        // 2400
  }
  val = root[PSTR("config")];
  if (val) {
    const char *serial_config = val.getStr();    // 8N1
    NrgModbus->serial_config = ConvertSerialConfig(ParseSerialConfig(serial_config));
  }
  val = root[PSTR("address")];
  if (val) {
    NrgModbus->device_address = val.getInt();    // 1
  }
  val = root[PSTR("function")];
  if (val) {
    NrgModbus->function = val.getInt();          // 4
  }

  char register_name[32];
  uint32_t phase;
  Energy.voltage_available = false;              // Disable voltage is measured
  Energy.current_available = false;              // Disable current is measured
  for (uint32_t names = 0; names < NRG_MBS_MAX_REGS; names++) {
    phase = 0;
    val = root[GetTextIndexed(register_name, sizeof(register_name), names, kEnergyModbusValues)];
    if (val.isArray()) {
      JsonParserArray arr = val.getArray();
      for (auto value : arr) {
        NrgModbus->register_address[names][phase] = value.getUInt();
        phase++;
        if (phase == ENERGY_MAX_PHASES) { break; }
      }
    } else if (val) {
      NrgModbus->register_address[names][phase] = val.getUInt();
      phase++;
    }
    if (phase) {
      if (phase > Energy.phase_count) {
        Energy.phase_count = phase;
      }
      switch(names) {
        case NRG_MBS_VOLTAGE:
          Energy.voltage_available = true;       // Enable if voltage is measured
          if (1 == phase) {
            Energy.voltage_common = true;        // Use common voltage
          }
          break;
        case NRG_MBS_CURRENT:
          Energy.current_available = true;       // Enable if current is measured
          break;
        case NRG_MBS_FREQUENCY:
          if (1 == phase) {
            Energy.frequency_common = true;      // Use common frequency
          }
          break;
      }
    }
  }

//  NrgModbus->state = 0;    // Set by calloc()
//  NrgModbus->phase = 0;

  return true;
#endif  // USE_RULES
  return false;
}

bool EnergyModbusRegisters(void) {
  if (EnergyModbusReadRegisters()) {
    return true;
  }
  AddLog(LOG_LEVEL_INFO, PSTR("NRG: No valid modbus data"));
  return false;
}

void EnergyModbusSnsInit(void) {
  if (EnergyModbusRegisters()) {
    EnergyModbus = new TasmotaModbus(Pin(GPIO_NRG_MBS_RX), Pin(GPIO_NRG_MBS_TX));
    uint8_t result = EnergyModbus->Begin(NrgModbus->serial_bps, NrgModbus->serial_config);
    if (result) {
      if (2 == result) { ClaimSerial(); }
      return;
    }
  }
  TasmotaGlobal.energy_driver = ENERGY_NONE;
}

void EnergyModbusDrvInit(void) {
  if (PinUsed(GPIO_NRG_MBS_RX) && PinUsed(GPIO_NRG_MBS_TX)) {
    TasmotaGlobal.energy_driver = XNRG_29;
  }
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xnrg29(uint8_t function) {
  bool result = false;

  switch (function) {
//    case FUNC_EVERY_250_MSECOND:
    case FUNC_EVERY_200_MSECOND:
      EnergyModbusLoop();
      break;
    case FUNC_ENERGY_RESET:
//      EnergyModbusReset();
      break;
    case FUNC_INIT:
      EnergyModbusSnsInit();
      break;
    case FUNC_PRE_INIT:
      EnergyModbusDrvInit();
      break;
  }
  return result;
}

#endif  // USE_MODBUS_ENERGY
#endif  // USE_ENERGY_SENSOR
