# Fluxora Agent Rules

These rules are project-specific and apply to all Codex/agent work in this repository.

## Architecture Boundaries

- C++ is the project core. Put business logic, domain rules, native integration, installer logic, mod-management behavior, file-system behavior, and low-level services in `backend/`.
- Do not put UI code in C++ unless it is required by native platform integration and has no C# UI responsibility.
- C# is the frontend. Put WPF UI, windows, views, view models, bindings, converters, visual state, and user interaction code in `frontend/`.
- Do not put business logic in C# when it belongs to the core. C# may orchestrate UI workflows and call backend services, but core behavior should stay in C++.

## Service Shape

- Split behavior into small, focused services.
- Avoid large master files, catch-all managers, and god objects.
- Prefer clear ownership: one service should have one main responsibility.
- When adding functionality, first look for an existing service boundary. If none fits, create a small new service instead of expanding an unrelated one.

## Change Process

- For any code change, use the relevant test skill before and after implementation.
- If no dedicated test skill is available in the current agent environment, define the validation plan before editing and run the smallest relevant automated test or build after editing.
- Keep changes scoped to the requested behavior and avoid unrelated refactors.
- Preserve user changes already present in the worktree.

## Validation Expectations

- Backend changes should be validated with the relevant CMake build or targeted backend tests when available.
- Frontend changes should be validated with `dotnet build frontend/Fluxora.App.csproj` or targeted frontend tests when available.
- Cross-layer changes should validate both the C++ backend boundary and the C# frontend integration path.
