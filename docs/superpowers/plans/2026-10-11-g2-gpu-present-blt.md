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
