"""
English Conversation Partner - Chainlit App
音声・テキスト入力に対応した英会話練習チャットUI
"""

import io
import json
import wave

import chainlit as cl
from dotenv import load_dotenv
from openai import AsyncOpenAI

load_dotenv()

client = AsyncOpenAI()

# ── 定数 ────────────────────────────────────────────
CHAT_MODEL = "gpt-4o-mini"
TTS_MODEL = "gpt-4o-mini-tts"
TTS_VOICE = "alloy"
STT_MODEL = "whisper-1"
SAMPLE_RATE = 24000  # config.toml の sample_rate と合わせる

SYSTEM_PROMPT = """\
You are a friendly and encouraging English conversation partner.
Your role is to help the user practice English through natural conversation.

Rules:
- Always respond in English, even if the user writes in another language.
- If the user speaks in another language, gently encourage them to try in English.
- Keep your responses concise (2-4 sentences) to encourage back-and-forth dialogue.
- Naturally correct grammar or vocabulary mistakes by rephrasing, without being preachy.
- Adjust your vocabulary level to match the user's apparent proficiency.
- Be warm, patient, and positive.
"""

TOPIC_PROMPT = """\
Suggest exactly 3 interesting and approachable English conversation topics.
Return ONLY a JSON array of 3 strings, each being a short topic title (5-10 words max).
Example: ["Favorite travel destinations and experiences", "Movies you watched recently", "Your dream job and why"]
Do not include any other text, just the JSON array.
"""

REVIEW_PROMPT = """\
You are an English language tutor giving direct feedback to your student.
Below is the conversation history from the practice session you just had together.

Review ONLY the student's (user's) messages and give feedback directly to them using "you/your" (second person).

Please provide:
1. **総合評価**: Briefly tell the student how they did overall.
2. **文法の指摘**: Point out grammatical errors, show the correction, and explain why in a supportive tone.
3. **語彙・表現の改善**: Highlight unnatural expressions and suggest more natural alternatives.
4. **良かった点**: Praise specific things the student did well — be encouraging.
5. **改善のヒント**: Give 2-3 concrete, actionable tips for the student to improve.

Write the entire review in Japanese so the student can easily understand.
Address the student directly (e.g., 「〜と言っていましたが」「あなたの〜は素晴らしいです」).
If there are few or no issues, acknowledge that and encourage them to keep going.
"""


# ── 共通ユーティリティ ──────────────────────────────────
async def text_to_speech(text: str) -> bytes:
    """OpenAI TTS API でテキストを音声に変換し、mp3 バイト列を返す。"""
    response = await client.audio.speech.create(
        model=TTS_MODEL,
        voice=TTS_VOICE,
        input=text,
        instructions="Speak clearly at a moderate pace, with a friendly and encouraging tone. This is for an English conversation practice session.",
    )
    return response.content


async def generate_response(user_text: str) -> None:
    """ユーザー入力を受け取り、LLM 応答を生成して音声付きメッセージを送信する。"""
    # 会話履歴を取得し、ユーザーメッセージを追加
    messages: list[dict] = cl.user_session.get("messages")
    messages.append({"role": "user", "content": user_text})

    # LLM 呼び出し
    response = await client.chat.completions.create(
        model=CHAT_MODEL,
        messages=messages,
    )
    assistant_text = response.choices[0].message.content

    # 会話履歴にアシスタント応答を追加
    messages.append({"role": "assistant", "content": assistant_text})
    cl.user_session.set("messages", messages)

    # TTS で音声生成
    audio_bytes = await text_to_speech(assistant_text)
    audio_el = cl.Audio(
        name="response.mp3",
        content=audio_bytes,
        display="inline",
        auto_play=True,
    )

    # レビューボタン付きでメッセージ送信（テキスト + 音声）
    review_action = cl.Action(name="review_conversation", label="📝 Review My English", payload={})
    await cl.Message(content=assistant_text, elements=[audio_el], actions=[review_action]).send()


def pcm_to_wav(pcm_data: bytes, sample_rate: int = SAMPLE_RATE, channels: int = 1, sample_width: int = 2) -> bytes:
    """生 PCM バイト列を WAV フォーマットに変換する。"""
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(sample_width)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_data)
    buf.seek(0)
    return buf.read()


# ── Chainlit ライフサイクル ──────────────────────────────
@cl.on_chat_start
async def on_chat_start():
    """セッション開始時にトピック候補を3つ提示し、ユーザーに選ばせる。"""
    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    cl.user_session.set("messages", messages)

    # LLM にトピック候補を3つ生成させる
    topic_response = await client.chat.completions.create(
        model=CHAT_MODEL,
        messages=[{"role": "system", "content": TOPIC_PROMPT}],
    )
    try:
        topics = json.loads(topic_response.choices[0].message.content.strip())
    except json.JSONDecodeError:
        topics = [
            "Favorite travel destinations and experiences",
            "Movies or TV shows you enjoyed recently",
            "Your hobbies and weekend activities",
        ]

    # トピック選択ボタンを表示
    actions = [
        cl.Action(
            name="select_topic",
            label=f"{i+1}. {topic}",
            payload={"topic": topic},
        )
        for i, topic in enumerate(topics)
    ]

    await cl.Message(
        content="👋 Welcome! Let's practice English conversation.\nPlease choose a topic you'd like to talk about:",
        actions=actions,
    ).send()


@cl.action_callback("select_topic")
async def on_select_topic(action: cl.Action):
    """ユーザーがトピックを選択した時の処理。"""
    topic = action.payload["topic"]

    # 選択を表示
    await cl.Message(content=f"📌 Topic: **{topic}**", author="You", type="user_message").send()

    # システムプロンプトにトピックを追加して最初のメッセージを生成
    messages: list[dict] = cl.user_session.get("messages")
    messages.append(
        {"role": "user", "content": f"Let's talk about: {topic}. Please start the conversation about this topic."}
    )

    response = await client.chat.completions.create(
        model=CHAT_MODEL,
        messages=messages,
    )
    assistant_text = response.choices[0].message.content
    messages.append({"role": "assistant", "content": assistant_text})
    cl.user_session.set("messages", messages)

    # TTS で音声生成
    audio_bytes = await text_to_speech(assistant_text)
    audio_el = cl.Audio(
        name="greeting.mp3",
        content=audio_bytes,
        display="inline",
        auto_play=True,
    )

    # レビューボタン付きで送信
    review_action = cl.Action(name="review_conversation", label="📝 Review My English", payload={})
    await cl.Message(content=assistant_text, elements=[audio_el], actions=[review_action]).send()


@cl.action_callback("review_conversation")
async def on_review(action: cl.Action):
    """会話のレビューを生成する。"""
    messages: list[dict] = cl.user_session.get("messages")

    # ユーザーのメッセージがあるか確認
    user_messages = [m for m in messages if m["role"] == "user"]
    if not user_messages:
        await cl.Message(content="まだ会話がありません。まず英語で話してみましょう！").send()
        return

    # 会話履歴をフォーマット
    conversation_text = "\n".join(
        f"{'Student' if m['role'] == 'user' else 'Partner'}: {m['content']}"
        for m in messages
        if m["role"] in ("user", "assistant")
    )

    # レビュー生成
    review_response = await client.chat.completions.create(
        model=CHAT_MODEL,
        messages=[
            {"role": "system", "content": REVIEW_PROMPT},
            {"role": "user", "content": f"Here is the conversation to review:\n\n{conversation_text}"},
        ],
    )
    review_text = review_response.choices[0].message.content

    await cl.Message(content=f"---\n## 📝 English Conversation Review\n\n{review_text}\n\n---").send()

    # レビュー後に新しいトピックを提案
    topic_response = await client.chat.completions.create(
        model=CHAT_MODEL,
        messages=[{"role": "system", "content": TOPIC_PROMPT}],
    )
    try:
        topics = json.loads(topic_response.choices[0].message.content.strip())
    except json.JSONDecodeError:
        topics = [
            "Favorite travel destinations and experiences",
            "Movies or TV shows you enjoyed recently",
            "Your hobbies and weekend activities",
        ]

    # セッションリセット
    new_messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    cl.user_session.set("messages", new_messages)

    actions = [
        cl.Action(
            name="select_topic",
            label=f"{i+1}. {topic}",
            payload={"topic": topic},
        )
        for i, topic in enumerate(topics)
    ]

    await cl.Message(
        content="Would you like to practice more? Choose a new topic:",
        actions=actions,
    ).send()


@cl.on_message
async def on_message(message: cl.Message):
    """テキスト入力を処理する。"""
    await generate_response(message.content)


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
        await cl.Message(content="No audio was recorded. Please try again.").send()
        return

    # PCM → WAV 変換
    wav_data = pcm_to_wav(audio_buffer)

    # Whisper API で文字起こし（英語を指定して英語のまま出力）
    audio_file = ("recording.wav", wav_data, "audio/wav")
    transcription = await client.audio.transcriptions.create(
        model=STT_MODEL,
        file=audio_file,
        language="en",
    )
    user_text = transcription.text.strip()

    if not user_text:
        await cl.Message(content="Could not transcribe audio. Please try again.").send()
        return

    # ユーザーの発言を表示
    await cl.Message(content=user_text, author="You", type="user_message").send()

    # LLM 応答を生成
    await generate_response(user_text)
