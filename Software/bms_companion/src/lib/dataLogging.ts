import { dataStore } from "@/main";

let writer = undefined;

export async function setEnableLogging(state: boolean) {
    if (state) {
        dataStore.setDataloggingMessage("");
        await saveFile();
        dataStore.resetDataDumpSize();
        let header = getHeaderRow();
        writer.write(getHeaderRow());
        dataStore.addDataDumpSize(header.length);
    } else {
        closeFile();
        writer = undefined;
    }
}

function getHeaderRow() {
    let base = "timestamp,pack_voltage,pack_current,soc,min_cell_volt,max_cell_volt,min_temp,max_temp,state,faults";
    let cellVoltages = dataStore.cellVoltages;
    let cids = Object.keys(cellVoltages);

    for (let i = 0; i < cids.length; i++) {
      for (let j = 0; j < cellVoltages[cids[i]].length; j++) {
        base += `,cell_${cids[i]}_${j}`
      }
    }

    for (let i = 0; i < cids.length; i++) {
        for (let j = 0; j < cellVoltages[cids[i]].length; j++) {
          base += `,bal_${cids[i]}_${j}`
        }
      }

    let temps = dataStore.temps;
    cids = Object.keys(temps);
    for (let i = 0; i < cids.length; i++) {
      for (let j = 0; j < temps[cids[i]].length; j++) {
        base += `,temp_${cids[i]}_${j}`
      }
    }

    let ictemps = dataStore.ictemps;
    for (let i = 0; i < ictemps.length; i++) {
        base += `,ictemp_${i}`
    }


    return base + "\r\n";

}

export async function captureData() {
    if (writer != undefined) {
        let row = dataStore.getCSVRow();
        await writer.write(row);
        dataStore.addDataDumpSize(row.length);
    }
    
}

export async function saveFile() {
    try {
        const opts = {
            types: [
                {
                    suggestedName: "BMS data log - " + (new Date()).toLocaleTimeString('nl-NL')+ ".csv",
                    description: "CSV",
                    accept: { "text/csv": [".csv"] },
                },
            ],
        };
        // create a new handle
        const newHandle = await window.showSaveFilePicker(opts);

        // create a FileSystemWritableFileStream to write to
        const writableStream = await newHandle.createWritable();

        writer = writableStream;

        // write our file
        // await writableStream.write(dataDump);

        // close the file and write the contents to disk.
        // await writableStream.close();
    } catch (err) {
        dataStore.setDataloggingMessage("Error opening file!");
        console.error(err.name, err.message);
    }
}

async function closeFile() {
    if (writer != undefined) {
        try {
            await writer.close();
            dataStore.setDataloggingMessage("Saved file!");
        } catch (err) {
            dataStore.setDataloggingMessage("Error closing file!");
            console.error(err.name, err.message);
        }

        
    }
}