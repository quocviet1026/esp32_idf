---
name: review-code
description: 'Review ESP-IDF firmware code against the team checklist (correctness, concurrency, memory, long-term robustness, security, edge cases). Use when the user asks to review the current changes, a file, a branch, or a PR.'
argument-hint: '[file | branch | "staged"] - default: uncommitted changes'
---

# Review code

1. Determine the scope:
   - no argument → `git diff HEAD` (both staged and unstaged);
   - `staged` → `git diff --cached`;
   - a branch name → `git diff origin/main...<branch>`;
   - a file path → that whole file.
2. For every changed function, **read both its callers and callees** (find usages) to understand the
   threading context, buffer ownership, and lifetime — don't review each diff line in isolation.
3. Apply the [review checklist](../../instructions/review.instructions.md) and the
   [C/ESP-IDF rules](../../instructions/c-esp-idf.instructions.md).
4. Only report issues with a **concrete failure scenario**. Double-check every finding before
   reporting it: drop it if it's already handled elsewhere.
5. Present results in the checklist's exact format (severity, file:line, scenario, suggested diff),
   ending with Approve / Request changes. **Don't modify code yourself** unless the user asks you to.
