# Guide for "teaching" GitHub Copilot a phase-1 repo

Use this when **taking over an existing firmware repo** (phase 1) to add features, refactor, and
optimize. Go through phases A to G in order. Every prompt below can be copy-pasted straight into
Copilot Chat in VS Code.

## 0. Understand this before you start

- **Copilot remembers nothing between chat sessions.** "Teaching" Copilot means **writing the
  knowledge into a file** that Copilot loads automatically every time:
  - `.github/copilot-instructions.md`: loaded on every request.
  - `.github/instructions/*.instructions.md`: loaded per `applyTo`.
  - `docs/*.md`: Copilot reads these when linked to, or when it finds them via `#codebase`.

  Knowledge that only lives in chat history is lost when you open a new chat.
- **Copilot can make things up.** Every prompt below requires `file:line` evidence. Documentation
  Copilot generates must be **reviewed by a person** before it's fed into instructions (phase B and
  C4).
- **The 3 main documents are filled in from the phase-1 code.** In the kit, `docs/architecture.md`,
  `docs/dataflow.md`, and `docs/security.md` **only contain section headings**. The process is:
  - Phase A: write raw survey notes into `docs/phase1/`.
  - Phase C: the agent **fills in content under each heading** from the phase-1 code. Keep the
    section names and order unchanged; sub-headings may be added as needed.

  From that point on, these 3 files are the source of truth Copilot reads in every later session.
- **Pick the right mode:**

  | Task | Chat mode |
  |---|---|
  | Survey, Q&A, no file changes | **Ask**, or the **Planner** agent |
  | Write survey notes to a file | **Agent** (only allowed to create/edit files under `docs/`) |
  | Plan a refactor/feature | the **Planner** agent |
  | Implement | **Agent** |

- **Context references in a prompt:**
  - `#codebase`: lets Copilot search the whole repo.
  - `#file:path/to/file`: attaches one specific file.
  - `#selection`: the currently selected code.

  Attach a specific file when you already know which one to read, for a more accurate answer.
- **One topic per new chat.** A chat that runs too long makes Copilot "forget" the beginning and mix
  up information.

---

## Phase A0 – Repo setup (done by a human, ~30 minutes)

1. Create a dedicated branch: `git switch -c chore/copilot-onboarding`.
2. Copy the kit into the repo root: `.github/`, `.vscode/`, `docs/`, `tools/`, `.clang-format`,
   `.clang-tidy`, `.clangd`, `.editorconfig`, `.gitmessage`. **Don't overwrite** a same-named file
   already in the old repo without comparing them first (especially `.gitignore`,
   `.vscode/settings.json`, `partitions.csv`).
3. `sh tools/install-hooks.sh`.
4. **Capture a baseline** before touching any code, and write it to `docs/phase1/baseline.md`:
   ```bash
   mkdir -p docs/phase1
   idf.py set-target esp32c5 && idf.py build 2>&1 | tee docs/phase1/build-baseline.log
   idf.py size > docs/phase1/size-baseline.txt
   idf.py size-components >> docs/phase1/size-baseline.txt
   grep -c ': warning: ' docs/phase1/build-baseline.log
   git tag phase1-baseline
   ```
   On real hardware, record: minimum heap after 1 hour of running, per-task stack watermark (if a
   CLI command exists for it), current draw, and time from boot to being connected.
5. **Don't run clang-format on the old code yet.** Formatting happens in phase E, once
   characterization tests exist.
6. Temporarily add the following to the end of `.github/copilot-instructions.md`, to be removed in
   step D2:
   ```markdown
   ## Current status: SURVEYING a phase-1 repo
   - The existing code does NOT yet follow the rules above. Do NOT refactor/fix old code unless asked.
   - When describing code: always cite `file:line`; clearly distinguish "observed in the code" from
     "inferred".
   - docs/architecture.md, docs/dataflow.md, docs/security.md currently only have headings (no content yet).
   ```

---

## Phase A – Surveying the current state (Copilot does the work, a human reviews it)

Each prompt runs in **a new chat**, in **Agent** mode, so Copilot writes its findings to a file under
`docs/phase1/`. This is **raw notes**, the input for phase C.

### A1. Repo overview

```text
Survey the whole repo #codebase. READ ONLY, do not modify code.
Create docs/phase1/overview.md containing:
1. What the device does, based on what the code shows.
2. The directory tree (2 levels) + a one-line description of each folder/component.
3. ESP-IDF version, target, toolchain (look in CMakeLists.txt, sdkconfig.defaults, dependencies.lock, CI).
4. The list of components (components/ and managed_components/ declared in idf_component.yml), their role, and dependencies.
5. How to actually build/flash/monitor (commands, existing scripts).
6. Anything unclear or contradictory -> an "Open questions" section.
Every claim must have file:line evidence. Don't guess; if you can't find something, write "not found".
```

### A2. Boot sequence, tasks, and concurrency

```text
Read #file:main/main.c and every place calling xTaskCreate*/esp_timer_create/esp_event_handler_register across #codebase.
Create docs/phase1/runtime.md containing:
1. The boot sequence from app_main() in execution order (cite file:line for each step).
2. A task table: name, function, stack, priority, core, what it waits on (queue/semaphore/delay), whether it registers with the TWDT.
3. A table of timers, ISRs, event handlers, and callbacks (which context each runs in).
4. Resources shared between tasks (global/static variables, buffers) and the protection mechanism (mutex/none).
5. Suspected race conditions/deadlocks/blocking in a callback, with code evidence.
Do not modify code.
```

### A3. Each component (repeat for every component)

```text
Analyze the component #file:components/<name>/ (read every .c/.h file, CMakeLists.txt, Kconfig).
Add a "## <name>" section to docs/phase1/components.md containing:
- Responsibility (1-2 sentences), public API (signature + meaning), internal state, its own task/callbacks.
- Current error handling: does it check esp_err_t, does it have a timeout, does it retry/backoff.
- Memory: where malloc/free happen, fixed buffer sizes, unbounded string functions (strcpy/sprintf...).
- Violations of .github/instructions/c-esp-idf.instructions.md (list them, do NOT fix them).
- How reusable it is for a refactor/new feature: keep as-is / wrap it / rewrite it (with reasoning).
Cite file:line. Do not modify code.
```

### A4. Data flow

```text
Trace the data flow across #codebase, create docs/phase1/dataflow-current.md with mermaid diagrams:
1. Uplink: from reading a sensor -> packaging -> sending over WiFi/MQTT and/or LoRa (which function calls which, which buffers).
2. Downlink: from receiving an MQTT message / AT "+EVT:RX" -> parsing -> executing the command.
3. OTA: receiving the command -> downloading -> verifying -> reboot -> confirm/rollback.
4. Display and buttons: where a button event goes, where the display gets its data, who calls the draw function.
5. On connection loss: is data dropped, buffered, or does the flow block?
Every arrow in a diagram must correspond to a real function call (cite file:line in a table under the diagram).
```

### A5. External communication (the contract with the server/hardware – must NOT be broken)

```text
Search #codebase and create docs/phase1/interfaces.md:
1. MQTT: broker URL/port, TLS or not, where the cert is, client id, LWT, the topic list (pub/sub, QoS, retain),
   the payload format of each topic (a real JSON example from the code).
2. LoRaWAN: region/band, class, OTAA/ABP, fPort, binary payload format (byte-by-byte), confirmed/unconfirmed.
3. Every AT command sent to the RAK3172 (command, timeout, how error responses are handled) and every "+EVT" handled.
4. OTA: URL, command format, how the image is verified.
5. CLI/console: the command list.
This is the "contract" with the server currently running: clearly mark which items the server depends on.
```

### A6. Configuration, storage, hardware

```text
Create docs/phase1/config-hw.md from #codebase:
1. The current partitions.csv (as a table), configured flash size.
2. NVS: namespace, keys, data types, the struct stored (layout, whether it has a version/crc), when it's written.
3. The project's Kconfig and important values in sdkconfig.defaults (security, WDT, logging, rollback).
4. Pin map: every GPIO/UART/SPI/I2C in use (pin, function, file:line). Flag any pin conflicts found.
5. Which security features are enabled/disabled: Secure Boot, Flash Encryption, NVS encryption, TLS verify.
```

### A7. Technical debt and risks

```text
Based on #codebase and the files already created under docs/phase1/, create docs/phase1/risks.md:
List issues by severity BLOCKER/MAJOR/MINOR (as defined in .github/instructions/review.instructions.md),
each item with: file:line, a concrete failure scenario (input/state -> consequence), and its impact for long-term operation.
Prioritize finding: buffer overflows, missing timeouts, blocking in a callback/ISR, memory leaks, retries with no backoff,
frequent NVS writes, hard-coded/logged secrets, unverified TLS, OTA with no rollback, tick-counter overflow.
Only record an issue that has evidence in the code. Do not modify code.
```

> Tip: in step A7, also run `/review-code <directory>` for each critical component
> (`rak3172`, transport, OTA), then merge the results into `risks.md`.

---

## Phase B – Verifying the survey notes (done by a human, mandatory)

Wrong documentation is worse than no documentation, because Copilot will trust it in every later
session.

1. Have Copilot double-check each file it wrote, each one in **a new chat**:
   ```text
   Check every claim in #file:docs/phase1/runtime.md against the real code in #codebase.
   For each claim: TRUE / FALSE / COULD NOT VERIFY + file:line evidence.
   Fix the FALSE ones directly in the document; mark anything unverifiable as "(unverified)".
   ```
2. The person in charge reads it over, cross-checking against real hardware: the boot log, trying CLI
   commands, capturing MQTT packets with `mosquitto_sub`, watching an uplink on the network server.
3. Ask the phase-1 team (if still reachable) about the "Open questions" items.
4. Commit: `docs: add phase1 current-state analysis`.

---

## Phase C – Filling in `architecture.md`, `dataflow.md`, `security.md` from the phase-1 code

Inputs, in order of trust:
1. **The real code**: `main/`, `components/`, `CMakeLists.txt`, `Kconfig*`, `sdkconfig.defaults*`,
   `partitions.csv`, `idf_component.yml`.
2. **Comments in the code and git history**: used to understand *why* the code was designed that way.
3. **Verified notes** under `docs/phase1/`.

The 3 files already have their headings in place. The agent **fills in content under each heading**,
keeping the section names and order unchanged (sub-headings may be added). If code has no matching
feature for a heading, write "Not present in the phase-1 code" — **never delete the heading**.

Write each file in **a new chat**, in **Agent** mode, in order C1 → C2 → C3. Dataflow and security
reuse component names from architecture, so architecture must be written first.

> If the repo is large: run the prompt section by section (e.g. "only write sections 1–5"), then
> "continue with sections 6–13" in the same chat. This makes Copilot actually read enough code for
> each section instead of producing a shallow summary.

### C1. `docs/architecture.md` – current architecture

```text
Fill in #file:docs/architecture.md (currently just headings), describing the CURRENT architecture of the
phase-1 firmware, learned from the real code. Keep the section names and order unchanged; sub-headings may
be added; if the code has no matching feature for a heading, write "Not present in the phase-1 code" -
never delete a heading.

Sources (in order of trust):
1) Code in #codebase: main/, components/, CMakeLists.txt, Kconfig*, sdkconfig.defaults*, partitions.csv, idf_component.yml.
2) Comments in the code and git history (run `git log --stat --follow <file>` in the terminal) to understand design decisions.
3) Verified notes: #file:docs/phase1/overview.md #file:docs/phase1/runtime.md
   #file:docs/phase1/components.md #file:docs/phase1/config-hw.md #file:docs/phase1/size-baseline.txt

Rules:
- Every technical claim has file:line evidence (or a commit hash).
- Numbers (stack, priority, buffer/queue sizes, timeouts, partitions) are taken VERBATIM from the code/Kconfig,
  naming the source macro/Kconfig. Do not round or estimate.
- Anything not read directly from the code is marked "(inferred)". Anything not found is written as "not found in the code".
- Describe only, do NOT modify code, do NOT propose a new design.

Content to fill in for each heading (the numbers match the section numbers in the file):
1. Overview: what the device does, the hardware (MCU, RAK3172, display, buttons, sensor - model if the code shows it),
   ESP-IDF version, target, firmware version (version.txt / PROJECT_VER).
2. A layering and component-dependency diagram (mermaid flowchart), built from REQUIRES/PRIV_REQUIRES in CMakeLists.txt
   and the real #include statements. Flag any circular dependency, or a lower layer calling back into a higher one.
3. A component table: name, responsibility, main public API, main file, dependencies.
4. Mechanisms/patterns in use (function-pointer interfaces, dispatch tables, state machines, callbacks, events, queues...):
   only what's actually in the code, with its location.
5. A task/timer/ISR table: name, function, stack, priority, core, what it waits on, whether it registers with the Task WDT.
6. State machines present in the code: the state enum + transition conditions (mermaid stateDiagram),
   each transition cited with file:line. If state is managed via several scattered bool flags, describe it exactly as such.
7. The boot sequence from app_main() in exact execution order, with the behavior when each step fails.
8. Configuration: the project's Kconfig, important values in sdkconfig.defaults, NVS (namespace/key/struct/version/crc).
9. Partitions and memory: a table of partitions.csv, app size, remaining free percentage of the app/OTA slots.
10. Design decisions observed (from code/comments/git log) and the reasoning, if found.
11. Deviations from .github/instructions/c-esp-idf.instructions.md (a summary, linking to docs/phase1/risks.md).
12. Open questions.
13. Directory structure (2 levels).

Write it concisely, favoring tables. When done, list in your reply (not in the file) every instance of
"(inferred)" or "not found in the code" for the reviewer to confirm.
```

### C2. `docs/dataflow.md` – current data flow

```text
Fill in #file:docs/dataflow.md (currently just headings), describing the CURRENT data flow of the
phase-1 firmware, learned from the real code. Keep the section names and order unchanged; sub-headings may
be added; if the code has no matching feature for a heading, write "Not present in the phase-1 code" -
never delete a heading.

Sources: code in #codebase; verified notes #file:docs/phase1/dataflow-current.md
#file:docs/phase1/interfaces.md; #file:docs/architecture.md (just filled in, use its component/task names).

Rules:
- Every claim has file:line evidence. Inferences are marked "(inferred)". Anything not found is written as "not found in the code".
- Every arrow in a mermaid diagram must be a real function call / queue / event / callback.
  Under each diagram, add a table "step | function | context (task/ISR/callback) | file:line".
- MQTT topics, fPort, AT commands, JSON keys, NVS key names: quote the exact strings from the code.
- Binary payloads: reconstruct the layout from the real encode/decode code (offset, type, endianness, scale factor).
- Describe only, do NOT modify code, do NOT propose a new format.

Content to fill in for each heading (the numbers match the section numbers in the file):
1. An overview diagram (mermaid flowchart): sensor, buttons, display, transport, server, OTA, configuration.
2. Uplink: reading a sensor -> processing -> packaging -> sending (WiFi/MQTT and LoRa) (mermaid sequenceDiagram);
   who owns the buffer, which task it runs in, where the send interval comes from.
3. Downlink: an MQTT message / AT "+EVT:RX..." -> parsing -> validation (if any) -> execution -> a response (if any).
4. OTA: receiving the command -> downloading -> verifying -> writing -> reboot -> confirm/rollback.
5. Buttons: interrupt/polling -> debounce (where, how many ms) -> where the event goes -> the resulting action.
6. Display: who calls the draw function, where the data comes from, how often it updates, which task it runs in.
7. Configuration: CLI / UI / downlink -> validation -> writing to NVS -> applied immediately or after a reboot.
8. Connection loss and recovery: is data dropped / buffered / does the flow block; how reconnect and backoff work.
9. Message formats - the CONTRACT with the server (state clearly: changing this section is a BREAKING CHANGE):
   9.1 MQTT: broker/port/TLS, client id, LWT; a topic table (exact strings, pub/sub, QoS, retain);
       a sample JSON for each topic, built from the encoding code.
   9.2 LoRaWAN: region/band, class, OTAA/ABP, an fPort table, the byte-by-byte payload layout, confirmed/unconfirmed.
   9.3 AT commands used with the RAK3172 (command, timeout, error handling) and every "+EVT" handled.
10. A queue/buffer table: name, length, element size, who writes / who reads, behavior when full.
11. Edge cases not handled in these flows (a summary, linking to docs/phase1/risks.md).
12. Open questions.

When done, list in your reply every instance of "(inferred)" or "not found in the code".
```

### C3. `docs/security.md` – current security posture

**A step for a human to do first** (Copilot must not run any eFuse command): on a board running the
phase-1 firmware, read the eFuse state. This command is **read-only**:

```bash
idf.py -p /dev/ttyACM0 efuse-summary > docs/phase1/efuse-summary.txt
```

Review the file before committing it (it contains the MAC and chip ID). If no board is available,
skip this; Copilot will then write "undetermined" for anything depending on eFuse data.

```text
Fill in #file:docs/security.md (currently just headings), describing the CURRENT security posture of the
phase-1 firmware, learned from the real code and configuration. Keep the section names and order unchanged;
sub-headings may be added; if something doesn't apply, write "Not present in the phase-1 code" - never delete
a heading.

Sources: code in #codebase; sdkconfig.defaults*; build/config/sdkconfig.h (the ACTUAL configuration values after building);
partitions.csv; embedded cert/key files (EMBED_TXTFILES/EMBED_FILES in CMakeLists.txt);
#file:docs/phase1/config-hw.md #file:docs/phase1/interfaces.md #file:docs/phase1/risks.md;
docs/phase1/efuse-summary.txt if present; #file:docs/architecture.md #file:docs/dataflow.md (use their names).
Do NOT run espefuse.py, espsecure.py, or any flash-writing command.

Rules:
- Every claim has file:line evidence (or a sdkconfig option name + value).
  Inferences are marked "(inferred)". Anything undeterminable is written as "undetermined".
- Do NOT copy real secrets (passwords, keys, tokens, private keys) into the document:
  only cite the file:line location and replace the value with "<REDACTED>".
- Describe the current state only, do NOT modify code, do NOT propose enabling eFuse features.

Content to fill in for each heading (the numbers match the section numbers in the file):
1. Assets to protect: WiFi/MQTT credentials, LoRaWAN keys (DevEUI/AppEUI/AppKey), TLS certs/keys, signing keys, firmware.
   For each: where it's stored (NVS key / hard-coded / embedded file / inside the RAK3172), whether it's encrypted,
   whether it's ever logged or printed to the CLI (file:line).
2. A security configuration table with ACTUAL values and their source: Secure Boot (version, scheme), Flash Encryption (mode),
   NVS encryption, app rollback, anti-rollback/secure_version, JTAG/USB-JTAG, ROM download mode,
   default log level, Task WDT/panic behavior, core dump.
3. Network connections: MQTT (mqtt:// or mqtts://, where the CA cert is embedded, is the hostname verified,
   is skip_cert_common_name_check used, client cert, username/password); HTTPS OTA (cert verified?); SNTP if any.
4. OTA: where the command comes from, is it authenticated, how is the image verified (signature / sha256 / version),
   rollback, protection against downgrades.
5. Entry points for external data: MQTT downlink, LoRa downlink, AT responses, CLI, NVS data, buttons.
   A table: entry point | handler function | is length/range/format validated | file:line.
6. Debug/maintenance interfaces: console/CLI (which commands are dangerous, are they authenticated, are they disabled in release), JTAG.
7. A threat model for the CURRENT state. A table: threat | current mitigation (file:line) | gap |
   severity BLOCKER/MAJOR/MINOR (per .github/instructions/review.instructions.md).
8. A production checklist with the current status: ☑ done / ☐ not done / ? undetermined.
9. Open questions.

When done, list in your reply every instance of "(inferred)" or "undetermined".
```

### C4. Verifying the 3 documents (mandatory)

1. **Copilot double-checks each file**, each one in a new chat:
   ```text
   Check every claim in #file:docs/architecture.md against the real code in #codebase.
   For each claim: TRUE / FALSE / COULD NOT VERIFY + file:line evidence.
   Check that every file:line citation in the document still points to the exact line it claims to.
   Fix the FALSE ones directly; mark anything unverifiable as "(unverified)". Show a diff.
   ```
   Repeat for `docs/dataflow.md` and `docs/security.md`.
2. **Cross-check all 3 files against each other** (a new chat):
   ```text
   Cross-check #file:docs/architecture.md #file:docs/dataflow.md #file:docs/security.md against each other and against #codebase:
   component names, task names, numbers (stack/priority/queue/timeout), topics, fPort, NVS keys, partitions
   must be consistent across all 3 files and match the code.
   List every inconsistency (its location in the docs + file:line in the code) and fix it to match the code. Show a diff.
   ```
3. **Check for empty headings** (a new chat):
   ```text
   In #file:docs/architecture.md #file:docs/dataflow.md #file:docs/security.md, list every heading that is still empty,
   too thin, or says "Not present in the phase-1 code".
   For each: search #codebase again to confirm the code really doesn't have that feature (cite file:line for where you
   looked), or whether not enough code was read earlier. If you find something, fill it in. Show a diff.
   ```
4. **Human review:**
   - Find the spots needing confirmation:
     `grep -n "(inferred)\|not found\|undetermined\|unverified" docs/architecture.md docs/dataflow.md docs/security.md`
   - Confirm against the code, the board, or the phase-1 team.
   - Open a few random `file:line` citations to check them.
   - Check that the mermaid diagrams render correctly. Check on GitHub, or install the
     *Markdown Preview Mermaid Support* extension for VS Code.
5. Commit: `docs: fill architecture, dataflow, security from phase1 code`.

---

## Phase D – Feeding the knowledge into instructions (Copilot proposes, a human approves)

### D1. Gap analysis between the current state and the quality rules

```text
Compare the current state (#file:docs/architecture.md #file:docs/dataflow.md #file:docs/security.md #file:docs/phase1/risks.md)
against the team's rules (#file:.github/copilot-instructions.md section "Mandatory architecture rules",
#file:.github/instructions/c-esp-idf.instructions.md, section 8 "Production checklist" in docs/security.md).
Create docs/phase1/gap-analysis.md: a table of each rule/aspect (layering, interfaces, error handling, memory, concurrency,
long-term robustness, security, testability) -> [met | partially met | not met], with the relevant component, file:line,
the risk, and a suggested approach (keep as-is / wrap it / refactor / rewrite).
Point out any rule that does NOT fit this repo (if any), for the team to consider adjusting the rule itself.
```

### D2. Updating `copilot-instructions.md`, skills, and the agent to match the real repo

```text
Propose edits to #file:.github/copilot-instructions.md to match this repo, based on
#file:docs/architecture.md #file:docs/dataflow.md #file:docs/security.md:
- Update the "Product", "Repo layout", "Mandatory architecture rules", and "Build & check commands" sections to reflect reality.
  Any rule referring to something not yet in the code (e.g. transport_if_t) becomes "a target, applying to new code only".
- Add a "Contracts that must not be broken" section: MQTT topics/payload, LoRa payload, NVS layout, partitions
  (a one-line summary each + a link to section 9 of docs/dataflow.md).
- Add a "Legacy code" section: which directories/files don't yet follow the rules, and a rule that "only refactor when the task asks for it".
- Keep the file under 2 pages; put detail in docs/ and link to it.
Then check .github/skills/*/SKILL.md and .github/agents/*.agent.md: list anywhere they reference a file/component/function
that doesn't exist in this repo, and propose a fix to match reality.
Show everything as a diff, do NOT write to any file yet.
```

The human reviews the diff before it's written. **Remove** the "SURVEYING" section added temporarily
in step A0.6.

### D3. Dedicated instructions for old code (optional)

If there's a large area of old code that won't be refactored right away, create
`.github/instructions/legacy.instructions.md`:

```markdown
---
name: 'Legacy code rules'
description: 'Rules for editing inherited phase-1 code that has not been refactored yet.'
applyTo: 'main/legacy/**,components/old_*/**'
---
- This code does NOT yet follow c-esp-idf.instructions.md. When fixing a bug: make the minimal fix, don't refactor alongside it.
- Don't change a public function's signature, a topic, a payload, or an NVS key.
- Any behavior change requires a characterization test (test/host/) first.
```

(Replace `applyTo` with the real paths in the repo.)

### D4. Checking whether Copilot has actually "understood"

Open **a new chat**, ask the questions below **with no file attached**, then check the *References*
section to see whether Copilot loaded the right instructions:

```text
1. When the device loses WiFi for 10 minutes, where does the sensor data from that period go?
2. Adding a "battery_mv" field to the sensor message requires changing which files, and would it break the server?
3. Which task can block the longest, and what's the consequence?
4. Does the firmware currently verify the OTA image's signature?
```

A wrong or vague answer means the docs or instructions are still incomplete. Add to them, then ask
again in a new chat.

---

## Phase E – A safety net before refactoring

### E1. Characterization tests (locking in current behavior)

```text
For the pure logic functions in #file:components/<name>/<file>.c (the AT parser, payload encode/decode, config validation),
write host Unity tests under test/host/ following #file:.github/instructions/testing.instructions.md.
Goal: RECORD THE CURRENT BEHAVIOR (even behavior that looks wrong - mark it with a "CURRENT BEHAVIOR, see risks.md" comment),
do NOT modify the source code. If a function can't be tested because of a hardware dependency, list it and suggest a minimal way to split it out.
```

Priority: payload encode/decode (the contract with the server, section 9 of `docs/dataflow.md`), the
AT parser, config load/save.

### E2. Format all the old code (a separate commit)

```bash
clang-format -i $(git ls-files '*.c' '*.h' ':!:managed_components/**')
idf.py build                    # must build identically, 0 behavior change
git commit -am "style: apply clang-format to phase1 code"
git rev-parse HEAD >> .git-blame-ignore-revs && git add .git-blame-ignore-revs
git commit -m "chore: ignore formatting commit in git blame"
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

A dedicated formatting commit keeps later refactor diffs clean, and `git blame` still points to the
original author.
After this step, the `file:line` citations in the docs may be off by a line or two. Re-run the first
prompt from step C4 to refresh them.

### E3. Turn on static analysis

```bash
idf.py clang-check --exclude-paths managed_components
```

```text
Read warnings.txt (the clang-tidy output), group it by category and component, and cross-check it against docs/phase1/risks.md.
Add to risks.md any warning that's a genuine bug (ignore style warnings). Do not modify code.
```

---

## Phase F – Building the backlog (the **Planner** agent)

```text
Based on #file:docs/phase1/gap-analysis.md #file:docs/phase1/risks.md #file:docs/security.md
and the list of new requirements below, build a backlog in docs/phase1/backlog.md.

New requirements:
- <feature 1>
- <feature 2>
- <an optimization goal: e.g. reduce current draw, reduce connection time, reduce firmware size>

Each backlog item has: a type (fix/refactor/feat/perf/sec), a description, the components affected, what it depends on,
the risk of breaking a contract (section 9 of docs/dataflow.md), how it will be verified, and a S/M/L estimate.
Order: safety/security BLOCKERs -> refactors that pave the way for features -> features -> optimizations.
Each item must be small enough for one PR. Mark any item that needs an ADR.
```

Ordering principles:

- **Fix dangerous bugs first**, then refactor, then new features last.
- **Refactor the "strangler" way:** wrap the old code behind a new interface, e.g. `transport_if_t`.
  Then migrate call sites to the interface step by step, and only delete the old code afterward.
  Never rewrite everything in one go.
- **Optimizations must be measured before and after**, against the A0.4 baseline. Don't optimize
  without measuring.

---

## Phase G – The loop for each backlog item

```
Planner (plan) -> human approval -> Agent (implements step by step, building after each step)
  -> host tests -> /review-code -> hardware test -> /commit -> PR (template) -> update docs
```

### G1. Plan one item (the Planner agent)

```text
Plan the backlog item "<item name>" from #file:docs/phase1/backlog.md.
Read the relevant code and #file:docs/architecture.md first. State clearly: which functions/files will change, which
contract might be affected (section 9 of #file:docs/dataflow.md), which tests protect it, the small steps (each one
buildable), and the corresponding commits.
```

### G2. Refactor (Agent)

```text
Carry out step <N> of the plan above. This is a REFACTOR: no observable behavior may change
(payload, topic, timing, NVS). After making the change: run idf.py build and the host tests, and report the results.
Don't touch code outside this step's scope; if you notice another issue, note it at the end of your reply instead of fixing it.
```

### G3. New feature (Agent)

```text
Implement step <N>. Follow the pattern already used in #file:docs/architecture.md; for any new design, follow the
rules in .github/copilot-instructions.md and the approved ADRs under docs/adr/.
If a new component is needed: use /new-component. If adding a message/command: use /add-message-type.
List the edge cases handled and anything not yet verified on hardware.
```

### G4. Optimize (Agent)

```text
Optimization target: <a metric, e.g. minimum heap / binary size / sleep current>. Baseline: <the figure from baseline.md>.
Propose up to 3 changes with the biggest impact, estimating the benefit and risk of each, WITHOUT modifying code yet.
```

Once one is chosen: implement it, re-measure, and record the before/after numbers in the commit
(`Tested: bench - ...`).

### G5. Wrapping up each item

```text
Based on the changes in this branch versus main, update #file:docs/architecture.md #file:docs/dataflow.md
#file:docs/security.md to match the new code (keep the rule: keep the headings unchanged, every claim has file:line,
only describe what's actually in the code). Mark the backlog item done. Show the doc diff.
```

---

## Keeping Copilot's knowledge current

- **When you change code, update the docs in the same PR.** The PR template checklist already has
  this item.
- **When Copilot repeats the same mistake**, add a short rule with the reasoning to the right
  instructions file. Don't keep repeating it in chat.
- **Every month (or after each major milestone)**, run this prompt in a new chat:
  ```text
  Check whether .github/copilot-instructions.md, .github/instructions/*.md, .github/skills/*/SKILL.md,
  docs/architecture.md, docs/dataflow.md, docs/security.md are still accurate against #codebase.
  List anything out of date/contradictory, with the file:line that's now off, and propose a diff. Do not write to any file yet.
  ```
- **Once the legacy code has been refactored:** delete `legacy.instructions.md` and the "Legacy code"
  section in `copilot-instructions.md`. `docs/phase1/` can be deleted too, keeping `baseline.md` and
  `backlog.md` as a historical record.

## Tips for writing effective prompts

| Do | Avoid |
|---|---|
| State clearly **READ ONLY**, or exactly **which files may be edited** | "Look at this and improve the code" (Copilot will wander) |
| Attach a specific `#file:` when you know it | Using only `#codebase` for a narrow question |
| Require `file:line` evidence | Trusting an answer with no citations |
| Require a diff to be shown before writing | Letting the agent write directly to an important file (instructions, partitions, Kconfig) |
| One topic per chat | Letting one chat run across many topics |
| Ask it to say "not sure" when it isn't | A leading question ("isn't X true?") |
| Break large work into small steps, building after each one | "Refactor the entire transport component" in one prompt |

## Overall checklist

- [ ] A0 – Branch, kit copied, hooks installed, baseline captured, `phase1-baseline` tag
- [ ] A1–A7 – Raw notes under `docs/phase1/`: overview, runtime, components, dataflow-current, interfaces, config-hw, risks
- [ ] B – Notes verified (Copilot self-check + human review + cross-checked against the board)
- [ ] C – `docs/architecture.md`, `docs/dataflow.md`, `docs/security.md` filled in by heading from the phase-1 code + verified in C4
- [ ] D – gap-analysis, `copilot-instructions.md`/skills/agent updated, `legacy.instructions.md` (if needed), confirmed Copilot understands correctly
- [ ] E – Characterization tests, a dedicated formatting commit + `.git-blame-ignore-revs`, clang-tidy
- [ ] F – `docs/phase1/backlog.md` approved, an ADR for any major decision
- [ ] G – For each backlog item: Planner → Agent → tests → `/review-code` → hardware → `/commit` → PR → docs
