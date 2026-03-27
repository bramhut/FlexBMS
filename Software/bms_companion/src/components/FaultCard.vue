<script setup lang="ts">
import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from '@/components/ui/card'
import { useDataStore } from '@/stores/dataStore';

const dataStore = useDataStore();




</script>


<template>
    <Card>
              <CardHeader class="flex flex-row pb-2">
                <CardTitle v-if="dataStore.faults.length > 0" class="font-medium text-sm m-auto text-red-700">Faults active</CardTitle>
              </CardHeader>
              <CardContent>
                <div v-if="dataStore.faults.length > 0" v-for="fault in dataStore.faults" class="font-bold text-center">
                    <span class="text-xl ">
                        {{ fault }}
                    </span>
                </div>
                <div v-else>
                    <span class="text-xl text-green-700">
                        No faults
                    </span>
                </div>
              </CardContent>
            </Card>
</template>