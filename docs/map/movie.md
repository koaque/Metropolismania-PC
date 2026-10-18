# movie

The PSS movie player (opening, mission and ending movies). Mostly Sony's ezmovie sample compiled into the game;
function names follow the sample where behaviour matches. Objects: read buffer at `0x00634C00` (0x50000 bytes),
stream file `0x00717C40`, video decoder `0x00717C80` (sceMpeg plus the video buffer at `+0x48`), audio decoder
`0x00717D40`, picture queue `0x00717DA0`. Audio goes to the IOP through `mm_Movie_AudioDec*`; the VSync handler
`mm_Movie_VblankHandler` sends a prebuilt display list per field.

<!-- Generated below by tools/map_docs.py from config/port_status.csv. Edit function notes there, not here. -->

## Functions

80 functions (typed 80). Blocks: 49 can reach a thread switch, 0 make indirect calls that were not followed; both wait for native threading (P6) before porting.

| Address | Function | Size | Blocks | Notes |
|---|---|---:|---|---|
| `0x0019B8D0` | `void mm_Movie_SetSpuMasterVolume(int volume)` | 100 | yes | sceSdRemote SetParam: master volume left/right (0x980/0xA80) of both cores to a0 |
| `0x0019B940` | `int mm_Movie_SendToIop2Area(int iop0, int iopSize0, int iop1, int iopSize1, int ee0, int eeSize0, int ee1, int eeSize1)` | 840 | no | (sendToIOP2area) copies up to two EE ring areas into up to two IOP ring areas with sceSifSetDma and waits for each |
| `0x0019BC90` | `int mm_Movie_AudioDecSendToIop(int32_t *audio, int a1, int a2, int a3)` | 476 | yes | (audioDecSendToIOP) in preset or play state, sends as much PCM as fits behind the SPU2 AutoDMA play position (sceSdRemote 0x8100) in 1 KB units |
| `0x0019BE70` | `int mm_Movie_AudioDecIsPreset(int audio)` | 20 | no | (audioDecIsPreset) true once the IOP buffer has been filled the first time |
| `0x0019BE90` | `void mm_Movie_AudioDecEndPut(int32_t *audio, uint32_t size)` | 292 | yes | (audioDecEndPut) commits a1 bytes; in header state collects the 0x28-byte SShd/SSbd header first and prints it |
| `0x0019BFC0` | `void mm_Movie_AudioDecBeginPut(int32_t *audio, int32_t *ptr0, int32_t *len0, int32_t *ptr1, int32_t *len1)` | 188 | no | (audioDecBeginPut) free space as up to two areas: the header area while parsing it, else the EE ring (0xC000 bytes) |
| `0x0019C080` | `void mm_Movie_AudioDecReset(uint32_t *audio)` | 192 | yes | (audioDecReset) stops AutoDMA, records the play position, plays the zero buffer, clears all counters |
| `0x0019C140` | `void mm_Movie_AudioDecStart(uint32_t *audio)` | 136 | yes | (audioDecStart) full volume and starts AutoDMA looping over the IOP buffer, state 2 (play) |
| `0x0019C1D0` | `void mm_Movie_AudioDecResume(uint32_t *audio)` | 136 | yes | same as mm_Movie_AudioDecStart, used after a pause |
| `0x0019C260` | `int mm_Movie_AudioDecDelete(int audio)` | 52 | yes | (audioDecDelete) frees the IOP buffers |
| `0x0019C2A0` | `int mm_Movie_AudioDecCreate(uint32_t *audio, int ring, int ringSize, int iopSize)` | 376 | yes | (audioDecCreate) EE ring a1/a2, allocates the IOP buffer (a3 bytes) and a 0x800-byte zero buffer on the IOP |
| `0x0019C420` | `void mm_Movie_DisplayStop(void)` | 12 | no | clears the display-running flags (0x1BDD68, 0x1BDD9C) |
| `0x0019C430` | `int mm_Movie_DisplayStart(int field)` | 76 | yes | waits for the field a0 to pass, then sets the display-running flag |
| `0x0019C480` | `void mm_Movie_DmaEndHandler(void)` | 56 | no | (handler_endimage) DMA interrupt handler: releases the shown picture (mm_Movie_VoBufDecCount) |
| `0x0019C4C0` | `void mm_Movie_VblankHandler(void)` | 1328 | yes | (vblankHandler) VSync interrupt handler: picks the buffer by the GS CSR field bit, swaps, sends the next decoded picture and subtitles |
| `0x0019C9F0` | `void mm_Movie_LayoutSubtitle(char *text)` | 376 | no | lays out a subtitle string into glyph indices at 0x215F00 using the font |
| `0x0019CB70` | `void mm_Movie_BuildSubtitlePacket(void)` | 1592 | no | builds the subtitle GS packet at 0x20C1A0 from the laid-out glyphs (font glyphs, positions) |
| `0x0019D1B0` | `int16_t mm_Movie_SendFrame(int picture)` | 596 | yes | sends the current picture and the subtitle packet to the GS (path 2 DMA, A+D setup) |
| `0x0019D410` | `void mm_Movie_BuildImagePacket(uint32_t packet, uint32_t image, int half, int width, int height)` | 1240 | no | turns a decoded RGB32 picture into a textured-sprite GS packet (strips, A+D registers) |
| `0x0019D8F0` | `void mm_Movie_ClearScreen(void)` | 684 | yes | clears both display buffers to black, swapping on vsync, reading the pad meanwhile |
| `0x0019DBA0` | `void mm_Movie_PlayPss(char *name, int16_t a1, int16_t a2, int width, int16_t height, int fadeAudio, uint16_t skipButtons, int16_t fadeStep, int stack0, int stack8)` | 624 | yes | (playPss) opens the PSS (cdrom0:), initialises, then pumps the demux and audio until the movie ends or a button skips it ("QuickView:%s") |
| `0x0019DE10` | `int mm_PlayMissionMovie(char mission)` | 400 | yes | plays DATA\MPEG\MISSION<n>.PSS with its .BIN subtitles (n = a0), or ENDING.PSS for 6, once the sound driver is idle |
| `0x0019DFA0` | `int mm_PlayOpeningMovie(void)` | 116 | yes | plays DATA\MPEG\OPENING.PSS at 640x448 once the sound driver is idle |
| `0x0019E020` | `void mm_Movie_SetSubtitleState(uint16_t state)` | 8 | no | 0x1BDD80 = a0 |
| `0x0019E030` | `void mm_Movie_Error(int message)` | 16 | yes | (ErrMessage) prints "[ Error ] %s" |
| `0x0019E060` | `void mm_Movie_SwitchThread(void)` | 8 | yes | (switchThread) RotateThreadReadyQueue syscall (0x2B) |
| `0x0019E070` | `void mm_Movie_TermAll(void)` | 348 | yes | (termAll) waits for the decoder to drain, stops audio, removes the handlers, deletes the threads and closes the stream |
| `0x0019E1D0` | `int mm_Movie_Update(void)` | 172 | yes | one step of playback: pumps the demux until the decoder reports the end, then terminates |
| `0x0019E280` | `int mm_Movie_ReadAndDemux(void)` | 380 | yes | reads disc data into the read buffer (sceCdStRead), demuxes it (sceMpegDemuxPssRing), sends audio to the IOP and starts display and audio once both are preloaded |
| `0x0019E400` | `void mm_Movie_ResumeAll(void)` | 80 | yes | restarts display and audio after a pause |
| `0x0019E450` | `void mm_Movie_InitAll(int16_t a0, int16_t a1, int width, int16_t height)` | 816 | yes | (initAll) display buffers, sceMpegInit, video decoder, audio decoder (when 0x1BD9E8 is set), stream callbacks, read and decode threads, VSync and DMA handlers |
| `0x0019E780` | `int mm_Movie_PcmCallback(int mpeg, int cbdata, int anyData)` | 432 | yes | (pcmCallback) stream callback for audio: skips the 4-byte sub-stream header, copies the packet into the audio decoder (ring-aware); returns 0 when there is no room |
| `0x0019E930` | `int mm_Movie_VideoCallback(int mpeg, int cbdata, int anyData)` | 504 | yes | (videoCallback) stream callback for video: copies the packet into the IPU input buffer, records its time stamp ("pts buffer overflow"); 0 when there is no room |
| `0x0019EB30` | `int mm_Movie_ReadBufEndGet(int readBuf, int size)` | 60 | no | (readBufEndGet) releases a1 bytes |
| `0x0019EB70` | `int mm_Movie_ReadBufBeginGet(int readBuf, int32_t *ptr)` | 96 | no | (readBufBeginGet) start of the readable data and its size |
| `0x0019EBD0` | `int mm_Movie_ReadBufEndPut(int readBuf, int size)` | 112 | no | (readBufEndPut) commits a1 written bytes |
| `0x0019EC40` | `int mm_Movie_ReadBufBeginPut(int readBuf, int32_t *ptr)` | 60 | no | (readBufBeginPut) write position unless the ring is full |
| `0x0019EC80` | `void mm_Movie_ReadBufDelete(void)` | 8 | no | (readBufDelete) empty |
| `0x0019EC90` | `void mm_Movie_ReadBufCreate(int readBuf)` | 44 | no | (readBufCreate) empty ring of 0x50000 bytes |
| `0x0019ECC0` | `int mm_Movie_StrFileRead(int32_t *file, int buffer, int size)` | 80 | yes | (strFileRead) sceCdStRead of a1/2048 sectors (or sceRead for host files); flags a read error |
| `0x0019ED10` | `int mm_Movie_StrFileClose(int32_t *file)` | 76 | yes | (strFileClose) sceCdStStop and frees the stream buffer (or sceClose) |
| `0x0019ED60` | `int mm_Movie_StrFileOpen(int32_t *file, int name)` | 756 | yes | (strFileOpen, was mm_OpenDiscFile) cdrom0: files via sceCdSearchFile and CD streaming, host0: files via sceOpen |
| `0x0019F060` | `int mm_Movie_ViBufGetTs(int32_t *vibuf, uint64_t *ts)` | 436 | yes | (viBufGetTs) time stamp of the data the IPU is reading now |
| `0x0019F220` | `int mm_Movie_ViBufPutTs(int vibuf, int ts)` | 300 | yes | (viBufPutTs) records a time stamp for the data just written |
| `0x0019F350` | `void mm_Movie_ViBufModifyPts(int vibuf, int ts)` | 324 | no | drops time stamps whose data the IPU has consumed |
| `0x0019F4A0` | `void mm_Movie_ViBufFlush(int vibuf)` | 80 | yes | (viBufFlush) rounds the write offset up to 2 KB |
| `0x0019F4F0` | `int mm_Movie_ViBufCount(int vibuf)` | 68 | yes | (viBufCount) bytes waiting for the IPU |
| `0x0019F540` | `int mm_Movie_ViBufDelete(int vibuf)` | 144 | no | (viBufDelete) stops IPU DMA, deletes the semaphore |
| `0x0019F5D0` | `int mm_Movie_ViBufRestartDma(uint32_t *vibuf)` | 944 | yes | (viBufRestartDMA) restarts the toIPU DMA where it stopped |
| `0x0019F980` | `int mm_Movie_ViBufStopDma(int vibuf)` | 352 | yes | (viBufStopDMA) stops the toIPU DMA and remembers its position |
| `0x0019FAE0` | `int mm_Movie_ViBufAddDma(int32_t *vibuf)` | 756 | yes | (viBufAddDMA) queues more ring data on the toIPU DMA chain ("DMA ADD not active") |
| `0x0019FDE0` | `void mm_Movie_ViBufEndPut(int vibuf, int size)` | 92 | yes | (viBufEndPut) commits a1 bytes |
| `0x0019FE40` | `void mm_Movie_ViBufBeginPut(int32_t *vibuf, int32_t *ptr0, int32_t *len0, int32_t *ptr1, int32_t *len1)` | 244 | yes | (viBufBeginPut) free space as up to two areas |
| `0x0019FF40` | `int mm_Movie_ViBufReset(int32_t *vibuf)` | 404 | no | (viBufReset) empties the ring and the time stamp table, builds the DMA tags |
| `0x001A00E0` | `int mm_Movie_ViBufCreate(int vibuf, int data, int tags, int size, int ts, int tsCount)` | 116 | no | (viBufCreate) ring a1 (uncached), DMA tag area, time stamp table, semaphore |
| `0x001A0160` | `int mm_Movie_CbTimeStamp(int mpeg, int cbdata)` | 68 | yes | (mpegTS) libmpeg time stamp callback: mm_Movie_ViBufGetTs |
| `0x001A01B0` | `int mm_Movie_CbRestartDma(void)` | 36 | yes | (mpegRestartDMA) libmpeg callback |
| `0x001A01E0` | `int mm_Movie_CbStopDma(void)` | 36 | yes | (mpegStopDMA) libmpeg callback |
| `0x001A0210` | `int mm_Movie_CbNoData(void)` | 44 | yes | (mpegNodata) libmpeg callback: switch threads, then queue more input |
| `0x001A0240` | `int mm_Movie_CbError(int mpeg, int cbdata)` | 40 | yes | (mpegError) libmpeg callback: prints the message |
| `0x001A0270` | `int mm_Movie_DecodeLoop(int videoDec)` | 692 | yes | (decBs0) decode thread loop: sceMpegGetPicture into the picture queue, builds image and subtitle packets, ends on sceMpegIsEnd ("decode thread: aborted") |
| `0x001A0530` | `void mm_Movie_VideoDecMain(int videoDec)` | 96 | yes | (videoDecMain) decode thread entry: resets the buffers, runs the loop, waits for the queue to drain, state 3 (done) |
| `0x001A0590` | `int mm_Movie_VideoDecIsDone(int videoDec)` | 84 | yes | input buffer empty and libmpeg has no more pictures |
| `0x001A05F0` | `int mm_Movie_VideoDecPutTerminator(int videoDec)` | 400 | yes | appends a sequence end code (0x1BD9F0) to the input once there is room; 0 until then |
| `0x001A0780` | `int mm_Movie_VideoDecPutTs(int videoDec, int pts, int dts, int start, int length)` | 60 | yes | (videoDecPutTs) time stamp for input offset a3 |
| `0x001A07C0` | `int mm_Movie_VideoDecGetState(int videoDec)` | 8 | no | (videoDecGetState) +0xA8 |
| `0x001A07D0` | `void mm_Movie_VideoDecSetStateStart(int videoDec)` | 12 | no | +0xA8 = 1 |
| `0x001A07E0` | `int mm_Movie_VideoDecDelete(int videoDec)` | 52 | no | (videoDecDelete) deletes the input buffer and the sceMpeg (0x10BF60) |
| `0x001A0820` | `void mm_Movie_VideoDecEndPut(int videoDec, int size)` | 8 | yes | (videoDecEndPut) commits a1 bytes of input |
| `0x001A0830` | `void mm_Movie_VideoDecBeginPut(int videoDec, int32_t *ptr0, int32_t *len0, int32_t *ptr1, int32_t *len1)` | 8 | yes | (videoDecBeginPut) input free space as up to two areas |
| `0x001A0840` | `int mm_Movie_VideoDecAddStrCallback(void)` | 32 | no | wrapper around sceMpegAddStrCallback; returns 1 |
| `0x001A0860` | `int mm_Movie_VideoDecCreate(int videoDec, int mpegWork, int mpegWorkSize, int data, int tags, int tagSize, int ts, int tsCount)` | 252 | yes | (videoDecCreate) sceMpegCreate, the five libmpeg callbacks, state 0, input buffer |
| `0x001A0960` | `void mm_Movie_VoBufDecCount(int vobuf)` | 32 | no | (voBufDecCount) one shown picture released |
| `0x001A0980` | `int mm_Movie_VoBufGetTag(int vobuf)` | 116 | no | (voBufGetTag) oldest decoded picture (0x39240-byte slots), 0 if none |
| `0x001A0A00` | `int mm_Movie_VoBufGetData(int32_t *vobuf)` | 72 | no | (voBufGetData) next free picture slot (0x12C000 bytes each), 0 if full |
| `0x001A0A50` | `void mm_Movie_VoBufIncCount(int vobuf)` | 140 | no | (voBufIncCount) marks the slot decoded and advances |
| `0x001A0AE0` | `int mm_Movie_VoBufIsFull(int vobuf)` | 20 | no | (voBufIsFull) count == size |
| `0x001A0B00` | `void mm_Movie_VoBufReset(int vobuf)` | 12 | no | (voBufReset) empties the queue |
| `0x001A0B10` | `void mm_Movie_VoBufDelete(void)` | 8 | no | (voBufDelete) empty |
| `0x001A0B20` | `void mm_Movie_VoBufCreate(uint32_t *vobuf, int data, int tags, int size)` | 280 | no | (voBufCreate) picture data a1, tag area a2, a3 slots |
