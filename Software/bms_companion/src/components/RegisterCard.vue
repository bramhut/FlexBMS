<script setup lang="ts">
import {
    Card,
    CardContent,
    CardHeader,
    CardTitle,
} from '@/components/ui/card'
import CIDPicker from '@/components/CIDPicker.vue';
import Input from './ui/input/Input.vue';
import Button from './ui/button/Button.vue';
import { computed, ref } from 'vue';
import { CIDRegisters, RetrievedRegister, useDataStore } from '@/stores/dataStore';
import { getWriter } from '@/lib/utils';
import {
    Table,
    TableBody,
    TableCaption,
    TableCell,
    TableHead,
    TableHeader,
    TableRow,
} from '@/components/ui/table'
import { registerValues } from '@/lib/registers';



const selectedCID = ref<number>();
const selectedAddress = ref<string>("");
const addressUppercase = computed(() => selectedAddress.value.toUpperCase());

const dataStore = useDataStore();

function requestRegister() {
    let writer = getWriter();
    if (writer != undefined) {
        let message = "05" + String(selectedCID.value).padStart(2, '0') + selectedAddress.value.toUpperCase();
        console.log(message);
        writer.write(message);
    }
}

function getTimeString(date: Date) {
    return date.getHours() + ":" + date.getMinutes() + ":" + date.getSeconds();
} 

</script>


<template>
    <Card class="m-4">
        <CardHeader class="flex flex-row pb-2">
            <CardTitle class="font-medium text-base m-auto">
                <div class="flex-col flex center">
                    <div>Read Registers</div>
                    <div class="flex flex-row">

                        <CIDPicker v-model="selectedCID" />
                        <Input type="text" placeholder="address (HEX)" v-model="selectedAddress"></Input>
                        <Button @click="requestRegister">Request</Button>
                    </div>

                </div>

            </CardTitle>
        </CardHeader>
        <CardContent>
            <div v-if="selectedAddress != ''">
                <div v-for="cidRegister in dataStore.retrievedRegister">
                    <div v-if="cidRegister.cid == selectedCID">
                        <div v-if="cidRegister.registers[addressUppercase] != undefined">
                            <div>
                                <p>Received at: {{ getTimeString(cidRegister.registers[addressUppercase].timestamp) }}, HEX Value: {{ cidRegister.registers[addressUppercase].regValue }}</p>
                            </div>


                            
                            <Table>
                                <TableHeader>
                                    <TableRow>
                                        <TableHead v-for="i in 16" :key="i" class="font-extrabold">{{ 16 - i }} </TableHead>
                                    </TableRow>
                                    </TableHeader>
                                    <TableBody>
                                        <TableRow v-if="registerValues[addressUppercase] != undefined">
                                            <TableCell v-for="value in registerValues[addressUppercase]" :colspan="value.length">{{ value.name  }}</TableCell>
                                        </TableRow>
                                        <TableRow>
                                            <TableCell v-for="char in parseInt(cidRegister.registers[addressUppercase].regValue, 16).toString(2).padStart(16, '0')">{{ char }}</TableCell>
                                        </TableRow>
                                    </TableBody>
                            </Table>
                        </div>
                    </div>

                </div>
            </div>

        </CardContent>
    </Card>
</template>
<style scoped>
td, th {
    border: 1px solid black;
    text-align: center;
}

th {

}
</style>