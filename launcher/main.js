const { app, BrowserWindow, ipcMain, shell } = require('electron');
const path = require('path');
const { exec, execFile } = require('child_process');

function createWindow () {
  const mainWindow = new BrowserWindow({
    width: 880,
    height: 560,
    frame: false,
    transparent: true,
    hasShadow: false,
    thickFrame: false,
    backgroundColor: '#00000000',
    resizable: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  });

  mainWindow.loadFile('лоадер.html');
}

app.whenReady().then(() => {
  createWindow();

  app.on('activate', function () {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', function () {
  if (process.platform !== 'darwin') app.quit();
});

// Настройка IPC для вызовов из лоадер.html
ipcMain.handle('launch', async () => {
  return new Promise((resolve) => {
    // Эмуляция задержки инжекта
    setTimeout(() => {
      console.log('Product launched');
      resolve("true");
    }, 1500);
  });
});

ipcMain.handle('unload', async () => {
  return new Promise((resolve) => {
    setTimeout(() => {
      console.log('Product unloaded');
      resolve("true");
    }, 1000);
  });
});

ipcMain.handle('close', () => {
  app.quit();
});

ipcMain.handle('openLink', async (event, url) => {
  await shell.openExternal(url);
});

ipcMain.handle('getHwid', async () => {
  return new Promise((resolve) => {
    exec('wmic csproduct get uuid', (error, stdout, stderr) => {
      if (error) {
        resolve("UNKNOWN-HWID");
        return;
      }
      const lines = stdout.split('\n');
      if (lines.length > 1) {
        resolve(lines[1].trim());
      } else {
        resolve("UNKNOWN-HWID");
      }
    });
  });
});
