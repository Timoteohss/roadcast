# CAN catalog source

`dbc.json` is the checked-in source for Roadcast's initial raw CAN catalog. It
contains bit layouts extracted from DWARF in the OEM VHAL binary and the small
calibration overlay maintained by the sibling `vhalpeek` project.

Imported from:

```text
<vhalpeek-checkout>/data/dbc.json
```

The import used the state present on 2026-07-26:

```text
frames: 111
signals: 815
```

Run the deterministic generator after changing the input:

```bash
make generate
```

Generated outputs:

- `include/roadcast_frames.h`
- `include/roadcast_catalog_generated.h`
- `src/catalog_generated.c`

Do not edit generated outputs directly. A catalog change must update
`ROADCAST_CAN_SCHEMA_HASH`. Stable signal IDs remain unchanged when the
semantic identity `(namespace, CAN ID, signal name)` remains unchanged.
