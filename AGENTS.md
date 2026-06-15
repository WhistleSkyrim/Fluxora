# Fluxora Agent Instructions

Read `.agents/PROJECT_RULES.md` before making changes in this repository.

Core rules:

- C++ in `backend/` is the project core. Business logic belongs there.
- C# in `frontend/` is the WPF frontend. UI belongs there.
- Do not put UI responsibilities into C++.
- Do not put core business logic into C#.
- Split work into small focused services. Avoid master files, catch-all managers, and god objects.
- For any code change, use the relevant test skill. If no dedicated test skill is available, define the validation plan before editing and run the smallest relevant test or build afterward.
