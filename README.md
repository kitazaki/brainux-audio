# Brainux audio — PW-SH1〜SH7

PW-SH1〜SH7で本体スピーカー再生を確認した、Linux 5.4共通カーネルと音声モジュールの配布物です。音量機能追加前の保存版です。

- 共通カーネル: `5.4.149-brain-audio-common-test1`
- PW-SH1/SH2: YMU818B、カードID `BrainYMU818B`
- PW-SH3〜SH7: BU26154、カードID `BrainBU26154`
- カーネルとモジュール一式は共通、DTBは機種別です。起動時にDTの情報から適切な音声モジュールを選びます。
- 既存のLinux 5.4版Brainux SDカードとsystemd環境が必要です。OS全体のイメージではありません。

| ファイル | 内容 |
|---|---|
| `boot/` | カーネルとSH1〜SH7用DTB |
| `modules-*.tar.gz` | 同じカーネル用モジュール一式（音声3モジュールを含む） |
| `source/` | 音声ドライバー、DTソース、対応ソース全体を復元する差分 |
| `build-info/` | ビルド設定・履歴・モジュール情報 |
| `install.sh` / `restore.sh` | バックアップ付き導入・復元 |
| `brain-audio-load` / `brain-audio.service` | DTに応じた自動ロード |
| `check-audio.sh` | 再生を開始しない状態確認 |
| `SHA256SUMS` | 配布ファイルのチェックサム |

[導入・再生・復元手順](docs/INSTALL.md) / [実機確認結果](docs/VERIFICATION.md) / [対応ソースとビルド](source/README.md)

YMU818Bのハードウェア音量調整は未実装です。初回は手順に記載した小さめのソフトウェア音量で再生してください。

元のカーネルソース全体、Windows CEのDLL、音楽ファイル、調査用ROMは含めていません。ライセンスは各ソースの表記と [COPYING](COPYING) を参照してください。
