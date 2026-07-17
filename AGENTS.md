# AGENTS.md — Mixxx Project Instructions

See [README.md](README.md) for a project overview, and
[CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, code style,
pre-commit setup, Git workflow, and pull request guidelines.

## Key Architecture

- **ControlObject/ControlProxy**: `[Group], key_name` inter-component communication.
- **Engine thread**: Real-time audio — no allocations, no locks, may emit Qt signals but cannot receive them.
- **parented_ptr/make_parented**: Qt object-tree ownership. Object must get a parent before `parented_ptr` destructs.

## Project Layout

```text
src/          C++ source (engine/, controllers/, library/, mixer/, effects/, qml/, preferences/, util/, test/)
res/          Resources (controllers/ JS/XML, skins/, qml/)
cmake/        CMake modules
tools/        Python helper scripts
```

## Local kemo-vibe-mixxx Workflow

- `origin` is the private repository `kemoshumai/kemo-vibe-mixxx` and is the normal destination for commits and pushes.
- `upstream` is the official `mixxxdj/mixxx` repository. Fetch from it to receive official updates, but never push to it.
- `origin/main` is the private integration branch. Keep feature work on branches named `feature/<name>` created from `origin/main`.
- Push feature branches only to `origin`; merge completed work into `main` through the private repository workflow.
- The local development environment is Windows with Visual Studio 2022 x64. Use the repository's Windows build environment setup before configuring CMake.
- Do not commit build directories, generated binaries, IDE-specific files, credentials, or other generated artifacts.
