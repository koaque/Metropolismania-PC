# Metropolismania PC

A native PC version of **Metropolismania** (PlayStation 2, Indi, 2001), being rebuilt as readable C++ source code, one function at a time.

[日本語はこのページの一番下にあります](#日本語) (Japanese version at the bottom of this page)

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

Progress per subsystem is tracked in [docs/PORT_STATUS.md](docs/PORT_STATUS.md). As far as we could find, this is the first decompilation or recompilation project for the game ([research notes](docs/RESEARCH.md)).

## Status

| Area | State |
|---|---|
| Boot, logos, title screen, memory card check | Works |
| Input | Works (keyboard, pad, scripted) |
| The city | Reached: HUD, cursor and 3D terrain render, about 23 fps on average in the heavy views of the explore autoplay (lowest about 19), 60 (the display rate) on the title and in menus (CPU rendering, pre-timed VU1 programs, see docs/VU1_PROGRAMS.md) |
| Movies, music, sound effects | Play, through a new SPU2 emulation (with reverb) |
| Saves | Memory card check passes; loading a save from a PCSX2 memory card works |
| Mapping (names, types, prototypes) | Done for every game function |
| Readable source port | In progress: all 1,481 of the game's functions rewritten, 1,432 of them verified against the original (97%) ([details](docs/PORT_STATUS.md)) |
| Platform layer (SDK and C runtime) | In progress: 123 of the 768 SDK functions rewritten as plain C++ in `src/port/platform/`; 91 verified by replay, 25 numeric ones (soft float, libm) by random-input comparison, 7 not yet shown. No recompiled SDK function is reachable from the ported code, and the explore and new-game runs enter none ([details](docs/PLATFORM_FINDINGS.md)) |
| VU1 microcode | The city's programs run natively, checked against the interpreter ([details](docs/VU1_PROGRAMS.md)) |
| Sound driver (IOP) | The game's own driver (YYSOUND, 114 functions) is rewritten as readable C++ in `src/port/iop/` and checked call by call against the original (0 differences, bar a few uninitialized stack bytes that the details list). All ten IOP modules, YYSOUND and the nine Sony modules (memory card, pad, SPU2 library, MIDI and sound-effect synthesizers), run as C++ translated ahead of time from your disc's modules, which matches the interpreter exactly (the same state at every checked point, the same audio, random programs of every instruction form; no instruction of the ten modules is interpreted in the scripted runs). The Sony modules are not rewritten as readable source ([details](docs/IOP.md)) |
| Widescreen, high refresh rate, hi-res, native controls, Linux | Proposed, not started ([see below](#proposed-patches)) |

## How it works

The starting point is a static recompilation made with [PS2Recomp](https://github.com/ran-j/PS2Recomp): the game's MIPS R5900 code is translated to C++ from your disc and linked against PS2Recomp's runtime, which provides memory, the kernel, graphics, VU1, DMA and an IOP that runs the game's original IOP modules (sound, pad, memory card). That machine-translated code is the scaffolding that keeps the game running. The readable port in `src/port/` replaces it function by function; once a function is rewritten, its generated version is kept only as the reference the differential tests compare against.

- `tools/extract_disc.py` reads the files straight from your BIN/CUE or ISO.
- Ghidra (with the Emotion Engine extension) finds the functions in the stripped executable and applies the names, types and prototypes from `config/`.
- Game-specific fixes live in `config/` and `src/overrides/`; changes to PS2Recomp itself live in `patches/`.

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
python tools/pipeline.py recompile          # -> generated/
python tools/pipeline.py iop-aot            # the IOP sound and memory card modules -> C++ in work/iop_aot/
python tools/pipeline.py stage              # generated code + overrides + src/port + work/iop_aot -> runtime tree
python tools/pipeline.py build              # ps2EntryRunner
python tools/pipeline.py run                # timed run, log in logs/
```

To copy your saves over from PCSX2: `python tools/mc_extract.py path/to/Mcd001.ps2`. The port's own tests (`capture`, `difftest`) are described in [src/port/README.md](src/port/README.md).

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

**Metropolismania PC**(街ingメーカー、PlayStation 2、Indi、2001年)を、読みやすいC++のソースコードとして一関数ずつ作り直しているプロジェクトです。目指すのは、PC上でネイティブに動くバージョンです。

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

サブシステムごとの進捗は [docs/PORT_STATUS.md](docs/PORT_STATUS.md) にあります。調べた限り、このゲームの逆コンパイルまたは再コンパイルのプロジェクトはこれが最初です([調査メモ](docs/RESEARCH.md))。

### 現状

| 項目 | 状態 |
|---|---|
| 起動、ロゴ、タイトル画面、メモリーカード確認 | 動作 |
| 入力 | 動作(キーボード、パッド、スクリプト) |
| 街 | 到達済み。HUD、カーソル、3D地形を描画。探索の自動操作の重い場面で平均約23 fps(最低約19 fps)、タイトルとメニューでは表示レートの60 fps |
| ムービー、音楽、効果音 | 再生可能(新しく作ったSPU2エミュレーションによる、リバーブ付き) |
| セーブ | メモリーカードの確認は通過。PCSX2のメモリーカードからのロードは動作 |
| マッピング(名前、型、プロトタイプ) | ゲームの全関数で完了 |
| 読みやすいソースへの移植 | 進行中。1,481関数すべてを書き直し済み、うち1,432関数を元のコードとの照合で検証済み(97%)([詳細](docs/PORT_STATUS.md)) |
| プラットフォーム層(SDKとCランタイム) | 進行中。768個のSDK関数のうち123個を `src/port/platform/` に素のC++で書き直し済み。91個はリプレイで、25個の数値関数(ソフトフロート、libm)はランダム入力との比較で検証済み、7個は未検証。移植済みコードから到達できる再コンパイル済みSDK関数はなく、探索と新規ゲームの実行でも再コンパイル済み関数は実行されません([詳細](docs/PLATFORM_FINDINGS.md)) |
| VU1マイクロコード | 街のプログラムはネイティブで動作し、インタプリタとの照合済み([詳細](docs/VU1_PROGRAMS.md)) |
| サウンドドライバ(IOP) | ゲーム自身のドライバ(YYSOUND、114関数)を `src/port/iop/` に読みやすいC++として書き直し済みで、元のコードと呼び出しごとに照合しています(差異は0件。初期化されていないスタックの数バイトを除く。詳細に記載)。YYSOUNDとSony製の9モジュール(メモリーカード、パッド、SPU2ライブラリ、MIDIと効果音のシンセサイザ)を含む10個のIOPモジュールすべてを、ディスクのモジュールから事前に変換したC++で実行しており、インタプリタと完全に一致します(照合した各時点で同じ状態、同じ音声、あらゆる命令形式のランダムなプログラム。スクリプトによる実行では10モジュールの命令をインタプリタで実行することはありません)。Sony製モジュールは読みやすいソースには書き直していません([詳細](docs/IOP.md))|
| ワイドスクリーン、高リフレッシュレート、高解像度、ネイティブ操作、Linux | 構想中、未着手 |

### 仕組み

出発点は [PS2Recomp](https://github.com/ran-j/PS2Recomp) による静的再コンパイルです。ゲームのMIPS R5900コードをお持ちのディスクからC++に変換し、メモリ、カーネル、グラフィックス、VU1、DMA、そしてゲーム本来のIOPモジュール(サウンド、パッド、メモリーカード)を動かすIOPを提供するPS2Recompのランタイムとリンクします。この機械的に変換されたコードは、ゲームを動かし続けるための足場です。`src/port/` の読みやすい移植版がそれを関数ごとに置き換えていき、書き直された関数の元の生成版は、差分テストの比較対象としてだけ残ります。

- `tools/extract_disc.py` は、BIN/CUEまたはISOから直接ファイルを読み出します。
- 機能が削ぎ落とされた実行ファイルの関数は、Ghidra(Emotion Engine拡張付き)が見つけ、`config/` の名前、型、プロトタイプを適用します。
- ゲーム固有の修正は `config/` と `src/overrides/` に、PS2Recomp本体への変更は `patches/` にあります。

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
python tools/pipeline.py recompile          # -> generated/
python tools/pipeline.py iop-aot            # the IOP sound and memory card modules -> C++ in work/iop_aot/
python tools/pipeline.py stage              # generated code + overrides + src/port + work/iop_aot -> runtime tree
python tools/pipeline.py build              # ps2EntryRunner
python tools/pipeline.py run                # timed run, log in logs/
```

PCSX2からセーブデータをコピーするには `python tools/mc_extract.py path/to/Mcd001.ps2` を使います。移植版のテスト(`capture`、`difftest`)は [src/port/README.md](src/port/README.md) に説明があります。

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
