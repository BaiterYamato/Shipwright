# Minimal provider for SDK validation

Build in Release following the author guide. Exports `ShipNative_Query`, queries
`linkspan.oot.engine` v1 and rejects incompatible layouts or sizeof values. The manifest requests
ABI 1.2, available in the current 1.3 descriptor. It installs no patches, loads no assets
and does not alter the game. `doctor` analyzes the PE without loading the DLL.

This probe is a draft for the external gate. It is not a complete native
conformance suite: host execution and negative tests for ABI, rollback,
hooks/unload, lifetime and service conflicts are still missing. Do not install it alongside variants
with the same ID. The presence of the ZIP never proves in-game execution.
