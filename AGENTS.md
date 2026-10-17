# FlexBMS agent instructions

## Canonical working directory

Use the existing repository root containing this `AGENTS.md` directly for all
work. Resolve it from the active checkout; do not hard-code an absolute path or
Windows user profile. Do not create or use an isolated Git worktree, branch, or
copy unless Bram explicitly requests one. This keeps every change immediately
visible in the user's VS Code checkout and ensures each task sees the current
uncommitted Gateway and STM32 work.

Use repository-relative paths in documentation and instructions. For a separate
project, identify its name and the path relative to that project's root instead
of assuming a fixed checkout location on every PC.

Do not run concurrent editing tasks in this checkout. Before starting a task,
inspect the existing working tree and preserve all unrelated staged and
unstaged changes.

## Long-running commands

For code generation, compilation, or any other command likely to exceed the
normal 60-second interaction window, start it in the background and monitor it
without blocking the agent. Do not use a short foreground command timeout for
such work. Report its actual completion, failure, or timeout before claiming
validation.

Gateway builds invoke Vite, whose success marker contains Unicode. On Windows,
always set `PYTHONIOENCODING=utf-8` for the PlatformIO Gateway build process;
otherwise PlatformIO's output thread can fail under CP1252 and leave the build
hung after Vite completes.
