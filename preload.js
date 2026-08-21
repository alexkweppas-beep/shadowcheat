const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('revolt', {
  launch: () => ipcRenderer.invoke('launch'),
  unload: () => ipcRenderer.invoke('unload'),
  close: () => ipcRenderer.invoke('close'),
  getHwid: () => ipcRenderer.invoke('getHwid'),
  openLink: (url) => ipcRenderer.invoke('openLink', url)
});
