<script setup lang="ts">
import {
    Card,
    CardContent,
    CardHeader,
    CardTitle,
} from '@/components/ui/card'

import Button from '@/components/ui/button/Button.vue';
import { getWriter } from '@/lib/utils';
import { captureData, saveFile, setEnableLogging } from '@/lib/dataLogging';
import { useDataStore } from '@/stores/dataStore';
import Switch from './ui/switch/Switch.vue';

const dataStore = useDataStore();

function sendTime() {
    let writer = getWriter();
    if (writer != undefined) {
        // let date = new Date();
        let message = "09" + Date.now().toString(10);
        console.log(message);
        writer.write(message);
    }
}

function handleLoggingSwitchChange(checked) {
    setEnableLogging(checked);
}

function handleClearFaults() {
    let writer = getWriter();
    if (writer != undefined) {
        let message = "11";
        writer.write(message);
    }
}

function formatBytes(a, b = 2) { if (!+a) return "0 Bytes"; const c = 0 > b ? 0 : b, d = Math.floor(Math.log(a) / Math.log(1024)); return `${parseFloat((a / Math.pow(1024, d)).toFixed(c))} ${["Bytes", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"][d]}` }

</script>


<template>
    <Card class="m-4">
        <CardHeader class="flex flex-row pb-2">
            <CardTitle class="font-medium text-base m-auto">Controls</CardTitle>
        </CardHeader>
        <CardContent class="flex flex-row justify-evenly">
            <Button @click="sendTime">Sync time</Button>
            <div>
                Log to file:
                <Switch @update:checked="handleLoggingSwitchChange" />
                {{ formatBytes(dataStore.dataDumpSize) }}
                {{ dataStore.dataLoggingMessage }}
            </div>
            <Button @click="handleClearFaults">Clear faults</Button>

        </CardContent>
    </Card>
</template>