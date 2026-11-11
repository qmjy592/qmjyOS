# qmjyOS Module Status

> Compatibility entry point. The historical status table formerly stored
> in this file is obsolete and must not be used for current status claims.

The authoritative module inventory is maintained in:

- `config/modules.conf`
- `config/modules.status`
- `config/modules.dep`

The status definitions, evidence levels and update rules are documented in:

- `docs/architecture/MODULE_STATUS_MODEL.md`

## Current status model

QBuild implementation maturity uses only:

- `planned`
- `experimental`
- `implemented`

Build and runtime verification evidence is tracked separately.
A module's implementation maturity must not be inferred from:

- Directory existence
- Header declarations
- Source-file existence
- Successful compilation alone
- Host-side tests that do not exercise the target hardware

## Verification boundary

A successful host-side test proves only the behavior covered by that test.
A successful static boot-image check does not prove that the image boots.
Runtime verification requires an actual supported runtime environment.

Do not restore or copy the historical status table into current reports.
Update `config/modules.status` only when implementation evidence justifies
a status transition.
