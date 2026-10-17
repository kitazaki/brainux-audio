# Brainux audio — PW-SH1〜SH7

PW-SH1〜SH7で本体スピーカー再生を確認した、Linux 5.4共通カーネルと音声モジュールの配布物です。音量機能追加前の保存版です。

- 共通カーネル: `5.4.149-brain-audio-common-test1`
- PW-SH1/SH2: YMU818B、カードID `BrainYMU818B`
- PW-SH3〜SH7: BU26154、カードID `BrainBU26154`
- カーネルとモジュール一式は共通、DTBは機種別です。起動時にDTの情報から適切な音声モジュールを選びます。
- 既存のLinux 5.4版Brainux SDカードとsystemd環境が必要です。OS全体のイメージではありません。

**ビルドは不要です。** GitHubの「Code → Download ZIP」で取得し、Brainux上で展開したディレクトリから `sudo sh install.sh` を実行すると、バックアップを作成してカーネル・DTB・モジュール・自動ロード設定を配置します。完了後に手動で再起動してください。

個別ダウンロード:

- [カーネル zImage](boot/zImage) / [機種別DTB](boot/)
- [PW-SH1/SH2: snd-soc-ymu818b.ko](modules/5.4.149-brain-audio-common-test1/kernel/sound/soc/codecs/snd-soc-ymu818b.ko)
- [PW-SH1/SH2: snd-soc-mxs-ymu818b.ko](modules/5.4.149-brain-audio-common-test1/kernel/sound/soc/mxs/snd-soc-mxs-ymu818b.ko)
- [PW-SH3〜SH7: brain-bu26154.ko](modules/5.4.149-brain-audio-common-test1/kernel/sound/soc/mxs/brain-bu26154.ko)
- [対応モジュール一式（展開済み）](modules/5.4.149-brain-audio-common-test1/)

個別ファイルの画面では「Download raw file」から保存できます。配置先と必要な起動設定は[導入手順](docs/INSTALL.md)を参照してください。モジュールは同梱カーネル専用です。

| ファイル | 内容 |
|---|---|
| `boot/` | カーネルとSH1〜SH7用DTB |
| `modules/` | 個別ダウンロードできるビルド済み `.ko` とモジュール情報 |
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
