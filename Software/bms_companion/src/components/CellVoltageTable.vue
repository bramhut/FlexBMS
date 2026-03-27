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
import { ChevronsDown } from 'lucide-vue-next';


const dataStore = useDataStore();
const cellVoltages = computed(() => transpose(dataStore.cellVoltages));

const minMax = computed(() => getMinMaxValues(cellVoltages.value)) ;

const displayColors = ref(false);

function voltageToString(voltage) {
    if (voltage == undefined) {
        return 0;
    }
    return voltage.toFixed(3).toString()
}

</script>

<template>
    <Card class="m-4">
        
        <CardHeader class="flex flex-row pb-2">
            <CardTitle class="font-medium text-base m-auto">
              <span>Individual cell voltages</span> <Switch v-model:checked="displayColors"/>
            </CardTitle>
        </CardHeader>
        <CardContent>
            <Table>
                <TableHeader>
                    <TableRow>
                        <TableHead v-for="i in Object.keys(dataStore.cellVoltages)" :key="i" class="">CID {{ parseInt(i) + 1 }}
                        </TableHead>
                    </TableRow>
                </TableHeader>
                <TableBody>
                    <TableRow v-for="cells in cellVoltages">
                        <TableCell v-for="cell in cells" :style="displayColors ? getHeatmapColor(minMax[0], minMax[1], cell.voltage): ''">
                           
                            <div class="flex flex-row justify-center">
                                <div>{{ voltageToString(cell.voltage) }}</div>
                                
                                <ChevronsDown v-if="cell.isBalancing" style="width: 1.2em;"/>
                            </div>
                            
                            
                            
                        </TableCell>

                    </TableRow>
                </TableBody>
            </Table>
        </CardContent>
    </Card>

</template>

<style scoped>
    td, th {
        text-align: center;
    }
</style>