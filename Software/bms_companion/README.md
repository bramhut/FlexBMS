# BMS Companion
BMS Companion is an application to interact with the custom BMS of Green Team Twente. It's main functionality is to display all live data of the BMS. 
The application communicates with the BMS over a serial connection. 
It was chosen to build this application as browser application since there is very little logic to implementon, it is mostly just making a UI. Which is easy in the browser :)

## Get started
To get started. First make sure you have a somewhat recent LTS version of nodeJS installed: https://nodejs.org/en
Then follow these steps:

- Install dependencies: `npm install`

- Run server: `npm run dev`

## Technologies

### Web Serial API
This application communicates with the BMS over serial, using the Web Serial API: https://developer.chrome.com/docs/capabilities/serial 

### VueJS
Vue3 is used in order to make building the UI a bit easier and have some nice state-management.

### Shadcn-vue
Shadcn-vue is a simple component library that provides some nice simple components for the UI. It uses tailwind under the hood, so tailwind is also used troughout the project.
https://www.shadcn-vue.com/ 

# Who to feut?
If there any problems or questions regarding this application, you may feut Arjan Blankestijn/ (I apologize for the spaghetti code)