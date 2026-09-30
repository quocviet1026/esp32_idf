# EFM – GitHub Copilot configuration kit for the firmware team

Copilot configuration files, format/lint tooling, commit workflow, and architecture docs for the
**ESP32-C5 + RAK3172** project (ESP-IDF v5.5.x, C, VS Code). Copy everything into the firmware repo root.

> Details/rationale for every item below live in [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Setup (once per dev)

```bash
cp -r .github .vscode docs tools .clang-format .clang-tidy .clangd .editorconfig .gitmessage .gitignore <repo>/
cd <repo>
sh tools/install-hooks.sh                   # commit-msg hook + commit template
export IDF_PATH="$HOME/esp-idf"             # add this line to ~/.bashrc
idf.py set-target esp32c5 && idf.py build   # generates build/compile_commands.json for clangd
```

Install the recommended extensions when VS Code prompts (`.vscode/extensions.json`). Turn off
IntelliSense for `ms-vscode.cpptools` (already configured via `C_Cpp.intelliSenseEngine: disabled`)
so only clangd is used, avoiding two analysis engines running side by side.

**Check that clangd is using the right binary** (mandatory, especially on Xtensa boards): open the
ESP-IDF terminal (below) and run `which clangd` — it must point to `.../esp-clang/.../bin/clangd`. If
not, see "wrong clangd binary" in [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Opening a terminal with ESP-IDF ready

The default terminal profile (**ESP-IDF**) already loads the environment — open a new terminal and
seeing `[idf-env] ESP-IDF v5.5.x ready` means `idf.py` works right away, no need to manually run
`. $IDF_PATH/export.sh`. Don't know your `IDF_PATH`, or don't see that line? See
[TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Mandatory steps for developers

This kit **does not ship a CI pipeline** — there is no other layer to catch mistakes; skip a step
and the corresponding bug goes straight into `main`.

| When | Command | Automatic? |
|---|---|---|
| Once, right after cloning | `sh tools/install-hooks.sh` | No |
| Every time you edit `.c`/`.h`, before calling it done | `idf.py build` — must be **0 warnings** | No |
| Before every commit | *(nothing to type — 2 hooks run automatically: format check then message check)* | **Yes**, once step 1 is done |
| Before opening a PR / calling a task done | `idf.py clang-check --exclude-paths managed_components`, read `warnings.txt` (known limitations apply) | No |
| Before opening a PR, if you changed a component/message type/command | `/review-code` (or `/review-code staged`) in Copilot Chat | No |
| Before merging, if you changed a driver/OTA/connectivity/NVS/partition | Test on real hardware per the checklist in `pull_request_template.md` | No |

If a commit is blocked for bad formatting: `git diff --name-only --cached -- '*.c' '*.h' | xargs -r clang-format -i`
then `git add` again. With `editor.formatOnSave` on (already enabled) you should almost never hit this.

## Recommended workflow

1. **Large feature** → pick the **Planner** agent → agree on a plan → *Implement the plan* (hands off to Agent).
2. **Add a component/message type** → `/new-component ...`, `/add-message-type ...`.
3. **Before committing** → `/review-code` (or `/review-code staged`).
4. **Commit** → the ✨ button in Source Control, or `/commit #42 bench`.
5. **Crash** → paste the log into chat with `/analyze-crash`.

## Running clang-format

`clang-format` ships inside the `esp-clang` toolchain, so it's available right away from the
ESP-IDF terminal — nothing extra to install.

| What you want | Command |
|---|---|
| Whole project | `clang-format -i $(git ls-files '*.c' '*.h' ':!:managed_components/**')` |
| Only staged files (before a commit) | `git diff --name-only --cached -- '*.c' '*.h' \| xargs -r clang-format -i` |
| Check only, no changes | `clang-format --dry-run --Werror $(git ls-files '*.c' '*.h' ':!:managed_components/**')` |
| One specific file | `clang-format -i main/main.c` |
| Automatic, no command | Open the `.c/.h` file, `Ctrl+S` (`editor.formatOnSave` already enabled) |

**Formatting existing code (e.g. a phase-1 repo):** don't run this carelessly — the formatting diff
will get mixed in with other logic changes and become very hard to review. Follow the order in
[copilot-onboarding-legacy.md, phase E2](docs/prompt/copilot-onboarding-legacy.md): write
characterization tests first → format → `idf.py build` to confirm no behavior change → **separate**
commit (`style: apply clang-format to phase1 code`) → add it to `.git-blame-ignore-revs`.

## Documentation

- [docs/architecture.md](docs/architecture.md), [docs/dataflow.md](docs/dataflow.md), [docs/security.md](docs/security.md) –
  **section skeletons**; the agent fills them in from the phase-1 code following phase C of
  [copilot-onboarding-legacy.md](docs/prompt/copilot-onboarding-legacy.md)
- [docs/adr/](docs/adr/) – records architecture decisions
- [docs/prompt/copilot-onboarding-legacy.md](docs/prompt/copilot-onboarding-legacy.md) – the steps and prompts for teaching Copilot a phase-1 repo before refactoring/adding features
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) – rationale, verified limitations, how the Copilot files work
