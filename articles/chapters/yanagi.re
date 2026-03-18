
= Chainlitで作るAI英会話ツール
//lead{
本章では、ChainlitとOpenAI APIを使った英会話ツールの実装を紹介します。マイクから英語を話すと音声認識でテキスト化され、AIが英語で返答し、その応答を音声で読み上げてくれます。
音声認識を用いたチャットツールの実装に興味がある方や、OpenAI APIが提供する音声関連のモデルを活用してみたい方におすすめの内容です。
//}

//pagebreak

本章では、音声対応のAI英会話ツールを作成します。ユーザーがマイクに向かって英語を話すと、AIによる返答がテキストと音声の両方で返却される仕組みです。
音声の入出力を伴うツールは実装が難しいと思われがちですが、Chainlitを使うと非常にシンプルなコードで実現できます。早速、ツールの全体像から見ていきましょう。

== ツールの全体像

=== どんなツールを作るのか

今回実装する英会話ツールには、以下の機能があります。

 * チャット開始時にAIが3つの会話トピックを提案し、ボタンで選択できる
 * テキスト入力と音声入力の両方に対応している
 * AIの応答は英語テキストに加え、音声（TTS）でも自動再生される
 * 会話終了後に「Review My English」ボタンで、自分の英語力のフィードバックを日本語で受けられる

完成画面は@<img>{eng_conversation_overall}のようになります。

//image[eng_conversation_overall][AI英会話ツールの完成画面][scale=0.8]{
//}

=== 使用する技術

 * @<b>{Chainlit} : チャットUIフレームワーク。音声入出力やアクションボタンなど、リッチなUI機能を標準で備えています。
 * @<b>{OpenAI API} : GPT-4o-miniによるチャット応答、Whisper APIによる音声認識（STT）、GPT-4o-mini-ttsによる音声合成（TTS）を利用します。

=== ファイル構成

プロジェクトの主要なファイル構成は以下のとおりです。
//emlist{
ch07-eng-conversation/
├── app.py              # アプリケーション本体
├── pyproject.toml      # 依存パッケージ定義
├── .env                # OpenAI APIキー
└── .chainlit/
    └── config.toml     # Chainlitの設定ファイル
//}

@<code>{app.py}にすべてのロジックが集約されており、約230行のシンプルな構成です。Chainlitの設定は@<code>{config.toml}で行い、特に音声入力に関するパラメータをここで制御します。

== 環境構築

=== プロジェクトのセットアップ
サポートページをcloneし、本章のプロジェクトディレクトリに移動してください。
//emlist{
$ git clone https://github.com/statditto/chainlit-techbook-support.git
$ cd ch07-eng-conversation
//}

uvを使って依存パッケージをインストールします。ChainlitとOpenAIのPythonクライアントがインストールされます。
//cmd{
$ uv sync
//}

本プロジェクトではOpenAIのAPIキーを使用するため、OpenAIのサイト@<fn>{openai-site}からAPIキーを発行し、@<code>{.env}ファイルに設定してください。

//footnote[openai-site][OpenAI APIキー発行ページ: @<href>{https://platform.openai.com/account/api-keys}]

//emlist[.env]{
OPENAI_API_KEY=sk-proj-your-api-key-here
//}

=== アプリケーションの起動

実際にアプリケーションを起動するには、以下のコマンドを実行します。

//cmd{
$ uv run chainlit run app.py
//}

== Chainlitでの音声入力
それでは、実装についての解説に入ります。
はじめに、本章の最も重要なトピックである音声入力の実装に入ります。Chainlitはブラウザのマイク入力に対応しており、音声データをリアルタイムにサーバーへストリーミングできます。この仕組みを活用して、ユーザーの音声をテキストに変換します。

=== config.tomlでの音声設定

まず、@<code>{config.toml}で音声入力を有効化し、パラメータを設定します。

//emlist[.chainlit/config.toml - 音声設定]{
[features.audio]
    enabled = true
    sample_rate = 24000
    min_decibels = -45
    initial_silence_timeout = 3000
    silence_timeout = 1500
    max_duration = 15000
    chunk_duration = 1000
//}

各パラメータの意味は以下のとおりです。

 * @<code>{sample_rate} : 音声のサンプリングレート（Hz）。後述するPCM→WAV変換時にもこの値を使います。
 * @<code>{min_decibels} : 音声と判定する最小デシベル値。値を小さくすると小さな声でも検知します。
 * @<code>{initial_silence_timeout} : 録音開始から最初の音声が検出されるまでの待機時間（ミリ秒）
 * @<code>{silence_timeout} : 発話後に無音が続いた場合に録音を終了するまでの時間（ミリ秒）
 * @<code>{max_duration} : 最大録音時間（ミリ秒）。ここでは15秒に設定しています。
 * @<code>{chunk_duration} : 音声データをサーバーに送信するチャンクの単位時間（ミリ秒）

=== 音声入力の処理

Chainlitの音声入力は、3つの処理で構成されます。

//emlistnum[app.py - 音声入力ハンドラ][python]{
@cl.on_audio_start
async def on_audio_start():
    """音声録音開始時にバッファを初期化する。"""
    cl.user_session.set("audio_buffer", b"")
    return True

@cl.on_audio_chunk
async def on_audio_chunk(chunk: cl.InputAudioChunk):
    """音声チャンクをバッファに蓄積する。"""
    audio_buffer = cl.user_session.get("audio_buffer")
    audio_buffer += chunk.data
    cl.user_session.set("audio_buffer", audio_buffer)

@cl.on_audio_end
async def on_audio_end():
    """録音終了時に音声を文字起こしし、LLM に送信する。"""
    audio_buffer: bytes = cl.user_session.get("audio_buffer")
    if not audio_buffer:
        await cl.Message(
            content="No audio was recorded. Please try again."
        ).send()
        return

    wav_data = pcm_to_wav(audio_buffer)

    audio_file = ("recording.wav", wav_data, "audio/wav")
    transcription = await client.audio.transcriptions.create(
        model=STT_MODEL, file=audio_file, language="en",
    )
    user_text = transcription.text.strip()
    if not user_text:
        await cl.Message(
            content="Could not transcribe audio. Please try again."
        ).send()
        return

    await cl.Message(
        content=user_text, author="You", type="user_message"
    ).send()
    await generate_response(user_text)
//}

処理の流れを整理すると、以下のようになります。

 1. @<b>{on_audio_start} : ユーザーがマイクボタンを押すと呼ばれます。空のバイト列をセッションにセットしてバッファを初期化します。@<code>{return True}で録音を許可します。
 2. @<b>{on_audio_chunk} : ブラウザから音声データがチャンク単位で送られてくるたびに呼ばれます。@<code>{chunk.data}にPCM形式の生バイナリデータが入っており、これをバッファに追記していきます。
 3. @<b>{on_audio_end} : 録音が終了すると呼ばれます。蓄積したPCMデータをWAV形式に変換し、Whisper APIに送信して文字起こしを行います。

=== PCMからWAVへの変換

Chainlitのブラウザ側から送られてくる音声データは生のPCM（Pulse Code Modulation）形式です。今回音声認識で使用するWhisper APIはWAVファイルを受け付けるため、変換処理が必要です。

//emlistnum[app.py - PCM→WAV変換][python]{
def pcm_to_wav(
    pcm_data: bytes, sample_rate: int = SAMPLE_RATE,
    channels: int = 1, sample_width: int = 2
) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(sample_width)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_data)
    buf.seek(0)
    return buf.read()
//}

Pythonの標準ライブラリ@<code>{wave}を使い、PCMデータにWAVヘッダを付与しています。@<code>{sample_rate}は@<code>{config.toml}で設定した値と一致させる必要がある点に注意してください。

== 音声認識と応答生成

この節では、音声入力で得たデータをWhisper APIで文字起こしし、LLMで応答を生成し、さらにTTSで音声として返すまでの一連の流れを解説します。

=== Whisper APIによる音声認識

@<code>{on_audio_end}内で、WAVに変換した音声データをOpenAIのWhisper APIに送信します。

//emlistnum[app.py - Whisper APIによる文字起こし][python]{
@cl.on_audio_end
async def on_audio_end():
    """録音終了時に音声を文字起こしし、LLM に送信する。"""
    audio_buffer: bytes = cl.user_session.get("audio_buffer")
    if not audio_buffer:
        await cl.Message(
            content="No audio was recorded. Please try again."
        ).send()
        return

    wav_data = pcm_to_wav(audio_buffer)

    audio_file = ("recording.wav", wav_data, "audio/wav")
    transcription = await client.audio.transcriptions.create(
        model=STT_MODEL, file=audio_file, language="en",
    )
    user_text = transcription.text.strip()
    if not user_text:
        await cl.Message(
            content="Could not transcribe audio. Please try again."
        ).send()
        return

    await cl.Message(
        content=user_text, author="You", type="user_message"
    ).send()
    await generate_response(user_text)
//}

@<code>{client.audio.transcriptions.create}がWhisper APIの呼び出しです。今回は英語を想定しているため、@<code>{language="en"}を指定しています。

文字起こし結果は@<code>{transcription.text}に文字列として格納されます。空文字列の場合は音声が認識できなかったことを意味するため、エラーメッセージを表示して処理を中断しています。

=== システムプロンプトの設計

音声認識でテキスト化されたユーザーの発言は、LLMに送信されて応答が生成されます。システムプロンプトは以下のように設計しており、ユーザーにとって難しい返答になりすぎないよう、語彙のレベルや応答の長さを調整するよう指示しています。

//emlist[app.py - システムプロンプト][python]{
SYSTEM_PROMPT = """\
You are a friendly and encouraging English conversation partner.
Your role is to help the user practice English through natural conversation.

Rules:
- Always respond in English, even if the user writes in another language.
- If the user speaks in another language,
  gently encourage them to try in English.
- Keep your responses concise (2-4 sentences)
  to encourage back-and-forth dialogue.
- Naturally correct grammar or vocabulary mistakes
  by rephrasing, without being preachy.
- Adjust your vocabulary level
  to match the user's apparent proficiency.
- Be warm, patient, and positive.
"""
//}

=== 応答生成と音声合成
今回は返答をそのままテキストで表示するだけでなく、音声で返す必要があります。
LLMでテキスト応答を生成した後にTTS APIを呼び出してテキストを音声に変換し、@<code>{cl.Audio}要素でメッセージに添付して返しています。

//emlistnum[app.py - 応答生成][python]{
async def generate_response(user_text: str) -> None:
    messages: list[dict] = cl.user_session.get("messages")
    messages.append({"role": "user", "content": user_text})

    response = await client.chat.completions.create(
        model=CHAT_MODEL, messages=messages,
    )
    assistant_text = response.choices[0].message.content
    messages.append(
        {"role": "assistant", "content": assistant_text}
    )
    cl.user_session.set("messages", messages)

    audio_bytes = await text_to_speech(assistant_text)
    audio_el = cl.Audio(
        name="response.mp3", content=audio_bytes,
        display="inline", auto_play=True,
    )
    review_action = cl.Action(
        name="review_conversation",
        label="📝 Review My English", payload={}
    )
    await cl.Message(
        content=assistant_text,
        elements=[audio_el], actions=[review_action]
    ).send()
//}

ポイントは@<code>{cl.Audio}要素です。@<code>{auto_play=True}を指定すると、メッセージが表示されると同時に音声が自動再生されます。@<code>{display="inline"}でメッセージ内に音声プレーヤーが埋め込み表示されるため、ユーザーは再度聞き返すこともできます。

実際の画面では、@<img>{eng_conversation_response}のように表示されます。

//image[eng_conversation_response][AIの応答メッセージと音声プレーヤー][scale=0.8]{
//}

=== テキスト読み上げ（TTS）

音声合成にはOpenAIのTTS APIを使用しています。

//emlistnum[app.py - TTS][python]{
async def text_to_speech(text: str) -> bytes:
    response = await client.audio.speech.create(
        model=TTS_MODEL, voice=TTS_VOICE, input=text,
        instructions="Speak clearly at a moderate pace, "
        "with a friendly and encouraging tone. "
        "This is for an English conversation practice session.",
    )
    return response.content
//}

@<code>{instructions}パラメータで話し方のトーンを指定できます。英会話練習用なので、はっきりとしたペースで親しみやすく話すよう指示しています。

== 便利機能の追加

ここまでで音声入力から応答生成・読み上げまでの中核機能を解説しました。本章の最後に、英会話ツールとしての使い勝手を向上させる2つの便利機能を紹介します。

=== トピック選択機能
英会話を始める際に、ユーザーが話す内容を決めるのは意外と難しいものです。
そこで、チャットを開始したときにAIが会話のトピックを提案し、ユーザーがその中から話したいものを選べるような仕組みを作ります。
Chainlitでは@<code>{@cl.on_chat_start}デコレータを使って、チャット開始時の処理を定義できます。本ツールでは、セッション開始時にAIが3つの会話トピックを自動生成し、ボタンとして提示します。

//emlistnum[app.py - チャット開始処理][python]{
@cl.on_chat_start
async def on_chat_start():
    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    cl.user_session.set("messages", messages)

    topic_response = await client.chat.completions.create(
        model=CHAT_MODEL,
        messages=[{"role": "system", "content": TOPIC_PROMPT}],
    )
    topics = json.loads(
        topic_response.choices[0].message.content.strip()
    )

    actions = [
        cl.Action(
            name="select_topic",
            label=f"{i+1}. {topic}",
            payload={"topic": topic},
        )
        for i, topic in enumerate(topics)
    ]

    await cl.Message(
        content="Welcome! Let's practice English conversation.\n"
        "Please choose a topic you'd like to talk about:",
        actions=actions,
    ).send()
//}

@<code>{cl.Action}を使うと、メッセージにクリック可能なボタンを添付できます。各ボタンには@<code>{payload}でデータを渡せるため、どのトピックが選択されたかを後続のコールバックで受け取れます。ボタンがクリックされると@<code>{@cl.action_callback("select_topic")}で登録した関数が呼び出され、選択されたトピックをもとにAIが会話を開始します。

実際の画面では、@<img>{eng_conversation_topic}のようにボタンが表示されます。

//image[eng_conversation_topic][トピック選択ボタンが表示されたチャット画面][scale=0.8]{
//}

=== 会話レビュー機能
せっかくなので、単純に話すだけでなく、自分の英語のどこが良いのか、どこを改善すればいいのかフィードバックをもらえるような機能も実装してみましょう。
本ツールでは、すべてのAI応答に「Review My English」ボタンが添付されています。このボタンを押すと、@<code>{@cl.action_callback("review_conversation")}で登録したコールバック関数が呼び出され、これまでの会話履歴をもとにユーザーの英語力をレビューしてくれます。

//emlistnum[app.py - 会話レビュー][python]{
@cl.action_callback("review_conversation")
async def on_review(action: cl.Action):
    """会話のレビューを生成する。"""
    messages: list[dict] = cl.user_session.get("messages")

    user_messages = [
        m for m in messages if m["role"] == "user"
    ]
    if not user_messages:
        await cl.Message(
            content="まだ会話がありません。"
            "まず英語で話してみましょう！"
        ).send()
        return

    conversation_text = "\n".join(
        f"{'Student' if m['role'] == 'user' else 'Partner'}"
        f": {m['content']}"
        for m in messages
        if m["role"] in ("user", "assistant")
    )

    review_response = await client.chat.completions.create(
        model=CHAT_MODEL,
        messages=[
            {"role": "system", "content": REVIEW_PROMPT},
            {"role": "user",
             "content": "Here is the conversation to review:"
             f"\n\n{conversation_text}"},
        ],
    )
    review_text = review_response.choices[0].message.content
    await cl.Message(content=review_text).send()
//}

会話履歴からユーザーの発言とAIの応答を@<code>{Student}/@<code>{Partner}の形式に整形し、レビュー用のプロンプトとともにLLMに送信しています。レビューのプロンプトでは、以下の5つの観点での評価を日本語で出力するよう指示しています。

 1. 全体的な評価
 2. 文法の問題点と訂正
 3. 不自然な表現の改善提案
 4. 良かった点
 5. 改善のためのアドバイス

レビュー結果は日本語で出力されるため、英語学習者にとって理解しやすい内容になっています。レビュー完了後は会話履歴がリセットされ、新しいトピックが提案されるため、何度でも繰り返し練習できます。

レビュー結果は@<img>{eng_conversation_review}のように表示されます。

//image[eng_conversation_review][英会話レビューの結果画面][scale=0.8]{
//}

== まとめ

本章では、Chainlitの音声入出力機能とOpenAI APIを組み合わせて、約230行のPythonコードで音声対応のAI英会話ツールを実装しました。

Chainlitでは、音声の入力や出力に関する処理を非常にシンプルに実装できるため、容易に音声ツールの開発が可能です。

本章のコードをベースに、学習言語を英語以外に変えたり、会話トピックを増やしたり、レビューの内容をカスタマイズするなどして、自分だけのAI会話ツールを作ってみてください。
