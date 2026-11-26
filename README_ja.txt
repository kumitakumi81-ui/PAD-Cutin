PAD Cutin 1.0.0
===============
Procedural Armor Damage (PAD) で防具が壊れた瞬間に、スローモーションのカットインを入れる SKSE プラグインです。
カメラワークは Kagura Style Armor Break 1.2.0 (miyata0105, GPL-3.0) の Cutin.h をもとにしています。

■ 前提
・Skyrim SE / AE、SKSE64、Address Library for SKSE Plugins
・Procedural Armor Damage
・(任意) SKSE Menu Framework … 上下の黒帯・周辺を暗く・集中線に必要。無くてもカメラは動きます

■ 入れ方 (MO2)
1. zip を「アーカイブからMODをインストール」で入れる
2. 有効にする (プラグイン一覧の変更は不要。ESP はありません)
3. 設定は SKSE\Plugins\PADCutin.ini (ゲーム中に保存すると読み直されます)
・ログ: Documents\My Games\Skyrim Special Edition\SKSE\PADCutin.log

■ 演出
ヒットストップ → スロー → 壊れた部位へカメラが回り込む → 顔へ移動(傾き・小さな動き) → 元の視点へ
白フラッシュ / 放射ブラー / カメラの揺れ / 上下黒帯 / 周辺を暗く。毎回アングルが変わります。
KSAB と一緒に入れても動きます (同じカメラのフックを順に通ります)。

■ ライセンス
GPL-3.0-or-later (LICENSE.txt)。CommonLibSSE-NG v9.3.0 を使用しています (THIRD_PARTY_NOTICES.txt)。
