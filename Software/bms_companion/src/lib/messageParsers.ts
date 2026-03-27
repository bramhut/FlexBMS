
import { CellInfo } from "./../stores/dataStore";

export const globalActiveBalance: Record<number, boolean[]> = {};
export const globalCellVoltages: Record<number, CellInfo[]> = {};
export const globalTemps: Record<number, number[]> = {};

export function handleUnknownMessage() {
  console.log("Unknown message type");
}

export function handleDebugMessage(message: string, buffer: string[]) {

  buffer.unshift(message)
  if (buffer.length > 100) {
    buffer.pop();
  }
  return buffer;

}

export function parseVoltages(message: string): CellInfo[] {
  let voltages = message.slice(4);
  // console.log(voltages);
  let parsedVoltages = [];
  for (let i = 0; i < voltages.length; i += 9) {

    parsedVoltages.push(
      {
        voltage: (parseInt(voltages.slice(i, i + 8), 16) / 1000000),
        isBalancing: (voltages.slice(i + 8, i + 9) == "1" ? true : false)
        // isBalancing: false,
      }
    );         
  }
  return parsedVoltages
}

export function parseBalancing(message: string) {
  let balances = message.slice(1);
  // console.log(voltages);
  let parsedBalances = [];
  for (let i = 0; i < balances.length; i += 1) {
    // console.log(parseInt(voltages.slice(i, i + 8), 16) / 1000000);
    if (balances[i] == "1") {
      parsedBalances.push(true);
    } else {
      parsedBalances.push(false);
    }

  }
  return parsedBalances;
}

export function parseTemps(message: string) {
  let temps = message.slice(4);
  let parsedTemps = [];
  for (let i = 0; i < temps.length; i += 4) {
    parsedTemps.push((parseInt(temps.slice(i, i + 4), 16) / 0xFFFF * 120) - 20);

  }
  return parsedTemps
}

export function parseICtemps(message: string) {
  let temps = message.slice(2);
  let parsedTemps = [];
  for (let i = 0; i < temps.length; i += 4) {
    parsedTemps.push((parseInt(temps.slice(i, i + 4), 16) / 0xFFFF * 120) - 20);

  }
  return parsedTemps
}

const faults = ["INVALID_CONFIG",
  "TPL_FAULT",
  "CID_INITIALIZATION_FAULT",
  "REGISTER_INITIALIZATION_FAULT",
  "CELL_BALANCING_FAULT",
  "DIAGNOSTICS_FAULT",
  "OVERVOLTAGE_LIMIT",
  "UNDERVOLTAGE_LIMIT",
  "TEMPERATURE_LIMIT",
  "OVERCURRENT_LIMIT",
  "CHIP_TEMPERATURE",
  "SOC_LIMIT",
  "OPEN_SHORT_FAULT",
  "SYSTEM_FAULT",
  "COMMUNICATION_TIMEOUT",];

export function getFaultsFromNumber(fault: number): string[] {
  if (fault == undefined) {
    return []
  }

  let binaryString = fault.toString(2).padStart(16, '0')
  let output = [];

  for (let i = 15; i >= 0; i--) {
    if (binaryString[i] == "1") {
      output.push(faults[15 - i])
    }

  }

  return output
}
