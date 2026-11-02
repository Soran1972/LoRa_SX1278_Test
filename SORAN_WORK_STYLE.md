# SORAN WORK STYLE

Company: SORAN ELECTRONIC FACTORY
Document Version: 1.1.0
Last Updated: 2026-10-18 +03:00
Scope: General work standard for SORAN firmware and web-interface projects

Version rule: After every completed document change, update the version and
last-updated date in both copies. Use MAJOR.MINOR.PATCH: PATCH for typo/format
corrections, MINOR for added or clarified rules, MAJOR for fundamental changes.
Increment once per completed change set, not for each line edited. Keep both
copies identical; update the date to the actual revision date.

## How to Use This Standard

Read this standard before project work. Sections1–8 are common rules; apply
section9 for STM32 and section10 for ESP32/Arduino and web interfaces as relevant.
Follow the project's established language, layout and approved configuration.

This document defines how to work. Product behavior, pins, baud rates, network
settings, storage addresses, commands, commercial rules and current progress
belong in each product's configuration and technical documents.

Keep an identical `SORAN_WORK_STYLE.md` in each participating repository so each
project remains usable independently. Maintain these as copies of one standard,
not separate platform policies. Synchronize intended revisions within the
authorized scope; do not silently edit copies in unrelated projects/libraries.

## 1. Small Safe Progress and Requested Scope

- Use SSP: work step by step, with one clear purpose per step. Inspect relevant
  files and the working tree; explain the affected files and intended effect.
- Questions, review and diagnosis authorize inspection and explanation. A clear
  implementation request authorizes necessary work within the requested scope.
- Retain authorization already given; do not repeatedly request confirmation for
  ordinary file access, checks or edits included in the approved task.
- Preserve user changes and the existing architecture. Do not silently add
  features, rename working APIs, move logic or perform unrelated cleanup.
- Deleting code/data, changing hardware assignments, schemas or partitions,
  adding dependencies and expanding a phase require authorization covering them.
- If a new material decision is needed, explain it before dependent work.
  Complete agreed checks and stop at the agreed boundary before unrelated work.
- Approval to change one part does not authorize changes to neighboring parts.

## 2. Existing Files and Libraries

- Keep reusable framework/library code separate from product application code.
  Prefer a project-owned module and small integration call when this preserves
  existing libraries without making the architecture unnecessarily complex.
- Before editing an existing file, identify it and explain why, the behavior
  impact and compatibility risk. Be especially careful with shared libraries
  and inherited project modules.
- Moving, removing or restructuring legacy logic requires explicitly approved
  scope. Preserve public interfaces and required standalone behavior.
- Mark necessary integration points with short English side comments naming
  the feature and purpose; a vague "changed" comment is not enough.
- Do not rename working conventions solely for style or hide a legacy edit as
  an isolated new feature. Keep the exact change visible and easy to review.
- Leave unrelated module/library documents untouched during product cleanup.
  A reusable component's guide stays with it so it can travel to other projects.

## 3. Structure, Services and Reuse

- Keep the application entry point short: initialization and service calls.
  Separate device drivers, shared services, application logic, communication,
  storage, presentation, tests, tools and documentation.
- Follow the established folder layout. Add folders only for clear practical
  responsibilities, not to create an elaborate hierarchy.
- Keep board/fixed configuration in clear configuration modules, rather than
  scattering constants throughout runtime code.
- Give each function/module one responsibility. Keep managers readable and
  preserve execution order and compile-time guards when organizing calls.
- Separate general transport from product commands and policy. Connect reusable
  services through clear interfaces, configuration or callbacks.
- Prefer cooperative tasks and elapsed-time checks. Document necessary bounded
  blocking operations; do not hide waits inside frequent service loops.
- Keep essential device operation independent of phone/browser connectivity.
- Preserve reusable boundaries during product development. Extract and test a
  standalone template after core behavior and hardware checks are stable;
  avoid speculative abstractions and premature framework expansion.

## 4. C/C++ Naming and SORAN Comments

Preserve existing public names and platform-required entry points. For new
independent C/C++ modules, prefer:

- `lower_snake_case` for functions, variables and filenames.
- `UPPER_SNAKE_CASE` for constants; `_t` for project-owned typedefs; enums for
  states, modes, commands and owners; `#pragma once` for headers.
- Small readable functions. Divide long functions into meaningful blocks and
  do not mix unrelated logic or make code unnecessarily clever.
- Avoid confusing prefixes such as `g_` and numeric `u` suffixes unless needed
  by existing conventions, compiler/API requirements or type safety.
- Avoid unnecessary allocation, oversized temporary buffers and repeated String
  growth in frequent loops; use bounded buffers where practical.

Write C/C++ comments in English, close to the code, explaining purpose, ownership
or non-obvious conditions. Do not align them far away or repeat obvious code.
For important declarations use roles such as `Struct:`, `Flag:`, `Counter:`,
`Timer:` or `Value:`:

```c
static uint8_t device_ready;     /* Flag: Set after initialization succeeds. */
static uint32_t last_poll_ms;   /* Timer: Start of the polling interval. */
static uint8_t retry_count;     /* Counter: Attempts for the active request. */
```

Use thin CCB dividers between meaningful logical blocks:

```c
/*--------------------------------[ Background services ]--------------------------------*/
```

Use an SFB banner above important public functions and task functions. Keep its
purpose short and practical; do not add banners to every tiny private helper:

```c
/*======================================================================================================================
 *  Function  : device_service_run
 *  Purpose   : Advance pending device work without blocking the main loop.
 *====================================================================================================================*/
```

C/C++ comments are removed during compilation. Embedded HTML/CSS/JavaScript
comments can consume Flash; their specific rules are in section10.

## 5. Hardware, Communication and Persistent Data

- Verify the exact board, pins, levels and active polarity. Preserve approved
  configuration and safe output defaults at boot/reset.
- Keep optional services/radios disabled until an approved phase needs them.
  Prefer available platform capabilities over unnecessary dependencies.
- Document protocol version, framing, bounds, payload, CRC, responses, timeouts
  and retries. Prefer bounded binary messages where appropriate; preserve the
  approved wire contract.
- Name each system side when explaining multi-controller/browser operations;
  state which side owns data, validation and persistent storage.
- Distinguish accepted requests, pending work and verified completion. Retries
  must not repeat a non-repeatable action; report uncertainty if completion
  cannot be confirmed instead of displaying false success.
- Use versioned schemas and document ownership/lifetime. Validate loaded data
  before hardware use. Store necessary data; calculate derived display values.
- Schema changes require migration or an explicit reset decision. Do not add
  hidden write-on-boot behavior or silently erase existing data.
- Document EEPROM addresses before using them, prevent overlap and preserve
  existing ownership. Maintain the full map with the controller owning the
  memory; other sides link to it instead of keeping competing full copies.

## 6. Verification and Memory Measurement

Use verification proportional to the change:

| Change | Verification |
| --- | --- |
| Calculations, validation or data | Relevant existing tests and meaningful boundary/error cases |
| Visible interface | Relevant tests and visual review on target displays/browser where possible |
| Firmware or embedded assets | Build for approved board/options; measure Flash and static RAM |
| Hardware/communication | Relevant device experiment and explicit acceptance result |
| Documentation only | Consistency, references and scope; no firmware build solely for prose |

- Separate actual results from pending checks. Do not claim visual, hardware,
  timing or communication success without evidence.
- Report bytes and relevant capacity limits, not percentages alone. Distinguish
  static RAM from stack/heap and embedded page size from the firmware binary.
- Record deltas after meaningful changes. Compare like-for-like board/core,
  partition and build options; investigate unexplained growth before a checkpoint.
- Do not change partitions to hide size growth. Keep approved diagnostic/build
  settings. Upload/program a board only when authorized.

## 7. Documentation Ownership

Keep one authoritative reference per topic. Do not create a new document for
every experiment or automatically add a standard list of empty files.

| Topic | Content |
| --- | --- |
| This general standard | Reusable collaboration, coding, verification and Git rules |
| Product rules | Product behavior, capabilities and approved commercial decisions |
| Progress report | Current state, completed checks, acceptance and agreed next step |
| Technical reference | Protocol, memory map or studied interfaces, maintained by their owner |
| Component guide | Usage of the reusable library/module, kept with it |
| README | Brief entry point and links to actual references |

- Include `SORAN ELECTRONIC FACTORY` near the top of important documents.
- Keep changing product facts outside this standard. Link references rather
  than copying their tables and maintaining conflicting versions.
- Update affected documents within the requested scope. Distinguish implemented,
  tested, planned and deferred behavior; keep history separate from current rules.
- Do not present a proposed phase as approved work. Consolidate/relocate documents
  only within authorized scope, preserving studied content and updating links.
- Documentation does not consume firmware Flash unless intentionally embedded
  in the application. Keep long explanations out of shipped web assets.

## 8. Git, Phase Boundaries and Handoff

- Run `git status`; identify the branch and existing changes before edits or
  branch switches. Preserve user work and previously authorized edits.
- Work in the intended project checkout. Use a dedicated branch for a substantial
  phase, with prefixes such as `codex/feature-name`, `codex/fix-name` or
  `codex/cleanup-name`. Follow owner instructions; small edits may continue on
  the authorized current branch. Do not silently start work on a production branch.
- Finish agreed work/checks and record acceptance before an unrelated phase.
- Commit after owner acceptance or an explicit commit request. Use a focused
  change set and clear English subject; identify what is included.
- Push, merge, rebase, force-push or delete branches only with authorization for
  that operation. Editing permission does not authorize publication.
- This standard itself authorizes no feature, hardware change, new phase,
  commit, push, merge or board programming.

Explain work concisely in Arabic with clear technical identifiers. Separate
measurements from estimates and actual behavior from intended behavior.
After editing, report the relevant items:

1. Actual changes, purpose and behavior/compatibility impact.
2. Existing, new, removed and generated files involved.
3. Checks/results and any remaining device verification.
4. Build and Flash/RAM delta when firmware was affected.
5. Git commit/push status.

Report genuine access/approval blockers with their cause. Routine authorized
work should not require repeated confirmations; explain any new owner decision
before proceeding with dependent work.

## 9. STM32 / HAL / CubeIDE

- Use STM32 HAL C style and preserve generated `Core`, `Drivers`, startup,
  linker, `.ioc` and IDE configuration boundaries.
- Keep reusable framework code separate from the product's `project` modules.
  Do not move application behavior into vendor drivers or generated files merely
  for convenience.
- Place necessary edits in CubeMX USER CODE regions where supported. Identify
  regeneration-sensitive edits and keep `.ioc` and generated settings consistent
  when those settings are within the authorized change.
- Keep standard interrupt entry points visible in the generated interrupt file;
  delegate work through clear project/library calls. Keep handlers bounded and
  defer longer processing to services.
- Use HAL time-base/elapsed-time checks for cooperative tasks. Preserve the
  approved manager call order and guards when grouping services.
- Build only the requested configuration; do not add or run Release builds as
  an automatic companion to Debug. Separate building from board programming.
- Keep RTT/printf diagnostic formatting independently configurable where used;
  disabling it must not disable essential communication or product services.

## 10. ESP32 / Arduino / Web Interfaces

### Arduino and Web Modules

- Preserve required `setup()`/`loop()` names and the product's approved board,
  core, USB/Serial, network and partition options.
- Keep the main `.ino` short. Put Wi-Fi, DNS, HTTP routes and page delivery in
  web modules, and hardware behavior/calculations in their own modules.
- Separate editable HTML/CSS/JavaScript from generated firmware assets. Editable
  sources are authoritative; run the project's generator after changes and
  verify deterministic output. Manual generated-file edits require explicit
  authorization for that exception.
- Reuse page shells/assets; prefer platform facilities over external libraries.
  A filesystem, external web framework, compression, minification, automatic
  comment stripping, large images/fonts or Base64 assets require approved need,
  output comparison and size measurement. Prefer compact CSS/SVG for simple
  graphics and PROGMEM for large immutable embedded assets.

### Interface, Translation and Web Comments

- Preserve existing JavaScript naming, commonly `camelCase`, and prefer pure
  calculation functions. Separate rendering, calculations, communication, storage,
  translation and configuration; avoid business logic inside DOM handlers.
- Preserve source metadata needed for editing. Validate inputs and keep records
  independent when changing one must not alter another.
- Use semantic labels and phone-friendly layouts with RTL where required. Put
  important choices before dependent controls and keep one clear workflow per
  operation rather than adding duplicate controls or parallel editors.
- Reuse the existing design and responsive breakpoints. Prefer `kebab-case` CSS
  classes; use inline styles only where generated data needs them.
- Put user-visible text in translation resources; technical identifiers may
  remain English. Do not scatter hard-coded translated text in source files.
- Embedded web comments should be short and necessary. Do not add decorative
  banners, long change histories or disabled code that increases firmware size.
- For visible changes, check representative phone widths such as360/390px and
  desktop where possible, including RTL. Regenerate changed embedded sources,
  run relevant tests, then compile using the approved options.
- Report the application partition maximum beside Flash percentages, separate
  page size from firmware size, and preserve the approved Core Debug Level.
