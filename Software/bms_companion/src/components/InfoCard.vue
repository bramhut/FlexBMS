<script setup lang="ts">
import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from '@/components/ui/card'

const props = defineProps({
  title: String,
  value: String,
  unit: String,
});



</script>


<template>
    <Card>
              <CardHeader class="flex flex-row pb-2">
                <CardTitle class="font-medium text-sm m-auto">
                  {{ props.title }}
                </CardTitle>
              </CardHeader>
              <CardContent>
                <div class="font-bold text-center">
                    <span class="text-xl ">
                        {{ props.value }}
                    </span>
                    <span v-if="props.unit != undefined" class="text-l">
                        {{ props.unit }}
                    </span>
                  
                </div>
              </CardContent>
            </Card>
</template>