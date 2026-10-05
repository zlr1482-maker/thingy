# AegisLab Offset Registry

Use this file only for **verified** offsets and structural relationships.

## Conventions

- **Module-relative offset**: offset from the live RobloxPlayer `__TEXT` base.
- **Preferred address**: Mach-O preferred VM address.
- **Live address**: current process address after ASLR.
- **Build**: record the exact Roblox build/version whenever possible.
- **Confidence**:
  - `Verified` = independently confirmed by repeatable evidence.
  - `Probable` = strong evidence, not yet fully confirmed.
  - `Unknown` = placeholder only; do not treat as usable.

## Current verified segment offsets

| Name | Relative Offset | Preferred Address | Meaning | Confidence |
|---|---:|---:|---|---|
| `__TEXT` | `+0x0000000` | `0x100000000` | Mach-O text segment base | Verified |
| `__DATA_CONST` | `+0x65f0000` | `0x1065f0000` | Mach-O data-const segment base | Verified |
| `__DATA` | `+0x6bd4000` | `0x106bd4000` | Mach-O data segment base | Verified |
| `__LINKEDIT` | `+0x73ac000` | `0x1073ac000` | Mach-O link-edit segment base | Verified |

## Runtime structures

No Roblox runtime object offsets have been verified yet.

| Name | Relative Offset | Structure / Field | Evidence | Confidence |
|---|---:|---|---|---|
| _none yet_ |  |  |  |  |

## Notes for the development team

Do not use an entry as a gameplay/runtime structure unless its confidence is `Verified`.
Raw live addresses are session-specific because of ASLR; communicate module-relative offsets instead.
