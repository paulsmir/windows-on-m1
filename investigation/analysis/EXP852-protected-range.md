# EXP852: почему защищённая для AGX страница принадлежит Windows

Дата: 2026-09-27. Только офлайн-анализ сохранённого EXP852 и текущих первичных
исходников; код, Air, firmware и пакеты не менялись. Решение пользователя:
ограниченный результат R133 (`d95cde20`, `85b28c65`) принят; общий fatal-recovery
контракт BuildPagingBuffer остаётся отдельной задачей.

## Вывод

Для **страницы `0x851420000` это ожидаемое освобождение загрузочной памяти,
а не обнаруженный дефект карты памяти Mu**. Mu прямо относит исходный FD к
`EfiBootServicesData` (тип 4). После ExitBootServices Windows вправе повторно
использовать его страницы; сохранённый дамп подтверждает, что данная страница
входит в physical-memory run Windows и передана VidMm при эвикции.

Слово «защищённый» здесь обозначает более узкий контракт: AGX broker исключает
диапазон из допустимых GPU backing по статическому снимку **до запуска Mu**.
Это не запрет CPU-доступа stage-2 и не обещание Mu оставить FD зарезервированным
после ExitBootServices. m1n1 отображает этот диапазон в гостя как обычную RW RAM.
Таким образом, корректные по отдельности правила имеют разный срок действия:
«когда-то содержало загрузочный образ» остаётся запретом для AGX, хотя уже
перестало быть запретом для аллокатора Windows. R133 безопасно обслуживает
такой случай как логическое отображение без GPU leaf.

Это не основание разрешить broker все запрещённые диапазоны. Если следующая
цель — GPU-доступ к освобождённым boot-only страницам, нужен контракт передачи
владения между Mu и m1n1, а не исправление VidMm и не безусловное снятие фильтра.

## Идентичность и границы доказательств

`E` ниже — `/Users/pavel/public_windows/.local/experiments/EXP852-r132-sysframes/`;
`A` — `/Users/pavel/public_windows/.local/experiments/EXP852-protected-range/`.
Ссылки `файл:строка` для исходников отсчитываются от корня worktree.

- EXP852 использует неизменённые R110 firmware: `E/firmware/J313_EFI-r110.fd`
  SHA256 `3a76857cb650cae6b8d7c5d22250debeec437cd28c2dfd032ebecb1cbd0f6eca`;
  `m1n1-r110.macho` SHA256
  `14872dba0a6ecab9d4e49e42237298b44909126f0d5f7267cc5deaed587372b1`.
- Записанные исходные коммиты: m1n1 `8769e5e981730ca5c971ad985e65bd47d005e8c0`,
  Mu `f0f1c50a040d490f78340b8995917ede24fc4220`.
  Содержимое восьми ключевых файлов (MemoryInitPeiLib, FDF, AGX ASL;
  retained_backing, retained_platform, golden_j313, hv_vm, Python HV)
  побайтно совпало с соответствующим HEAD этих подмодулей. Подмодули уже dirty;
  полные текущие diff-хэши отличаются от записанных в manifest, поэтому
  **не утверждается** идентичность всех dirty-деревьев или воспроизведение сборки.
  Для реально выполненного поведения опорой служат сохранённые log/contract/dump.
- `E/contract.bin`: SHA256
  `65039095770e4c185e50fecebdf85645b2ba039a35a9ba8aa5f1a60b538e3d71`.
  Декодирован штатным `tools/launch_contract.py:105` с проверкой внешнего и
  внутреннего CRC; четыре записи, checkpoint/sequence `0/1, 1/2, 2/3, 3/4`.
  Результат — `A/contract-decoded.json`.
- `E/full.log`: SHA256
  `2c082c9e82ef66734e000766e22448590a92337f8da54ce33e77924159d32b7e`.
- `E/hardware-evidence/MEMORY.DMP`: SHA256
  `dde45931f203446dc087f7f0d0891f0c38b5c3a35a1907d439ccfc0a9b885876`.
  CDB на билдере, Microsoft kernel symbols и совпавший private KMD PDB;
  исходный разбор — `investigation/analysis/R133-leaf-publication.md:100`.
  Дополнительные команды и вывод сохранены в `A/dump-pfn.txt` и
  `A/dump-physical-runs.txt`. Air для этого не использовался.

## 1. Что действительно записано в launch contract

Во всех четырёх snapshots содержатся следующие записи `regions[]`:

| Индекс / kind | Base | Size | Полуоткрытый диапазон |
|---|---:|---:|---|
| 0 / GUEST_RAM=0 | `0x850000000` | `0x18f708000` | `[0x850000000,0x9df708000)` |
| 1 / HEAP=1 | `0x850000000` | `0x1000000` | `[0x850000000,0x851000000)` |
| 2 / FIRMWARE=2 | `0x8510b4000` | `0x1d88000` | `[0x8510b4000,0x852e3c000)` |

Типы определены в `m1n1_windows/src/hv_launch_contract.h:39`; структура
region — строка 90; offset массива внутри snapshot — 152 байта
(`tools/launch_contract.py:23,139`). В первом framed record region2 начинается
на файловом offset `32 + 152 + 2*24 = 0xe8`. Эти числа взяты из декодированного
файла, не выведены из имени firmware. `hv_launch_golden_j313.c:62–77` содержит
те же исходные записи. Общий GUEST_RAM здесь намеренно перекрывается с
именованными boot-областями: это не список непересекающихся UEFI descriptors.

Страница смещена от начала firmware на `0x36c000`; все четыре 4-КиБ страницы
`[0x851420000,0x851424000)` целиком внутри region2.
`E/full.log:125,131,213,219–220` подтверждает размещение образа и вход Mu.

В snapshots checkpoint2/3 запись **mappings[46]** имеет:
`ipa=0x850000000`, raw `pa=0x8500007fc`, `size=0x18f708000`,
`attributes=0x100000001`. Последнее кодирует kind1 и increment1
(`m1n1_windows/src/hv_launch_j313.c:247–255`). Поле raw `pa` содержит PTE-биты:
это **не физический сдвиг на 0x7fc**. Python HV определяет RW/access/memattr
в `proxyclient/m1n1/hv/__init__.py:24–30` и передаёт их вместе с PA в
`map_hw:157–176`. `TraceMode.OFF` вызывает identity mapping на строках 346–348.
`E/full.log:195`: `PT[850000000:9df708000] -> HW:RAM-HIGH`.

Следовательно, для данной страницы launch-state stage-2 разрешает обычный
CPU RW-доступ IPA=PA. Регистрация region2 сама по себе не создаёт stage-2 deny.
Функция `load_raw` отображает RAM и отдельно удаляет TZ carveouts
(`__init__.py:2138–2141`); диапазон FD не является одним из удалённых carveouts.
Standalone-путь имеет ту же RAM identity-семантику в
`src/hv_autonomous_runtime.c:305–321`; это сравнение текущих контрактов, не
утверждение о новом standalone hardware-прогоне.

## 2. Что Mu отдаёт Windows

`mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.fdf:37–41` задаёт FD:
base `0x8510b4000`, size `0x1e00000`, конец `0x852eb4000`.
Это на `0x78000` больше загруженного firmware region2, но искомая страница
попадает в оба диапазона. Область FVMAIN_COMPACT: offset `0x8000`,
size `0x1d80000` (строки 77–79); в ней находится упакованный FVMAIN
(строки 343–367). Размер FD, размер загруженного образа и размер FV
нельзя молча считать одинаковыми.

`mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c`:

- строки 215–222 создают resource HOB `EFI_RESOURCE_SYSTEM_MEMORY`;
- строки 232–285 выделяют FD внутри системной DRAM, чтобы DXE не затёр его
  до завершения загрузки;
- **строки 287–292** создают allocation HOB для всего FD с типом
  **`EfiBootServicesData`**, а не `EfiReservedMemoryType` и не Runtime;
- для сравнения, framebuffer и R64 получают постоянный Reserved type
  на строках 327–328 и 441–442.

Это не только предположение по исходнику. В EXP852:

```text
E/full.log:260  Memory Allocation 0x00000004 0x8510B4000 - 0x852EB3FFF
E/full.log:324  Memory Range: 0x8510B4000 - 0x852EB4000. Type:4, Attributes: 0x0
E/full.log:262  Memory Allocation 0x00000000 0x8E0000000 - 0x8E3FFFFFF
```

Это карта DXE в момент печати, **не сохранённый последний GetMemoryMap
непосредственно перед ExitBootServices**. Однако тип FD согласуется с кодом,
а включение страницы в фактическую RAM Windows независимо подтверждено ниже.

UEFI 2.10 §7.4.6 разрешает OS loader после успешного ExitBootServices
использовать BootServicesCode/Data как свободную память:
[UEFI Boot Services](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html#efi-boot-services-exitbootservices).
ACPI §15.3 Table15.6 отображает UEFI type4 в AddressRangeMemory, тогда как
Reserved и Runtime types — в AddressRangeReserved:
[ACPI UEFI GetMemoryMap](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/15_System_Address_Map_Interfaces/uefi-getmemorymap-boot-services-function.html).
Поэтому повторное использование FD после этого перехода разрешено контрактом.

Освобождение исходного контейнера FD не означает освобождение исполняемого
runtime firmware. DXE loader выделяет runtime images как
EfiRuntimeServicesCode/Data (`mu/MU_BASECORE/MdeModulePkg/Core/Dxe/Image/Image.c:647–659`),
загружает их в выделенные страницы (729–754). Лог показывает отдельный
распакованный FV на `0x9dd9f1000` (281–284), загруженные drivers на иных адресах
(298,308) и отдельные Type5/6 allocations (340–342). Свидетельств живого
runtime-кода по адресу `0x851420000` нет; наблюдаемый тип этой страницы — boot data.

В исследованных ACPI-источниках дополнительной постоянной reservation FD нет.
В `DSDT.asl:232–243` PNP0C02/RES0 описывает ECAM `0x690000000`,
а `J313AppleAgxAbiAdmission.asl.inc:13–73` — четыре AGX ресурса и динамический
R64 `[0x8e0000000,0x8e4000000)`, не FD. `_CCA=1` (строка 6) описывает coherency,
не владение RAM. Саму начальную системную RAM loader получает через
UEFI GetMemoryMap, а не из списка AGX `_CRS` (ACPI §15.3 выше).
Лог EXP852 подтверждает установку DSDT/SSDT (701–702) и размещение ACPI tables
в районе `0x9dc...` (718–727). Полный post-EBS namespace здесь не извлекался;
доказательство, что FD не был исключён из RAM Windows, даёт сам kernel dump.

## 3. Windows уже владеет именно этой страницей

В kernel dump `nt!MmPhysicalMemoryBlock` указывает на `ffffa50981653730`;
NumberOfRuns=`0xd`. Run[1]: BasePage=`0x850000`, PageCount=`0xf000`, то есть
**`[0x850000000,0x85f000000)`** (`A/dump-physical-runs.txt:66–69`).
Это интервал физической RAM Windows; целевой PFN `0x851420` входит в него.

`!pfn 851420` даёт reference count1, PteAddress=`ffffe7849dc8e000`,
Modified/Shared, WriteComb (`A/dump-pfn.txt:61–65`). Это наблюдение PFN,
не доказательство совместимости какого-либо нового CPU alias или AGX cache type.

Ранее извлечённый DXGK input в том же дампе: paging ProcessId1,
UpdateMode=GPU_PHYSICAL, таблица offsetC000, StartIndex430/count400,
FirstPteVirtualAddress2430000; входные PFNs группы630 — 851420..851423,
flags1/segment0. Стек проходит `MapScratchAreaVaRange`,
`MemoryTransferUsingGpuVaWorker`, `TransferToSystem`, `EvictResource`.
См. main `.local/experiments/R133-offline/dump-update.txt:55`,
`dump-graph.txt:111`, `dump-targeted.txt:68` и R133 analysis.
Именно VidMm выбрал законную system-memory страницу для paging scratch mapping
при вытеснении; Windows не обязан знать частный список запретов AGX broker.

## 4. Почему broker всё равно запрещает grant; владелец следующего изменения

`m1n1_windows/src/hv_agx_retained_platform.c:395–402` один раз захватывает
PRE_HV_INIT launch snapshot в `launch_memory`. В этом модуле нет обработки
ExitBootServices и нет обновления этого снимка по UEFI memory types.
`translate_guest:48–67` сначала проверяет реальное stage-2 IPA→PA, затем
вызывает `hv_agx_retained_backing_allowed`. Последний отвергает пересечение
со **всяким** ненулевым region, кроме GUEST_RAM/LOW_MEMORY
(`hv_agx_retained_backing.c:18–26`). Именно FIRMWARE region2 блокирует эту группу.
`hv_agx_gpuva_v5.c:213–230` переводит нулевой результат translate_page в OWNERSHIP
до выдачи grant. Таким образом, stage-2 разрешение и AGX разрешение — две
раздельные проверки, а не противоречивые ответы одного механизма.

Для сопоставления: Asahi `drivers/gpu/drm/asahi/mmu.rs:485–510` отображает
принадлежащие GEM SG-runs с проверкой UAT alignment; он не доказывает, что
вечное исключение исходного Mu FD необходимо Windows. Инициализация/IRQ/power
не участвуют в этом отказе. Windows владеет residency, Mu — boot/runtime
разметкой, m1n1 broker — grant eligibility и UAT/TLB; KMD не вправе обходить
отказ m1n1. В R133 это уже соблюдено.

**Исправлять карту Mu для этой страницы оснований нет.** При желании устранить
ограничение GPU-доступа будущий владелец изменения — lifetime-протокол
Mu↔m1n1 и фильтр backing в m1n1: отдельные boot-only и retained/runtime
диапазоны, подтверждённое завершение boot-time использования и проверенная
передача лишь освобождённых страниц. Нельзя убрать FIRMWARE/все exclusions
одним условием или довериться произвольному KMD PFN. Альтернативная политика
«этот FD всегда принадлежит прошивке» потребовала бы постоянной reservation
Mu; это осознанное изменение политики с потерей доступной RAM, а не ремонт
доказанного дефекта текущей карты.

Минимальный будущий offline gate: до передачи boot-only frame запрещён;
после подтверждённой передачи разрешён лишь он; runtime/EL2 heap/UAT/ramdisk/
другие retained regions остаются запрещены; stale epoch и повторный handoff
не меняют владение. Ни реализация такого протокола, ни hardware checkpoint
этим анализом не выполнены/не авторизованы. Для будущего hardware сохраняются
R110 full-owner и неизменяемый ordinary EXP377/392 recovery; новые артефакты
нуждаются в отдельном preregistration и разрешении.

## Проверка и review

Проверены CRC всех четырёх contract records, интервальная принадлежность,
согласованность лога Mu, stage-2 snapshot и Windows PFN/run. Хэши первичных и
производных артефактов: `A/evidence-hashes.json`. Тесты продукта/сборка не нужны
для изменения одного аналитического документа; код не изменён.

REVIEW R111: ACCEPT — тип CPU alias не меняется; WriteComb из PFN не даёт нового разрешения.
REVIEW R110: ACCEPT — динамический 64-битный R64 `_CRS` сохранён и отделён от FD.
REVIEW R63: ACCEPT — R64 остаётся постоянной reservation; с boot-only FD его не смешивать.
REVIEW R57: DEFER — общий fatal DDI recovery вынесен пользователем за принятый R133.
Остальные OPEN dispositions из `R133-leaf-publication.md:47–95` сохраняются;
этот анализ не пересматривает исторические эксперименты и не разрешает Air.
