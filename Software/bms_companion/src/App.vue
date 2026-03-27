<script setup lang="ts">

import { ref } from 'vue';
import { useDataStore } from './stores/dataStore';
import MainDataView from './views/MainDataView.vue';
import ErrorAlert from './components/ErrorAlert.vue';
import Button from './components/ui/button/Button.vue';
import { getWriter, sendHeartbeat, setWriter } from './lib/utils';
import { text } from 'stream/consumers';
import AlertDialog from './components/ui/alert-dialog/AlertDialog.vue';
import AlertDialogContent from './components/ui/alert-dialog/AlertDialogContent.vue';
import AlertDialogHeader from './components/ui/alert-dialog/AlertDialogHeader.vue';
import AlertDialogTitle from './components/ui/alert-dialog/AlertDialogTitle.vue';
import AlertDialogDescription from './components/ui/alert-dialog/AlertDialogDescription.vue';
import AlertDialogFooter from './components/ui/alert-dialog/AlertDialogFooter.vue';

// import "./assets/fonts/akira_bold.ttf";


const firstTimeConnected = ref(false);

const lostConnection = ref(false);
const errorMessage = ref("");

const dataStore = useDataStore();

let heartBeatInterValId;


async function initSerial() {
  let port = await navigator.serial.requestPort();

  await port.open({ baudRate: 9600 });
  // console.log(port);

  // TODO add more check if port was opened and stuff
  dataStore.reset();
  firstTimeConnected.value = true;
  lostConnection.value = false;
  errorMessage.value = ''
  
  let textDecoder = new TextDecoderStream();
  let textEncoder = new TextEncoderStream();
  const readableStreamClosed = port.readable.pipeTo(textDecoder.writable);
  const writableStreamClosed = textEncoder.readable.pipeTo(port.writable);

  const reader = textDecoder.readable.getReader();

  let writer = textEncoder.writable.getWriter();
  
  setWriter(writer);
  // writer = textEncoder.writable.getWriter();

  let incomingBuffer = "";

  heartBeatInterValId = setInterval(sendHeartbeat, 1000);

  try {
  while (true) {

    
    const { value, done } = await reader.read();
    if (done) {
      reader.releaseLock();
      break;
    }

    // console.log(value);
    incomingBuffer += value;
    let messages = incomingBuffer.split("\n");

    for (let i = 0; i < messages.length - 1; i++) {
      if (messages[i].includes("\n")) {

      }
      dataStore.parseIncomingMessage(messages[i]);
    }

    incomingBuffer = messages[messages.length - 1];

  }
  } catch (error) {
    console.log("LOST CONNECTION:");
    console.log(error);
    lostConnection.value = true;
    errorMessage.value = "Lost connection to Master...."
    clearInterval(heartBeatInterValId);
  }

}


</script>

<template>
  <div>
    <div v-if="!firstTimeConnected" class="flex mt-56">
     
      <div class="grid grid-cols-1 gap-4 mx-auto">
        <div>
          <img src="/HyDriven-logo.png" class="max-w-xs">
          <h1 class="text-center">BMS Companion</h1>
        </div>
        <div class="m-auto">
          <Button @click="initSerial">connect</Button>
        </div>
        
      </div>
  </div>

  <MainDataView v-if="firstTimeConnected" /> 
  <AlertDialog :open="lostConnection">
    <AlertDialogContent>
      <AlertDialogHeader>
        <AlertDialogTitle>Disconnected</AlertDialogTitle>
        <AlertDialogDescription>Lost connection with BMS Master</AlertDialogDescription>
        <AlertDialogFooter>
          <Button @click="initSerial">Reconnect</Button>
        </AlertDialogFooter>
      </AlertDialogHeader>

    </AlertDialogContent>
  </AlertDialog>
  </div>

</template>

<style scoped>
/* TODO This should be global  */
@font-face {
  font-family: "Saira Condensed";
  src: local("Saira Condensed"),url('./assets/fonts/Saira_Condensed-Bold.ttf') format('truetype');
}
    h1 {
      font-size: xx-large;
        font-family: 'Saira Condensed', sans-serif;
    }
</style>
