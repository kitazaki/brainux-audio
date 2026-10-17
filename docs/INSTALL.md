# 導入・再生・復元

## 導入

systemdを使用するLinux 5.4版の既存Brainux環境向けです。リポジトリを取得・展開し、そのディレクトリで実行してください。OS全体のイメージではありません。作業前にSDカードのバックアップを保管してください。

```sh
sudo sh install.sh
```

バックアップは `/home/user/backup-brain-audio-common-XXXXXXXX` に作成します。元のカーネル、存在するSH1〜SH7のDTB、モジュール起動設定を保存します。旧カーネル用モジュールは削除しません。`/lib/modules`の下へ対応モジュールを展開するため、merged-usr環境の`/lib`シンボリックリンクを置き換えません。

従来の `/etc/modules` と `/etc/modules-load.d/*.conf` にある対象音声モジュールの無条件ロード行を取り除き、`brain-audio.service` に切り替えます。新しいサービスはDTのcompatibleでロードするモジュールを選びます。YMU818BのDT/udevによる自動ロードと重なってもmodprobeは同じモジュールを重複登録しません。独自の音声起動スクリプトがある場合は、対象モジュールの無条件ロードが残らないよう確認してください。

インストーラーは再起動・再生を開始しません。完了後に手動で再起動してください。

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

