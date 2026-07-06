# Borgo Labs Repository and Collaboration Guidelines

These rules apply to Borgo Pumps hardware, firmware, and software work.

## Repository Structure

Borgo Pumps is one product and therefore one GitHub repository:

```text
firmware/     Shared embedded code and hardware revision configuration
software/     Android app and supporting software tools
pcb/          KiCad or EasyEDA project files
docs/         Datasheets, wiring diagrams, BOMs, plans, and design references
```

OpenSpec remains in `openspec/` at root for CLI discovery. Repository-wide governance files also remain at root.

Casing and native 3D CAD files do not live in git. Fusion 360 is the source of truth. Finished STEP/STL exports are attached to the matching GitHub Release and stored in the Borgo Labs Google Drive release folder.

## Hardware Revisions

- Firmware is one shared codebase across supported revisions.
- Hardware differences are isolated under `firmware/boards/<revision>/`.
- `firmware/COMPATIBILITY.md` is the source of truth for firmware and hardware compatibility.
- Retired deployed hardware may receive critical fixes from `support/<revision>-legacy` after lead approval.

## Branches

| Branch | Purpose |
| --- | --- |
| `main` | Protected shipping version; receives release and hotfix merges only |
| `develop` | Integration branch for completed feature work |
| `feature/<name>` | One feature or fix, branched from `develop` |
| `release/<version>` | Stabilization from `develop`; bug fixes only |
| `hotfix/<name>` | Urgent production fix from `main` |
| `support/<revision>-legacy` | Critical maintenance for retired field hardware |

No other branch type is introduced without team agreement.

## Versioning

- Hardware uses revision letters: Rev A, Rev B, and so on. A fabricated revision letter is never reused.
- Firmware and software use semantic versioning: `MAJOR.MINOR.PATCH`.
- Firmware releases use tags such as `fw-v1.4.2`; Android releases use tags such as `app-v1.4.2`.

## Pull Requests

- Changes to `develop` and `main` require a pull request.
- Each PR includes what changed, why, and how it was tested.
- At least one team member approves before merge.
- Required builds, tests, lint, and OpenSpec validation must pass.
- Keep PRs scoped to one logical change and delete the feature branch after merge.

## Releases

Every release must:

1. Update version numbers and `firmware/COMPATIBILITY.md` when compatibility is affected.
2. Pass CI for every supported board configuration.
3. Be tagged and published as a GitHub Release.
4. Attach compiled firmware, Android artifacts, fabrication outputs when applicable, and a changelog.

## Commits and Ownership

- Use one logical change per commit with a short imperative summary.
- Any team member may open a `feature/*` branch and PR.
- Opening `release/*`, `hotfix/*`, or `support/*` requires maintainer sign-off.
- Compatibility-table accuracy is a shared responsibility and changes in the same PR as affected firmware or hardware.
