# CLion用のスタブ CMakeLists.txt
# 実際のビルドには使用しません。`pebble build` を使ってください。
# CLionの補完・静的解析のためにPebble SDKのヘッダーパスを指定します。

cmake_minimum_required(VERSION 3.10)
project(doseisan C)

set(CMAKE_C_STANDARD 99)

# Pebble SDK ヘッダーのパス（basaltを基準に指定）
set(PEBBLE_SDK_PATH "$ENV{HOME}/.pebble-sdk/SDKs/current/sdk-core/pebble/basalt/include")

include_directories(${PEBBLE_SDK_PATH})

# ソースファイル
file(GLOB_RECURSE SOURCES "src/c/*.c")

add_executable(${PROJECT_NAME} ${SOURCES})

# Pebble SDK のマクロ定義（CLionの解析用）
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PBL_COLOR
    PBL_RECT
    SCREEN_COLOR_DEPTH_BITS=8
)