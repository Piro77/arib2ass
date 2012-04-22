 arib2ass

  https://github.com/Piro77/arib2ass

  TSに含まれる字幕データをASS形式に変換します。
  
  https://skydrive.live.com/?cid=2DAB0D8D07FA4EBF&id=2DAB0D8D07FA4EBF%21117
  にあるvlc用のデコーダ単独で使えるようにしたものです。

  arib2ass --file input.ts

  変換したASSをinput.ts.assとして出力します。


  drcs_conv.ini drcs外字の書き換えファイルです。詳細は上記のURLを参照。
                基本は外字のハッシュ=書き換えたいコードとなります。
  assheader.ini ASSファイルのテンプレートです。

  処理中に変換テーブルにないdrcs文字が現れるとカレントのdataフォルダに
  pngファイルを作成します。
