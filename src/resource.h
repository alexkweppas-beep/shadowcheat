#pragma once
// IDR_VULN_DRV — уязвимый драйвер (iqvw64e.sys / gdrv.sys) для kernel-маппинга.
// Вшивается в бинарь лоадера при компиляции.
// Основной driver.sys больше НЕ в ресурсах — скачивается с сервера в память.
#define IDR_VULN_DRV  101
