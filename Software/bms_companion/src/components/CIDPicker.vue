<script setup lang="ts">
import { Check, ChevronsUpDown } from 'lucide-vue-next'

import { ref } from 'vue'
import { cn } from '@/lib/utils'
import { Button } from '@/components/ui/button'
import {
  Command,
  CommandEmpty,
  CommandGroup,
  CommandInput,
  CommandItem,
  CommandList
} from '@/components/ui/command'
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from '@/components/ui/popover'
import { useDataStore } from '@/stores/dataStore'

const frameworks = [
  { value: 'next.js', label: 'Next.js' },
  { value: 'sveltekit', label: 'SvelteKit' },
  { value: 'nuxt.js', label: 'Nuxt.js' },
  { value: 'remix', label: 'Remix' },
  { value: 'astro', label: 'Astro' },
]

const value = defineModel<number>();
const dataStore = useDataStore();


const open = ref(false)
// const value = ref<number>();
</script>

<template>
  <Popover v-model:open="open">
    <PopoverTrigger as-child>
      <Button
        variant="outline"
        role="combobox"
        :aria-expanded="open"
        class="w-[200px] justify-between"
      >
        {{ value ? value: 'Select CID...' }}

        <ChevronsUpDown class="ml-2 h-4 w-4 shrink-0 opacity-50" />
      </Button>
    </PopoverTrigger>
    <PopoverContent class="w-[200px] p-0">
      <Command v-model="value">
        <CommandInput placeholder="Search cid..." />
        <CommandEmpty>No CID found</CommandEmpty>
        <CommandList>
          <CommandGroup>
            <CommandItem
              v-for="cid in dataStore.cidCount"
              :key="cid"
              :value="cid"
              @select="open = false"
            >
              <Check
                :class="cn(
                  'mr-2 h-4 w-4',
                  value === cid ? 'opacity-100' : 'opacity-0',
                )"
              />
              {{ cid }}
            </CommandItem>
          </CommandGroup>
        </CommandList>
      </Command>
    </PopoverContent>
  </Popover>
</template>