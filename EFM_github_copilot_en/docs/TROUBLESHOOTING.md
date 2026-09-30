# Details & troubleshooting

This document explains the **why** and the **verified limitations** behind the items in
[README.md](../README.md). The README only lists what to do; the details/rationale live here.

## 4 code-checking tools, none of them a substitute for another

| File/hook | Answers the question | Runs when |
|---|---|---|
| `.clang-format` | Is the code correctly formatted? (indentation, line breaks, spacing — **not** logic) | On save (`editor.formatOnSave`), or run manually |
| `.clang-tidy` | Does the code have a logic/safety bug? (buffers, pointers, unchecked errors, naming violations...) | Run manually (`idf.py clang-check`), or live via clangd while typing |
| `.clangd` | How does the editor understand the code for suggestions, go-to-definition, inline error squiggles? | The whole time VS Code is open; needs `build/compile_commands.json` to exist first |
| `tools/hooks/pre-commit` | Are the staged `.c`/`.h` files formatted per `.clang-format`? | Automatically on `git commit`, running **before** `commit-msg`, once `tools/install-hooks.sh` has been run |
| `tools/hooks/commit-msg` | Does the commit message follow the team's format? | Automatically on `git commit`, once `tools/install-hooks.sh` has been run |

## clangd using the wrong binary

**Verified in practice:** clangd only analyzes correctly when using **the `clangd` build that ships
with `esp-clang`** (which has real Xtensa support — a stock clangd, or the one the extension
downloads by itself, does **not** support Xtensa). Using the wrong binary causes a false
`'stdio.h' file not found` (or some other unrelated header) for the **entire** file, even though the
code has no error at all.
Concrete test: the same file, the same `--query-driver` configuration, run with the system's
(vanilla) clangd → reports an unrelated missing header; run with esp-clang's own clangd → parses
successfully, leaving only genuine warnings.

- Check: open the ESP-IDF terminal and run `which clangd` — it must point to
  `.../esp-clang/.../bin/clangd`, not `/usr/bin/clangd` or anywhere else.
- If the VS Code clangd extension doesn't pick that up automatically (e.g. VS Code was launched from
  a desktop icon and doesn't inherit the terminal's PATH), set it in **User Settings** (don't set it
  in the repo's `.vscode/settings.json`, since the path includes a version number that differs by
  machine and install date):
  ```json
  "clangd.path": "/home/<user>/.espressif/tools/esp-clang/<version>/esp-clang/bin/clangd"
  ```
  Use the exact value from `which clangd` above.

`.vscode/settings.json` sets `--query-driver=**/riscv32-esp-elf-*,**/xtensa-esp32*-elf-*` to match
both architectures (RISC-V for the ESP32-C5, Xtensa for other test boards) — missing the glob for
either architecture makes the standard headers (`stdio.h`...) for that architecture report
"file not found" for the whole file, with nothing analyzed at all.

## ESP-IDF terminal

`.vscode/settings.json` declares a terminal profile named **ESP-IDF** as the default, which loads
`tools/idf-env.sh` (loading `~/.bashrc` and then `$IDF_PATH/export.sh`).

**Don't know your machine's `IDF_PATH`?**
```bash
type get_idf                                                        # option 1: if installed per Espressif's own guide
find ~ -maxdepth 4 -iname export.sh 2>/dev/null | grep -v /build/   # option 2: find a folder with export.sh + tools/idf.py
```
The directory containing the `export.sh` you find (with `tools/idf.py` inside it) is exactly the
value to set `IDF_PATH` to.

**Don't see the `[idf-env] ESP-IDF v5.5.x ready` line when opening a new terminal:**
1. Did you open the right repo folder as the workspace root (File → Open Folder)?
2. Just edited `.vscode/settings.json` or `~/.bashrc`? → run `Developer: Reload Window`
   (`Ctrl+Shift+P`).
3. Is the new terminal actually on the **ESP-IDF** profile? — check the dropdown arrow next to the
   `+` button in the Terminal panel.
4. Seeing `[idf-env] IDF_PATH is not set` → `IDF_PATH` isn't exported correctly in `~/.bashrc`.

## Mandatory steps – why, and the consequences of skipping them

Nothing in the README's table runs "silently" except the 2 hooks `pre-commit`/`commit-msg` (once
installed). For every other row, if the dev doesn't type the command themselves, treat it as never
having been checked — this kit **doesn't ship a CI pipeline**.

| Step | Consequence if skipped |
|---|---|
| `sh tools/install-hooks.sh` | The `pre-commit`/`commit-msg` hooks aren't installed → both badly formatted files and bad messages slip through on that machine |
| `idf.py build` after editing `.c`/`.h` | A warning slips into `main`; the "0 warnings" rule becomes just a suggestion |
| `idf.py clang-check` before opening a PR | bugprone/cert/clang-analyzer bugs (buffer overflow, use-after-free...) go unnoticed before merge |
| `/review-code` when changing a component/message type/command | Missed edge cases, layering-architecture violations, nothing checked before a human reviewer sees it |
| Testing on real hardware when changing a driver/OTA/connectivity/NVS/partition | The firmware builds cleanly but bricks/fails to connect out in the field |

`xargs -r` in the command that formats staged files: skips calling `clang-format` when no file is
currently staged (avoiding the `cannot use -i when reading from stdin` error when the `git diff` side
is empty). The `-r` flag is GNU xargs (available on Linux/WSL); on macOS (BSD xargs), drop `-r` and
check for an empty list yourself first.

## Verified limitations of `idf.py clang-check`

**Verified in practice on a real project:** `idf.py clang-check` (via ESP-IDF's `pyclang`) replaces
`Checks:`/`HeaderFilterRegex` with its own hardcoded list (it does not honor this repo's
`.clang-tidy`), and the list of compiler flags it strips is **incomplete** for both architectures
(missing `-fno-shrink-wrap`, `-mno-relax`... which `.clangd` had to strip separately) — the result:
**18 out of 19 files reported "file not found"/"unknown argument" errors instead of being analyzed
at all**.

Because of this: treat `warnings.txt` as a **secondary tool that needs manual noise-filtering**
(ignore `clang-diagnostic-error` caused by missing flags/headers, which aren't real code bugs). A
more trustworthy source is the **clang-tidy warnings shown live in VS Code via clangd** while typing
(see "clangd using the wrong binary" above) — clangd correctly discovers the sysroot via
`--query-driver` and isn't affected by this limitation.

`.clang-tidy` currently **only runs when a dev types the command manually, or via clangd** — there is
no step that forces `idf.py clang-check` to run automatically before a merge. If the team uses
GitHub, a workflow can be added to call the command above; on Bitbucket, add it to
`bitbucket-pipelines.yml`.

## `git ls-files` vs `git diff`

`git ls-files` lists **every file git is tracking** (matching the intent of "the whole project"),
unlike `git diff`, which only lists changed files. Both the whole-project format command and the
check command exclude `managed_components/` — never auto-editing third-party library code.

## Which file serves which Copilot feature

| File | Loaded when | Used by |
|---|---|---|
| `.github/copilot-instructions.md` | **Every** chat/agent request in the workspace | VS Code Chat/Agent, GitHub PR code review, Copilot cloud agent |
| `.github/instructions/c-esp-idf.instructions.md` | Automatically when working on `**/*.c,**/*.h` (`applyTo`) | VS Code Chat/Agent, GitHub PR code review |
| `.github/instructions/cmake-kconfig.instructions.md` | Automatically with CMakeLists/Kconfig/sdkconfig/partitions | same as above |
| `.github/instructions/testing.instructions.md` | Automatically with files under `test/` | same as above |
| `.github/instructions/commit.instructions.md` | The ✨ *Generate Commit Message* button (via a setting), the `/commit` skill, or an agent picking it up via `description` | VS Code |
| `.github/instructions/review.instructions.md` | *Review selection / uncommitted changes* (via a setting), the `/review-code` skill | VS Code |
| `.github/skills/*/SKILL.md` | Typing `/commit`, `/review-code`, `/new-component`, `/add-message-type`, `/analyze-crash`; an agent also auto-loads them when their `description` fits | VS Code Agent, cloud agent |
| `.github/agents/planner.agent.md` | Selecting the **Planner** agent from the chat dropdown | VS Code |
| `.github/pull_request_template.md` | Opening a PR on GitHub; *Generate PR description* (via a setting) | GitHub, VS Code |

`commit.instructions.md` and `review.instructions.md` **deliberately have no `applyTo`**, so they
aren't injected into every code-generation request (wasting context). GitHub PR code review still
applies the core rules via `copilot-instructions.md` and `c-esp-idf.instructions.md`. To make PR
review on GitHub use the full checklist: add `applyTo: '**'` and `excludeAgent: 'cloud-agent'` to
`review.instructions.md`'s frontmatter.

## Other things to know when using Copilot

- **Custom instructions do NOT apply to inline completion** (ghost text while typing). They only
  apply to Chat, Agent, and commit/review/PR generation. Code from inline completion still has to go
  through clang-format, clang-tidy, and review.
- Checking which files were loaded: see the **References** section at the top of a chat reply. If
  something's missing, turn on `github.copilot.chat.codeGeneration.useInstructionFiles`, check the
  frontmatter/`applyTo` glob syntax, and check the log in *Output → GitHub Copilot Chat*.
- The settings `github.copilot.chat.commitMessageGeneration.instructions` /
  `reviewSelection.instructions` / `pullRequestDescriptionGeneration.instructions` **have been marked
  deprecated since VS Code 1.102** but are currently still the way to make the ✨ button read the
  team's rules. If a future VS Code build removes them, use the `/commit` skill instead. Commit
  message generation uses the `chat.utilitySmallModel` (a small model) — if the quality is poor, use
  `/commit` (which runs on whichever model the agent is currently using).
- A skill's folder name **must match** the `name` field in its `SKILL.md` (lowercase, digits, `-`),
  otherwise the skill silently fails to load.
- The tool names in `planner.agent.md` (`search/codebase`, `search/usages`, `web/fetch`) follow VS
  Code's documentation — if your VS Code build renamed them, fix it via the **Configure Tools**
  button in chat.
- GitHub PR code review reads instructions/skills from **the branch containing the changes** (the
  head branch), not `main`.
- `chat.tools.terminal.autoApprove` only auto-approves safe read/build commands; flashing, erasing,
  eFuse, pushing, and any command containing `--no-verify`/`-n` always ask first. Don't extend this
  list to cover `idf.py flash`/`espefuse.py`.
