---
name: commit
description: 'Generate a commit message following the team standard from the staged changes (git diff --cached). Use when the user wants to commit, write a commit message, or bundle changes into a commit.'
argument-hint: '[issue id, e.g. #42] [test level: hw|bench|build|none]'
disable-model-invocation: true
---

# Generate a commit message

1. Run `git diff --cached --stat` and `git diff --cached` to read the **entire** staged diff.
   If nothing is staged: tell the user, list `git status --short`, **don't run `git add` yourself**.
2. Group the changes by purpose. If there's more than 1 independent purpose (e.g. refactor + feat, or
   more than 2 unrelated components): suggest splitting into separate commits with the corresponding
   `git restore --staged <file>` commands, then stop.
3. Write the message per the [commit rules](../../instructions/commit.instructions.md):
   - header `<type>(<scope>): <subject>` ≤ 72 characters;
   - body as bullets, each line ≤ 72 characters, covering every change (including
     Kconfig/CMake/partitions/docs);
   - footer `Refs:` (from the argument if given), `BREAKING CHANGE:` if the payload/topic/NVS/
     partitions/API changed, `Tested:` required for feat/fix/perf/sec — if the user didn't state a
     test level, use `Tested: none - <reason>` and **ask them** rather than assuming it was tested.
4. Show the message in a code block and **ask for confirmation** before running
   `git commit -F <temp file>`. No `--no-verify`, no `push`.
