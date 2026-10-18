# G2: blt при показе выполняет GPU (KMD compute-задание)

Дата: 2026-10-11. Шаг G2 плана `2026-10-11-agx-driver-roadmap.md`. Решение
оператора: вариант 1 — документированный путь WDDM (`DxgkDdiPresent` строит
работу, которую исполняет GPU), без копий на CPU.

## Что есть сейчас (проверено чтением кода)

- `DxgkDdiPresent` (`present_windows.c` `AdmissionPresentBlt`) записывает в DMA-буфер
  описание blt: GPU VA источника и получателя из
  `pAllocationInfo[].AllocationVirtualAddress`, прямоугольники.
- Отправка (`AdmissionPresentSubmitVirtual`) уходит в CPU-очередь KMD
  (`AdmissionCpuQueueSubmit`). Рабочий поток ядра копирует построчно через
  промежуточный буфер (`AdmissionGpuvaG3ExecutePresentVirtual` →
  `AdmissionPresentBltExecuteGpuvaSeparate`); получатель пишется через
  write-combined отображение локального резерва. GPU не участвует.
- В KMD есть сборщики compute-задания прошивки (`apple_agx_g13_compute_work.c`:
  RunCompute и микропоследовательность, выведены из Asahi `fw/compute.rs`), память
  и очередь compute инициализируются при старте. Но compute-задание ни разу не
  исполнялось на железе:
  - compute запускается только как пролог к TA/3D;
  - этот путь передаёт VM slot 63 при проверке `< 0x10`;
  - после compute провайдер всегда продолжает TA/3D;
  - кодировщика CDM и шейдера копирования в KMD нет;
  - G4 и UMD compute отклоняют.

## WINDOWS CONTRACT

- `DXGKARG_PRESENT` (Microsoft Learn): источник `pAllocationList[1]`, получатель
  `pAllocationList[2]`. Растяжение задаётся соотношением `SrcRect`/`DstRect`, копия
  идёт по `pDstSubRects` (уже обрезанным dxgkrnl). `Flags.ColorFill` / `Blt`.
  Буфер исполняется через `DxgkDdiSubmitCommandVirtual` с `Flags.Present` в
  контексте устройства, которое показывает кадр; адреса — GPU VA этого процесса.
- `D3DKMTPresent` (Microsoft Learn, пример для OpenGL ICD): `Flags.Blt`,
  `hWindow`, `SrcRect`/`DstRect`. Перенаправление в DWM делает dxgkrnl. Требование:
  контекст, из которого показывают, должен быть «drainable».
- Завершение — обычный fence отправки (`DXGK_INTERRUPT_DMA_COMPLETED`); вытеснение
  и повторная отправка — как для render (GPU Preemption; не-paging пакеты
  повторяются с новым fence).

## AGX/ASAHI CONTRACT

- Фиксированного блиттера нет (`fw/workqueue.rs`: RunBlitter не используется).
  Копии в Mesa — compute (`libagx/copy.cl`) или render-задания.
- Минимальное compute-задание на G13G:
  - CDM-поток: Launch Word 0/1, global и local size, Barrier, Terminate —
    `genxml/cmdbuf.xml`, `libagx_dgc.h` `agx_cdm_launch`/`barrier`/`terminate`;
  - USC-слова: Uniform → u0 с аргументами, Shared (без shared-памяти),
    Shader, Registers, NoPreshader — порядок `agx_bg_eot.c`;
  - бинарник шейдера: выравнивание 128 байт, внутри окна
    `usc_exec_base + 4 GiB` той же VM;
  - буфер аргументов.
- RunCompute (`fw/compute.rs`, `queue/compute.rs`):
  - CDM base/end;
  - `usc_exec_base`;
  - буфер вытеснения CPMT (0x7f80 + 32 на t8103);
  - микропоследовательность StartCompute → WaitForIdle(Compute) → FinalizeCompute →
    RetireStamp;
  - sampler heap и helper программа равны 0, если шейдер их не использует.
- Раскладка: render target по умолчанию сжатый GPU-tiled. Линейный формат
  обязателен для SCANOUT/SHARED (`agx_pipe.c`), шаг строки выровнен на 16 байт.
  Сжатый источник шейдер на сырых указателях не прочитает.

## TRANSLATION

1. **Шейдер копирования** — наш собственный, без копирования кода libagx.
   - Строится генератором через `nir_builder` и компилируется компилятором AGX из
     нашей сборки Mesa (AD04). Результат — заголовок с бинарником G13G и метаданными
     (`nr_gprs`, `push_count`, преамбула) и хэшем исходника.
   - Аргументы: адреса источника и получателя, шаги строк, прямоугольник,
     коэффициенты растяжения (ближайший пиксель).
   - Варианты: копия linear→linear и заливка (ColorFill).
2. **Кодировщик CDM и USC-слов** в `shared/` (чистый C, хост-тесты против
   раскладки `cmdbuf.xml`).
3. **Арена KMD для показа в адресном пространстве процесса**, внутри окна USC:
   шейдер, USC-слова, CDM-поток, аргументы, буфер вытеснения. Выделяется один раз
   на процесс, рядом с частными диапазонами сцен G4.
4. **Самостоятельное compute-задание:**
   - вход провайдера рядом с `AppleAgxG13ProviderRunTa`;
   - завершение compute заканчивает задание и сигналит fence Windows (без TA/3D);
   - VM slot берётся из графа процесса (`AdmissionGpuvaG3BeginJob`), а не 63;
   - sidecar на задание.
5. **Present** (`AdmissionPresentSubmitVirtual`) ставит задание в очередь GPU
   (render packet + `AppleAgxSchedulerQueueFence`), а не в CPU-очередь. CPU-путь
   удаляется после проверки на железе; неподдерживаемые варианты (color key,
   поворот) отклоняются, а не копируются процессором.
6. **ICD:**
   - показывает через `D3DKMTPresent` (Blt, `hWindow`, источник — линейная
     аллокация, в которую GPU уже копирует кадр);
   - кольцо с CPU-копией и GDI удаляется;
   - позже — показ прямо из back buffer, когда шейдер научится читать сжатый
     источник через дескрипторы.

## WHAT IS STILL UNKNOWN

- Примет ли прошивка самостоятельный RunCompute от этого KMD: первый `NewQueue`
  для compute ни разу не отправлялся.
- Значения битов CDM Barrier (Mesa ставит почти все) — берём значения Mesa.
- Где разместить арену внутри окна USC процесса без конфликта с
  `APPLE_AGX_G4_USC_EXECUTION_BASE` и выделениями Mesa.
- Раскладка и формат поверхности перенаправления DWM, которую dxgkrnl отдаёт
  получателем при `D3DKMTPresent` из ICD (ожидаю линейный BGRA8 от нашего UMD;
  проверить приёмником).
- Достаточно ли барьера CDM и FinalizeCompute для того, чтобы DWM увидел запись.

## Уточнение после HW1 (EXP1214, 2026-10-11)

HW1 выполнен: первое compute-задание KMD (конверт AGX4 от UMD) завершилось,
COPY_BUFFER PASS. Найденные по пути контракты:

- compute-задание входит в Queue вместе с TA и 3D: notifier и JobList у Queue
  общие (шаблон EventControl, объект 11);
- threshold (`event_count`, объект 12) = 2 × render-последовательность + число
  compute-заданий с момента инициализации очереди. Так считает и Asahi:
  `notification_count = 2*nr_render + nr_compute`;
- gpu_buf, fw stamp и статистика принадлежат work queue и сохраняются между
  заданиями.

### Решение по арене (вместо второго приватного диапазона)

Отдельный второй диапазон не нужен. Приватный диапазон процесса (32 MiB, одна
leaf-таблица) резервируется по `BaseAddress = 0x11FE000000`: это верхний
leaf-пролёт USC-окна (root 1, middle 255). В `d3dkmddi.h` `BaseAddress`
описан как вход, выровненный по покрытию leaf-таблицы;
`AllowUserModeMapping = 0`, поэтому UMD туда не отобразит.

- Данные сцен G4 остаются в том же диапазоне: адреса данных — полные VA.
- Арена present (128 KiB) — экстент того же private pool. Поэтому
  attach/re-attach, квоты и уничтожение остаются в одной реализации.
- Это **HW2a**: размещение диапазона, квитанция `Wom1G3Reserve`, регрессия
  render и два compute-задания в одной жизни очереди.

### HW2b: арена и blt

1. **Арена создаётся в `DxgkDdiCreateContext`** (виртуальная адресация, не
   system/GDI) при простое процесса (нет задания, нет lease):
   - private-таблицы;
   - экстент 128 KiB;
   - ядра копии и заливки (`AppleAgxPresentArenaInstall`) записываются через
     WC-отображение пула;
   - страницы публикуются в leaf процесса.

   Почему не в момент present: у процесса там обычно идёт render-задание, а
   ждать в DDI нельзя. Так арену получают ровно процессы с GPU-контекстами —
   DWM и GL создают их при старте. Квитанция фиксирует создание и отказ с
   предикатом.
2. **Present** (`AdmissionPresentSubmitVirtual`): если у процесса контекста
   есть арена и геометрия поддержана (32 бит/пиксель, линейные поверхности),
   ставится KMD-задание compute:
   - render packet Kind COMPUTE, привязка present-команды к BackendImage;
   - все проверки (прямоугольники, диапазоны источника и получателя в графе
     процесса) делаются на стороне submit, как требует дефект сброса TDR.
3. **BeginJob:** граф процесса, VM slot 1.
4. **Build:** `AppleAgxPresentBuild` пишет CDM-поток и аргументы в арену. В
   этот момент прошлое задание уже завершено (одно задание за раз).
   `RunCompute`: CDM арены, буфер вытеснения арены +0x10000,
   `usc_exec_base = 0x1100000000`.
5. **Завершение:** fence present-контекста.

CPU-путь остаётся для процессов без арены. Он удаляется после проверки GPU-пути
на железе, как в пункте 5 TRANSLATION.

## Проверки на железе (по одной переменной)

1. **HW1 «первое compute-задание».** Диагностический escape KMD отправляет одну
   заливку известным значением в буфер процесса. Приёмник: fence завершился, CPU
   читает ожидаемое значение, сбоев нет. Отказ — квитанция прошивки или fault с
   адресом. Откат — обычный.
2. **HW2 «blt на GPU».** `DxgkDdiPresent` Blt идёт через compute-задание; тест
   (`D3DKMTPresent` из маленькой программы) сравнивает хэш результата с
   CPU-эталоном.
3. **HW3 «ICD через D3DKMTPresent».** CS 1.6: 100% показанных кадров, ноль
   CPU-копий в ICD, fps и темп как в EXP1209.
