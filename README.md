# Лаунчер

Release собирается отдельно:

```powershell
msbuild loader.sln /m /p:Configuration=Release /p:Platform=x64 `
  /p:ShadowVmpRoot=D:\tools\VMProtect
```

Линкер создаёт только временный `build/loader.raw.exe`. Post-build обязательно
пропускает его через VMProtect, проверяет секции/импорты и удаляет raw/PDB.
Shipping-файл — только `build/loader.exe`.

Для internal сервер возвращает персонализированный защищённый payload,
cohort `variant_id`, customer tag и короткоживущий product ticket. После
проверки SHA-256 loader передаёт variant/ticket через `DllMain::lpReserved`;
DLL принимает bootstrap только если cohort совпадает с её build identity.
Именованная IPC-секция для internal не используется.
