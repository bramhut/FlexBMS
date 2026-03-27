import { CellInfo } from '@/stores/dataStore';
import { type ClassValue, clsx } from 'clsx'
import { twMerge } from 'tailwind-merge'

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs))
}


// This function transposes from a map of cid -> voltages to map voltages -> cid
// also works for temps
export function transpose(cellVoltages: Record<number, CellInfo[]>) {

  let transposed = [];
  let cids = Object.keys(cellVoltages);

  if (cids.length > 0) {
    // console.log(cellVoltages[cids[0]]);
  }
  // loop over cellvoltages
  for (let i = 0; i < cids.length; i++) {
    for (let j = 0; j < cellVoltages[cids[i]].length; j++) {
      if (transposed[j] == undefined) {
        transposed[j] = [];
      }
      transposed[j][cids[i]] = cellVoltages[cids[i]][j];
    }
  }
  // console.log(cellVoltages)


  // console.log(transposed)
  return transposed;

}

let writer: WritableStreamDefaultWriter<string> | undefined = undefined;

export function getWriter() {
  return writer;
}



export function sendHeartbeat() {
  if (writer != undefined) {
    // console.log("Send heartbeat");
    writer.write("00")
  }
}

export function setWriter(w: WritableStreamDefaultWriter<string>) {
  writer = w;
}

export function hexStringToSignedInt(hex: string): number {

  let value = parseInt(hex, 16);
  if ((value & 0x8000) > 0) {
    value = value - 0x10000;
  }
  return value
}

export function getMinMaxValues(values) {
  let min = Infinity;
  let max = 0;
  values.forEach((value) => {
    value.forEach((v) => {
      // Super hacky way to differentiate between cellInfo and temp
      if (typeof v == "number") {
        if (v > max) {
          max = v;
        }
        if (v < min) {
          min = v;
        }
      } else {
        if (v.voltage > max) {
          max = v.voltage;
        }
        if (v.voltage < min) {
          min = v.voltage;
        }
      }
    })
  })
  return [min, max]
}

export function getHeatmapColor(min, max, value) {
  // return "background-color: red;";

  if (value == undefined) {
    return `background-color: rgb(255, 255, 255);`;
  }
  let percent = (value - min) / (max - min);

  let redPercent;
  let greenPercent;
  let bluePercent;


  if (percent > 0.5) {
    redPercent = 1;
    bluePercent = ((1 - percent)) + 0.35;
    greenPercent = ((1 - percent)) + 0.35;


  } else {
    bluePercent = 1;
    redPercent = (percent) + 0.45;
    greenPercent = (percent) + 0.45;
  }
  // console.log(`background-color: rgb(${255 * redPercent}, ${255 * greenPercent}, ${255 * bluePercent});`);

  return `background-color: rgb(${255 * redPercent}, ${255 * greenPercent}, ${255 * bluePercent});`;


}