
= Official data layer を使う！

//lead{
Chainlit の official data layer 使ってチャット履歴を保存してみます。
オフィシャルの手順に従って作業してみて気づいたことを紹介します。
//}

//pagebreak

== Official data layer

Chainlit ではチャット履歴を永続化するための data layer として次の 4 つの選択肢があります@<fn>{data-layers}。
//footnote[data-layers][https://docs.chainlit.io/data-layers/overview]

 * Official
 * SQLAlchemy
 * DynamoDB
 * Custom

Official は PostgreSQL と asyncpg ですぐに使える。
SQLAlchemy は PostgreSQL でテストされており、他の DB にも対応できるはず。
DynamoDB は DyanamoDB の場合に良いが single table design などの制限がある。
Custom は BaseDataLayer を継承して必要メソッドを自分で実装する。
ということで、今回は最初に検討される official data layer を試します。

== ハンズオン

オフィシャルの手順@<fn>{chainlit-datalayer}に従って動作確認をします。
本文中のコードやコマンドは要点を抜粋したものです。
//footnote[chainlit-datalayer][https://github.com/Chainlit/chainlit-datalayer]

=== Data layer

Data layer 側の手順は次のようになります。

//emlist[Data layer 側の手順]{
# クローン
git clone https://github.com/Chainlit/chainlit-datalayer.git
cd chainlit-datalayer

# 環境設定
conda create -n data-layer python=3.13
conda activate data-layer
pip install asyncpg boto3
cp .env.example .env

# 起動
docker compose up -d
npx prisma@6.19.0 migrate deploy
npx prisma@6.19.0 studio
//}

asyncpg は python の asyncio 向けの PostgreSQL データベースクライアントライブラリ。
boto3 は python から S3 EC2, DynamoDB を操作するための AWS 公式 SDK。
prisma は Node.js や TypeScript 環境で利用される ORM ツール。

今回は python 3.13 と prisma 6.19 を利用しました。
特に prisma では version 7 から schema.prisma の datasource に url / directUrl を書けなくなったので、
最小変更でテストするために version 6 にする必要がありました。

Data layer 側の .env は DATABASE_URL だけです。

//emlist[Data layer 側の .env]{
DATABASE_URL=postgresql://root:root@localhost:5432/postgres
//}

compose.yml は次のようになっています。

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

PostgreSQL と LocalStack を同時に立ち上げる設定になっています。
PostrgreSQL はリレーショナルデータベース管理システムで、
LocalStack は docker container の中で AWS の各種サービスをエミュレートするツールです。

=== Demo app

Demo app 側の手順は次のようになります。

//emlist[Demo app 側の手順]{
# 環境設定
conda create -n demo python=3.13
conda activate demo
pip install chainlit==2.9.6
pip install asyncpg boto3
cd demo_app
cp .env.template .env
chainlit create-secret

# 起動
chainlit run app.py
//}

asyncpg と boto3 は demo app の環境でも必要です。
今回は python 3.13 と chainlit ２.9.6 を利用しました。
chainlit create-secret の後は出力された secret を .env に追加します。

demo app 側の .env は次のようになっています。

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

先ほど追加した secret の他に DATABASE_URL や S3 の設定があります。

app.py は次のようになっています。

//emlist[app.py]{
import chainlit as cl

@cl.password_auth_callback
def auth_callback(username: str, password: str):
    # Fetch the user matching username from your database
    # and compare the hashed password with the value stored in the database
    if (username, password) == ("admin", "admin"):
        return cl.User(
            identifier="admin", metadata={"role": "admin", "provider": "credentials"}
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

@cl.on_message  # this function will be called every time a user inputs a message in the UI
async def main(message: cl.Message):
    """
    This function is called every time a user inputs a message in the UI.
    It sends back an intermediate response from the tool, followed by the final answer.

    Args:
        message: The user's message.

    Returns:
        None.
    """

    # Call the tool
    tool_res = await tool()

    await cl.Message(content=tool_res).send()
//}

auth_callback ではログイン画面で入力されたユーザー名とパスワードを確認します。
本来は DB からユーザー名とハッシュ化されたパスワードを取得して比較しますが、
今回は簡易的に admin admin になっています。
なので起動したら admin adimn でログインします。

on_chat_resume では何もしていませんが、chainlit が復元した後に追加で何もしない設定です。
tool では 2 秒待ってレスポンスを返す処理を一つの step として定義しています。
main ではユーザーがメッセージを入力した後の処理を設定しています。
今回は tool のレスポンスを返すだけになっています。

=== テスト

今回はチャットで "hello" と打った場合と jpg を添付した場合をテストしました（@<img>{ckato-01}）。
prisma studio でデータが記録されていることを確認できます（@<img>{ckato-02}）。
demo app を再起動した時に過去のスレッドを選択して復元することができました。

//image[ckato-01][チャット][scale=0.9]{
//}

//image[ckato-02][prisma studio][scale=0.9]{
//}

== スキーマ

Official data layer のテーブルは次の 5 つです。

 * User
 * Thread
 * Step
 * Feedback
 * Element

User が Thread を持つ。
Thread が Step を含む。
Step が Feedback や Element を持つ。
という構造になっています。
詳しくはオフィシャルのコードや 
deepwiki@<fn>{deepwiki} が参考になります。
各テーブルは次のようになっています。
//footnote[deepwiki][https://deepwiki.com/Chainlit/chainlit/6.4-data-model] 

//tsize[|table|3cm,3cm,6cm]
//table[user][User]{
Column	Type	Description
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

== オブジェクト永続化

画像などのオブジェクトの永続化については、オフィシャルの方法だと docker compose stop などでコンテナを停止した時点で画像が失われるようです。
LocakStack の永続化には pro 契約が必要そうなので、MinIO@<fn>{minio} という s3 互換のストレージを使う方法を紹介します。
//footnote[minio][https://www.min.io/] 

まず data layer 側の compose.yaml を次のように修正します。

//emlist[compose.yaml の修正]{
services:
  postgres:
    image: postgres:16
    container_name: data-layer-postgres
    volumes:
      - ./.data/postgres:/var/lib/postgresql/data
    environment:
      POSTGRES_USER: ${POSTGRES_USER:-root}
      POSTGRES_PASSWORD: ${POSTGRES_PASSWORD:-root}
      POSTGRES_DB: ${POSTGRES_DB:-postgres}
    ports:
      - "${POSTGRES_PORT:-5432}:5432"

  minio:
    image: quay.io/minio/minio:RELEASE.2025-09-07T16-13-09Z
    container_name: data-layer-minio
    command: server /data --console-address ":9001"
    environment:
      MINIO_ROOT_USER: ${MINIO_ROOT_USER:-minio}
      MINIO_ROOT_PASSWORD: ${MINIO_ROOT_PASSWORD:-minio12345}
    ports:
      - "9000:9000"
      - "9001:9001"
    volumes:
      - ./.data/minio:/data

  minio-init:
    image: quay.io/minio/mc:latest
    depends_on:
      - minio
    entrypoint: >
      /bin/sh -c "
      echo 'Waiting for MinIO...';
      until mc alias set local http://minio:9000 ${MINIO_ROOT_USER:-minio} ${MINIO_ROOT_PASSWORD:-minio12345}; do sleep 2; done;
      mc mb -p local/${BUCKET_NAME:-my-bucket} || true;
      echo 'Bucket ensured';
      exit 0;
      "
    environment:
      BUCKET_NAME: ${BUCKET_NAME:-my-bucket}
//}

LocalStack の代わりに MinIO を用いています。
port 9000 の API と port 9001 のコンソールを用意して init します。

次に demo app 側の .env を次のように修正します。

//emlist[demo app 側の .env の修正]{
CHAINLIT_AUTH_SECRET="xxx"

# To link to the PostgreSQL instance.
DATABASE_URL=postgresql://root:root@localhost:5432/postgres

# S3 configuration (for MinIO)
BUCKET_NAME=my-bucket
APP_AWS_ACCESS_KEY=minio
APP_AWS_SECRET_KEY=minio12345
APP_AWS_REGION=us-east-1
DEV_AWS_ENDPOINT=http://localhost:9000
//}

S3 endpoint を Data layer 側で用意した MinIO の API にしています。
これで docker compose stop や docker compose down の後でも画像こみでチャット履歴を復旧することができました。

== まとめ

Chainlit の official data layer 使ってチャット履歴を保存することができました。
User, Thread, Step, Feedback, Element などのテーブルがあって便利そうです！
