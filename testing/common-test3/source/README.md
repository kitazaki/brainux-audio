# 対応ソース

上流 https://github.com/brain-hackers/linux-brain の `aa3edcd5fcbc4697e3b52fde3a1b1f8bd9c94e82` に `build-source-delta.patch` を適用すると、本配布物のソース・設定を復元できます。C/DTSファイルは閲覧用です。

```sh
git clone https://github.com/brain-hackers/linux-brain.git
cd linux-brain
git checkout aa3edcd5fcbc4697e3b52fde3a1b1f8bd9c94e82
git apply /path/to/common-test3/source/build-source-delta.patch
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- olddefconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -j8 zImage modules dtbs
```

Linuxの大文字・小文字を区別するファイルシステムを使用してください。基準はcommon-test3コミット `2c3234d6283428ff1576dd778169c96fe302a43e`。同梱BU26154モジュールは、さらに `headphone-input.patch` を適用し、common-test3の設定・Module.symversを使って外部モジュールとしてビルドしたものです。全体差分にはこの追加修正も含まれます。配布用ソースのGit treeは `f89468fcd0750128879940d0e9ebd8e37c113d70`。再ビルド時の日時等によりバイナリのハッシュは異なり得ます。

元のソース全体やCE DLLは収録しません。ライセンスは各ソースとCOPYINGを参照してください。

YMU818Bのソースは保存版の実装へ戻してあり、音量・PM変更は含みません。YMU818Bのkoは本試験版には同梱しません。カーネルとDTBは従来のcommon-test3ビルドのままです（YMU818Bはモジュール構成のため、カーネル本体に同ドライバーの処理は含まれません）。
