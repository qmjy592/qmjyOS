# qmjyOS Module Status Model

## 1. Two Independent Dimensions

qmjyOS tracks module implementation state and verification evidence separately.

They must never be treated as the same thing.

## 2. QBuild Implementation States

The machine-readable file is:

config/modules.status

Allowed values:

| Value | Meaning |
|---|---|
| planned | Planned, implementation not yet available |
| experimental | Partial or experimental implementation exists |
| implemented | Implementation source is mapped and available |

These values are consumed by QBuild v0.5.0.

They describe implementation maturity, not runtime correctness.

## 3. Verification Evidence

Verification evidence is recorded separately.

| Evidence | Meaning |
|---|---|
| NOT_VERIFIED | No relevant verification recorded |
| SOURCE_CHECKED | Source or interface inspection completed |
| BUILD_VERIFIED | Relevant build completed successfully |
| HOST_TESTED | Relevant host-side tests passed |
| BOOT_TESTED | Relevant boot test passed |
| RUNTIME_VERIFIED | Target runtime behavior verified |

A module may have implementation state experimental and still have
successful tests for a limited subset of its functionality.

A successful host test does not prove target hardware behavior.

## 4. Rules

1. Do not write verification states into config/modules.status.
2. Do not change planned to implemented merely because a header exists.
3. Do not change experimental to implemented merely because compilation succeeds.
4. Keep evidence tied to the actual test scope.
5. Record architecture and boot mode for hardware verification.
6. A skipped test is not a passed test.
7. Documentation must not claim verification that has not occurred.

## 5. Current QBuild Compatibility

QBuild v0.5.0 accepts exactly:

- implemented
- experimental
- planned

Any additional state requires an explicit QBuild format change.

This document does not change the current configuration format.
