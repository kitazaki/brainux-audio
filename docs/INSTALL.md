# 導入・再生・復元

## 導入

systemdを使用するLinux 5.4版の既存Brainux環境向けです。リポジトリを取得・展開し、そのディレクトリで実行してください。OS全体のイメージではありません。作業前にSDカードのバックアップを保管してください。

ビルド環境は不要です。GitHubの「Code → Download ZIP」で取得してBrainuxへコピーし、展開します（`unzip`がない場合はPCで展開してからコピーできます）。展開先で以下を実行します。

```sh
sudo sh install.sh
```

バックアップは `/home/user/backup-brain-audio-common-XXXXXXXX` に作成します。元のカーネル、存在するSH1〜SH7のDTB、モジュール起動設定を保存します。旧カーネル用モジュールは削除しません。`/lib/modules`の下へ対応モジュールを展開するため、merged-usr環境の`/lib`シンボリックリンクを置き換えません。

従来の `/etc/modules` と `/etc/modules-load.d/*.conf` にある対象音声モジュールの無条件ロード行を取り除き、`brain-audio.service` に切り替えます。新しいサービスはDTのcompatibleでロードするモジュールを選びます。YMU818BのDT/udevによる自動ロードと重なってもmodprobeは同じモジュールを重複登録しません。独自の音声起動スクリプトがある場合は、対象モジュールの無条件ロードが残らないよう確認してください。

インストーラーは再起動・再生を開始しません。完了後に手動で再起動してください。

```sh
sudo reboot
```

## カーネル・DTB・koを手動で配置する場合

`.ko`は `modules/5.4.149-brain-audio-common-test1/` 以下に展開済みで置いてあります。圧縮ファイルにも同じ内容が入っています。GitHubの個別ファイル画面の「Download raw file」からダウンロードできます。

| ダウンロードするファイル | Brainuxでの配置先 |
|---|---|
| `boot/zImage` | `/boot/zImage` |
| `boot/imx28-pwshN.dtb`（Nは機種番号1〜7） | `/boot/imx28-pwshN.dtb` |
| `modules/5.4.149-brain-audio-common-test1/` の内容 | `/lib/modules/5.4.149-brain-audio-common-test1/` |
| `brain-audio-load` | `/usr/local/sbin/brain-audio-load`（実行権限755） |
| `brain-audio.service` | `/etc/systemd/system/brain-audio.service` |

音声用koの内訳（配置先は上記リリースディレクトリの下です）:

| 対応機種 | ko | 配置するサブディレクトリ |
|---|---|---|
| SH1/SH2 | `snd-soc-ymu818b.ko` | `kernel/sound/soc/codecs/` |
| SH1/SH2 | `snd-soc-mxs-ymu818b.ko` | `kernel/sound/soc/mxs/` |
| SH3〜SH7 | `brain-bu26154.ko` | `kernel/sound/soc/mxs/` |

既存のタッチパネルやRTCなどのモジュールもカーネルのバージョンに依存するため、通常は `modules/` の一式を配置してください。旧カーネルのディレクトリにあるkoで代用しないでください。同じSDカードを7機種で使用する場合は、7個のDTBと音声3モジュールをすべて配置します。

手動作業でも、元のカーネル・DTB・起動設定をバックアップしてからコピーしてください。`/boot`が読み取り専用の場合は書き込み可能にしてコピーし、書き込み後に`sync`して読み取り専用へ戻します。koはSDカードのLinuxルートファイルシステム側へ置きます。`/boot`へ置くだけではmodprobeから見つかりません。

配置後は依存関係を生成して自動ロードを有効にします。

```sh
sudo chmod 755 /usr/local/sbin/brain-audio-load
sudo depmod -a 5.4.149-brain-audio-common-test1
sudo systemctl daemon-reload
sudo systemctl enable brain-audio.service
```

従来の`/etc/modules`や`/etc/modules-load.d/*.conf`に音声モジュールの無条件ロード設定がある場合は、対象行を取り除いてください。再起動後、起動した機種のDTBに応じてサービスが必要なモジュールをロードします。これらの配置と設定をまとめて行うのが冒頭の`install.sh`です。

## 実機確認

同じSDカードを使用できます。機種を入れ替える際は電源を切ってからSDカードを移してください。再起動後、機種ごとに以下を記録します。

```sh
sudo sh check-audio.sh
```

カーネルのリリース名、モデル名、カードID、ロードされたモジュール、起動サービス成功を確認します。YMU818Bで`lsmod`に2個のモジュールが表示されることが、今回の組み込み版との主な違いです。

SH1/SH2の再生（音量は控えめから）:

```sh
mpg123 -o alsa -a 'plughw:CARD=BrainYMU818B,DEV=0' -r 44100 -f 512 /path/to/music.mp3
```

SH3〜SH7の再生:

```sh
amixer -c BrainBU26154 sset Output Speaker
amixer -c BrainBU26154 sset Playback 2
mpg123 -o alsa -a 'plughw:CARD=BrainBU26154,DEV=0' -r 44100 -f 4096 /path/to/music.mp3
```

各機種で本体スピーカーの実際の音を確認してください。停止後の再再生、再起動後の再生も確認します。YMU818Bのハードウェア音量機能は今回追加しません。

## 戻す場合

```sh
sudo sh restore.sh /home/user/backup-brain-audio-common-XXXXXXXX
```

実際のバックアップ名を指定します。復元後は手動で再起動します。起動できない場合は、別のLinux環境からSDカードを開き、バックアップの `boot/` 内のカーネルと該当機種のDTBを元の位置へ戻してください。元のモジュール起動設定はバックアップの `config/` に保存されています。

