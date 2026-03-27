<script setup lang="ts">
import CellVoltageTable from '@/components/CellVoltageTable.vue';
import InfoCard from '@/components/InfoCard.vue';
import FaultCard from '@/components/FaultCard.vue';
import RegisterCard from '@/components/RegisterCard.vue';
import TempsTable from '@/components/TempsTable.vue';
import Terminal from '@/components/Terminal.vue';
import { useDataStore } from '@/stores/dataStore';
import ControlCard from '@/components/ControlCard.vue';


const dataStore = useDataStore();

function getStateString(stateValue: number) {
  if (stateValue == undefined) {
    return ""
  }

  switch (stateValue) {
    case 0:
      return "DEVICE INITILIAZATION"
    case 1:
      return "REGISTER INITIALIZATON"
    case 2:
      return "DIAGNOSTICS"
    case 3:
      return "RUNNING"
    case 4:
      return "PANIC"
  }
}

function getFaultString(fault: number) {
  if (fault == undefined) {
    return ""
  }
  let binaryString = fault.toString(2)
  let output = [];
  // console.log(binaryString);

  for (let i = 15; i >= 0; i--) {
    if (binaryString[i] == "1") {
      output.push(faults[15-i])
    }

  }

  return output
}


</script>




<template>
  <div>
    <div class="grid grid-cols-[80%_20%]">
      <div class="grid grid-cols-4 gap-4 m-4">
        <InfoCard title="State" :value="getStateString(dataStore.state)" />
        <!-- <InfoCard title="Faults" :value="getFaultString(dataStore.faults)" /> -->
        <InfoCard title="Pack voltage" :value="dataStore.packVoltage.toFixed(3).toString()" unit="V" />
        <InfoCard title="Pack current" :value="dataStore.packCurrent.toFixed(3).toString()" unit="A" />
        <InfoCard title="SOC" :value="dataStore.packSOC" unit="%" />
        <InfoCard title="Min. cell voltage" :value="dataStore.minCellVoltage.toFixed(3).toString()" unit="V" />
        <InfoCard title="Max. cell voltage" :value="dataStore.maxCellVoltage.toFixed(3).toString()" unit="V" />
        <InfoCard title="Min. temperature" :value="dataStore.minTemperature.toFixed(1).toString()" unit="°C" />
        <InfoCard title="Max. temperature" :value="dataStore.maxTemperature.toFixed(1).toString()" unit="°C" />
      </div>
      <div class="my-4 mr-4">
        <FaultCard></FaultCard>
      </div>

    </div>
    <div class="columns-2">
      <CellVoltageTable />
      <TempsTable />
      <RegisterCard />
      <ControlCard />
    </div>
    <div>
        <Terminal />
      </div>

  </div>
</template>
