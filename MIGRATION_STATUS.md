# PFS3 migration — 2026-10-30

Canonical PFS3 core/design copied unchanged from Filesystems commit ca2fa1210c3c1f2b6f52cd15b3da6dfa83d453ad. Defragmenter's native PFS3 analysis and relocation code copied unchanged to `migration/defragmenter/native/` from Defragmenter commit 30af892d8897d3aeaeefeac1bfcdb9620a0a5c72.

No externally licensed `native/filesystems/pfs3/original/` reference source was imported in this pass. Full Linux adapter still lives in Filesystems. The copied source requires reconciliation and integration with its host services; a tested standalone PFS3.so and common interface are not yet available here. Active source and consumers remain unchanged until tested migration.
