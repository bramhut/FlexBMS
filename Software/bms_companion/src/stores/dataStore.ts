import { ref, computed } from 'vue'
import { defineStore } from 'pinia'
import { getFaultsFromNumber, handleDebugMessage, handleUnknownMessage, parseTemps, globalCellVoltages, globalTemps, globalActiveBalance, parseICtemps } from '@/lib/messageParsers'
import { parseVoltages, parseBalancing } from '@/lib/messageParsers'
import {hexStringToSignedInt} from '@/lib/utils'
import { captureData } from '@/lib/dataLogging'

export type CellInfo = {
  voltage: number;
  isBalancing: boolean;
}

export type CIDRegisters = {
  cid: number;
  registers: Record<string, RetrievedRegister>;
}
export type RetrievedRegister = {
  regValue: string;
  timestamp: Date;
}

export const useDataStore = defineStore('data', () => {

  const terminalBuffer = ref<string[]>([])
  const cidCount = ref(0)
  const cellCount = ref(0);
  const ntcCount = ref(0);
  const packVoltage = ref(0);
  const packCurrent = ref(0);
  const packSOC = ref("");
  const minCellVoltage = ref(0);
  const maxCellVoltage = ref(0);
  const minTemperature = ref(0);
  const maxTemperature = ref(0);

  const timeLastMessageReceived = ref(0);

  const state = ref<number|undefined>(undefined);
  const faults = ref<string[]>([]);

  // parent array is list of cell id
  // child arrayu is list of slave ids
  const cellVoltages = ref<Record<number, CellInfo[]>>({});

  const testcells = computed(() => cellVoltages);
  const temps = ref<Record<number, number[]>>({});

  const ictemps = ref<number[]>([]);

  const retrievedRegister = ref<CIDRegisters[]>([]);

  const dataDumpSize = ref(0);

  const dataLoggingMessage = ref("");



  let cellVoltageUpdatecount = 0;
  let tempUpdateCount = 0;


  function addDataDumpSize(size: number) {
    dataDumpSize.value += size;
  }

  function resetDataDumpSize() {
    dataDumpSize.value = 0;
  }

  function setDataloggingMessage(message: string) {
    dataLoggingMessage.value = message;
  }

  function parseIncomingMessage(message: string) {
    
    timeLastMessageReceived.value = Date.now();

    if (message.slice(0, 2) != "*!") {
      terminalBuffer.value = handleDebugMessage(message, terminalBuffer.value);
      return;
    }
    message = message.slice(2);
    // console.log(message);

    let messageType = message.slice(0, 2);
    switch (messageType) {
      case "00":

        break;
      case "01":
        // BMS info (cellCount, cid count etc)
        // console.log(message);
        // counts given in hexadecimal
        cidCount.value = parseInt(message.slice(2, 4), 10);
        cellCount.value = parseInt(message.slice(4, 7), 10);
        ntcCount.value = parseInt(message.slice(7, 10), 10);

        break;

      case "02":
        // Pack measurements
        // console.log("Pack measurements");
        // console.log(message);
        packVoltage.value = parseInt(message.slice(2, 10), 16) / 1000000;
        packCurrent.value = hexStringToSignedInt(message.slice(10, 14)) / 64;
        packSOC.value = (parseInt(message.slice(14, 18), 16) / 0xFFFF * 300 - 100).toFixed(3);
        break;

      case "03":
        // console.log(message)
        globalCellVoltages[parseInt(message.slice(2, 4))] = parseVoltages(message);
        cellVoltageUpdatecount++;
        if (cellVoltageUpdatecount > Object.keys(globalCellVoltages).length) {
          cellVoltageUpdatecount = 0;
          cellVoltages.value = { ...globalCellVoltages };
        }
        break;
      case "04":
        
        globalTemps[parseInt(message.slice(2, 4))] = parseTemps(message);
        tempUpdateCount++;
        if (tempUpdateCount > Object.keys(temps.value).length) {
          tempUpdateCount = 0;
          temps.value = { ...globalTemps };
        }
        break;
      case "05":
        let cid = parseInt(message.slice(2, 4));
        let regaddr = message.slice(4, 6).toUpperCase();
        let regVal = message.slice(6, 10).toUpperCase();
        let found = false;
        retrievedRegister.value.forEach((cidRegisters) => {
          if (cidRegisters.cid === cid) {
            found = true;
            cidRegisters.registers[regaddr] = { regValue: regVal, timestamp: new Date(Date.now()) };
          }
        });
        if (!found) {
          retrievedRegister.value.push({ cid: cid, registers: { [regaddr]: { regValue: regVal, timestamp: new Date(Date.now()) } } });
        }

        // console.log(message)
        break;
      case "06":
        maxCellVoltage.value = parseInt(message.slice(2, 10), 16) / 1000000;
        minCellVoltage.value = parseInt(message.slice(10, 18), 16) / 1000000;
        maxTemperature.value = (parseInt(message.slice(18, 22), 16) / 0xFFFF * 120) - 20;
        minTemperature.value = (parseInt(message.slice(22, 26), 16) / 0xFFFF * 120) - 20;
        break;
      case "07":
        state.value = parseInt(message.slice(2, 3), 16);
        faults.value = getFaultsFromNumber(parseInt(message.slice(3, 7), 16));
        break;
      case "08":
        ictemps.value = parseICtemps(message);
        break;
      case "10":
        captureData();
        break;
      default:
        handleUnknownMessage();
        break;
    }
  }

  function getCSVRow() {
    let row =  `${Date.now()},${packVoltage.value.toFixed(4)},${packCurrent.value},${packSOC.value},${minCellVoltage.value},${maxCellVoltage.value},${minTemperature.value.toFixed(4)},${maxTemperature.value.toFixed(4)},${state.value},"${faults.value.join(";")}"`
    // loop over cellVoltages
    let cids = Object.keys(cellVoltages.value);

    for (let i = 0; i < cids.length; i++) {
      for (let j = 0; j < cellVoltages.value[cids[i]].length; j++) {
        row += `,${cellVoltages.value[cids[i]][j].voltage}`
      }
    }

    for (let i = 0; i < cids.length; i++) {
      for (let j = 0; j < cellVoltages.value[cids[i]].length; j++) {
        row += `,${cellVoltages.value[cids[i]][j].isBalancing ? 1 : 0}`
      }
    }

    // loop over temps
    cids = Object.keys(temps.value);
    for (let i = 0; i < cids.length; i++) {
      for (let j = 0; j < temps.value[cids[i]].length; j++) {
        row += `,${temps.value[cids[i]][j].toFixed(4)}`
      }
    }

    for (let i = 0; i < ictemps.value.length; i++) {
      row += `,${ictemps.value[i].toFixed(4)}`
    }
    
    row += "\r\n";
    return row;
  }

  function reset() {
    terminalBuffer.value = [];
    cidCount.value = 0;
    cellCount.value = 0;
    ntcCount.value = 0;
    packVoltage.value = 0;
    packCurrent.value = 0;
    packSOC.value = "";
    cellVoltages.value = {};
    temps.value = {};
    retrievedRegister.value = [];
    minCellVoltage.value = 0;
    maxCellVoltage.value = 0;
    minTemperature.value = 0;
    maxTemperature.value = 0;
    state.value = undefined;
    faults.value = [];
    ictemps.value = [];
    
  }

  return { parseIncomingMessage, setDataloggingMessage, resetDataDumpSize, reset, addDataDumpSize, getCSVRow,dataLoggingMessage,  dataDumpSize, terminalBuffer, cidCount, cellCount, ntcCount, packVoltage, packCurrent, packSOC, cellVoltages, temps, testcells, retrievedRegister, minCellVoltage, maxCellVoltage, minTemperature, maxTemperature, state, faults,ictemps }
})
