# Transist4 — 4 バンドのトランジェントシェイパー (VST3)

コードは Claude Code (Anthropic の AI) が書き、MIDy が仕様を決めて Ableton Live で確認しました。
無保証です。サポート・不具合対応・要望への対応はしません (Issue / Pull Request も受け付けません)。
ライセンスは AGPLv3 (LICENSE)。JUCE (AGPLv3) と VST3 SDK (MIT) を使っています。Copyright (C) 2026 MIDy

Made with Claude Code. Provided as-is, without support. Issues and pull requests are not accepted. Licensed under AGPLv3.

**ダウンロード**: Releases に Mac 版 (Intel / Apple Silicon 両対応) と Windows 版 (x64) の zip があります。

- Mac: `Transist4.vst3` を `~/Library/Audio/Plug-Ins/VST3/` に入れる。署名していないので、入れたあとターミナルで

  ```
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Transist4.vst3
  ```

- Windows: `Transist4.vst3` フォルダごと `C:\Program Files\Common Files\VST3\` に入れる

ビルド方法は CLAUDE.md の「ビルド」。

帯域を 4 つに分けて、それぞれのアタックを強めたり弱めたりする。レイテンシ 0。
4 バンドを足すと元の音に戻る (どのスロープでも足し戻しはオールパス)。

パラメータ
- バンドごと (High / Mid / Low-Mid / Low の横帯、上が高域)
  - Transient: -45〜+45 dB。アタックと余韻の差がこの値になる傾き。全体の音量はオートゲインで揃う
  - Makeup: そのバンドの音量
  - M / S / B: ミュート / ソロ / バイパス
- クロスオーバー: 帯の境目の数字を上下にドラッグ (ダブルクリックで初期値 125 / 450 / 3500 Hz)
- MASTER
  - Slope: 24 / 12 / 6 dB/oct
  - Dry/Wet / Output
  - Delta: 処理で加わった差分だけを聴く
  - Bypass
