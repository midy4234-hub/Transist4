# Transist4 — Claude Code 向けの仕様書

4 バンドのトランジェントシェイパー (VST3、JUCE 8.0.15)。旧仮名 TranQuad。
作者は MIDy (音楽プロデューサー)。Claude Code が作り、MIDy が Ableton Live で確認している (2026-09-26 実機で「とても良い」)。
このファイルは、別のマシン (Windows を含む) の Claude Code が作業を引き継げるように、仕様・決定事項・ビルド方法をまとめたもの。

## 前提と作業の約束

- 音を聴いて良し悪しを判断するのはユーザー。Claude は数値で壊れていないこと (足し戻し誤差・レイテンシ・レベル・CPU) を確かめる
- パラメータ ID・PLUGIN_CODE (Trs4)・PLUGIN_MANUFACTURER_CODE (MIDy)・BUNDLE_ID (com.midy.transist4) は変えない。
  変えると保存済みの Live セットで設定が読めなくなる。Mac 版と Windows 版も同じ ID なので、セットを持ち回せる
- 機能はこの構成で確定。検出速度のつまみやオートゲインのスイッチは意図して付けていない。足すときはユーザーに確認する
- コミットの身元は `git config user.name` = midy4234-hub、`user.email` = 331112920+midy4234-hub@users.noreply.github.com。
  本名やホスト名由来のアドレス、個人の Gmail をコミットに入れない。コミット前に `git config --show-origin --get-regexp "^user\."` で確認する

## パラメータ (ID / 表示名 / 範囲 / 既定)

バンド b = 0 Low / 1 Low-Mid / 2 Mid / 3 High。

| ID | 表示名 | 範囲 | 既定 |
|---|---|---|---|
| t0〜t3 | <バンド> Transient | -45〜45 dB | 0 |
| mk0〜mk3 | <バンド> Makeup | -24〜24 dB | 0 |
| mute0〜3 / solo0〜3 / byp0〜3 | Mute / Solo / Bypass | on/off | off |
| xo1 | Crossover Low | 20〜1000 Hz | 125 Hz |
| xo2 | Crossover Mid | 100〜8000 Hz | 450 Hz |
| xo3 | Crossover High | 500〜18000 Hz | 3500 Hz |
| slope | Slope | 0 = 24 / 1 = 12 / 2 = 6 dB/oct | 24 |
| mix | Dry/Wet | 0〜100 % | 100 % |
| out | Output | -24〜24 dB | 0 |
| delta | Delta | on/off | off |
| bypass | Bypass | on/off | off |

## DSP

### クロスオーバー (Source/Crossover.h)

- どのスロープでも「4 バンドを足すとオールパス」になるように組む。各分割点で LP を作り、HP 側は「そのスロープの足し戻しと同じオールパス − LP」
  - 24 dB/oct: LR4 (LP = BW2 LP を 2 段、足し戻し = 2 次オールパス Q = 1/√2)
  - 12 dB/oct: LR2 (LP = 1 次 LP を 2 段、足し戻し = 1 次オールパス。HP 側は逆相)
  - 6 dB/oct: 1 次 (LP + HP = 1 なので補償不要)
- 木構造: f2 で Low/High に分け、Low 側に f3、High 側に f1 のオールパスを掛けて位相をそろえてから、Low を f1、High を f3 で分ける
- レイテンシ 0。足し戻し誤差 0.0000 dB (全スロープ)

### 検出とゲイン (Source/Detector.h)

- fast: |x| の区間最大。区間幅はゼロ交差から推定した半周期 × 1.5 に自動で追従する
  (固定幅だと、他バンドから漏れた低音のうねりをトランジェントと誤認して -17 dB の歪みが出た)
- slow: fast に遅れて追いつく (バンド別 τ = 25 / 15 / 8 / 4 ms)。fast が下がるときは即座に合わせる
- t = (fast − slow) / fast ∈ [0, 1]
- ゲイン [dB] = T · (t − b)。T は Transient つまみ = 「アタックと余韻の差が T dB になる傾き」
- b はオートゲイン: 約 1.5 秒で処理前後のエネルギーが等しくなる値。t のエネルギー重み付きヒストグラムから計算するので、T を回した瞬間から正しい b が出る
- 目安: +18 でドラムのピークが約 +10 dB 上がる (RMS は同じ)。CPU 約 0.7 %

## UI (Source/PluginEditor.cpp)

- ChordRes と同じ Ableton 純正寄りの見た目 (別の見た目で作った初版は却下された)
- 横帯型: 1 バンド = 1 本の横帯 (名前 | 波形 | Transient | Makeup | M/S/B)。上から HIGH → LOW に積む
- クロスオーバーは帯の境目に数字で表示し、数字そのものを上下ドラッグ (約 80 px / オクターブ、ダブルクリックで初期値)。つまみにはしない (ユーザー指定)
- 最下段に MASTER (Slope・Dry/Wet・Output・Delta/Bypass・IN/OUT 横メーター)
- 却下された案: 「上に波形・下に横並びパネル」(波形は上→下で HIGH→LOW、パネルは左→右で LOW→HIGH と向きが食い違い「直感的じゃない」)

## 耐久テストの状態

- 2026-09-26 に修正済み: ブロックサイズ依存、起動直後に +44 dBFS 跳ねる、操作時のクリック
- 残っている WARN: クロスオーバーを全域ジャンプさせたとき (オートメーションで端から端へ飛ばす場合)。実用上の問題としては扱っていない
- Windows (GitHub Actions、MSVC) でも robust は 0 FAIL / 1 WARN で Mac と同じ傾向 (2026-10-01)

## ビルド

JUCE は `../_deps/JUCE` があればそれを使い、無ければ CMake が GitHub から 8.0.15 を取ってくる (初回は数分かかる)。
`lab/` は PluginLab (MIDy の非公開の作業場) の共通ヘッダ (LabTest.h / LabRobust.h) の写し。隣に `../PluginLab` があればそちらが優先される。

### Windows

必要なもの: Visual Studio 2022 以降 (「C++ によるデスクトップ開発」ワークロード。CMake も同梱)、Git。

```
cmake -B build -A x64
cmake --build build --config Release --target Transist4_VST3
```

- できるもの: `build\Transist4_artefacts\Release\VST3\Transist4.vst3` (フォルダ)
- フォルダごと `C:\Program Files\Common Files\VST3\` にコピーする (管理者権限が要る)。
  管理者のシェルなら `-DLAB_INSTALL=ON` を付けて configure すればビルド後に自動でコピーされる (既定は OFF。権限が無いとビルドが失敗するため)
- Live: 環境設定 → Plug-ins → 「VST3 プラグイン システムフォルダ」をオンにして再スキャン
- ソースは UTF-8 の日本語コメント入り。CMakeLists で MSVC に `/utf-8` を渡している (外すと C4819 や誤コンパイル)

### Mac

```
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```
ユニバーサル (x86_64 + arm64)。`~/Library/Audio/Plug-Ins/VST3/` に自動でコピーされる。

### GitHub Actions

`.github/workflows/windows.yml`: main への push (と手動実行) で Windows x64 の VST3 をビルドし、
Actions の実行ページの Artifacts に `Transist4-windows-x64` として置く。同時に検証ドライバの耐久テストも走らせる。

## 検証ドライバ (DAW 無し)

```
cmake -B build-test -DTRANSIST4_BUILD_TESTS=ON
cmake --build build-test --config Release --target Transist4Test
build-test/Transist4Test_artefacts/Release/Transist4Test [出力先]     # 固有テスト (平坦性・分離・持続音・バースト・インパルス・CPU) + UI 画像
build-test/Transist4Test_artefacts/Release/Transist4Test robust      # 標準の耐久テスト 12 項目
build-test/Transist4Test_artefacts/Release/Transist4Test render in.wav out.wav t0=18 byp3=1   # 任意設定の試聴用 A/B
```
固有テストのドラムループ書き出しは MIDy の Mac の外付け SSD (VORSO_COMPONENTS) を読むので、他のマシンでは素材が無く書き出されない。
変更したら最低でも固有テストと robust を通す。
