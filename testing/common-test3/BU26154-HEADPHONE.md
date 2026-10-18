> この資料はtest3作成時の設計記録です。最新の変更・実機確認状況はREADME.mdとsource/headphone-input.patchを参照してください。

# BU26154 ヘッドフォン音量・手動出力切替の調査

2026-10-17。ユーザー報告:

- SH3〜SH5: Speaker/Headphoneの切替は可能だがHeadphoneの音量が変わらない。
- SH6/SH7: Headphoneを選ぶと無音。
- ヘッドフォンの切替は手動とし、自動切替を実装しない。

## 現行コードで確認した点

- Output RouteはMAP0 0x26のSPEN(bit1)だけをSOC_ENUMで変更していた。
- Playback VolumeはMAP0 0x3aのアナログ音量だけを操作していた。
- デジタル音量MAP0 0x3eはprobeで0xe7に固定され、ミキサーから操作できなかった。
- prepareは両方のHPアンプを含むMAP0 0x20=0xcaを固定で書き、出力ごとの電源・経路設定を持たなかった。
- MAP2のヘッドフォン用レベルシフターを明示的に設定していなかった。

この不足は報告された症状の手掛かりになるが、実機での原因確定・修正効果の確認とは分ける。

## データシートとCEの裏付け

ROHM BU26154MUV Rev.002 (2015)、主な参照ページ:

- [28ページ](https://htmlapp.alldatasheet.com/html-pdf/873342/ROHM/BU26154MUV/2934/28/BU26154MUV.html): COEFSELによってデジタル音量・係数のA/Bバンクが切り替わる。
- [49ページ](https://htmlapp.alldatasheet.com/html-pdf/873342/ROHM/BU26154MUV/5139/49/BU26154MUV.html): SPEN、SPMDSEL、AVLEN/AVREN、COEFSEL。
- [51ページ](https://htmlapp.alldatasheet.com/html-pdf/873342/ROHM/BU26154MUV/5349/51/BU26154MUV.html): 0x3aのアナログ音量、0x3eとMAP2 0x72のデジタル音量。
- [52ページ](https://htmlapp.alldatasheet.com/html-pdf/873342/ROHM/BU26154MUV/5454/52/BU26154MUV.html): PDATT=0x6fはmute、0x70は-71.5dB。0x00〜0x6eは禁止値であり、ミキサーの0をそのまま書いてはいけない。
- [73ページ](https://htmlapp.alldatasheet.com/html-pdf/873342/ROHM/BU26154MUV/7659/73/BU26154MUV.html): MAP2 0x04にヘッドフォン用レベルシフター。34ページのオーディオ使用例は0x22→0x26で有効にしている。

取得を試みたPDFの一部は403/404で、上記のメーカー資料の検索インデックスと保存済みCE逆コンパイルを照合した。

SH6 CE `wavedev2_BU26154.dll` の `c07ab4c0` は、出力変更時にsoft muteをかけてから以下を操作する。

- Speaker側: MAP0 0x20=0x0a、MAP2 0x20=4/7（プロファイルによる）、MAP1 0x20、MAP0 0x26=0x8d→0x8f。
- Headphone側: MAP0 0x26=4、MAP1 0x20=0、MAP2 0x20=4/7、MAP0 0x20=0xca。

同DLLの初期化はMAP2 0x04を0x22または0x26にする。したがって、GPIOによる起動制御後にヘッドフォン経路が未設定のままという可能性がある。

SH6とSH7の保存済み逆コンパイル結果は、以前の調査で一致を確認している。

## 修正候補（common-test3）

- Outputは手動のキャッシュ付きコントロールへ変更。停止中は選択を記憶し、prepare時に適用。再生中はmute→経路変更→mute復元の順で切り替える。
- Headphone側でMAP1 0x20=0、MAP2のレベルシフター有効化、MAP2 0x20=4、MAP0 0x26=4、MAP0 0x20=0xcaを設定する。
- 初回の経路設定時にMAP1/MAP2のスピーカー用設定を保存し、HeadphoneからSpeakerへ戻す際に復元する。機種固有の設定を固定値で置き換えない。
- Speaker側では従来のAB/D級指定と係数Aを維持し、SPENを一旦0にしてから設定・有効化する。HPアンプは停止し、MAP0 0x20=0x0aとする。
- CEの0x8d/0x8fは係数Bを選ぶが、Linux側ではその係数をロードしていないため、そこは流用しない。両出力で係数Aを使う。
- 新しい `Headphone Playback Volume` は0〜120。値に0x6fを加えてPDATTへ書く。0=mute、120=-12dB。初期値48=-48dBで、従来の固定-12dBより小さくする。
- Speakerのデジタル音量は従来どおり0xe7に戻す。従来の `Playback` コントロールはスピーカーのアナログ音量として維持する。
- Headphone音量は非選択・停止中もキャッシュし、選択・再生開始時に適用する。I2C失敗ではキャッシュを変更しない。
- MAP切替と全ミキサー・prepare/hw_params/shutdownを同じmutexで直列化し、別MAPへの誤アクセスを防ぐ。エラー時もMAP0へ戻し、経路変更失敗ではアンプを停止してエラーを返す。

MAP2のチャージポンプ設定4はCEの通常側に合わせた試験値であり、機種固有のプロファイル差や追加要因が残る可能性がある。実機確認前に修正済みと断定しない。

## 検証と次の手順

`test-bu26154-headphone.py` で実コードの関数を抽出し、バンクを持つregmapモックで以下を確認した。

- 両出力の設定値、SPEN停止後のAB/D級切替。
- Headphone 0/120のエンコードが0x6f/0xe7となり、禁止値を書かない。
- 停止中・非選択側・録音中はHeadphone値やOutput選択をキャッシュだけ更新する。
- 64箇所までの個別I2C失敗を注入し、失敗時のMAP0復元・出力停止・キャッシュ保持・ロック解放を確認。
- YMU818B音量・PMの既存モック試験も再実行して成功。

本試験版での実機再生・ヘッドフォン切替・音量変更は未確認。まずSH6/SH7で無音の改善を確認し、SH3〜SH5で切替と音量の回帰確認を行う。YMU818BのGPIO検出・自動切替は追加していない。GitHubの安定版は変更していない。

試験版での手動操作例（安定版test1にはHeadphone音量コントロールは存在しない）:

```sh
amixer -c BrainBU26154 sset Output Headphone
amixer -c BrainBU26154 sset Headphone 48
mpg123 -o alsa -a 'plughw:CARD=BrainBU26154,DEV=0' -r 44100 -f 4096 ~/after_2.mp3
```

音量が小さければ、再生中にHeadphoneを60、72などへ段階的に上げて確認する。0で消音し、元の値へ戻ることを確認する。Speakerへ戻す場合はOutput=Speaker、Playback=2とする。
