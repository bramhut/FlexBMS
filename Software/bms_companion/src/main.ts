import { createApp } from 'vue'
import './assets/index.css'
import App from './App.vue'
import { createPinia } from 'pinia'
import { useDataStore } from './stores/dataStore'


const pinia = createPinia()
const app = createApp(App)

app.use(pinia)

export const dataStore = useDataStore();
app.mount('#app')