import { app, BrowserWindow, dialog, Menu, session } from 'electron'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const directory = path.dirname(fileURLToPath(import.meta.url))
let mainWindow

function configureSerialAccess(webSession) {
  webSession.setPermissionCheckHandler((_webContents, permission, _origin, details) =>
    permission === 'serial' && details.securityOrigin === 'file:///'
  )
  webSession.setDevicePermissionHandler(details =>
    details.deviceType === 'serial' && details.origin === 'file://'
  )
  webSession.on('select-serial-port', (event, ports, _webContents, callback) => {
    event.preventDefault()
    const choices = ports.filter(port => port.displayName)
    if (choices.length === 0 || !mainWindow) {
      callback('')
      return
    }
    void dialog.showMessageBox(mainWindow, {
      type: 'question',
      message: 'Select the FlexBMS USB serial port',
      buttons: [...choices.map(port => port.displayName), 'Cancel'],
      cancelId: choices.length,
      noLink: true,
    }).then(({ response }) => callback(response < choices.length ? choices[response].portId : ''))
  })
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1280,
    height: 900,
    minWidth: 720,
    minHeight: 560,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  })
  configureSerialAccess(mainWindow.webContents.session)
  Menu.setApplicationMenu(null)
  void mainWindow.loadFile(path.join(directory, '../dist/desktop/index.html'))
}

app.whenReady().then(() => {
  app.setAppUserModelId('nl.flexbms.companion')
  createWindow()
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow() })
})

app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit() })
