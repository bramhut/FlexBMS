<script setup lang="ts">
import { useDataStore } from '@/stores/dataStore';
import Input from './ui/input/Input.vue';
import { ref } from 'vue';
import { getWriter } from '@/lib/utils';


const dataStore = useDataStore();

const commandText = ref<string>("");

function sendCommand() {
  let writer = getWriter();
  if (writer != undefined) {
    writer.write(commandText.value);
  }
  commandText.value = "";
}

</script>

<template>
  <div>
    <div class="m-4">
      <form action="#" @submit.prevent="sendCommand">
        <Input class="text-white bg-black" type="text" placeholder="Enter command...." v-model="commandText"></Input>
      </form>
      
    </div>
    <div class="bg-black max-h-72 h-72 m-4 rounded-xl overflow-y-scroll text-white text-lg font-mono p-2">
      
      <div v-for="line in dataStore.terminalBuffer" class="">
        {{ line }}
      </div>
    </div>
  </div>


</template>