# Metropolismania PC

**Metropolismania PC** is a work-in-progress, open-source native PC port of **Metropolismania** (街ingメーカー, *Machi-ing Maker*), the PlayStation 2 city builder by Indi (2001). The game's code is being rebuilt as readable C++ source, one function at a time, and the rewritten functions are checked against the original by differential tests.

[日本語版はこの下にあります](#日本語) (Japanese version below; a summary, FAQ and keywords in both languages are at the very end)

> [!IMPORTANT]
> This repository contains **no game files**: no disc image, executable, IOP modules, movies, textures or BIOS, and none of the code generated from them. You need **your own copy** of Metropolismania for the PS2 (USA, SLUS-20377).

> [!WARNING]
> Early, experimental work. The game boots and reaches the city, but it is not ready to play through. See [docs/STATUS.md](docs/STATUS.md).

## The game

Metropolismania (街ingメーカー, *Machi-ing Maker*, in Japan) was made by Indi and published by Media Factory in Japan (2001) and by Natsume in North America and Europe (2002). It is a city builder played on foot. You are the town planner: you walk the streets, put up homes, shops and offices, and then have to talk people into actually living and working in them. A town only grows if its residents are happy.

Taking the game apart shows how much simulation sits under that small premise:

- **Families, not population numbers.** Every resident belongs to a family from `FAMILY.BIN`, with a home building, members, jobs and a daily schedule. Each family has a friendship meter (0 to 120); close friends get listed in your Town's Pages, where you can phone them, and they can introduce other families or agree to favours.
- **Buildings with opinions.** Businesses raise claims when their needs go unmet, and a claim meter keeps rising until the problem is fixed.
- **A calendar and a mood.** Each in-game day is a month, with weather, holiday months and a town approval rating from -99 to 120. At 118 and up the town counts as popular and families start calling to ask about moving in.
- **96 kinds of events.** Paydays and farewell parties, but also a thief, a forgetful granny, missing children, a crazy cult, a caveman and an alien, each with its own handlers.
- **Talking is the core.** Visiting a building or stopping someone in the street opens a talk view, and conversations run on the game's own little script language stored in `WORDS*.BIN`.
- **Mission goals per town.** Each stage sets a population target and a theme share (high-class, business, leisure, fishing, farming or industrial).

The full picture, subsystem by subsystem, is in [docs/map](docs/map/README.md). Disc layout and data formats are in [docs/GAME_NOTES.md](docs/GAME_NOTES.md).

## The goal

It already runs well in PCSX2, so this project is not about making the game playable. It is about understanding it completely and giving it a native, readable codebase that can be fixed, modded and extended: widescreen, higher resolutions and other platforms without an emulator.

The approach is the one OpenRCT2 used for RollerCoaster Tycoon 2: keep the game running at every step and replace its code piece by piece.

1. **Map everything.** Every function in the game is named, classified by subsystem and given a full C prototype (`config/signatures.txt`), with its structs in `config/types/`. This step is done for all 1,481 game functions.
2. **Rewrite by hand.** [src/port](src/port/README.md) holds the game's code as readable C++, organised by subsystem (city, families, events, talks, the WORDS script, saves, sound, UI). Each rewritten function takes over from the original at its address.
3. **Prove it matches.** Every rewritten function is checked against the original by differential tests: calls recorded from a running game are replayed through both versions, and memory, registers and results must come out identical before the function counts as done.
4. **Understand it.** With every function rewritten, the code is gone over again to give it the names a real source tree would have: struct fields instead of raw memory offsets (about 2,700 such lines when this pass began, about 150 now, most of them in the movie player, which is left as it is on purpose), real names for placeholder functions and structs, and a map of how the city is drawn, down to each VU1 microcode program ([docs/RENDER_PIPELINE.md](docs/RENDER_PIPELINE.md)). This is the current phase. A native renderer that replaces the emulated graphics hardware is meant to build on it.

Progress per subsystem is tracked in [docs/PORT_STATUS.md](docs/PORT_STATUS.md). As far as we could find, this is the first decompilation or recompilation project for the game ([research notes](docs/RESEARCH.md)).

## Status

| Area | State |
|---|---|
| Boot, logos, title screen, memory card check | Works |
| Input | Works (keyboard, pad, scripted) |
| The city | Reached: HUD, cursor and 3D terrain render, about 43 fps on average in the heavy views of the explore autoplay (lowest about 34), 60 (the display rate) on the title and in menus (CPU rendering, compiled VU1 programs, see docs/VU1_PROGRAMS.md). Frame skip keeps the game close to its speed there: 50 to 58 game frames a second, with the frames that cannot be drawn in time left undrawn (`PS2X_FRAMESKIP=0` turns it off) |
| Movies, music, sound effects | Play, through a new SPU2 emulation (with reverb) |
| Saves | Memory card check passes; loading a save from a PCSX2 memory card works |
| Mapping (names, types, prototypes) | Done for every game function |
| Readable source port | In progress: all 1,481 of the game's functions rewritten, 1,432 of them verified against the original (97%) ([details](docs/PORT_STATUS.md)) |
| Reverse-engineering pass | In progress: struct fields by name (raw memory offsets down from about 2,700 lines to about 150), placeholder names replaced, the city's render pipeline mapped down to each of its 13 VU1 programs ([details](docs/RENDER_PIPELINE.md)) |
| Platform layer (SDK and C runtime) | In progress: 123 of the 768 SDK functions rewritten as plain C++ in `src/port/platform/`; 91 verified by replay, 25 numeric ones (soft float, libm) by random-input comparison, 7 not yet shown. No recompiled SDK function is reachable from the ported code, and the explore and new-game runs enter none ([details](docs/PLATFORM_FINDINGS.md)) |
| VU1 microcode | The city's programs run natively, checked against the interpreter ([details](docs/VU1_PROGRAMS.md)) |
| Sound driver (IOP) | The game's own driver (YYSOUND, 114 functions) is rewritten as readable C++ in `src/port/iop/` and checked call by call against the original (0 differences, bar a few uninitialized stack bytes that the details list). All ten IOP modules, YYSOUND and the nine Sony modules (memory card, pad, SPU2 library, MIDI and sound-effect synthesizers), run as C++ translated ahead of time from your disc's modules, which matches the interpreter exactly (the same state at every checked point, the same audio, random programs of every instruction form; no instruction of the ten modules is interpreted in the scripted runs). The Sony modules are not rewritten as readable source ([details](docs/IOP.md)) |
| Widescreen, high refresh rate, hi-res, native controls, Linux | Proposed, not started ([see below](#proposed-patches)) |

## How it works

The starting point is a static recompilation made with [PS2Recomp](https://github.com/ran-j/PS2Recomp): the game's MIPS R5900 code is translated to C++ from your disc and linked against PS2Recomp's runtime, which provides memory, the kernel, graphics, VU1, DMA and an IOP that runs the game's original IOP modules (sound, pad, memory card). That machine-translated code is the scaffolding that keeps the game running. The readable port in `src/port/` replaces it function by function; once a function is rewritten, its generated version is kept only as the reference the differential tests compare against.

- `tools/extract_disc.py` reads the files straight from your BIN/CUE or ISO.
- Ghidra (with the Emotion Engine extension) finds the functions in the stripped executable and applies the names, types and prototypes from `config/`.
- Game-specific fixes live in `config/` and `src/overrides/`; changes to PS2Recomp itself live in `patches/`.

## Documentation

- [docs/map](docs/map/README.md): every subsystem and every function, with notes.
- [docs/GAME_NOTES.md](docs/GAME_NOTES.md): the disc, the data files and their formats.
- [docs/RENDER_PIPELINE.md](docs/RENDER_PIPELINE.md): how the city is drawn, from the game's draw calls down to each VU1 microcode program.
- [docs/VU1_PROGRAMS.md](docs/VU1_PROGRAMS.md): the VU1 programs run natively and compiled to C++.
- [docs/IOP.md](docs/IOP.md): the sound driver and the IOP modules.
- [docs/PLATFORM_FINDINGS.md](docs/PLATFORM_FINDINGS.md): what rewriting the SDK layer found out about the runtime and the recompiler.
- [docs/PORT_STATUS.md](docs/PORT_STATUS.md): progress per subsystem.
- [docs/RESEARCH.md](docs/RESEARCH.md): background research and prior work on the game.
- [src/port/README.md](src/port/README.md): how the readable port and its tests work.

## Requirements (Windows, for now)

- Your own Metropolismania disc dump (USA, SLUS-20377, disc VER 1.02), as BIN/CUE or ISO.
- Visual Studio 2022 with the C++ x64 workload (MSVC, CMake and Ninja come with it).
- CMake 3.21 or newer, Python 3.11 or newer, Git.
- Ghidra 12.1.4 with JDK 21, plus the [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) extension (v2.1.38, Ghidra 12.1.4 build).

## Building

```sh
git clone --recurse-submodules https://github.com/koaque/Metropolismania-PC.git
cd Metropolismania-PC
```

Tell the pipeline where Ghidra lives by creating `work/local.json`:

```json
{ "ghidra_install_dir": "C:/path/to/ghidra_12.1.4_PUBLIC" }
```

Then run the steps, or `all` in one go:

```sh
python tools/pipeline.py bootstrap          # pinned PS2Recomp + patches/ -> work/PS2Recomp
python tools/pipeline.py tools              # build ps2_analyzer and ps2_recomp
python tools/pipeline.py extract path/to/your/game.cue   # -> original/ (checks the build is SLUS-20377 v1.02)
python tools/pipeline.py symbols            # SDK names from PS2Recomp's signature scan -> config/symbols.csv
python tools/pipeline.py analyze            # Ghidra headless -> config/funcmap.csv
python tools/pipeline.py pointers           # functions reached only through pointers, re-analyzed until stable
python tools/pipeline.py recompile          # -> generated/
python tools/pipeline.py iop-aot            # the IOP sound and memory card modules -> C++ in work/iop_aot/
python tools/pipeline.py stage              # generated code + overrides + src/port + work/iop_aot -> runtime tree
python tools/pipeline.py build              # ps2EntryRunner
python tools/pipeline.py run                # timed run, log in logs/
```

To copy your saves over from PCSX2: `python tools/mc_extract.py path/to/Mcd001.ps2`. The port's own tests (`capture`, `difftest`) are described in [src/port/README.md](src/port/README.md).

The city's VU1 programs run faster as compiled C++, which is made from your disc's microcode and so is not in the repository. After a first build, with a save copied over, `python tools/pipeline.py vu1-capture` records the programs a city run uses, `vu1-emit` writes them as C++ into `work/vu1_aot/gen/`, and `stage` and `build` build them in ([docs/VU1_PROGRAMS.md](docs/VU1_PROGRAMS.md)). Without them the same programs run in the slower executor.

## Proposed patches

Once the port is complete these are the changes it is meant to make possible. None of them exists yet.

- **Widescreen.** True 16:9 and wider, with the field of view and the HUD corrected. The needed addresses are already known from the PCSX2 widescreen patch ([docs/GAME_NOTES.md](docs/GAME_NOTES.md)).
- **High refresh rates (120 Hz).** The game logic steps once per frame at the PS2's field rate (59.94 Hz), so it must stay at that rate to keep its speed; the proposal is to draw in-between frames for 120 Hz and faster displays without changing the logic.
- **Higher resolution.** Render the city at a multiple of the PS2's 640x448 instead of the fixed output size.
- **Native PC controls.** Keyboard and mouse, any gamepad, and rebinding, instead of a mapped PS2 pad.
- **And more:** a Linux build, saves in a normal folder instead of a memory card image, display options (fullscreen, borderless, vsync) and fixes for the remaining rendering glitches.

## Support the project

If this project is useful to you, you can support its development:

**[Donate with PayPal](https://www.paypal.com/ncp/payment/LV3M4HY3PJHU4)**

**Bitcoin**

```
3CUTagummxA2SMFrS2vxGKyLj4gtQ9mrbW
```

**Ethereum and any ERC-20 token**

```
0xabcdec4cf4c515e266876ae2e4323ff30bda36e9
```

## Credits

- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors, the recompiler and runtime this project starts from.
- Open PS2Recomp pull requests carried in `patches/` until they are merged: VU0/VU1, VIF and GIF fixes by @VariableDeclared, @yanm1103, @Fiztba, @luisbraganca and @drakolordx7 (see each patch header).
- [psx-spx](https://psx-spx.consoledev.net/) (SPU voice pipeline, envelope and interpolation details) for the SPU2 emulation.
- [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) by chaoticgd.
- [PCSX2](https://github.com/PCSX2/pcsx2), the reference for how the hardware behaves, and Arapapa's widescreen patch for the known FOV addresses.
- [rootlm/metropolismaniaresearch](https://github.com/rootlm/metropolismaniaresearch) for early notes on the texture format.
- [OpenRCT2](https://github.com/OpenRCT2/OpenRCT2), whose function-by-function approach this port follows.

## License and disclaimer

The code in this repository is licensed under the [GNU GPL v3](LICENSE), the same license as PS2Recomp, whose runtime it links against.

This is a fan preservation project. It is not affiliated with or endorsed by Indi, Media Factory, Natsume, D3 Publisher or Sony Interactive Entertainment. "PlayStation" and "PS2" are trademarks of Sony Interactive Entertainment. Metropolismania is the property of its respective owners. Do not ask for or share game files.

---

## 日本語

**Metropolismania PC** は、Indiが開発したPlayStation 2の街づくりゲーム **街ingメーカー**(英語版タイトル *Metropolismania*、2001年)を、PC上でネイティブに動かすためのオープンソースの移植プロジェクトです(開発中)。ゲームのコードを読みやすいC++のソースコードとして一関数ずつ作り直しており、書き直した関数は差分テストで元のコードと照合しています。

> [!IMPORTANT]
> このリポジトリには**ゲームのファイルは一切含まれていません**。ディスクイメージ、実行ファイル、IOPモジュール、ムービー、テクスチャ、BIOS、およびそれらから生成されたコードも含みません。PS2版のMetropolismania(北米版、SLUS-20377)を**ご自身でお持ちである必要があります**。

> [!WARNING]
> 初期段階の実験的な作業です。ゲームは起動して街まで到達しますが、最後まで遊べる状態ではありません。詳しくは [docs/STATUS.md](docs/STATUS.md) を参照してください。

### このゲームについて

街ingメーカー(英語名 Metropolismania)は、Indiが開発し、日本ではメディアファクトリーが2001年に、北米とヨーロッパではNatsumeが2002年に発売した、自分の足で歩いて遊ぶ街づくりゲームです。プレイヤーは街の都市計画者として通りを歩き、住宅、店、オフィスを建て、そこに実際に住んで働いてくれるよう人々を説得します。住民が幸せでなければ街は育ちません。

ゲームを解析すると、この小さな前提の下に多くのシミュレーションがあることが分かります。

- **人口の数ではなく家族。** 住民は `FAMILY.BIN` の家族に属し、家、メンバー、仕事、一日のスケジュールを持ちます。家族ごとに友好度(0〜120)があり、親しい家族はタウンページに載って電話をかけられ、他の家族を紹介したり頼みを聞いてくれたりします。
- **意見を持つ建物。** 店や会社は要望が満たされないとクレームを出し、解決するまでクレームのメーターは上がり続けます。
- **カレンダーと気分。** ゲーム内の一日が一か月で、天気、祝日の月、-99から120までの街の支持率があります。118以上で街は人気者となり、引っ越しの問い合わせの電話が来ます。
- **96種類のイベント。** 給料日や送別会のほか、泥棒、物忘れのおばあさん、迷子、怪しい教団、原始人、宇宙人など、それぞれ専用の処理があります。
- **会話がゲームの中心。** 建物を訪ねたり路上で人を呼び止めたりすると会話画面が開き、会話は `WORDS*.BIN` に格納されたゲーム独自の小さなスクリプト言語で動いています。
- **街ごとのミッション目標。** ステージごとに人口の目標と、テーマの比率(高級、ビジネス、レジャー、漁業、農業、工業)が決まっています。

全体像はサブシステムごとに [docs/map](docs/map/README.md) にまとめてあります。ディスク構成とデータ形式は [docs/GAME_NOTES.md](docs/GAME_NOTES.md) にあります。

### 目標

このゲームはPCSX2で既に問題なく動くので、このプロジェクトの目的はゲームを遊べるようにすることではありません。ゲームを完全に理解し、修正、改造、拡張できる、ネイティブで読みやすいコードベースを作ることです。ワイドスクリーン、高解像度、エミュレータなしでの他のプラットフォーム対応などを目指します。

方法はOpenRCT2がRollerCoaster Tycoon 2で使ったものと同じです。常にゲームを動かしたまま、コードを少しずつ置き換えていきます。

1. **すべてを把握する。** ゲームの全関数に名前を付け、サブシステムに分類し、完全なCのプロトタイプ(`config/signatures.txt`)を付けています。構造体は `config/types/` にあります。ゲームの1,481関数すべてについて完了しています。
2. **手で書き直す。** [src/port](src/port/README.md) にゲームのコードを、サブシステム(街、家族、イベント、会話、WORDSスクリプト、セーブ、サウンド、UI)ごとに読みやすいC++として置いています。書き直した関数は、元の関数のアドレスで元の関数に代わって動きます。
3. **一致を証明する。** 書き直した関数はすべて、差分テストで元のものと照合されます。実行中のゲームから記録した呼び出しを両方のバージョンで再生し、メモリ、レジスタ、結果がすべて一致して初めて完了となります。
4. **理解する。** 全関数を書き直したうえで、コードをもう一度見直し、本物のソースコードにあるような名前を付けています。生のメモリオフセットの代わりに構造体のフィールド名(この作業を始めた時点で約2,700行、現在は約150行。その大半は、意図的にそのまま残しているムービー再生部分です)、仮の名前だった関数や構造体の本当の名前、そして街がどのように描画されるかをVU1マイクロコードの各プログラムまで追った解析([docs/RENDER_PIPELINE.md](docs/RENDER_PIPELINE.md))です。これが現在の段階です。エミュレートしているグラフィックハードウェアを置き換えるネイティブのレンダラーは、この上に作る予定です。

サブシステムごとの進捗は [docs/PORT_STATUS.md](docs/PORT_STATUS.md) にあります。調べた限り、このゲームの逆コンパイルまたは再コンパイルのプロジェクトはこれが最初です([調査メモ](docs/RESEARCH.md))。

### 現状

| 項目 | 状態 |
|---|---|
| 起動、ロゴ、タイトル画面、メモリーカード確認 | 動作 |
| 入力 | 動作(キーボード、パッド、スクリプト) |
| 街 | 到達済み。HUD、カーソル、3D地形を描画。探索の自動操作の重い場面で平均約43 fps(最低約34 fps)、タイトルとメニューでは表示レートの60 fps。フレームスキップにより、重い場面でもゲームは毎秒50〜58フレーム進み、本来の速度に近づきます(間に合わないフレームは描画を省きます。`PS2X_FRAMESKIP=0`で無効) |
| ムービー、音楽、効果音 | 再生可能(新しく作ったSPU2エミュレーションによる、リバーブ付き) |
| セーブ | メモリーカードの確認は通過。PCSX2のメモリーカードからのロードは動作 |
| マッピング(名前、型、プロトタイプ) | ゲームの全関数で完了 |
| 読みやすいソースへの移植 | 進行中。1,481関数すべてを書き直し済み、うち1,432関数を元のコードとの照合で検証済み(97%)([詳細](docs/PORT_STATUS.md)) |
| リバースエンジニアリングの見直し | 進行中。生のメモリオフセットを構造体のフィールド名に置き換え(約2,700行から約150行に)、仮の名前を本当の名前に変更、街の描画パイプラインを13個のVU1プログラムそれぞれまで解析済み([詳細](docs/RENDER_PIPELINE.md)) |
| プラットフォーム層(SDKとCランタイム) | 進行中。768個のSDK関数のうち123個を `src/port/platform/` に素のC++で書き直し済み。91個はリプレイで、25個の数値関数(ソフトフロート、libm)はランダム入力との比較で検証済み、7個は未検証。移植済みコードから到達できる再コンパイル済みSDK関数はなく、探索と新規ゲームの実行でも再コンパイル済み関数は実行されません([詳細](docs/PLATFORM_FINDINGS.md)) |
| VU1マイクロコード | 街のプログラムはネイティブで動作し、インタプリタとの照合済み([詳細](docs/VU1_PROGRAMS.md)) |
| サウンドドライバ(IOP) | ゲーム自身のドライバ(YYSOUND、114関数)を `src/port/iop/` に読みやすいC++として書き直し済みで、元のコードと呼び出しごとに照合しています(差異は0件。初期化されていないスタックの数バイトを除く。詳細に記載)。YYSOUNDとSony製の9モジュール(メモリーカード、パッド、SPU2ライブラリ、MIDIと効果音のシンセサイザ)を含む10個のIOPモジュールすべてを、ディスクのモジュールから事前に変換したC++で実行しており、インタプリタと完全に一致します(照合した各時点で同じ状態、同じ音声、あらゆる命令形式のランダムなプログラム。スクリプトによる実行では10モジュールの命令をインタプリタで実行することはありません)。Sony製モジュールは読みやすいソースには書き直していません([詳細](docs/IOP.md))|
| ワイドスクリーン、高リフレッシュレート、高解像度、ネイティブ操作、Linux | 構想中、未着手 |

### 仕組み

出発点は [PS2Recomp](https://github.com/ran-j/PS2Recomp) による静的再コンパイルです。ゲームのMIPS R5900コードをお持ちのディスクからC++に変換し、メモリ、カーネル、グラフィックス、VU1、DMA、そしてゲーム本来のIOPモジュール(サウンド、パッド、メモリーカード)を動かすIOPを提供するPS2Recompのランタイムとリンクします。この機械的に変換されたコードは、ゲームを動かし続けるための足場です。`src/port/` の読みやすい移植版がそれを関数ごとに置き換えていき、書き直された関数の元の生成版は、差分テストの比較対象としてだけ残ります。

- `tools/extract_disc.py` は、BIN/CUEまたはISOから直接ファイルを読み出します。
- 機能が削ぎ落とされた実行ファイルの関数は、Ghidra(Emotion Engine拡張付き)が見つけ、`config/` の名前、型、プロトタイプを適用します。
- ゲーム固有の修正は `config/` と `src/overrides/` に、PS2Recomp本体への変更は `patches/` にあります。

### ドキュメント

- [docs/map](docs/map/README.md): すべてのサブシステムとすべての関数(説明付き)。
- [docs/GAME_NOTES.md](docs/GAME_NOTES.md): ディスク、データファイルとその形式。
- [docs/RENDER_PIPELINE.md](docs/RENDER_PIPELINE.md): 街の描画の仕組み。ゲームの描画呼び出しからVU1マイクロコードの各プログラムまで。
- [docs/VU1_PROGRAMS.md](docs/VU1_PROGRAMS.md): ネイティブで動かし、C++にコンパイルしたVU1プログラム。
- [docs/IOP.md](docs/IOP.md): サウンドドライバとIOPモジュール。
- [docs/PLATFORM_FINDINGS.md](docs/PLATFORM_FINDINGS.md): SDK層を書き直して分かった、ランタイムと再コンパイラの挙動。
- [docs/PORT_STATUS.md](docs/PORT_STATUS.md): サブシステムごとの進捗。
- [docs/RESEARCH.md](docs/RESEARCH.md): 事前調査とこのゲームに関する先行研究。
- [src/port/README.md](src/port/README.md): 読みやすい移植版とそのテストの仕組み。

### 必要なもの(現在はWindowsのみ)

- ご自身のMetropolismaniaのディスクダンプ(北米版、SLUS-20377、ディスクVER 1.02)。BIN/CUEまたはISO。
- C++ x64ワークロード入りのVisual Studio 2022(MSVC、CMake、Ninjaが付属)。
- CMake 3.21以降、Python 3.11以降、Git。
- Ghidra 12.1.4とJDK 21、および [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) 拡張(v2.1.38、Ghidra 12.1.4用ビルド)。

### ビルド方法

```sh
git clone --recurse-submodules https://github.com/koaque/Metropolismania-PC.git
cd Metropolismania-PC
```

Ghidraの場所を `work/local.json` に書いて知らせます。

```json
{ "ghidra_install_dir": "C:/path/to/ghidra_12.1.4_PUBLIC" }
```

続けて各ステップを実行します(`all` で一括実行もできます)。

```sh
python tools/pipeline.py bootstrap          # pinned PS2Recomp + patches/ -> work/PS2Recomp
python tools/pipeline.py tools              # build ps2_analyzer and ps2_recomp
python tools/pipeline.py extract path/to/your/game.cue   # -> original/ (checks the build is SLUS-20377 v1.02)
python tools/pipeline.py symbols            # SDK names from PS2Recomp's signature scan -> config/symbols.csv
python tools/pipeline.py analyze            # Ghidra headless -> config/funcmap.csv
python tools/pipeline.py pointers           # functions reached only through pointers, re-analyzed until stable
python tools/pipeline.py recompile          # -> generated/
python tools/pipeline.py iop-aot            # the IOP sound and memory card modules -> C++ in work/iop_aot/
python tools/pipeline.py stage              # generated code + overrides + src/port + work/iop_aot -> runtime tree
python tools/pipeline.py build              # ps2EntryRunner
python tools/pipeline.py run                # timed run, log in logs/
```

PCSX2からセーブデータをコピーするには `python tools/mc_extract.py path/to/Mcd001.ps2` を使います。移植版のテスト(`capture`、`difftest`)は [src/port/README.md](src/port/README.md) に説明があります。

街のVU1プログラムは、C++にコンパイルしたものを使うと速く動きます。これはお持ちのディスクのマイクロコードから作るため、リポジトリには含まれていません。一度ビルドしてセーブデータをコピーした後、`python tools/pipeline.py vu1-capture` で街の実行に使われるプログラムを記録し、`vu1-emit` でそれをC++として `work/vu1_aot/gen/` に書き出し、`stage` と `build` で組み込みます([docs/VU1_PROGRAMS.md](docs/VU1_PROGRAMS.md))。これがない場合、同じプログラムはより遅い実行器で動きます。

### 構想中のパッチ

移植が完成した後に、これらの変更ができるようになることを目指しています。いずれもまだ存在しません。

- **ワイドスクリーン。** 16:9以上の本物のワイドスクリーン。視野角とHUDも補正します。必要なアドレスはPCSX2のワイドスクリーンパッチから既に分かっています([docs/GAME_NOTES.md](docs/GAME_NOTES.md))。
- **高リフレッシュレート(120 Hz)。** ゲームのロジックはPS2のフィールドレート(59.94 Hz)で1フレームごとに進むため、速度を保つにはこのレートのままにする必要があります。ロジックを変えずに中間フレームを描き、120 Hz以上のディスプレイでなめらかに表示することを目指します。
- **高解像度。** 固定の出力サイズではなく、PS2の640x448の整数倍で街を描画します。
- **PCネイティブの操作。** PS2パッドに割り当てる方式ではなく、キーボードとマウス、あらゆるゲームパッド、キー割り当ての変更に対応します。
- **その他:** Linux版、メモリーカードイメージではなく通常のフォルダへのセーブ、表示オプション(フルスクリーン、ボーダーレス、垂直同期)、残っている描画の不具合の修正。

### プロジェクトを支援する

このプロジェクトが役に立った場合は、開発を支援していただけます。

**[PayPalで寄付する](https://www.paypal.com/ncp/payment/LV3M4HY3PJHU4)**

**Bitcoin**

```
3CUTagummxA2SMFrS2vxGKyLj4gtQ9mrbW
```

**Ethereum と ERC-20 トークン全般**

```
0xabcdec4cf4c515e266876ae2e4323ff30bda36e9
```

### クレジット

- [PS2Recomp](https://github.com/ran-j/PS2Recomp)(ran-jと貢献者の皆さん)。このプロジェクトの出発点となった再コンパイラとランタイム。
- マージされるまで `patches/` に取り込んでいるPS2Recompのオープンなプルリクエスト。@VariableDeclared、@yanm1103、@Fiztba、@luisbraganca、@drakolordx7 によるVU0/VU1、VIF、GIFの修正(各パッチのヘッダを参照)。
- [psx-spx](https://psx-spx.consoledev.net/)(SPUのボイスパイプライン、エンベロープ、補間の詳細)。SPU2エミュレーションに使用。
- [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)(chaoticgd)。
- [PCSX2](https://github.com/PCSX2/pcsx2)。ハードウェアの挙動の基準。また、既知の視野角アドレスについてはArapapaさんのワイドスクリーンパッチ。
- [rootlm/metropolismaniaresearch](https://github.com/rootlm/metropolismaniaresearch)。テクスチャ形式についての初期のメモ。
- [OpenRCT2](https://github.com/OpenRCT2/OpenRCT2)。この移植が従っている、関数ごとのアプローチの手本。

### ライセンスと免責事項

このリポジトリのコードは [GNU GPL v3](LICENSE) でライセンスされています。リンクしているランタイムのPS2Recompと同じライセンスです。

これはファンによる保存プロジェクトです。Indi、メディアファクトリー、Natsume、D3 Publisher、Sony Interactive Entertainmentとは提携しておらず、承認も受けていません。「PlayStation」と「PS2」はSony Interactive Entertainmentの商標です。Metropolismaniaはそれぞれの権利者の所有物です。ゲームファイルを求めたり共有したりしないでください。

---

## Summary, FAQ and search keywords

Last updated: 2026-11-06.

**At a glance**

- **What it is:** Metropolismania PC, an open-source native PC port (a source port) of the PlayStation 2 game Metropolismania, in progress. It is not an emulator: the game's own code runs on the PC as C++.
- **The game:** Metropolismania (Japanese title 街ingメーカー, *Machi-ing Maker*), a city-building game played on foot, developed by Indi. Published by Media Factory in Japan (27 September 2001) and by Natsume in North America (17 October 2002) and Europe (8 November 2002).
- **Supported version:** the USA disc only (SLUS-20377, disc VER 1.02). Every address in the project is specific to that build.
- **How:** a static recompilation made with PS2Recomp keeps the game running while a hand-written, readable C++ port replaces it function by function, and differential tests check that each rewritten function matches the original.
- **State:** boots, plays its movies, music and sound, loads saves and reaches the city; all 1,481 game functions are rewritten and 1,432 of them verified. Not yet playable from start to finish.
- **Platform and license:** Windows (MSVC) for now; GNU GPL v3. No game files are included: you need your own disc.

### What is Metropolismania?

Metropolismania is a 2001 PlayStation 2 city builder by Indi, released in Japan as 街ingメーカー (*Machi-ing Maker*). You play the town planner on foot: you put up homes, shops and offices, then talk families into moving in and working there, and the town grows only while its residents are happy.

### Can I play Metropolismania on PC?

Not with this port yet: it boots and reaches the city, but it cannot be played through. Today the way to play Metropolismania on a PC is the PCSX2 emulator, which runs it well. This project works towards a native version that needs no emulator.

### Is Metropolismania PC an emulator?

No. The game's MIPS code is translated to C++ ahead of time (static recompilation), and that code is being rewritten by hand as readable C++. Parts of the PS2 hardware are still emulated by the runtime underneath (the graphics synthesizer, DMA and the sound chip; the VU1 microcode runs translated to C++), and replacing the graphics side with native rendering is the plan.

### Does the repository contain the game, an ISO or a ROM?

No. It holds only the project's own code, configuration and documentation. You extract the files from your own disc with `tools/extract_disc.py`, and the code generated from them stays on your machine.

### Which versions of the game are supported?

Only the North American release, SLUS-20377, disc VER 1.02. The European release (SLES-51162) and the Japanese 街ingメーカー would each need their own map of addresses.

### Does it support Metropolismania 2?

No. Metropolismania 2 (*Machi-ing Maker 2: Zoku Boku no Machi Zukuri*, 2006 in Japan, 2007 in North America) is a different game with different code.

### How is it different from PCSX2?

PCSX2 emulates the whole console and runs the original disc. Metropolismania PC turns the game's own code into C++ source that can be read, fixed and changed, which is what widescreen, higher resolutions, high refresh rates, native controls and other platforms need.

### What is static recompilation?

Translating a program's machine code into source code for another platform ahead of time, instead of interpreting it while it runs. This project uses PS2Recomp, which translates PS2 (MIPS R5900) code into C++, and then rewrites the result by hand, function by function, as OpenRCT2 did for RollerCoaster Tycoon 2.

### Is there a widescreen or HD version?

Not yet. Widescreen (16:9 and wider), higher resolutions, 120 Hz and native controls are planned once the port is complete ([proposed patches](#proposed-patches)). Until then, PCSX2 has a 16:9 patch for the game.

### Who made Metropolismania?

Indi developed it (Indi Software, as its title screen credits it). Media Factory published it in Japan and Natsume in North America and Europe. The sequel, Metropolismania 2, was published by D3 Publisher in Japan and by Natsume in North America and Europe.

**Keywords:** Metropolismania PC, Metropolismania PC port, Metropolismania for Windows, Metropolis Mania, Metropolismania PS2, Metropolismania decompilation, Metropolismania recompilation, Machi-ing Maker, 街ingメーカー, Natsume, Media Factory, Indi, PlayStation 2 city builder, PS2 city-building game, PS2 static recompilation, PS2Recomp, PS2 decompilation, PS2 source port, native PC port, SLUS-20377, reverse engineering, game preservation.

### 日本語: 概要、よくある質問、検索キーワード

**概要**

- **これは何か:** PlayStation 2のゲーム「街ingメーカー」(英語版 Metropolismania)をPCでネイティブに動かす、開発中のオープンソースの移植(ソースポート)です。エミュレータではなく、ゲーム自身のコードがC++としてPC上で動きます。
- **ゲーム:** 街ingメーカー(英語版 Metropolismania)。Indiが開発した、自分の足で歩いて遊ぶ街づくりゲームです。日本ではメディアファクトリーが2001年9月27日に、北米とヨーロッパではNatsumeが2002年(北米10月17日、ヨーロッパ11月8日)に発売しました。
- **対応バージョン:** 北米版のディスクのみ(SLUS-20377、ディスクVER 1.02)。プロジェクト内のアドレスはすべてこのビルド専用です。
- **方法:** PS2Recompによる静的再コンパイルでゲームを動かし続けながら、手で書いた読みやすいC++の移植で関数ごとに置き換えます。書き直した関数は、差分テストで元のコードと一致することを確かめます。
- **現状:** 起動し、ムービー、音楽、効果音を再生し、セーブデータを読み込んで街まで進みます。ゲームの1,481関数すべてを書き直し、うち1,432関数を検証済みです。まだ最後まで遊べる状態ではありません。
- **対応環境とライセンス:** 現在はWindows(MSVC)のみ。GNU GPL v3。ゲームのファイルは含まれていません。ご自身のディスクが必要です。

#### 街ingメーカーとはどんなゲームですか?

Indiが開発し、2001年にPlayStation 2で発売された街づくりゲームで、英語版のタイトルはMetropolismaniaです。プレイヤーは街の都市計画者として街を歩き、住宅、店、オフィスを建て、家族を説得して住んだり働いたりしてもらいます。住民が幸せでなければ街は育ちません。

#### 街ingメーカーをPCで遊べますか?

この移植ではまだ遊べません。起動して街まで進みますが、最後まで遊ぶことはできません。現在PCで遊ぶ方法はエミュレータのPCSX2で、問題なく動作します。このプロジェクトは、エミュレータを必要としないネイティブ版を目指しています。

#### エミュレータですか?

いいえ。ゲームのMIPSコードを事前にC++へ変換し(静的再コンパイル)、そのコードを読みやすいC++として手で書き直しています。PS2のハードウェアの一部(グラフィックスシンセサイザ、DMA、サウンドチップ)は今も下のランタイムがエミュレートしており(VU1のマイクロコードはC++に変換して実行)、グラフィックス部分をネイティブの描画で置き換える予定です。

#### ゲーム本体、ISO、ROMは含まれていますか?

いいえ。含まれているのはプロジェクト自身のコード、設定、ドキュメントだけです。ファイルはご自身のディスクから `tools/extract_disc.py` で取り出し、そこから生成されたコードはお使いのPCの中だけに置かれます。

#### 日本版の街ingメーカーに対応していますか?

いいえ。対応しているのは北米版(SLUS-20377、ディスクVER 1.02)だけです。日本版とヨーロッパ版(SLES-51162)には、それぞれ専用のアドレスの対応表が必要です。

#### 街ingメーカー2に対応していますか?

いいえ。街ingメーカー2(英語版 Metropolismania 2)は、コードの異なる別のゲームです。

#### PCSX2との違いは?

PCSX2はゲーム機全体をエミュレートし、元のディスクをそのまま動かします。Metropolismania PCは、ゲーム自身のコードを、読んで直して変更できるC++のソースにします。ワイドスクリーン、高解像度、高リフレッシュレート、PC向けの操作、他のプラットフォームへの対応には、これが必要です。

#### 静的再コンパイルとは?

プログラムの機械語を、実行中に解釈するのではなく、事前に別のプラットフォーム向けのソースコードへ変換することです。このプロジェクトはPS2(MIPS R5900)のコードをC++に変換するPS2Recompを使い、その結果を関数ごとに手で書き直しています。OpenRCT2がRollerCoaster Tycoon 2で行ったのと同じ方法です。

#### ワイドスクリーンやHD版はありますか?

まだありません。ワイドスクリーン(16:9以上)、高解像度、120 Hz、PC向けの操作は、移植の完成後に予定しています([構想中のパッチ](#構想中のパッチ))。それまでは、PCSX2にこのゲーム用の16:9パッチがあります。

#### 誰が作ったゲームですか?

開発はIndi(タイトル画面の表記はIndi Software)です。日本ではメディアファクトリー、北米とヨーロッパではNatsumeが発売しました。続編の街ingメーカー2は、日本ではD3パブリッシャー、北米とヨーロッパではNatsumeが発売しました。

**検索キーワード:** 街ingメーカー, 街ingメーカー PC, 街ingメーカー PC版, 街ingメーカー 移植, Machi-ing Maker, Metropolismania, Metropolismania PC, PS2 街づくりゲーム, PS2 静的再コンパイル, PS2 逆コンパイル, PS2Recomp, ソースポート, ネイティブ移植, リバースエンジニアリング, ゲーム保存, メディアファクトリー, Natsume, SLUS-20377.
