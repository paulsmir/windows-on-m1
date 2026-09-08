# Apple AGX → ускоренный рабочий стол Windows

Обновлено: 2026-09-08. Planning baseline: `60a24f79c8c56e5ea49955d9fe19e2a259b1901d`.

## Где мы сейчас

**Текущий этап: AD03 — Mesa compiler/encoder для изменяемых GPU workloads.**
AD00 завершён в ограниченном объёме fixed/private qualification.
EXP648 hardware доказал: UMD loaded → GetCaps pipeline0 → CreateDevice не вызван.
Loader/admission-location investigation закончено; следующий EXP ради того же ответа не нужен.

AD01 OFFLINE_PROVEN: выбран `mesa-d3d-reuse` на
`D3D10_0_DDI_INTERFACE_VERSION` / `D3D_FEATURE_LEVEL_10_0`; exact selected
pipeline mask1, advertised mask0. Machine contract содержит121 mandatory rows,
все отсутствующие implementation rows отмечены false. Permanent retirement
failure теперь отделён от successful release; DBWIN capture имеет ready/stop,
PID/token и loss-aware acceptance. x64 mock/runtime и ARM64 analysis gates GREEN.

**Сейчас нельзя утверждать:** accelerated DWM, standard Present, desktop-ready UMD.
**Сейчас можно утверждать:** AGX/физические кадры/fences/private scanout работают в проверенном scope.

Последняя проверенная машина: ordinary377/392 после EXP648, Code28, no AGX package,
8 CPU, healthy storage/USB/input по сохранённому health. Перед любой операцией состояние
проверяется заново; эта строка не заменяет live preflight.

AD02 HW_PROVEN по EXP651: два distinct immutable128-byte commands прошли
Render/Patch/Submit, physical AGX fences256/257, exact dynamic full-green и
bottom-band-blue output hashes, два query/latch,15s HOLD и clean retirement.
Ближайшее действие: написать и исполнить source-first AD03 compiler/encoder
plan. Hardware допускается только после typed job/relocation/resource tests;
feature-level readiness и advertised mask остаются0.

## Как читать и обновлять карту

| Статус | Значение |
|---|---|
| NOT_STARTED | Реализация этапа не начата |
| IN_PROGRESS | Есть работа, критерии выхода не выполнены |
| OFFLINE_PROVEN | Exact source прошёл обязательные executable tests/build gates |
| HW_PROVEN | Exact hardware acceptance выполнен, evidence связано с artifacts |
| BLOCKED | Указан конкретный внешний blocker; не синоним сложной задачи |

Для offline-only AD01 финал — OFFLINE_PROVEN. Для остальных аппаратных этапов
OFFLINE_PROVEN не открывает следующий hardware-dependent gate без HW_PROVEN.
Проверки независимых lower modules можно делать раньше, но не объявлять интеграцию завершённой.
Не считать проценты по числу commits/tests/EXP. Этапы имеют разный объём работы.

После значимого результата обновить только соответствующую строку, «Где мы сейчас»,
proof map и краткий GPU_CURRENT_STATE.md. Detailed logs — experiment-local.
Исторические EXPERIMENTS.md/CHANGES.csv не переписывать для превращения failure в success.

## Карта этапов

| ID | Результат | Зависит от | Сейчас | Чем закрывается |
|---|---|---|---|---|
| AD00 | Fixed AGX render/fence/scanout foundation | — | HW_PROVEN | EXP640 + EXP591/598; ограничения ниже |
| AD01 | Выбранный D3D/frontend contract, исправный lifetime, полный gap inventory | AD00 | OFFLINE_PROVEN | Contract matrix, offline tests, source/build lock |
| AD02 | Windows allocation/command transport для динамического renderer | AD01 | HW_PROVEN | EXP651 two distinct dynamic commands/outputs/fences/latches |
| AD03 | Mesa compiler/encoder исполняет изменяемые GPU workloads | AD02 | IN_PROGRESS | Geometry/texture/blend outputs и повторные fences |
| AD04 | Полный обязательный UMD contract выбранного feature level | AD01–03 | NOT_STARTED | Callback/caps/format matrix и executable conformance gates |
| AD05 | Стандартный D3D device и аппаратное offscreen rendering | AD04 | NOT_STARTED | D3D11CreateDevice на Apple + реальные API workloads |
| AD06 | DWM/runtime-managed presentation и кадр на панели | AD05 | NOT_STARTED | Корреляция DWM/API → AGX → primary → D589 |
| AD07 | Ускоренный интерактивный desktop | AD06 | NOT_STARTED | Explorer/окна/курсор/resize и 10 минут DWM hardware work |
| AD08 | Рабочая стабильность, recovery и финальная установленная версия | AD07 | NOT_STARTED | Полный acceptance design, 30 минут, ресурсы/restart |

```text
AD00 DONE
  → AD01 OFFLINE_PROVEN
  → AD02 HW_PROVEN transport
  → AD03 CURRENT dynamic GPU
  → AD04 complete UMD
  → AD05 D3D device
  → AD06 standard Present/DWM
  → AD07 accelerated desktop
  → AD08 stable installed driver
```

## Proof map: что не повторять

| Доказательство | Scope | Не означает |
|---|---|---|
| EXP475/477/478 | retained root, broker, firmware, runtime start | Dynamic API frontend |
| EXP581/585/586/588 | Windows-triggered fixed render, output, fence | Arbitrary shader/state/resource support |
| EXP591/598 | Физически наблюдённый scanout/AGX image | Рабочий desktop |
| EXP631/632/634/640 | 2/4/16 fixed frames, output/latches/HOLD/retirement | Standard DXGI Present или DWM |
| EXP649/650/651 | immutable dynamic command transport, two colors/geometries, fences/output/latches | General shader/encoder or D3D pipeline |
| EXP636–640 | Scope0x101 расследован; bounded verifier16 runs проходит | Все CPU/driver stability причины устранены навсегда |
| EXP646/648 | Caps0 blocks D3D CreateDevice; DLL load подтверждён | Нужно просто включить bit |
| EXP641/643/644/647 | Direct presentation обходные пути не закрыли desktop | Нужно ещё менять token/timing/exclusive owner |

Canonical evidence: записи этих EXP в `investigation/EXPERIMENTS.md` и
`.local/experiments/EXP640-bounded-output/`, `.local/experiments/EXP648-umd-admission/`.
Последние локальные UMD fixes: `fac65c79f4b179219b6afaf3bad64d0064b0049e`,
`3a44a29cf6a88117ae10dba85980cb2119be57ec`; review exceptions ещё открыты.

## AD01 — что точно строим и какими DDIs это выражается

**Deliverable:** `docs/superpowers/specs/accelerated-desktop-contract.json` и DDI
matrix, выбранные DDI/feature level и reuse path, исправленные error/retirement tests.

**Сначала проверить:** UMD0mask сохраняется; permanent deallocation failure не
объявляется успешным release; generic E_FAIL не считается recoverable там, где runtime
превращает его в device loss; DBWIN empty/self-test не даёт false positive.

**Решение:** сопоставить current WDDM1_3 UMD table, D3D feature obligations и pinned
Mesa frontend (software/TGSI caveat). Planning candidate — feature level10_0; выбрать
его только после полного аудита requirements. Не обещать DWM только по CreateDevice.

**Gate:** каждая mandatory строка имеет implementation owner, test ID, dependency и
первичный источник. Все отсутствующие операции перечислены; не скрыты под «minimal D3D».
Frontend выбран и не возвращается в пересмотр после каждой сложности.
Hardware: не требуется; EXP648 не повторять.

## AD02 — динамические allocation и transport

**Existing:** `src/umd_render_windows.c`, `src/submission_windows.c`,
`src/memory_runtime_windows.c`, `src/physical_memory_windows.c`, shared memory/residency.
**Planned:** `shared/include/apple_agx_win32_abi.h`, `shared/src/apple_agx_win32_abi.c`,
`mesa/winsys/agx_win32_transport.[ch]`, `shared/tests/apple_agx_win32_abi_test.c`,
`tests/test_apple_agx_win32_abi.py`. Пути относительно `drivers/apple-agx/`, кроме `tests/`.

- [x] Immutable allocation-relative command snapshot: sizes/version/generation/count/access/ranges.
- [x] Не доверять CPU/physical/firmware pointers; referenced BO ranges принадлежат device.
- [x] Pin/residency semantics различены с private OpenCount; queued references сохраняются.
- [x] Map/unmap/upload/readback/reset epochs и rollback before/after publication покрыты.
- [x] Нужное desktop resource sharing спланировано через supported Windows handles,
      с per-process ownership; не переносить «single process/no sharing» как desktop architecture.
- [x] Generic clear меняет цвет/размер/attachment через данные запроса, не patch констант.

**Offline:** malformed/overflow/stale owner/generation, producer mutation after validation,
noncontiguous ADL/range handling, active surface write, failure rollback, fence lifetime.
**Hardware:** один generic-clear workload на минимум двух допустимых значениях
geometry/color, exact full output/fence. Это ещё не D3D feature-level PASS.

## AD03 — shader/encoder path, а не replay

**Existing references:** Mesa `src/asahi/compiler`, `src/gallium/drivers/asahi`,
`src/gallium/frontends/d3d10umd`, Asahi Linux materializer, m1n1 AGX.
**Planned owned files:** `drivers/apple-agx/mesa/winsys/`,
`drivers/apple-agx/render-admission/src/render_dynamic_windows.c`,
`drivers/apple-agx/shared/src/apple_agx_dynamic_job.c`,
`drivers/apple-agx/shared/tests/apple_agx_dynamic_job_test.c`.

- [ ] Compile validated API shader input through chosen frontend → NIR → AGX compiler.
- [ ] Typed relocations cover complete encoder/pipeline/descriptor graph; no stale captured VA.
- [ ] Vertex/index/constants/textures/samplers/attachments have allocation-relative references.
- [ ] Geometry, viewport/scissor, texture and blend changes give distinct predicted output.
- [ ] Resource barriers/readback/format/color-space are explicit, not inferred from one color.
- [ ] Keep hardware queue/RTKit/root implementation shared with proven backend.

**Hardware checkpoints:** triangle; textured draw; alpha-overlap/scissor; repeated varying
frames, output oracle and fences. These may be separate experiments with one new invariant
each. Do not use software rasterizer outputs as proof that GPU executed the workload.

## AD04 — полная обязательная UMD поверхность

**Modify:** `render-admission/umd/src/umd.c`, `umd_resource_lifetime.c`, `umd_internal.h`,
UMD project and tests. **Planned modules:** `umd/src/umd_adapter.c`, `umd_device.c`,
`umd_resource.c`, `umd_views.c`, `umd_pipeline.c`, `umd_commands.c`, `umd_present.c`.
Split incrementally only as behavior moves; do not duplicate competing owners.

- [ ] Complete chosen-level required device/format/resource/RTV/DSV/SRV/state/DDIs.
- [ ] Complete required shader stages/draw/topology/sampling/depth/blend semantics,
      including GS/stream-output/MSAA/etc when chosen level makes them mandatory.
- [ ] Immediate context serialisation, callback threading, Flush, deferred release,
      resource association and rotation identity have executable tests.
- [ ] Implement required sharing/primary/open-resource contract for cross-process desktop.
- [ ] API unsupported/invalid/device-removed errors obey each DDI, not blanket E_FAIL.
- [ ] CreateDevice table, GetSupportedVersions, pipeline mask and format caps derive
      from the same complete implementation contract.

**Gate:** all mandatory matrix rows implemented/tested; no success stubs. Capability
publication may then enter a hardware candidate. Hardware verification status still NO
until AD05. A small passing app cannot waive other mandatory feature-level obligations.

## AD05 — D3D runtime использует Apple аппаратно

**Existing control:** `.local/experiments/EXP646-d3d11-device-probe/d3d11_probe.cpp`.
**Planned committed test:** `tools/apple-agx-desktop-probe/main.cpp`, its pinned project
and `tests/test_apple_agx_desktop_probe.py`; promote/reuse the exact useful probe code.

- [ ] Select Apple by LUID; D3D_DRIVER_TYPE_UNKNOWN with explicit adapter, no automatic WARP fallback.
- [ ] D3D11CreateDevice negotiates selected level, callbacks/version match, device ACTIVE.
- [ ] Standard API creates variable resources/views and submits clear + textured/blended draw.
- [ ] Readback oracle passes; UMD/BO/job/fence identities and AGX counters correlate to this process.
- [ ] Recreate devices/resources100 times; correct destruction and reset generation behavior.

**Hardware PASS:** runtime-created Apple device and at least two non-fixed API workloads
actually execute AGX and complete. No new cold-loader-only run.

## AD06 — стандартный Present и DWM bring-up

**Existing:** `umd/src/umd.c` Present functions, `src/present_windows.c`,
`src/display.c`, `src/scanout_windows.c`, `src/render_present.c`, m1n1 scanout broker.

- [ ] Run in real console session with HWND and runtime-created swapchain, not exclusive KMT shortcut.
- [ ] Trace actual DWM device creation and first required DDIs; shared resources/primary
      contract are completed before claiming windowed Present will work.
- [ ] Choose supported standard DXGI swap effect/format from AD01 contract; do not accept
      arbitrary flags. Handle occlusion/minimize/resize per runtime semantics.
- [ ] Same output allocation tracked through runtime Present → primary → DCP latch.
- [ ] Present fence, GPU completion and display retirement remain distinct.
- [ ] VSync subscription/refresh cadence checked separately from per-flip D589.
      No fake physical vblank from an unqualified timer.
- [ ] Existing direct qualification presentation is disabled for this acceptance workload.

**Hardware PASS:** standard application image appears on physical panel and updates;
DWM composition executes on Apple/AGX, not only application rendering. Record actual
PID/device/LUID/packet/fence correlation; physical observation is additional evidence.
If DWM fails creation, stay in AD04/AD06 contract correction, do not launch WGL as a bypass.

## AD07 — рабочий стол, которым можно пользоваться

- [ ] Physical login → Explorer desktop via same accepted adapter.
- [ ] 10 minutes of window move/resize/overlap/transparency, scrolling and cursor movement.
- [ ] DWM-owned AGX work increases during composition; no software fallback.
- [ ] ResizeBuffers/minimize/restore/destroy do not corrupt active/pending surfaces.
- [ ] Show a recognizable animated window; capture screen photo/video AND machine correlation.
- [ ] No deadlock/stuck PresentGate/stale fence/duplicate latch; input remains responsive.

**PASS:** `ACCELERATED_DESKTOP_VISIBLE=YES`; still not AD08 stability acceptance.

## AD08 — завершение текущего пользовательского результата

- [ ] Execute full 30-minute acceptance from design, with >=1000 standard presents.
- [ ] >=100 resource/swapchain lifecycle cycles with measured outstanding allocations/fences.
- [ ] Device/context teardown and controlled restart/login; no active buffer freed early.
- [ ] Exact installed package/versions/hashes/commits and dependencies frozen.
- [ ] Save limits, supported features/formats, sleep/resume status and recovery instructions.
- [ ] Leave desktop-accepted driver installed/enabled for use; do not silently restore Code28
      merely because this checkpoint ended. If it fails acceptance, rollback remains required.

**PASS:** `ACCELERATED_DESKTOP_ACCEPTED=YES`. Original OpenGL/CS1.6 mission is not
declared complete; these become subsequent roadmap work, not prerequisites for desktop.

## Reporting после каждого значимого изменения

```text
CURRENT: ADxx / task-name
DONE: exact contract and proof (offline or HW)
NOT PROVEN: nearest open requirement
SOURCE/ARTIFACT: commit + manifest/evidence path
NEXT: one executable action
MACHINE: last verified state + timestamp
```

Если exact HW verdict не продвинул boundary, следующий action обязан отличаться причиной,
а не номером EXP. Два inconclusive runs из-за одного transport → исправить transport offline.
Не повторять locked lower proof. Не назначать заранее «следующий fix будет scheduler».

## Открытые риски — не скрытые задачи

- Реальная стоимость TGSI/NIR/Windows winsys frontend reuse определится в AD01.
- Required feature-level matrix шире задач compositor; неполный subset нельзя рекламировать.
- DWM standard sharing/presentation — отдельный contract, WARP control не доказывает его.
- Fixed64MiB-era pool может оказаться недостаточен для desktop resources. Сначала измерить
  budgets/residency в AD02, затем менять owner layer по evidence, не увеличивать константу вслепую.
- CPU mapped-memory0x101 workaround не считается универсальным устранением всех watchdog причин.
- License lock одного MIT файла не заменяет per-file/dependency review при интеграции Mesa.

## Дальше текущей цели

После AD08: WGL/ICD ARM64 → x86 process/ABI → compatibility OpenGL → CS1.6 scene →
application stability. Прежние Mesa/OpenGL документы используются здесь как материал,
а не как причина отложить Direct3D/DWM desktop.
