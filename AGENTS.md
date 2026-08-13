# FlexBMS agent instructions

## Canonical working directory

Use `C:\Users\Bram\Documents\Git\FlexBMS` directly for all work. Do not
create or use an isolated Git worktree, branch, or copy unless Bram explicitly
requests one. This keeps every change immediately visible in the user's VS Code
checkout and ensures each task sees the current uncommitted Gateway and STM32
work.

Do not run concurrent editing tasks in this checkout. Before starting a task,
inspect the existing working tree and preserve all unrelated staged and
unstaged changes.

## Long-running commands

For code generation, compilation, or any other command likely to exceed the
normal 60-second interaction window, start it in the background and monitor it
without blocking the agent. Do not use a short foreground command timeout for
such work. Report its actual completion, failure, or timeout before claiming
validation.
