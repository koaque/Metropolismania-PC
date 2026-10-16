# mem

Heap helper.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

1 functions (typed 1). Blocks: 1 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00184CD0` | `int mm_Heap_Probe(void)` | 84 | yes | malloc(0x17E5D70) then free; returns that size (checks the heap can hold it) |
