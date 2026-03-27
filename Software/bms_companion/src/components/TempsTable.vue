<script setup lang="ts">
import {
    Table,
    TableBody,
    TableCaption,
    TableCell,
    TableHead,
    TableHeader,
    TableRow,
} from '@/components/ui/table'
import {
    Card,
    CardContent,
    CardHeader,
    CardTitle,
} from '@/components/ui/card'
import { useDataStore } from '@/stores/dataStore';
import { getHeatmapColor, getMinMaxValues, transpose } from '@/lib/utils';
import { computed, ref } from 'vue';
import Switch from './ui/switch/Switch.vue';



const dataStore = useDataStore();
const temps = computed(() => transpose(dataStore.temps));
const minMax = computed(() => getMinMaxValues(temps.value));

const displayColors = ref(false);


function tempToString(temp) {
    if (temp == undefined) {
        return 0;
    }
    return temp.toFixed(1).toString()
}

</script>

<template>
    <Card class="m-4">
        <CardHeader class="flex flex-row pb-2">
            <CardTitle class="font-medium text-base m-auto">
                <span>Individual temperatures</span>
                <Switch v-model:checked="displayColors" />
            </CardTitle>
        </CardHeader>
        <CardContent>
            <Table>
                <TableHeader>
                    <TableRow>
                        <TableHead v-for="i in Object.keys(dataStore.temps)" :key="i" class="">CID {{ parseInt(i) + 1 }}
                        </TableHead>
                    </TableRow>
                </TableHeader>
                <TableBody>
                    
                    <TableRow>
                        <TableCell v-for="ictemp, i in dataStore.ictemps" :key="i">
                            {{ tempToString(ictemp) }}
                        </TableCell>
                    </TableRow>
                    <hr style="height: 4px; background-color: black;">
                    <TableRow v-for="cells in temps">
                        <TableCell v-for="temp in cells"
                            :style="displayColors ? getHeatmapColor(minMax[0], minMax[1], temp) : ''">
                            {{ tempToString(temp) }}
                        </TableCell>
                    </TableRow>
                </TableBody>
            </Table>
        </CardContent>
    </Card>

</template>

<style scoped>
td,
th {
    text-align: center;
}
</style>