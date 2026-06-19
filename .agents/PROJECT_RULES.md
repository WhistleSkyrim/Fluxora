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
- When investigating any bug, check the program logs in `logs/` early and use relevant operation, bridge, core, UI, installer, and crash entries to guide the diagnosis.
- C++ unit tests are built with Google Test in `backend/tests/`. Keep extending that suite as backend behavior grows: every new or changed core function should get focused Google Test coverage unless there is a clear reason it cannot be tested directly.
- C# unit tests are built with xUnit.net in `frontend.Tests/`. Keep extending that suite as frontend behavior grows: ViewModel state, commands, field validation, list sorting/filtering, DTO-to-UI model mapping, bridge error handling, progress/status transitions, pause/cancel/resume button state, operation re-entry protection, and UI reactions to core events are appropriate targets. Do not add broad visual snapshots or tests for every XAML file, individual button, color, or animation.
- Keep changes scoped to the requested behavior and avoid unrelated refactors.
- Preserve user changes already present in the worktree.

## Validation Expectations

- Backend changes should be validated with the relevant CMake build and targeted Google Test/CTest run when available.
- Frontend changes should be validated with `dotnet test frontend.Tests/Fluxora.App.Tests.csproj` when targeted xUnit coverage exists, otherwise with `dotnet build frontend/Fluxora.App.csproj`.
- Cross-layer changes should validate both the C++ backend boundary and the C# frontend integration path.

## Release Distribution

- Fluxora releases are installer-only. Never commit, push, publish, attach, or distribute the portable build folder, loose portable files, portable zip archives, or any equivalent portable package.
- The only approved public release artifact is the branded installer produced as `output-installer/FluxoraSetup.exe`.
- The `output/` folder is a local build staging area used to create the installer payload. Treat it as temporary build output, not as a distributable product.

## Logging Expectations

- Fluxora keeps separate crash-safe logs for core/native work, UI work, bridge calls, operations, and crashes. Preserve that split when adding or changing features.
- User-triggered business operations must create or reuse an operation id at the UI/installer boundary, then keep it flowing through C# UI, bridge calls, native C++ core, installer, deploy, rollback, cleanup, and file-operation paths.
- Log real business behavior and filesystem behavior: startup paths, selected game/profile/project paths, install/import/delete/deploy/rollback stages, archive validation/extraction, copy/move/delete/rename failures, metadata writes, launch/VFS setup, progress callback failures, and native bridge failures.
- Do not log purely visual UI state, colors, animation details, or noisy polling unless it directly explains a business operation.
- When adding a feature that changes files, native state, project/profile state, downloads, installs, deploys, rollback/cleanup, or external integration state, add or update the relevant UI/bridge/core/operation/crash log entries as part of the feature.
- Use `operationId` consistently in new logs. The expected chain is: UI creates an operation id and logs the request, Bridge logs the native call start/result, Core/Installer logs domain stages and file operations, and crash/error logs preserve the same operation id when available.

## Legal And Privacy Expectations

- Treat legal/privacy review as part of feature work, not as a separate afterthought.
- When adding an important feature, check whether the bundled privacy policy and terms of use need updates. This especially applies to reports, telemetry, diagnostics, crash/support bundles, uploads, downloads, cloud or online services, account/auth flows, third-party APIs, external integrations, payments, subscriptions, analytics, notifications, or any new collection, storage, processing, disclosure, or transfer of user/device/project data.
- Review the legal documents with German/EU expectations in mind, including GDPR/DSGVO transparency requirements. If a feature changes data processing, document the relevant data categories, purpose, legal basis or consent/opt-in flow when applicable, recipients or third-party services, transfer destination, retention or deletion behavior, and user controls.
- If a feature depends on a third-party service or terms, make sure the terms of use and privacy policy describe that dependency clearly enough for users before they enable or use it.
- Update every bundled legal document/localization that ships in the installer when a legal/privacy change is needed. Current installer legal resources live under `installer/Fluxora.Installer/Resources/Legal/`.
- If the correct legal wording is uncertain, do not invent final legal advice. Add a clear implementation note/TODO for owner or legal review and explain the risk in the handoff.

## Backend Integration Test Areas

- C++ file-operation integration tests live in `backend/tests/` alongside the Google Test suite. Extend them when changing mod install/delete behavior, archive extraction, profile load order, conflict summaries, manifest/database persistence, virtual file-system file views, transferred build paths, or bridge-facing path/string handling.
- Keep archive tests security-focused: path traversal entries, absolute/rooted paths, case-only duplicate paths on Windows, Unicode paths, spaces, Cyrillic, German characters, and normal English paths should stay covered as the installer evolves.
- Reinstall/update, read-only or access-denied replacement failures, and broader virtual file-system scenarios are expected future integration-test additions when those behaviors are implemented or changed.
