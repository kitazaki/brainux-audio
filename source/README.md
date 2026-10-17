# 対応ソースとビルド

上流: https://github.com/brain-hackers/linux-brain

基準コミット: `aa3edcd5fcbc4697e3b52fde3a1b1f8bd9c94e82`

ビルド元コミット: `e1e7f5d1ab7aa1d45fa8134e69dc1702984a8800`（ビルド環境のローカル履歴）。

`build-source-delta.patch` は基準コミットから同梱カーネル・モジュール・DTBのビルド元までの全差分です。ドライバー、SAIFの変更、各機種のDT定義、`.config`、`.scmversion`を含みます。元のカーネルソース全体は収録していません。C/DTSファイルは閲覧用の原本で、差分適用後に追加コピーする必要はありません。

Linuxの大文字・小文字を区別するファイルシステム上で復元します。

```sh
git clone https://github.com/brain-hackers/linux-brain.git
cd linux-brain
git checkout aa3edcd5fcbc4697e3b52fde3a1b1f8bd9c94e82
git apply /path/to/brainux-audio/source/build-source-delta.patch
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- olddefconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -j8 zImage modules dtbs
```

設定・シンボル情報・コンパイラー・ビルドログは `../build-info/` に保存しています。YMU818B用のコーデックとマシンドライバー、BU26154ドライバーはいずれも `=m` です。`CONFIG_TRIM_UNUSED_KSYMS=y` のため、この構成でカーネルとモジュールをまとめてビルドしてください。

各ソースのライセンス表記とルートの `COPYING` を参照してください。
