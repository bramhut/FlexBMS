<script setup lang="ts">
import { AlertCircle } from 'lucide-vue-next'
import {Alert, AlertDescription, AlertTitle} from '@/components/ui/alert'

const props = defineProps({
    message: String
  });
</script>


  

<template>
    <Alert variant="destructive">
        <AlertCircle class="w-4 h-4" />
        <AlertTitle>Error</AlertTitle>
        <AlertDescription>{{props.message}}</AlertDescription>
    </Alert>
</template>