# SDK

Sony SDK and runtime code linked into the game: the Metrowerks C++ runtime and crt0, newlib libc and libm,
libkernl (syscall wrappers and the kernel patches copied into kernel RAM), libsif, libfileio, libcdvd, libpad, libmc,
libgraph, libdma, libpkt, libvu0, libmpeg/libipu, libscf and libtty. Most names come from the signature scan
(`ps2_analyzer`); the `sdk_<lib>_` names were given by hand where the scan found nothing and are kept distinct so
they never bind to the runtime's HLE handlers. P7 replaces whatever is still on the executed path.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions by library

### crt0

3 functions (named 3). Blocks: 1 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00100008` | `_start` | 172 | yes |  |
| `0x001000B8` | `sdk_libc__exit` | 8 | no | _exit(a0): Exit through 0x116C30 |
| `0x001000C0` | `sdk_crt0_ExitThread` | 16 | no | syscall 0x23 (ExitThread), the main thread's return address from _start |

### libc

32 functions (named 32). Blocks: 6 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0010E420` | `sdk_libc_StubEio1` | 40 | no | errno = EIO, returns -1 |
| `0x0010E448` | `sdk_libc_close` | 8 | no | stub, returns -1 |
| `0x0010E458` | `sdk_libc_lseek` | 8 | no | stub, returns -1 |
| `0x0010E510` | `sdk_libc_isatty` | 8 | no | returns 1 |
| `0x0010E518` | `sdk_libc_fstat` | 20 | no | character device, block size 0x2000 |
| `0x0010E560` | `sdk_libc_StubEio2` | 40 | no | errno = EIO, returns -1 |
| `0x0010E588` | `sdk_libc_StubEio3` | 40 | no | errno = EIO, returns -1 |
| `0x001199C8` | `sdk_libc_isinf` | 72 | no | isinf (double), for printf |
| `0x00119A10` | `sdk_libc_isnan` | 60 | no | isnan (double), for printf |
| `0x0011E7F8` | `sdk_libc_abort` | 24 | yes | raise(SIGABRT), then _exit(1) |
| `0x0011E810` | `sdk_libc_abs` | 20 | no | abs |
| `0x0011E8E0` | `sdk_libc__close_r` | 88 | no | close with errno in the reent |
| `0x0011FDD0` | `sdk_libc___errno` | 12 | no | errno location (the reent) |
| `0x001200C8` | `sdk_libc__cleanup_r` | 28 | no | _fwalk with the flush function |
| `0x00120600` | `sdk_libc__fstat_r` | 92 | no | fstat with errno in the reent |
| `0x00120AD8` | `sdk_libc_index` | 20 | no | index (strchr) |
| `0x00120B80` | `sdk_libc__localeconv_r` | 12 | no | the locale data |
| `0x00120B90` | `sdk_libc_setlocale` | 36 | no | setlocale |
| `0x00120BB8` | `sdk_libc_localeconv` | 28 | no | localeconv |
| `0x00120BD8` | `sdk_libc__lseek_r` | 96 | no | lseek with errno in the reent |
| `0x00120D90` | `sdk_libc_memalign` | 100 | yes | memalign under the malloc lock |
| `0x00122FC0` | `sdk_libc__read_r` | 96 | no | read with errno in the reent |
| `0x00123020` | `sdk_libc__sbrk_r` | 88 | no | sbrk with errno in the reent |
| `0x001232D8` | `sdk_libc_raise` | 32 | yes | raise |
| `0x001232F8` | `sdk_libc_signal` | 36 | yes | signal |
| `0x00123360` | `sdk_libc__kill_r` | 92 | no | kill with errno in the reent |
| `0x001233C0` | `sdk_libc__getpid_r` | 28 | no | returns 1 |
| `0x00123460` | `sdk_libc_sprintf` | 144 | yes | sprintf |
| `0x00123640` | `sdk_libc___sclose` | 28 | no | stdio close hook |
| `0x001240B0` | `sdk_libc_toupper` | 32 | no | toupper |
| `0x001241D0` | `sdk_libc_vfprintf` | 116 | yes | vfprintf (initialises stdio first) |
| `0x00125B58` | `sdk_libc__write_r` | 96 | no | write with errno in the reent |

### libcdvd

4 functions (named 4). Blocks: 3 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0011A3F0` | `sdk_cdvd_Callback` | 160 | no | (provisional) command end callback, signals the command semaphore |
| `0x0011ABF8` | `sdk_cdvd_NcmdPrologue` | 364 | yes | N command semaphore and RPC bind ("Libcdvd bind err N CMD") |
| `0x0011AF10` | `sdk_cdvd_ScmdPrologue` | 364 | yes | S command semaphore and RPC bind ("Libcdvd bind err S cmd") |
| `0x0011B360` | `sceCdDiskReady` | 500 | yes |  |

### libfileio

7 functions (named 7). Blocks: 6 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00111BB8` | `sdk_fileio_Unlock` | 12 | yes | SignalSema on the file I/O semaphore |
| `0x00111E50` | `sdk_fileio_CheckVersion` | 140 | no | (provisional) compares the IOP module version ("2430") |
| `0x00113160` | `sdk_sceRemove` | 28 | yes | (provisional) _sceCallCode 6 |
| `0x00113330` | `sdk_sceRmdir` | 28 | yes | (provisional) _sceCallCode 8 |
| `0x001136E0` | `sdk_sceDelDrv` | 28 | yes | (provisional) _sceCallCode 0x10 |
| `0x00114058` | `sdk_sceChdir` | 28 | yes | (provisional) _sceCallCode 0x12 |
| `0x00114480` | `sdk_sceUmount` | 28 | yes | (provisional) _sceCallCode 0x15 |

### libgcc

1 functions (named 1). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001000D0` | `sdk_fp_ltdf2` | 32 | no | double a0 < a1 through dpcmp |

### libgraph

3 functions (named 3). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001028A0` | `sdk_sceGsGetGParam` | 12 | no | pointer to the libgraph parameters (0x1AB230) |
| `0x00103730` | `sdk_sceGsSetHalfOffset` | 136 | no | (provisional) half-pixel XYOFFSET for the field in a draw environment |
| `0x00103998` | `sdk_sceGsSetHalfOffset2` | 136 | no | (provisional) same for the second draw environment |

### libkernl

92 functions (named 92). Blocks: 9 can reach a thread switch, 2 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0010D960` | `SetGsCrt` | 16 | no |  |
| `0x0010D980` | `_Exit` | 16 | no |  |
| `0x0010D9A0` | `_LoadExecPS2` | 16 | no |  |
| `0x0010DA10` | `SetVTLBRefillHandler` | 16 | no |  |
| `0x0010DA40` | `AddIntcHandler` | 16 | no |  |
| `0x0010DA60` | `RemoveIntcHandler` | 16 | no |  |
| `0x0010DA70` | `AddDmacHandler` | 16 | no |  |
| `0x0010DA80` | `AddDmacHandler` | 16 | no |  |
| `0x0010DA90` | `RemoveDmacHandler` | 16 | no |  |
| `0x0010DAA0` | `_EnableIntc` | 16 | no |  |
| `0x0010DAB0` | `_DisableIntc` | 16 | no |  |
| `0x0010DAC0` | `_EnableDmac` | 16 | no |  |
| `0x0010DAD0` | `_DisableDmac` | 16 | no |  |
| `0x0010DAE0` | `SetAlarm` | 16 | no |  |
| `0x0010DB00` | `_iEnableIntc` | 16 | no |  |
| `0x0010DB10` | `_iDisableIntc` | 16 | no |  |
| `0x0010DB20` | `_iEnableDmac` | 16 | no |  |
| `0x0010DB30` | `_iDisableDmac` | 16 | no |  |
| `0x0010DB60` | `CreateThread` | 16 | no |  |
| `0x0010DB70` | `DeleteThread` | 16 | no |  |
| `0x0010DB80` | `_StartThread` | 16 | no |  |
| `0x0010DBA0` | `ExitDeleteThread` | 16 | yes |  |
| `0x0010DBB0` | `TerminateThread` | 16 | yes |  |
| `0x0010DBF0` | `ChangeThreadPriority` | 16 | yes |  |
| `0x0010DC10` | `RotateThreadReadyQueue` | 16 | yes |  |
| `0x0010DC50` | `GetThreadId` | 16 | no |  |
| `0x0010DC60` | `ReferThreadStatus` | 16 | no |  |
| `0x0010DC80` | `SleepThread` | 16 | yes |  |
| `0x0010DC90` | `WakeupThread` | 16 | yes |  |
| `0x0010DCA0` | `_iWakeupThread` | 16 | no |  |
| `0x0010DCD0` | `SuspendThread` | 16 | yes |  |
| `0x0010DCE0` | `_iSuspendThread` | 16 | no |  |
| `0x0010DD40` | `EndOfHeap` | 16 | no |  |
| `0x0010DD60` | `CreateSema` | 16 | no |  |
| `0x0010DD70` | `DeleteSema` | 16 | no |  |
| `0x0010DD80` | `SignalSema` | 16 | yes |  |
| `0x0010DD90` | `iSignalSema` | 16 | no |  |
| `0x0010DDA0` | `WaitSema` | 16 | yes |  |
| `0x0010DDB0` | `PollSema` | 16 | no |  |
| `0x0010DE00` | `SetOsdConfigParam` | 16 | no |  |
| `0x0010DE10` | `GetOsdConfigParam` | 16 | no |  |
| `0x0010DFE0` | `FlushCache` | 16 | no |  |
| `0x0010E030` | `sceSifStopDma` | 16 | no |  |
| `0x0010E070` | `GetOsdConfigParam2` | 16 | no |  |
| `0x0010E0A0` | `GsPutIMR` | 16 | no |  |
| `0x0010E0D0` | `SetVSyncFlag` | 16 | no |  |
| `0x0010E100` | `sceSifDmaStat` | 16 | no |  |
| `0x0010E120` | `sceSifSetDma` | 16 | no |  |
| `0x0010E130` | `isceSifSetDma` | 16 | no |  |
| `0x0010E140` | `sceSifSetDChain` | 16 | no |  |
| `0x0010E150` | `isceSifSetDChain` | 16 | no |  |
| `0x0010E160` | `sceSifSetReg` | 16 | no |  |
| `0x0010E170` | `sceSifGetReg` | 16 | no |  |
| `0x0010E180` | `_ExecOSD` | 16 | no |  |
| `0x0010E190` | `Deci2Call` | 16 | no |  |
| `0x0010E1C0` | `GetMemorySize` | 16 | no |  |
| `0x0010E1D0` | `_InitTLB` | 16 | no |  |
| `0x0010E658` | `sdk_kernel_SyncDCache` | 124 | no | SyncDCache with interrupts off |
| `0x0010E798` | `sdk_kernel_InvalidDCache` | 124 | no | InvalidDCache with interrupts off |
| `0x0010E840` | `sdk_kernel_DisableIntc` | 104 | no | DisableIntc with interrupts off |
| `0x0010E8A8` | `sdk_kernel_EnableIntc` | 104 | no | EnableIntc with interrupts off |
| `0x0010E910` | `sdk_kernel_DisableDmac` | 104 | no | DisableDmac with interrupts off |
| `0x0010E978` | `sdk_kernel_EnableDmac` | 104 | no | EnableDmac with interrupts off |
| `0x0010E9E0` | `sdk_kernel_iEnableIntc` | 32 | no | iEnableIntc and sync |
| `0x0010EA00` | `sdk_kernel_iDisableIntc` | 32 | no | iDisableIntc and sync |
| `0x0010EA20` | `sdk_kernel_iEnableDmac` | 32 | no | iEnableDmac and sync |
| `0x0010EA40` | `sdk_kernel_iDisableDmac` | 32 | no | iDisableDmac and sync |
| `0x0010EA60` | `RFU116_SetSyscall` | 16 | no |  |
| `0x0010EA70` | `sdk_kernel_KCopy1` | 16 | no | (provisional) syscall 0x5A (copy into kernel memory), from InitAlarm |
| `0x0010EAB8` | `RFU091` | 16 | no |  |
| `0x001160B0` | `sdk_kernel_KCopy2` | 16 | no | (provisional) syscall 0x5A, from InitTLBFunctions |
| `0x001160F8` | `RFU091` | 16 | no |  |
| `0x00116108` | `RFU116_SetSyscall` | 16 | no |  |
| `0x001161F0` | `RFU086_WaitEvnetFlag` | 16 | no |  |
| `0x001168A8` | `sdk_kernel_EIntr` | 24 | no | EI, then returns Status.EIE (bit 16) |
| `0x00116940` | `sdk_kernel_GetSyscall` | 16 | no | (provisional) syscall 0x83, from GetSystemCallTableEntry |
| `0x00116990` | `RFU116_SetSyscall` | 16 | no |  |
| `0x00116A00` | `RFU116_SetSyscall` | 16 | no |  |
| `0x00116A10` | `sdk_kernel_KCopy3` | 16 | no | (provisional) syscall 0x5A, from InitExecPS2 |
| `0x00116A58` | `RFU091` | 16 | no |  |
| `0x00116B80` | `InitTLB` | 8 | no |  |
| `0x00116C30` | `sdk_kernel_Exit` | 40 | no | InitTLB, then _Exit |
| `0x0011D960` | `sdk_kernel_WakeupHandler` | 36 | no | (provisional) alarm handler: iWakeupThread(a2) |
| `0x001AB630` | `sdk_kpatch_AlarmInsert` | 260 | no | kernel patch: inserts into the sorted alarm list (0x80076740, count 0x80076700) |
| `0x001AB738` | `sdk_kpatch_SetAlarm` | 316 | no | kernel patch: SetAlarm, up to 64 alarms on timer 0x10001800 |
| `0x001AB878` | `sdk_kpatch_ReleaseAlarm` | 416 | no | kernel patch: ReleaseAlarm |
| `0x001ABA18` | `sdk_kpatch_AlarmSetCompare` | 32 | no | (provisional) kernel patch: programs the timer compare and syncs |
| `0x001ABA60` | `sdk_kpatch_AlarmHandler` | 480 | no | kernel patch: timer interrupt, runs due alarms |
| `0x001ABFF0` | `sdk_kpatch_SetTlbEntry` | 304 | no | (provisional) kernel patch: maps or unmaps a TLB entry (Wired) |
| `0x001AC688` | `sdk_kpatch_CopyOsdConfig` | 192 | no | (provisional) kernel patch: copies 0x26 bytes of OSD config into kernel RAM |
| `0x001AC868` | `sdk_kpatch_ResetHardware` | 208 | indirect | (provisional) kernel patch: waits for the timer interrupt, then resets through kernel calls |
| `0x001AC938` | `sdk_kpatch_TerminateThreads` | 484 | indirect | (provisional) kernel patch: terminates and deletes threads 1..255 |

### libm

9 functions (named 9). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00119948` | `sdk_libm_copysign` | 68 | no | copysign (double) |
| `0x00119A50` | `sdk_libm_scalbn` | 464 | no | scalbn (double) |
| `0x00119EC8` | `sdk_libm_copysignf` | 60 | no | copysignf |
| `0x0011A0E0` | `sdk_libm_scalbnf` | 200 | no | scalbnf |
| `0x0011A290` | `sdk_libm_pow` | 20 | no | pow (to __ieee754_pow) |
| `0x0011A2A8` | `sdk_libm_acosf` | 20 | no | acosf |
| `0x0011A2C0` | `sdk_libm_atan2f` | 20 | no | atan2f |
| `0x0011A2D8` | `sdk_libm_fmodf` | 20 | no | fmodf |
| `0x0011A2F0` | `sdk_libm_sqrtf` | 20 | no | sqrtf |

### libmpeg

16 functions (named 16). Blocks: 5 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001077E0` | `sdk_mpeg_VdecMotionCode` | 28 | no | (provisional) _ipuVdec with table 3 (motion code) |
| `0x00108428` | `sdk_mpeg_MotionVectorComponent` | 136 | no | (provisional) reconstructs one motion vector component |
| `0x0010ACD8` | `sdk_mpeg_ShowBits` | 28 | no | top a1 bits of the bit buffer |
| `0x0010AF38` | `sdk_mpeg_FindStreamId` | 508 | no | (provisional) looks up a stream id in the table at 0x1AB3E8 |
| `0x0010B430` | `sdk_sceMpegDemuxPss` | 32 | yes | (provisional) sceMpegDemuxPssRing over a linear buffer |
| `0x0010BF60` | `sdk_sceMpegDelete` | 8 | no | (provisional) returns 1 |
| `0x0010C0A8` | `sdk_sceMpegIsEnd` | 12 | no | (provisional) the decoder's end flag |
| `0x0010C0B8` | `sdk_sceMpegIsRefBuffEmpty` | 16 | no | (provisional) no reference pictures left |
| `0x0010C2B0` | `sdk_mpeg_WorkInit` | 20 | no | work area base and size |
| `0x0010C2C8` | `sdk_mpeg_WorkMark` | 12 | no | remembers the allocation point |
| `0x0010C2D8` | `sdk_mpeg_WorkRestore` | 12 | no | returns to the remembered point |
| `0x0010C2E8` | `sdk_mpeg_WorkAlloc` | 108 | yes | aligned allocation from the work area ("work area size is too small") |
| `0x0010C808` | `sdk_mpeg_ResetIpu` | 12 | no | (provisional) clears +0x848 and sets the IPU to MPEG-1 |
| `0x0010C9B8` | `sdk_mpeg_PrintError` | 16 | yes | printf "[MPEG ERROR]%s" |
| `0x0010D2B0` | `sdk_mpeg_SequenceScalableExt` | 12 | yes | error: not implemented |
| `0x0010D2D0` | `sdk_mpeg_PictureSpatialScalableExt` | 12 | yes | error: not supported |

### libpkt

8 functions (named 8). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00104838` | `sdk_sceGifPkInit` | 16 | no | packet = base, no open DMA tag |
| `0x00104848` | `sdk_sceGifPkReset` | 16 | no | back to the base |
| `0x00104858` | `sdk_sceGifPkTerminate` | 88 | no | (provisional) pads to 16 bytes and closes the open DMA tag (qwc) |
| `0x001048B0` | `sdk_sceGifPkCnt` | 116 | no | opens a CNT DMA tag |
| `0x00104928` | `sdk_sceGifPkRef` | 152 | no | adds a REF DMA tag |
| `0x001049C0` | `sdk_sceGifPkEnd` | 116 | no | opens an END DMA tag |
| `0x00104A38` | `sdk_sceGifPkReserve` | 20 | no | skips a0 words |
| `0x00104A50` | `sdk_sceGifPkOpenGifTag` | 24 | no | writes a GIF tag and remembers it for sceGifPkCloseGifTag |

### libscf

2 functions (named 2). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001A38F0` | `sdk_scf_BcdToTime` | 104 | no | converts the six BCD time fields (frombcd), used by AdjustTime |
| `0x001A3958` | `sdk_scf_TimeToBcd` | 104 | no | converts back to BCD (tobcd), used by AdjustTime |

### libsif

13 functions (named 13). Blocks: 7 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0010E1E0` | `sdk_sif_ClearIopSyncFlag` | 12 | no | (provisional) clears 0x1AB5D0 |
| `0x0010FFA8` | `sdk_sif_GetRpcSlot` | 24 | no | (provisional) entry a0 of the table at 0x1BF080 |
| `0x00110860` | `sdk_sceSifExitRpc` | 36 | no | (provisional) sceSifExitCmd and clears the init flag |
| `0x00110888` | `sdk_sif_AllocRpcPacket` | 168 | no | free RPC packet slot with interrupts off |
| `0x00110930` | `sdk_sif_FreeRpcPacket` | 28 | no | marks an RPC packet free |
| `0x00114E30` | `sdk_sceSifFreeIopHeap` | 120 | yes | IOP heap RPC (server 0x80000003) function 2 |
| `0x00114EA8` | `sdk_sceSifFreeSysMemory` | 28 | yes | frees IOP memory (to sceSifFreeIopHeap) |
| `0x001150B8` | `sdk_loadfile_CheckVersion` | 140 | no | (provisional) compares the LOADFILE module version ("2430") |
| `0x00115750` | `sdk_sceSifLoadModuleBuffer` | 28 | yes | (provisional) _sceSifLoadModuleBuffer wrapper |
| `0x00115770` | `sdk_sceSifLoadStartModuleBuffer` | 28 | yes | (provisional) _sceSifLoadModuleBuffer wrapper |
| `0x001159B8` | `sceSifLoadModule` | 32 | yes |  |
| `0x001159D8` | `sceSifLoadStartModule` | 28 | yes |  |
| `0x00115B00` | `sdk_sceSifLoadElfPart` | 28 | yes | (provisional) _sceSifLoadElfPart wrapper |

### libtty

1 functions (named 1). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0010F0E0` | `sdk_tty_InitDevice` | 36 | no | (provisional) fills the tty device record at 0x1BEA40 |

### libvu0

3 functions (named 3). Blocks: 0 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00104BB0` | `sdk_sceVu0OuterProduct` | 28 | no | cross product |
| `0x00104DA0` | `sdk_sceVu0ScaleVector` | 24 | no | vector times scalar |
| `0x00104DE8` | `sdk_sceVu0CopyVector` | 12 | no | copies a vector |

### mwcpp

1 functions (named 1). Blocks: 1 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00100420` | `sdk_operator_delete` | 44 | yes | Metrowerks C++ runtime operator delete (calls free); destructors call it when their delete flag is > 0 |

### mwrt

15 functions (named 15). Blocks: 8 can reach a thread switch, 1 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x00100140` | `__destroy_global_chain` | 8 | indirect |  |
| `0x00100150` | `sdk_crt0_InitCpp` | 36 | no | __initialize_cpp_rts with the ctor and exception tables |
| `0x00100400` | `sdk_mw_DeleteArray` | 28 | yes | operator delete[] |
| `0x001004B0` | `sdk_mw_exception_what` | 12 | no | std::exception::what ("exception") |
| `0x001004C0` | `sdk_mw_NewArray` | 28 | yes | operator new[] |
| `0x00100540` | `sdk_mw_bad_alloc_dtor` | 96 | yes | std::bad_alloc destructor (vtable 0x1BCDF0) |
| `0x001005A0` | `sdk_mw_bad_alloc_what` | 12 | no | std::bad_alloc::what ("bad_alloc") |
| `0x001008E0` | `sdk_mw_unexpected` | 36 | yes | calls the unexpected handler (0x1AB218) |
| `0x00100910` | `sdk_mw_terminate` | 36 | yes | calls the terminate handler (0x1AB210) |
| `0x00100940` | `sdk_mw_terminate2` | 36 | yes | calls the terminate handler (from unexpected) |
| `0x00100970` | `sdk_mw_default_terminate` | 28 | yes | default terminate: abort |
| `0x001009E0` | `sdk_mw_register_global_object` | 36 | no | links a global object and its destructor into the exit list (0x1BDFC8) |
| `0x00101300` | `sdk_mw_bad_exception_dtor` | 96 | yes | std::bad_exception destructor (vtable 0x1BCE00) |
| `0x00102290` | `sdk_mw_bad_exception_what` | 12 | no | std::bad_exception::what |
| `0x001023E0` | `sdk_mw_ExceptionRecordDefault` | 32 | no | (provisional) default exception record ranges |

### signature-scan

558 functions (named 558). Blocks: 223 can reach a thread switch, 18 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x001000F0` | `_d_utod` | 76 | no |  |
| `0x00100180` | `__construct_array` | 292 | indirect |  |
| `0x001002B0` | `__construct_new_array` | 332 | indirect |  |
| `0x00100460` | `__dt__Q23std9exceptionFv` | 76 | yes |  |
| `0x001004E0` | `default_new_handler__3stdFv` | 76 | yes |  |
| `0x001005B0` | `__nw__FUi` | 176 | yes |  |
| `0x00100670` | `__throw_catch_compare` | 608 | no |  |
| `0x00100990` | `__destroy_global_chain` | 68 | indirect |  |
| `0x00100A10` | `__initialize_cpp_rts` | 84 | indirect |  |
| `0x00100A70` | `__ptmf_scall` | 52 | indirect |  |
| `0x00100AB0` | `__DecodeUnsignedNumber__FPcPUi` | 156 | no |  |
| `0x00100B50` | `__DecodeSignedNumber__FPcPi` | 156 | no |  |
| `0x00100BF0` | `__end__catch` | 52 | indirect |  |
| `0x00100C30` | `__ThrowHandler__FP12ThrowContext` | 400 | yes |  |
| `0x00100DC0` | `FindExceptionHandler__FP12ThrowContextP13ExceptionInfoPl` | 868 | yes |  |
| `0x00101130` | `__unexpected` | 96 | yes |  |
| `0x00101360` | `FindMostRecentException__FP12ThrowContextP13ExceptionInfo` | 452 | yes |  |
| `0x00101530` | `UnwindStack__FP12ThrowContextP13ExceptionInfoPc` | 2164 | yes |  |
| `0x00101DB0` | `NextAction__FP14ActionIterator` | 788 | yes |  |
| `0x001020D0` | `FindExceptionRecord__FPcP13ExceptionInfo` | 440 | no |  |
| `0x001022A0` | `__TransferControl__FP12ThrowContextP13ExceptionInfoPc` | 100 | indirect |  |
| `0x00102310` | `__throw` | 128 | yes |  |
| `0x00102390` | `__SkipUnwindInfo__FPc` | 76 | no |  |
| `0x00102400` | `__SetupFrameInfo__FP12ThrowContextP13ExceptionInfo` | 176 | no |  |
| `0x001024B0` | `__PopStackFrame__FP12ThrowContextP13ExceptionInfo` | 608 | no |  |
| `0x00102710` | `sceGsResetGraph` | 400 | no |  |
| `0x001028B0` | `sceGsResetPath` | 104 | no |  |
| `0x00102918` | `sceGsSetDefDispEnv` | 624 | yes |  |
| `0x00102B88` | `sceGsPutDispEnv` | 188 | no |  |
| `0x00102C48` | `sceGszbufaddr` | 200 | no |  |
| `0x00102D10` | `sceGsSetDefDrawEnv` | 484 | no |  |
| `0x00102EF8` | `sceGsSetDefClear` | 260 | no |  |
| `0x00103000` | `sceGsPutDrawEnv` | 232 | yes |  |
| `0x001030E8` | `sceGsSetDefDBuff` | 664 | yes |  |
| `0x00103380` | `sceGsSyncV` | 148 | yes |  |
| `0x00103418` | `sceGsSyncPath` | 788 | yes |  |
| `0x001037B8` | `sceGsSetDefDrawEnv2` | 480 | no |  |
| `0x00103A20` | `sceGsSetDefDBuffDc` | 740 | yes |  |
| `0x00103D08` | `sceGsSwapDBuffDc` | 92 | yes |  |
| `0x00103D68` | `memclr` | 52 | no |  |
| `0x00103DA0` | `sceDmaGetChan` | 40 | no |  |
| `0x00103DC8` | `sceDmaReset` | 220 | no |  |
| `0x00103EB8` | `sceDmaPutEnv` | 472 | no |  |
| `0x00104090` | `sceDmaGetEnv` | 56 | no |  |
| `0x001040C8` | `sceDmaPutStallAddr` | 40 | no |  |
| `0x001040F0` | `sceDmaSend` | 216 | yes |  |
| `0x001041C8` | `sceDmaSendN` | 228 | yes |  |
| `0x001042B0` | `sceDmaSendI` | 228 | yes |  |
| `0x00104398` | `sceDmaRecv` | 196 | yes |  |
| `0x00104460` | `sceDmaRecvN` | 236 | yes |  |
| `0x00104550` | `sceDmaRecvI` | 240 | yes |  |
| `0x00104640` | `sceDmaSync` | 196 | yes |  |
| `0x00104708` | `sceDmaWatch` | 208 | yes |  |
| `0x001047D8` | `sceDmaPause` | 72 | no |  |
| `0x00104A68` | `sceGifPkCloseGifTag` | 164 | no |  |
| `0x00104B10` | `sceGifPkAddGsAD` | 36 | no |  |
| `0x00104B38` | `sceVu0ApplyMatrix` | 44 | no |  |
| `0x00104B68` | `sceVu0MulMatrix` | 68 | no |  |
| `0x00104BD0` | `sceVu0InnerProduct` | 36 | no |  |
| `0x00104BF8` | `sceVu0Normalize` | 60 | no |  |
| `0x00104C38` | `sceVu0TransposeMatrix` | 68 | no |  |
| `0x00104C80` | `sceVu0InversMatrix` | 108 | no |  |
| `0x00104CF0` | `sceVu0DivVector` | 32 | no |  |
| `0x00104D10` | `sceVu0DivVectorXYZ` | 32 | no |  |
| `0x00104D30` | `sceVu0InterVector` | 40 | no |  |
| `0x00104DB8` | `sceVu0TransMatrix` | 44 | no |  |
| `0x00104DF8` | `sceVu0CopyMatrix` | 36 | no |  |
| `0x00104E60` | `sceVu0UnitMatrix` | 40 | no |  |
| `0x00104E88` | `_sceVu0ecossin` | 116 | no |  |
| `0x00104F00` | `sceVu0RotMatrixZ` | 164 | no |  |
| `0x00104FA8` | `sceVu0RotMatrixX` | 168 | no |  |
| `0x00105050` | `sceVu0RotMatrixY` | 168 | no |  |
| `0x001050F8` | `sceVu0RotMatrix` | 80 | no |  |
| `0x00105148` | `sceVu0ClampVector` | 36 | no |  |
| `0x00105170` | `sceVu0CameraMatrix` | 176 | no |  |
| `0x00105220` | `sceVu0NormalLightMatrix` | 192 | no |  |
| `0x001052E0` | `sceVu0LightColorMatrix` | 104 | no |  |
| `0x00105348` | `sceVu0ViewScreenMatrix` | 260 | no |  |
| `0x00105450` | `sceVu0DropShadowMatrix` | 400 | no |  |
| `0x001055E0` | `sceVu0RotTransPersN` | 88 | no |  |
| `0x00105638` | `sceVu0RotTransPers` | 68 | no |  |
| `0x001056A0` | `sceVu0InterVectorXYZ` | 44 | no |  |
| `0x001056E8` | `sceVu0ClipScreen` | 72 | no |  |
| `0x00105730` | `sceVu0ClipScreen3` | 96 | no |  |
| `0x00105790` | `sceVu0ClipAll` | 140 | no |  |
| `0x00105820` | `sceVpu0Reset` | 92 | no |  |
| `0x00105880` | `_motionComp0` | 592 | yes |  |
| `0x00105AD0` | `_getAllRefs` | 1796 | yes |  |
| `0x001061D8` | `_getRef0` | 1052 | no |  |
| `0x001065F8` | `_doMC` | 540 | yes |  |
| `0x00106818` | `_rix_000` | 116 | no |  |
| `0x00106890` | `_ri0_000` | 148 | no |  |
| `0x00106928` | `_rix_001` | 180 | no |  |
| `0x001069E0` | `_ri0_001` | 204 | no |  |
| `0x00106AB0` | `_rix_010` | 172 | no |  |
| `0x00106B60` | `_ri0_010` | 184 | no |  |
| `0x00106C18` | `_rix_011` | 248 | no |  |
| `0x00106D10` | `_ri0_011` | 252 | no |  |
| `0x00106E10` | `_rix_100` | 156 | no |  |
| `0x00106EB0` | `_ri0_100` | 172 | no |  |
| `0x00106F60` | `_rix_101` | 220 | no |  |
| `0x00107040` | `_ri0_101` | 228 | no |  |
| `0x00107128` | `_rix_110` | 212 | no |  |
| `0x00107200` | `_ri0_110` | 208 | no |  |
| `0x001072D0` | `_rix_111` | 288 | no |  |
| `0x001073F0` | `_ri0_111` | 276 | no |  |
| `0x00107508` | `_copyAddRefImage` | 92 | no |  |
| `0x00107568` | `_copyRefImage` | 96 | no |  |
| `0x001075C8` | `_ipuSetMPEG1` | 40 | no |  |
| `0x001075F0` | `_waitBdecOut` | 496 | yes |  |
| `0x00107800` | `_dualPrimeVector` | 388 | no |  |
| `0x00107988` | `_mbAddressIncrement` | 272 | yes |  |
| `0x00107A98` | `_pictureData0` | 264 | yes |  |
| `0x00107BA0` | `_sliceA0` | 300 | yes |  |
| `0x00107CD0` | `_slice0` | 496 | yes |  |
| `0x00107EC0` | `_skipMB0` | 188 | yes |  |
| `0x00107F80` | `_decMB0` | 1192 | yes |  |
| `0x001084B0` | `_motionVectors` | 412 | no |  |
| `0x00108650` | `_motionVector` | 332 | no |  |
| `0x001087A0` | `_sendIpuCommand` | 44 | no |  |
| `0x001087D0` | `_waitIpuIdle` | 164 | no |  |
| `0x00108878` | `_waitIpuIdle64` | 176 | no |  |
| `0x00108928` | `_ipuVdec` | 376 | no |  |
| `0x00108AA0` | `_peepBit` | 264 | no |  |
| `0x00108BA8` | `_flushBuf` | 240 | no |  |
| `0x00108C98` | `_nextBit` | 336 | no |  |
| `0x00108DE8` | `_nextStartCode` | 124 | no |  |
| `0x00108E68` | `_sliceB` | 100 | no |  |
| `0x00108ED0` | `_nextHeader` | 268 | yes |  |
| `0x00108FE0` | `_pictureHeader` | 196 | no |  |
| `0x001090A8` | `_extensionAndUserData` | 292 | indirect |  |
| `0x001091D0` | `_pictureCodingExtension` | 496 | no |  |
| `0x001093C0` | `_extrainfo` | 68 | no |  |
| `0x00109408` | `_updateTempTackData` | 116 | no |  |
| `0x00109480` | `_groupOfPicturesHeader` | 160 | no |  |
| `0x00109520` | `_quantMatrixExtension` | 196 | yes |  |
| `0x001095E8` | `_pictureDisplayExtension` | 244 | no |  |
| `0x001096E0` | `_copyrightExtension` | 144 | no |  |
| `0x00109770` | `_decPicture` | 204 | yes |  |
| `0x00109840` | `_outputFrame` | 144 | yes |  |
| `0x001098D0` | `_updateRefImage` | 692 | no |  |
| `0x00109B88` | `_isOutSizeOK` | 160 | yes |  |
| `0x00109C28` | `_cpr8` | 624 | no |  |
| `0x00109E98` | `_markOutput` | 40 | no |  |
| `0x00109EC0` | `_getPtsDtsFlags` | 396 | no |  |
| `0x0010A050` | `_dispRefImage` | 272 | yes |  |
| `0x0010A160` | `_dispRefImageField` | 440 | yes |  |
| `0x0010A318` | `dmaRefImage` | 340 | no |  |
| `0x0010A470` | `receiveDataFromIPU` | 112 | no |  |
| `0x0010A4E0` | `_doCSC` | 280 | no |  |
| `0x0010A5F8` | `_ch3dmaCSC` | 372 | no |  |
| `0x0010A770` | `_doCSC2` | 432 | yes |  |
| `0x0010A920` | `_ch4dma` | 260 | no |  |
| `0x0010AA28` | `_csc_storeRefImage` | 628 | yes |  |
| `0x0010ACA0` | `_sysbitInit` | 52 | no |  |
| `0x0010ACF8` | `_sysbitFlush` | 152 | no |  |
| `0x0010AD90` | `_sysbitGet` | 76 | no |  |
| `0x0010ADE0` | `_sysbitMarker` | 68 | no |  |
| `0x0010AE28` | `_sysbitJump` | 84 | no |  |
| `0x0010AE80` | `_sysbitPtr` | 44 | no |  |
| `0x0010AEB0` | `_type2id` | 136 | no |  |
| `0x0010B138` | `sceMpegDemuxPssRing` | 756 | yes |  |
| `0x0010B450` | `sceMpegAddStrCallback` | 244 | no |  |
| `0x0010B548` | `_pack_header` | 336 | no |  |
| `0x0010B698` | `_system_header` | 104 | no |  |
| `0x0010B708` | `_PES_packet` | 1456 | yes |  |
| `0x0010BCB8` | `sceMpegInit` | 156 | no |  |
| `0x0010BD58` | `sceMpegCreate` | 520 | yes |  |
| `0x0010BF68` | `sceMpegAddBs` | 36 | no |  |
| `0x0010BF90` | `sceMpegGetPicture` | 72 | yes |  |
| `0x0010BFD8` | `sceMpegGetPictureRAW8` | 68 | yes |  |
| `0x0010C020` | `sceMpegGetPictureRAW8xy` | 80 | yes |  |
| `0x0010C088` | `sceMpegGetDecodeMode` | 32 | no |  |
| `0x0010C0C8` | `sceMpegReset` | 80 | no |  |
| `0x0010C118` | `sceMpegClearRefBuff` | 84 | no |  |
| `0x0010C170` | `sceMpegAddCallback` | 36 | no |  |
| `0x0010C198` | `_dispatchMpegCallback` | 80 | indirect |  |
| `0x0010C1E8` | `_dispatchMpegCbNodata` | 36 | no |  |
| `0x0010C370` | `_getpic` | 232 | yes |  |
| `0x0010C4E0` | `_decodeOrSkipFrame` | 276 | yes |  |
| `0x0010C5F8` | `_decodeOrSkip` | 68 | yes |  |
| `0x0010C640` | `_decodeOrSkipField` | 348 | yes |  |
| `0x0010C7A0` | `_sceMpegFlush` | 104 | yes |  |
| `0x0010C818` | `_lastFrame` | 124 | yes |  |
| `0x0010C898` | `_clearOnce` | 96 | no |  |
| `0x0010C8F8` | `_clearEach` | 188 | no |  |
| `0x0010C9C8` | `_Error1` | 52 | yes |  |
| `0x0010CA00` | `_Error` | 84 | yes |  |
| `0x0010CA58` | `_sendDataToIPU` | 240 | no |  |
| `0x0010CB48` | `_RefImageInit` | 32 | no |  |
| `0x0010CB68` | `_sequenceHeader` | 292 | yes |  |
| `0x0010CC90` | `_initSeq` | 680 | yes |  |
| `0x0010CF38` | `_initRefImages` | 224 | no |  |
| `0x0010D018` | `_setDefaultQM` | 212 | no |  |
| `0x0010D0F0` | `_sequenceExtension` | 304 | yes |  |
| `0x0010D220` | `_sequenceDisplayExtension` | 140 | no |  |
| `0x0010D310` | `setD3_CHCR` | 100 | no |  |
| `0x0010D378` | `setD4_CHCR` | 100 | no |  |
| `0x0010D3E0` | `sceIpuStopDMA` | 228 | no |  |
| `0x0010D4C8` | `sceIpuRestartDMA` | 336 | no |  |
| `0x0010D618` | `sceIpuSync` | 104 | no |  |
| `0x0010D680` | `setD4_CHCR` | 100 | no |  |
| `0x0010D6E8` | `sceIpuInit` | 568 | no |  |
| `0x0010E1F0` | `VSync` | 144 | no |  |
| `0x0010E280` | `VSync2` | 164 | no |  |
| `0x0010E328` | `write` | 124 | no |  |
| `0x0010E3A8` | `read` | 116 | no |  |
| `0x0010E460` | `sbrk` | 172 | no |  |
| `0x0010E538` | `kill` | 40 | no |  |
| `0x0010E5B0` | `_sceSDC` | 164 | no |  |
| `0x0010E6F0` | `_sceIDC` | 164 | no |  |
| `0x0010EA80` | `kCopy` | 56 | no |  |
| `0x0010EAC8` | `InitAlarm` | 212 | no |  |
| `0x0010EBA0` | `topThread` | 216 | yes |  |
| `0x0010EC78` | `InitThread` | 212 | yes |  |
| `0x0010ED50` | `iWakeupThread` | 148 | no |  |
| `0x0010EDE8` | `iRotateThreadReadyQueue` | 124 | no |  |
| `0x0010EE68` | `iSuspendThread` | 152 | no |  |
| `0x0010EF00` | `sceDeci2Open` | 68 | no |  |
| `0x0010EF48` | `sceDeci2Close` | 36 | no |  |
| `0x0010EF70` | `sceDeci2ReqSend` | 48 | no |  |
| `0x0010EFA0` | `sceDeci2Poll` | 36 | no |  |
| `0x0010EFC8` | `sceDeci2ExRecv` | 52 | no |  |
| `0x0010F000` | `sceDeci2ExSend` | 52 | no |  |
| `0x0010F038` | `sceDeci2ExReqSend` | 48 | no |  |
| `0x0010F068` | `sceDeci2ExLock` | 36 | no |  |
| `0x0010F090` | `sceDeci2ExUnLock` | 36 | no |  |
| `0x0010F0B8` | `kputs` | 36 | no |  |
| `0x0010F108` | `QueuePeekWriteDone` | 60 | no |  |
| `0x0010F148` | `QueuePeekReadDone` | 60 | no |  |
| `0x0010F188` | `sceTtyHandler` | 400 | no |  |
| `0x0010F320` | `sceTtyWrite` | 336 | no |  |
| `0x0010F470` | `sceTtyRead` | 208 | no |  |
| `0x0010F540` | `sceTtyInit` | 188 | no |  |
| `0x0010F600` | `kputchar` | 56 | no |  |
| `0x0010F638` | `deci2Putchar` | 176 | no |  |
| `0x0010F6E8` | `serialPutchar` | 52 | no |  |
| `0x0010F720` | `ftoi` | 144 | no |  |
| `0x0010F7B0` | `printfloat` | 352 | indirect |  |
| `0x0010F918` | `_printf` | 1476 | indirect |  |
| `0x0010FEE0` | `kprintf` | 56 | no |  |
| `0x0010FF18` | `scePrintf` | 96 | no |  |
| `0x0010FFF0` | `sceSifInitCmd` | 640 | no |  |
| `0x00110270` | `sceSifExitCmd` | 52 | no |  |
| `0x001102D8` | `sceSifAddCmdHandler` | 44 | no |  |
| `0x00110308` | `sceSifRemoveCmdHandler` | 40 | no |  |
| `0x00110330` | `_sceSifSendCmd` | 308 | no |  |
| `0x00110468` | `sceSifSendCmd` | 60 | no |  |
| `0x001104A8` | `isceSifSendCmd` | 60 | no |  |
| `0x001104E8` | `_sceSifCmdIntrHdlr` | 296 | indirect |  |
| `0x00110610` | `sceSifWriteBackDCache` | 172 | no |  |
| `0x001106C0` | `sceSifInitRpc` | 412 | no |  |
| `0x00110950` | `_sceRpcGetFPacket` | 48 | no |  |
| `0x00110980` | `_sceRpcGetFPacket2` | 64 | no |  |
| `0x001109C0` | `_request_end` | 180 | indirect |  |
| `0x00110A78` | `_request_rdata` | 96 | no |  |
| `0x00110AD8` | `sceSifGetOtherData` | 344 | yes |  |
| `0x00110C30` | `_search_svdata` | 76 | no |  |
| `0x00110C80` | `_request_bind` | 176 | no |  |
| `0x00110D30` | `sceSifBindRpc` | 320 | yes |  |
| `0x00110E70` | `_request_call` | 140 | no |  |
| `0x00110F00` | `sceSifCallRpc` | 492 | yes |  |
| `0x001110F0` | `sceSifCheckStatRpc` | 60 | no |  |
| `0x00111130` | `sceSifSetRpcQueue` | 148 | no |  |
| `0x001111C8` | `sceSifRegisterRpc` | 208 | no |  |
| `0x00111298` | `sceSifRemoveRpc` | 148 | no |  |
| `0x00111330` | `sceSifRemoveRpcQueue` | 144 | no |  |
| `0x001113C0` | `sceSifGetNextRequest` | 88 | no |  |
| `0x00111418` | `sceSifExecRequest` | 452 | indirect |  |
| `0x001115E0` | `sceSifRpcLoop` | 60 | yes |  |
| `0x00111620` | `_sceFsIobSemaMK` | 92 | no |  |
| `0x00111680` | `new_iob` | 136 | yes |  |
| `0x00111708` | `get_iob` | 108 | yes |  |
| `0x00111778` | `_sceFs_Rcv_Intr` | 956 | no |  |
| `0x00111B38` | `_sceFsSemInit` | 76 | no |  |
| `0x00111B88` | `_sceFsWaitS` | 44 | yes |  |
| `0x00111BC8` | `scePowerOffHandler` | 144 | yes |  |
| `0x00111C58` | `_sceFs_Poff_Intr` | 44 | indirect |  |
| `0x00111C88` | `sceFsInit` | 452 | yes |  |
| `0x00111EE0` | `sceFsReset` | 52 | no |  |
| `0x00111F18` | `sceOpen` | 644 | yes |  |
| `0x001121A0` | `sceClose` | 380 | yes |  |
| `0x00112320` | `sceLseek` | 564 | yes |  |
| `0x00112558` | `sceRead` | 624 | yes |  |
| `0x001127C8` | `sceWrite` | 700 | yes |  |
| `0x00112A88` | `sceIoctl` | 844 | yes |  |
| `0x00112DD8` | `sceIoctl2` | 476 | yes |  |
| `0x00112FB8` | `_sceCallCode` | 420 | yes |  |
| `0x00113180` | `sceMkdir` | 428 | yes |  |
| `0x00113350` | `sceFormat` | 620 | yes |  |
| `0x001135C0` | `sceAddDrv` | 288 | yes |  |
| `0x00113700` | `sceDopen` | 200 | yes |  |
| `0x001137C8` | `sceDclose` | 360 | yes |  |
| `0x00113930` | `sceDread` | 344 | yes |  |
| `0x00113A88` | `sceGetstat` | 416 | yes |  |
| `0x00113C28` | `sceChstat` | 572 | yes |  |
| `0x00113E68` | `sceRename` | 496 | yes |  |
| `0x00114078` | `sceSync` | 408 | yes |  |
| `0x00114210` | `sceMount` | 620 | yes |  |
| `0x001144A0` | `sceLseek64` | 564 | yes |  |
| `0x001146D8` | `sceDevctl` | 568 | yes |  |
| `0x00114910` | `sceSymlink` | 480 | yes |  |
| `0x00114AF0` | `sceReadlink` | 456 | yes |  |
| `0x00114CB8` | `sceSifInitIopHeap` | 136 | yes |  |
| `0x00114D40` | `sceSifAllocIopHeap` | 112 | yes |  |
| `0x00114DB0` | `sceSifAllocSysMemory` | 128 | yes |  |
| `0x00114EC8` | `sceSifLoadIopHeap` | 240 | yes |  |
| `0x00114FB8` | `_lf_bind` | 256 | yes |  |
| `0x00115148` | `sceSifLoadFileReset` | 56 | no |  |
| `0x00115180` | `_sceSifLoadModuleBuffer` | 520 | yes |  |
| `0x00115388` | `sceSifStopModule` | 520 | yes |  |
| `0x00115590` | `sceSifUnloadModule` | 144 | yes |  |
| `0x00115620` | `sceSifSearchModuleByName` | 160 | yes |  |
| `0x001156C0` | `sceSifSearchModuleByAddress` | 144 | yes |  |
| `0x00115790` | `_sceSifLoadModule` | 548 | yes |  |
| `0x001159F8` | `_sceSifLoadElfPart` | 260 | yes |  |
| `0x00115B20` | `sceSifLoadElf` | 40 | yes |  |
| `0x00115B48` | `sceSifGetIopAddr` | 236 | yes |  |
| `0x00115C38` | `sceSifSetIopAddr` | 220 | yes |  |
| `0x00115D18` | `sceSifResetIop` | 316 | no |  |
| `0x00115E58` | `sceSifIsAliveIop` | 40 | no |  |
| `0x00115E80` | `sceSifSyncIop` | 56 | yes |  |
| `0x00115EB8` | `sceSifRebootIop` | 272 | yes |  |
| `0x00115FC8` | `SetTLBHandler` | 96 | no |  |
| `0x00116028` | `SetDebugHandler` | 132 | no |  |
| `0x001160C0` | `kCopy` | 56 | no |  |
| `0x00116118` | `InitTLBFunctions` | 180 | no |  |
| `0x00116200` | `SetTLBEntry` | 48 | no |  |
| `0x00116290` | `InitTLB` | 64 | no |  |
| `0x001162D0` | `InitTLB32MB` | 500 | no |  |
| `0x00116500` | `_kTLBException` | 256 | no |  |
| `0x00116640` | `_kExitTLBHandler` | 244 | no |  |
| `0x00116740` | `_kDebugException` | 252 | no |  |
| `0x00116860` | `DIntr` | 72 | no |  |
| `0x001168C0` | `supplement_crt0` | 72 | no |  |
| `0x00116908` | `kFindAddress` | 52 | no |  |
| `0x00116950` | `GetSystemCallTableEntry` | 60 | no |  |
| `0x001169C0` | `_InitSys` | 60 | yes |  |
| `0x00116A20` | `kCopy` | 56 | no |  |
| `0x00116A68` | `PatchIsNeeded` | 104 | no |  |
| `0x00116AD0` | `InitExecPS2` | 172 | no |  |
| `0x00116B88` | `ExecPS2` | 92 | no |  |
| `0x00116BE8` | `LoadExecPS2` | 72 | no |  |
| `0x00116C58` | `ExecOSD` | 56 | no |  |
| `0x00116C90` | `sceSdRemoteInit` | 164 | yes |  |
| `0x00116D38` | `sceSdTransToIOP` | 132 | no |  |
| `0x00116DD0` | `sceSdRemote` | 496 | yes |  |
| `0x00116FC0` | `__ieee754_pow` | 3856 | no |  |
| `0x00117F10` | `__ieee754_sqrt` | 796 | no |  |
| `0x00118230` | `__ieee754_acosf` | 1052 | no |  |
| `0x00118658` | `__ieee754_atan2f` | 468 | no |  |
| `0x00118838` | `__ieee754_fmodf` | 448 | no |  |
| `0x00118A08` | `__ieee754_rem_pio2f` | 928 | no |  |
| `0x00118DB8` | `__ieee754_sqrtf` | 204 | no |  |
| `0x00118E88` | `__kernel_cosf` | 336 | no |  |
| `0x00118FD8` | `__kernel_rem_pio2f` | 2116 | no |  |
| `0x00119840` | `__kernel_sinf` | 260 | no |  |
| `0x00119990` | `fabs` | 52 | no |  |
| `0x00119C38` | `atanf` | 640 | no |  |
| `0x00119F08` | `cosf` | 212 | no |  |
| `0x00119FE0` | `fabsf` | 40 | no |  |
| `0x0011A008` | `floorf` | 216 | no |  |
| `0x0011A1A8` | `sinf` | 220 | no |  |
| `0x0011A330` | `sceCdDelayThread` | 104 | yes |  |
| `0x0011A398` | `sceCdCallback` | 84 | yes |  |
| `0x0011A490` | `_Cdvd_cbLoop` | 188 | yes |  |
| `0x0011A550` | `sceCdInitEeCB` | 212 | yes |  |
| `0x0011A6C8` | `cmd_sem_init` | 148 | no |  |
| `0x0011A760` | `cdvd_exit` | 128 | yes |  |
| `0x0011A7E0` | `sceCdPOffCallback` | 108 | no |  |
| `0x0011A850` | `_sceCd_Poff_Intr` | 60 | indirect |  |
| `0x0011A890` | `PowerOffCB` | 100 | no |  |
| `0x0011A8F8` | `sceCdSearchFile` | 764 | yes |  |
| `0x0011AD68` | `sceCdNcmdDiskReady` | 152 | yes |  |
| `0x0011AE00` | `sceCdSync` | 160 | yes |  |
| `0x0011AEA0` | `sceCdSyncS` | 108 | yes |  |
| `0x0011B080` | `sceCdInit` | 736 | yes |  |
| `0x0011B558` | `sceCdMmode` | 196 | yes |  |
| `0x0011B620` | `sceCdReadClock` | 248 | yes |  |
| `0x0011B718` | `sceCdStInit` | 44 | yes |  |
| `0x0011B748` | `sceCdStStart` | 52 | yes |  |
| `0x0011B780` | `sceCdStSeekF` | 44 | yes |  |
| `0x0011B7B0` | `sceCdStSeek` | 44 | yes |  |
| `0x0011B7E0` | `sceCdStStop` | 56 | yes |  |
| `0x0011B818` | `sceCdStRead` | 392 | yes |  |
| `0x0011B9A0` | `sceCdStPause` | 80 | yes |  |
| `0x0011B9F0` | `sceCdStResume` | 84 | yes |  |
| `0x0011BA48` | `sceCdStStat` | 72 | yes |  |
| `0x0011BA90` | `sceCdStream` | 360 | yes |  |
| `0x0011BBF8` | `_send_to_iop` | 276 | yes |  |
| `0x0011BD10` | `scePadInit` | 312 | yes |  |
| `0x0011BE50` | `scePadInit2` | 156 | yes |  |
| `0x0011BEF0` | `scePadEnd` | 128 | yes |  |
| `0x0011BF70` | `scePadPortOpen` | 484 | yes |  |
| `0x0011C158` | `scePadPortClose` | 184 | yes |  |
| `0x0011C210` | `scePadGetDmaStr` | 96 | no |  |
| `0x0011C270` | `scePadGetFrameCount` | 80 | no |  |
| `0x0011C2C0` | `scePadRead` | 124 | no |  |
| `0x0011C340` | `scePadGetState` | 120 | no |  |
| `0x0011C3B8` | `scePadStateIntToStr` | 52 | no |  |
| `0x0011C3F0` | `scePadSetReqState` | 100 | no |  |
| `0x0011C458` | `scePadGetReqState` | 80 | no |  |
| `0x0011C4E0` | `scePadInfoAct` | 288 | no |  |
| `0x0011C600` | `scePadInfoComb` | 284 | no |  |
| `0x0011C720` | `scePadInfoMode` | 312 | no |  |
| `0x0011C858` | `scePadSetMainMode` | 180 | yes |  |
| `0x0011C910` | `scePadSetActDirect` | 192 | yes |  |
| `0x0011C9D0` | `scePadSetActAlign` | 216 | yes |  |
| `0x0011CAA8` | `scePadGetButtonMask` | 180 | no |  |
| `0x0011CB60` | `scePadSetButtonInfo` | 176 | yes |  |
| `0x0011CC10` | `scePadInfoPressMode` | 92 | no |  |
| `0x0011CC70` | `scePadEnterPressMode` | 84 | yes |  |
| `0x0011CCC8` | `scePadExitPressMode` | 84 | yes |  |
| `0x0011CD20` | `scePadSetVrefParam` | 204 | yes |  |
| `0x0011CDF0` | `scePadGetPortMax` | 100 | yes |  |
| `0x0011CE58` | `scePadGetSlotMax` | 104 | yes |  |
| `0x0011CEC0` | `scePadGetModVersion` | 100 | yes |  |
| `0x0011CF28` | `scePadSetWarningLevel` | 104 | yes |  |
| `0x0011CF90` | `sceMcInit` | 432 | yes |  |
| `0x0011D148` | `sceMcEnd` | 64 | no |  |
| `0x0011D188` | `_lmcGetClientPtr` | 48 | no |  |
| `0x0011D1B8` | `sceMcChangeThreadPriority` | 180 | yes |  |
| `0x0011D270` | `sceMcGetSlotMax` | 192 | yes |  |
| `0x0011D330` | `sceMcOpen` | 308 | yes |  |
| `0x0011D468` | `sceMcMkdir` | 52 | yes |  |
| `0x0011D4A0` | `sceMcClose` | 180 | yes |  |
| `0x0011D558` | `sceMcSeek` | 212 | yes |  |
| `0x0011D630` | `mceIntrReadFixAlign` | 144 | no |  |
| `0x0011D6C0` | `sceMcRead` | 284 | yes |  |
| `0x0011D7E0` | `sceMcWrite` | 380 | yes |  |
| `0x0011D988` | `mcDelayThread` | 68 | yes |  |
| `0x0011D9D0` | `sceMcSync` | 220 | yes |  |
| `0x0011DAB0` | `mceGetInfoApdx` | 84 | no |  |
| `0x0011DB08` | `sceMcGetInfo` | 388 | yes |  |
| `0x0011DC90` | `sceMcUdCheckNewCard` | 108 | yes |  |
| `0x0011DD00` | `sceMcGetDir` | 344 | yes |  |
| `0x0011DE58` | `mceStorePwd` | 132 | no |  |
| `0x0011DEE0` | `sceMcChdir` | 336 | yes |  |
| `0x0011E030` | `sceMcFormat` | 200 | yes |  |
| `0x0011E0F8` | `sceMcDelete` | 296 | yes |  |
| `0x0011E220` | `sceMcFlush` | 180 | yes |  |
| `0x0011E2D8` | `sceMcSetFileInfo` | 472 | yes |  |
| `0x0011E4B0` | `sceMcRename` | 360 | yes |  |
| `0x0011E618` | `sceMcUnformat` | 200 | yes |  |
| `0x0011E6E0` | `sceMcGetEntSpace` | 280 | yes |  |
| `0x0011E828` | `_calloc_r` | 176 | yes |  |
| `0x0011E938` | `quorem` | 552 | no |  |
| `0x0011EB68` | `_dtoa_r` | 4632 | yes |  |
| `0x0011FDE0` | `fflush` | 268 | indirect |  |
| `0x0011FEF8` | `std` | 92 | no |  |
| `0x0011FF58` | `__sfmoreglue` | 120 | yes |  |
| `0x0011FFD0` | `__sfp` | 248 | yes |  |
| `0x00120108` | `__sinit` | 140 | no |  |
| `0x00120198` | `_free_r` | 744 | yes |  |
| `0x00120490` | `_malloc_trim_r` | 364 | yes |  |
| `0x00120660` | `__sfvwrite` | 972 | yes |  |
| `0x00120A40` | `_fwalk` | 148 | indirect |  |
| `0x00120AF0` | `_setlocale_r` | 136 | no |  |
| `0x00120C38` | `__smakebuf` | 332 | yes |  |
| `0x00120DF8` | `_memalign_r` | 448 | yes |  |
| `0x00120FC0` | `malloc` | 84 | yes |  |
| `0x00121018` | `free` | 72 | yes |  |
| `0x00121060` | `malloc_extend_top` | 584 | yes |  |
| `0x001212B8` | `_malloc_r` | 1788 | yes |  |
| `0x001219E0` | `_mbtowc_r` | 52 | no |  |
| `0x00121A18` | `memchr` | 224 | no |  |
| `0x00121AF8` | `memcmp` | 148 | no |  |
| `0x00121B90` | `memcpy` | 176 | no |  |
| `0x00121C40` | `memmove` | 260 | no |  |
| `0x00121D48` | `memset` | 192 | no |  |
| `0x00121E08` | `__malloc_lock` | 124 | yes |  |
| `0x00121E88` | `__malloc_unlock` | 76 | yes |  |
| `0x00121ED8` | `_Balloc` | 164 | yes |  |
| `0x00121F80` | `_Bfree` | 44 | no |  |
| `0x00121FB0` | `_multadd` | 264 | yes |  |
| `0x001220B8` | `_s2b` | 304 | yes |  |
| `0x001221E8` | `_hi0bits` | 128 | no |  |
| `0x00122268` | `_lo0bits` | 188 | no |  |
| `0x00122328` | `_i2b` | 56 | yes |  |
| `0x00122360` | `_multiply` | 524 | yes |  |
| `0x00122570` | `_pow5mult` | 248 | yes |  |
| `0x00122670` | `_lshift` | 344 | yes |  |
| `0x001227C8` | `__mcmp` | 104 | no |  |
| `0x00122830` | `__mdiff` | 400 | yes |  |
| `0x001229C0` | `_ulp` | 200 | no |  |
| `0x00122A90` | `_b2d` | 408 | no |  |
| `0x00122C28` | `_d2b` | 380 | yes |  |
| `0x00122DB0` | `_ratio` | 200 | no |  |
| `0x00122E80` | `_mprec_log10` | 112 | no |  |
| `0x00122EF0` | `_printf_r` | 92 | yes |  |
| `0x00122F50` | `printf` | 112 | yes |  |
| `0x00123078` | `_init_signal_r` | 112 | yes |  |
| `0x001230E8` | `_signal_r` | 128 | yes |  |
| `0x00123168` | `_raise_r` | 208 | yes |  |
| `0x00123240` | `__sigtramp_r` | 152 | yes |  |
| `0x001233D8` | `_sprintf_r` | 136 | yes |  |
| `0x001234F0` | `__sread` | 96 | no |  |
| `0x00123558` | `__swrite` | 128 | no |  |
| `0x001235D8` | `__sseek` | 96 | no |  |
| `0x00123660` | `strcat` | 300 | no |  |
| `0x00123790` | `strchr` | 400 | no |  |
| `0x00123920` | `strcmp` | 324 | no |  |
| `0x00123A68` | `strcpy` | 276 | no |  |
| `0x00123B80` | `strcspn` | 124 | no |  |
| `0x00123C00` | `strlen` | 312 | no |  |
| `0x00123D38` | `strncmp` | 436 | no |  |
| `0x00123EF0` | `strncpy` | 444 | no |  |
| `0x001240D0` | `__sprint` | 68 | yes |  |
| `0x00124118` | `__sbprintf` | 180 | yes |  |
| `0x00124248` | `_vfprintf_r` | 5652 | yes |  |
| `0x001258B8` | `cvt` | 444 | yes |  |
| `0x00125A78` | `exponent` | 212 | no |  |
| `0x00125BB8` | `__swsetup` | 264 | yes |  |
| `0x00125CC8` | `__muldi3` | 116 | no |  |
| `0x00125D40` | `__divdi3` | 1664 | no |  |
| `0x001263D8` | `__moddi3` | 1664 | no |  |
| `0x00126A68` | `__udivdi3` | 1388 | no |  |
| `0x00126FE8` | `__umoddi3` | 1368 | no |  |
| `0x00127550` | `__floatdidf` | 168 | no |  |
| `0x001275F8` | `__fixunsdfdi` | 280 | no |  |
| `0x00127710` | `__pack_f` | 176 | no |  |
| `0x001277C8` | `__unpack_f` | 88 | no |  |
| `0x00127828` | `fptosi` | 120 | no |  |
| `0x001278A8` | `__make_fp` | 44 | no |  |
| `0x001278D8` | `fptodp` | 60 | no |  |
| `0x00127918` | `fptoui` | 124 | no |  |
| `0x00127998` | `__pack_d` | 212 | no |  |
| `0x00127A70` | `__unpack_d` | 176 | no |  |
| `0x00127B30` | `_fpadd_parts` | 592 | no |  |
| `0x00127D90` | `dpadd` | 88 | no |  |
| `0x00127DE8` | `dpsub` | 100 | no |  |
| `0x00127E50` | `dpmul` | 632 | no |  |
| `0x001280D8` | `dpdiv` | 316 | no |  |
| `0x00128218` | `__fpcmp_parts_d` | 280 | no |  |
| `0x00128338` | `dpcmp` | 76 | no |  |
| `0x00128388` | `litodp` | 180 | no |  |
| `0x00128440` | `dptoli` | 156 | no |  |
| `0x001284E0` | `__make_dp` | 44 | no |  |
| `0x00128510` | `dptofp` | 84 | no |  |
| `0x00128568` | `dptoul` | 156 | no |  |
| `0x001826B0` | `__dt__Q23std9exceptionFv` | 76 | yes |  |
| `0x001A3538` | `GetRomName` | 156 | yes |  |
| `0x001A35D8` | `IsT10K` | 64 | yes |  |
| `0x001A3618` | `sceScfGetLanguage` | 96 | yes |  |
| `0x001A3698` | `sceScfGetAspect` | 64 | yes |  |
| `0x001A36D8` | `sceScfGetSpdif` | 60 | yes |  |
| `0x001A3718` | `sceScfGetTimeZone` | 76 | yes |  |
| `0x001A3768` | `sceScfGetDateNotation` | 100 | yes |  |
| `0x001A37D0` | `sceScfGetSummerTime` | 104 | yes |  |
| `0x001A3838` | `sceScfGetTimeNotation` | 104 | yes |  |
| `0x001A38A0` | `tobcd` | 44 | no |  |
| `0x001A38D0` | `frombcd` | 32 | no |  |
| `0x001A39C0` | `adddate` | 180 | no |  |
| `0x001A3A78` | `subdate` | 164 | no |  |
| `0x001A3B20` | `addhour` | 48 | no |  |
| `0x001A3B50` | `subhour` | 36 | no |  |
| `0x001A3B78` | `AdjustTime` | 136 | no |  |
| `0x001A3C10` | `sceScfGetLocalTimefromRTC` | 72 | yes |  |

