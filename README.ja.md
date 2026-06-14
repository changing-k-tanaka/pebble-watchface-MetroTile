# MetroTile

Windows Metro UI にインスパイアされた Pebble スマートウォッチ用ウォッチフェイスです。2列×3行の6つの設定可能なタイルで構成され、それぞれのタイルに異なる情報を表示し、色を自由にカスタマイズできます。

## App Store

MetroTile - Pebble Appstore https://apps.repebble.com/metrotile_1491156b8f6f46b1b305ec7c

## スクリーンショット

|               Emery (Pebble Time 2)               |               Gabbro (Pebble Round 2)               |               Aplite (Pebble Classic)               |
|:-------------------------------------------------:|:---------------------------------------------------:|:---------------------------------------------------:|
| ![Emery](market/screenshot/ver2/pebble_emery.png) | ![Gabbro](market/screenshot/ver2/pebble_gabbro.png) | ![Aplite](market/screenshot/ver2/pebble_aplite.png) |

### カラープリセット

|                       Default                       |                            Nishikigoi                             |                     Evangelion                      |                            Moby Dick                            |
|:---------------------------------------------------:|:-----------------------------------------------------------------:|:---------------------------------------------------:|:---------------------------------------------------------------:|
| ![Default](market/screenshot/ver2/pebble_emery.png) | ![Nishikigoi](market/screenshot/ver2/pebble_emery_nishikigoi.png) | ![EVA](market/screenshot/ver2/pebble_emery_eva.png) | ![Moby Dick](market/screenshot/ver2/pebble_emery_moby_dick.png) |

## 機能

- **6つの設定可能なタイル** (2×3グリッド)
- **11種類のタイルコンテンツ**: 曜日、日付、時刻、年、バッテリー、歩数、心拍数、気温、降水確率、天気、または空白
- **タイルごとのカラーカスタマイズ**: 背景色と文字色をそれぞれ独立して設定可能
- **カラープリセット**: DEFAULT、NISHIKIGOI、MARIO、EVANGELION、DORAEMON、MOBY DICK、PIKA、Custom
- **JSONカラーインポート/エクスポート**: カラースキームの共有・再利用が可能
- **リアルタイム天気情報**: Open-Meteo API を使用した現在の気温・天気・降水確率の表示（無料、APIキー不要）
- **ヘルスデータ**: 歩数と心拍数（対応ハードウェアのみ）
- **全7プラットフォーム対応**: aplite、basalt、chalk、diorite、emery、flint、gabbro
- **丸型ディスプレイ対応**: chalk・gabbro 向けのセーフエリア内接表示
- **モノクロディスプレイ対応**: aplite・diorite 向けの自動パレット調整
- **Bluetooth切断バイブレーション** (オン/オフ設定可能)
- **設定の永続化**: ウォッチフェイス再起動後も設定を保持

## 設定

スマートフォンの Pebble アプリから設定ページを開いて以下を設定できます。

| 設定項目 | 選択肢 |
|---------|--------|
| タイルコンテンツ | なし / 時刻 / 日付 / 曜日 / 年 / 心拍数 / 歩数 / 天気 / バッテリー / 気温 / 降水確率 |
| タイル背景色 | 64色パレット（カラーモデル）または 黒・グレー・白（モノクロモデル） |
| タイル文字色 | 64色パレット（カラーモデル）または 黒・グレー・白（モノクロモデル） |
| カラープリセット | DEFAULT / NISHIKIGOI / MARIO / EVANGELION / DORAEMON / MOBY DICK / PIKA / Custom |
| 日付フォーマット | MM/DD または DD/MM |
| 気温単位 | 摂氏（℃）または 華氏（℉） |
| Bluetooth切断バイブ | オン / オフ |

## タイルコンテンツの種類

| 種類 | 説明 |
|------|------|
| なし (None) | 空白タイル |
| 時刻 (Time) | 現在時刻（HH:MM） |
| 日付 (Date) | 現在の日付 |
| 曜日 (Day) | 曜日 |
| 年 (Year) | 現在の年 |
| 心拍数 (Heart Rate) | ヘルスセンサーからの BPM（Pebble 2 / Time 2 のみ） |
| 歩数 (Steps) | ヘルスセンサーからの歩数 |
| 天気 (Weather) | 天気の状態と都市名 |
| バッテリー (Battery) | バッテリー残量（%） |
| 気温 (Temperature) | 現在の気温と単位 |
| 降水確率 (Precipitation) | 降水確率（%） |

## 対応プラットフォーム

| プラットフォーム | モデル | 解像度 | カラー |
|----------------|--------|--------|--------|
| emery | Pebble Time 2 | 200×228 | 64色 |
| gabbro | Pebble Round 2 | 260×260 | 64色 |
| basalt | Pebble Time | 144×168 | 64色 |
| chalk | Pebble Time Round | 180×180 | 64色 |
| flint | Pebble 2 | 144×168 | 64色 |
| aplite | Pebble Classic | 144×168 | モノクロ |
| diorite | Pebble 2 SE | 144×168 | モノクロ |

## ビルド

### 前提条件

- [Pebble SDK](https://developer.rebble.io/developer.pebble.com/sdk/index.html) のインストール
- Python 3

### コマンド

```bash
# 全プラットフォーム向けビルド
pebble build

# エミュレーターでインストール・起動（Pebble Time 2）
pebble install --emulator emery

# ログの確認
pebble logs --emulator emery

# スクリーンショットの取得
pebble screenshot --no-open --emulator emery
```

ビルドされた PBW ファイルは `build/metrotile.pbw` に出力されます。

## 天気情報

天気データは [Open-Meteo](https://open-meteo.com/)（無料のオープンソース天気API）から取得します。APIキーは不要です。位置情報はスマートフォンのGPSを使用します。天気は30分ごとに更新されます。

## ライセンス

[LICENSE.md](LICENSE.md) を参照してください。
