# 音量・手動ヘッドフォン切替 試験版

カーネルは `5.4.149-brain-audio-common-test3`。PW-SH3〜SH7向けに、BU26154のヘッドフォン音量・手動切替修正を含みます。YMU818Bの音量・PM変更は今回の配布対象外です。ヘッドフォンの自動切替は実装していません。

## 実機確認状況（2026-10-18）

- PW-SH3/SH4/SH5：ヘッドフォン音量と手動切替を確認済み。今回追加した入力経路の初期化後の再確認は未実施です。
- PW-SH6：入力経路初期化の追加後、再起動してユーザーがヘッドフォン再生の正常動作を確認しました。
- PW-SH7：今回の修正版は未確認です。
- PW-SH1/SH2：本試験版の導入対象外です。従来の保存版をご利用ください。

従来の共通カーネルでの本体スピーカー再生はSH1〜SH7で確認済みですが、上記の新機能確認とは区別しています。既存の保存版はリポジトリのルートにあります。

## 導入（ビルド不要）

GitHubのCode → Download ZIPから取得・展開し、既存のLinux 5.4版Brainux上でこのフォルダーへ移動して実行します。

```sh
cd testing/common-test3
sudo sh install.sh
sudo reboot
```

カーネル・機種別DTB・対応モジュール一式と自動ロード設定を配置します。導入時に表示されるバックアップのパスを保存してください。復元は `sudo sh restore.sh バックアップのパス` を実行後、手動で再起動します。

個別ファイル：[カーネル・DTB](boot/) / [BU26154 ko](modules/5.4.149-brain-audio-common-test3/kernel/sound/soc/mxs/brain-bu26154.ko) / [対応ソース](source/)

各koはこのカーネル専用です。他バージョンのkoと混在させず、モジュール一式を配置してください。既存のcommon-test3環境でもinstall.shで更新できます。

## BU26154（SH3〜SH7）

```sh
amixer -c BrainBU26154 sset Headphone 48
amixer -c BrainBU26154 sset Output Headphone
mpg123 -o alsa -a 'plughw:CARD=BrainBU26154,DEV=0' -r 44100 ~/after_2.mp3
```

ファイル名は手元のMP3へ置き換えてください。Headphoneは0〜120（0で消音）。小さい場合は72、100などへ徐々に上げます。`-f` は省略しています。スピーカーへ手動で戻す場合：

```sh
amixer -c BrainBU26154 sset Playback 2
amixer -c BrainBU26154 sset Output Speaker
```

Playbackはスピーカー音量、Headphoneはヘッドフォン音量です。今回の追加修正はCE同様にMAP1の0x82へ0x11を書き、L-DACを左右ヘッドフォンへ明示接続します。
