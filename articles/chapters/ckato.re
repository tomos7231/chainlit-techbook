
= Official data layer を使う！

//lead{
Chainlit の official data layer 使ってチャット履歴を保存してみます。
オフィシャルのガイドに従って作業してみて気づいたことを紹介します。
//}

//pagebreak

== Official data layer

Chainlit ではチャット履歴を永続化するための data layer として次の 4 つの選択肢があります@<fn>{data-layers}。
//footnote[data-layers][https://docs.chainlit.io/data-layers/overview]

 * Official: PostgreSQL と S3 互換のストレージで最も手軽に利用可能です。
 * SQLAlchemy: SQLAlchemy の抽象化により、他の DB にも対応可能です。
 * DynamoDB: AWS に適しますが、単一テーブルなどの制限があります。
 * Custom: BaseDataLayer クラスを継承し、必要なメソッドを実装します。

今回はデフォルトの選択肢として提供されている official data layer の標準的な実装を試します。

== ハンズオン

オフィシャルのガイドに従って動作確認をします@<fn>{chainlit-datalayer}。
なお、本章で紹介するコードやコマンドは解説のために要点を抜粋したものです。
//footnote[chainlit-datalayer][https://github.com/Chainlit/chainlit-datalayer]

=== Data layer

Data layer 側のセットアップは次のようになります。

//emlist[Data layer 側のセットアップ]{
# リポジトリのクローン
git clone https://github.com/Chainlit/chainlit-datalayer.git
cd chainlit-datalayer

# 環境設定
uv venv --python 3.13.4
uv pip install asyncpg boto3
source .venv/bin/activate
cp .env.example .env

# データベースの起動
docker compose up -d
npx prisma@6.19.0 migrate deploy
npx prisma@6.19.0 studio
//}

本章で利用している主要なライブラリおよびツールについて補足します。

 * asyncpg: asyncio 向けの PostgreSQL DB クライアントです。
 * Boto3: AWS の S3 などを操作するための公式 SDK です。
 * Prisma: Node.js や TypeScript 環境で利用される ORM ツールです。

今回は Python 3.13 と Prisma 6.19 を利用しました。
特に Prisma ではバージョン 7 から @<code>{schema.prisma} の仕様に変更があったため、
最小限の変更でテストをするためにバージョン 6 を選択しました。

Data layer 側の @<code>{.env} には接続先の @<code>{DATABASE_URL} のみ記述されています。

//emlist[Data layer 側の .env]{
DATABASE_URL=postgresql://root:root@localhost:5432/postgres
//}

@<code>{compose.yml} は次のようになっています。

//emlist[compose.yml]{
services:
  postgres:
    image: postgres:16
    volumes:
      - ./.data/postgres:/var/lib/postgresql/data
    environment:
      - POSTGRES_USER=${POSTGRES_USER:-root}
      - POSTGRES_PASSWORD=${POSTGRES_PASSWORD:-root}
      - POSTGRES_DB=${POSTGRES_DB:-postgres}
    ports:
      - ${POSTGRES_PORT:-5432}:5432
  localstack:
    image: localstack/localstack:latest
    environment:
      SERVICES: s3
    ports:
      - 4566:4566
    volumes:
      - ./localstack-script.sh:/etc/localstack/init/ready.d/script.sh
      - "/var/run/docker.sock:/var/run/docker.sock"
//}

PostgreSQL はリレーショナルデータベースとして機能し、
LocalStack は Docker コンテナの中で S3 などの AWS をエミュレートします。

=== Demo app

続いて、Data layer と連携する demo app 側のセットアップをします。

//emlist[Demo app 側のセットアップ]{
# 環境設定
cd demo_app
uv venv --python 3.13.4
uv pip install asyncpg boto3 chainlit==2.9.6
source .venv/bin/activate
cp .env.template .env
chainlit create-secret

# デモアプリの起動
chainlit run app.py
//}

@<code>{chainlit create-secret} の後は、出力された secret を @<code>{.env} に追加します。
Demo app 側の @<code>{.env} は最終的に次のような構成になります。

//emlist[demo_app 側の .env]{
CHAINLIT_AUTH_SECRET="xxx"

# To link to the PostgreSQL instance.
DATABASE_URL=postgresql://root:root@localhost:5432/postgres

# S3 configuration.
BUCKET_NAME=my-bucket
APP_AWS_ACCESS_KEY=random-key
APP_AWS_SECRET_KEY=random-key
APP_AWS_REGION=eu-central-1
DEV_AWS_ENDPOINT=http://localhost:4566
//}

先ほど追加した secret の他に DATABASE_URL や S3 の設定が含まれています。

メインの @<code>{app.py} は次のようになっています。

//emlist[app.py]{
import chainlit as cl

@cl.password_auth_callback
def auth_callback(username: str, password: str):
    # Fetch the user matching username from your database
    # and compare the hashed password with the value stored in the database
    if (username, password) == ("admin", "admin"):
        return cl.User(
            identifier="admin",
            metadata={"role": "admin", "provider": "credentials"}
        )
    else:
        return None

@cl.on_chat_resume
async def on_chat_resume(thread):
    pass

@cl.step(type="tool")
async def tool():
    # Fake tool
    await cl.sleep(2)
    return "Response from the tool!"

# this function will be called every time a user inputs a message in the UI
@cl.on_message
async def main(message: cl.Message):
    """
    This function is called every time a user inputs a message in the UI.
    It sends back an intermediate response from the tool, 
    followed by the final answer.

    Args:
        message: The user's message.

    Returns:
        None.
    """

    # Call the tool
    tool_res = await tool()

    await cl.Message(content=tool_res).send()
//}

実装のポイントは次の通りです。

 * @<code>{auth_callback}: ログイン画面での認証ロジックです。本来は DB からユーザー名とハッシュ化されたパスワードを取得して比較しますが、今回は簡易的に @<code>{admin,admin} でログインできるようになっています。
 * @<code>{on_chat_resume}: 過去のチャット履歴が復元された際に実行されますが、今回は特に処理を追加していません。
 * @<code>{tool}: 2 秒待ってレスポンスを返す処理を一つの step として定義しています。@<code>{@cl.step} デコレータを付与することで、Chainlit の UI 上でツールが動いている状態をステップとして可視化できます。
 * @<code>{main}: メインのチャットの処理です。ユーザーのメッセージを受け取ったあと、今回は @<code>{tool} のレスポンスを返すのみになっています。

=== テスト

今回はチャットでのテキスト送信と画像の添付をテストしました（@<img>{ckato-01}）。
Prisma Studio でデータが記録されていることを確認できます（@<img>{ckato-02}）。
また、demo app を再起動した後も、過去のスレッドを選択して会話を復元できることを確認しました。

//image[ckato-01][チャット][scale=1.0]{
//}

//image[ckato-02][Prisma Studio][scale=1.0]{
//}

== スキーマ

Official data layer のテーブルは次の 5 つです。

 * User: チャットを利用するユーザー情報
 * Thread: 会話のスレッド単位のデータ
 * Step: 会話内のメッセージやツールの実行など、個別のステップを管理
 * Feedback: メッセージやステップへのユーザー評価
 * Element: メッセージに付随する画像ファイルなどの要素を管理

詳しくはオフィシャルのコードや 
deepwiki@<fn>{deepwiki} が参考になります。
各テーブルは次のようになっています。
//footnote[deepwiki][https://deepwiki.com/Chainlit/chainlit/6.4-data-model] 

//tsize[|table|3cm,3cm,6cm]
//table[user][User]{
@<b>{Column}	@<b>{Type}	@<b>{Description}
------------	------------	------------
id	UUID/String	主キーとなる識別子
identifier	String	一意なユーザー識別子（メールアドレスやユーザー名など）
metadata	JSON	ユーザープロファイル情報やカスタム属性
createdAt	Timestamp	アカウント作成日時
updatedAt	Timestamp	最終更新日時
//}

//table[thread][Thread]{
@<b>{Column}	@<b>{Type}	@<b>{Description}
id	UUID/String	主キー
userId	UUID/String	User への外部キー
userIdentifier	String	検索用に保持するユーザー識別子（非正規化）
name	String	スレッドの表示名
metadata	JSON	セッション状態などのカスタムデータ
tags	Array/JSON	スレッド分類用タグ
createdAt	Timestamp	スレッド作成日時
updatedAt	Timestamp	最終アクティビティ日時
deletedAt	Timestamp	論理削除日時（NULL 可）
//}

//table[step][Step]{
@<b>{Column}	@<b>{Type}	@<b>{Description}
id	UUID/String	主キー
threadId	UUID/String	Thread への外部キー
parentId	UUID/String	親 Step への外部キー（NULL 可）
name	String	ステップの表示名
type	String	ステップ種別（user_message, tool など）
input	Text/JSON	入力内容
output	Text/JSON	出力内容
metadata	JSON	表示設定やタグなどのメタ情報
generation	JSON	LLM の生成情報（モデル名、トークン数など）
startTime	Timestamp	ステップ開始時刻
endTime	Timestamp	ステップ終了時刻
createdAt	Timestamp	作成日時
showInput	String	入力の表示モード
isError	Boolean	エラーかどうか
streaming	Boolean	ストリーミング対応かどうか
waitForAnswer	Boolean	ユーザー入力待ちかどうか
language	String	コード表示時の言語指定
//}

//table[feedback][Feedback]{
@<b>{Column}	@<b>{Type}	@<b>{Description}
id	UUID/String	主キー
forId	UUID/String	対象 Step への外部キー
value	Float/Numeric	数値による評価値
name	String	フィードバック種別名
comment	String	任意のコメント
createdAt	Timestamp	作成日時
updatedAt	Timestamp	更新日時
threadId	UUID/String	Thread への外部キー
//}

//table[element][Element]{
@<b>{Column}	@<b>{Type}	@<b>{Description}
id	UUID/String	主キー
threadId	UUID/String	Thread への外部キー
forId	UUID/String	関連する Step への外部キー（NULL 可）
type	String	要素の種別
url	String	要素データの保存先 URL
objectKey	String	ストレージ内の参照キー
name	String	要素名
display	String	表示モード（inline/page/side など）
size	String	サイズ情報（任意）
mime	String	MIME タイプ（任意）
language	String	コード要素の言語指定（任意）
page	Integer	ドキュメントのページ番号（任意）
props	JSON	追加プロパティ
metadata	JSON	要素のメタデータ
//}

== まとめ

Chainlit の official data layer 使ってチャット履歴を保存することができました。
PostgresQL と S3 互換のストレージがあればチャットを復元できて、
User, Thread, Step, Feedback, Element などのテーブルがあって便利そうです！