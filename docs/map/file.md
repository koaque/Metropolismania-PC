# file

Disc file access. `mm_File_*` opens `cdrom0:\...` paths with `sceOpen` and reads through an asynchronous request
queue (`mm_FileQueue_*`) that `mm_MainLoop` pumps every frame; `mm_FileQueue_WaitAll` blocks until it drains.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

12 functions (named 12). Blocks: 9 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Name | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00184120` | `mm_File_Seek` | 388 | yes | sceLseek; a synchronous file (+4 == 0) first waits for the request queue to drain |
| `0x001842B0` | `mm_File_Read` | 408 | yes | sceRead; synchronous files wait for the queue first, asynchronous ones queue the request (0x1F4ED0) |
| `0x00184450` | `mm_File_Close` | 320 | yes | sceClose, fd (+0) = -1; synchronous files wait for the queue first |
| `0x00184590` | `mm_File_Open` | 384 | yes | waits for the disc, builds "cdrom0:\" + path + ";1" (0x1B9AF8), sceOpen |
| `0x00184710` | `mm_File_LoadWhole` | 164 | yes | opens a1, mallocs the file size, reads it all, closes; size to *a2 if given; returns the buffer |
| `0x001847C0` | `mm_FileQueue_IsDone` | 72 | no | 1 when request a1 is no longer in the 64-entry queue (head 0x1BDC88, tail 0x1BDC8C) |
| `0x00184810` | `mm_FileQueue_WaitFor` | 212 | yes | pumps the queue until request a1 is done |
| `0x001848F0` | `mm_FileQueue_WaitAll` | 140 | yes | pumps the queue until it is empty |
| `0x00184980` | `mm_FileQueue_Init` | 76 | no | clears the 64 request slots (0x1C bytes each at 0x1F4ED0) and the head/tail counters |
| `0x001849D0` | `mm_FileQueue_Pump` | 396 | yes | runs the request at the queue tail (open/read/seek/close/ioctl) once the disc is ready |
| `0x00184B60` | `mm_File_dtor` | 112 | yes | closes a synchronous open file; deletes this when the flag is > 0 |
| `0x00184BD0` | `mm_File_ctor` | 20 | no | fd = -1, +4 = asynchronous flag |
