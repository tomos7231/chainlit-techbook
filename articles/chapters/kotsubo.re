
= AIが今何をしているか？で不安にならないようにしよう！

//lead{
LLMがツールを使って複雑な処理をこなすようになった今、応答が返るまでの「待ち時間」はユーザー体験の一つの課題です。本章では、Chainlit の @<code>{cl.Step} を起点に、Markdownによる構造化・@<code>{cl.TaskList} によるタスクの表示・@<code>{cl.Plotly} による可視化の強化・@<code>{asyncio.gather} を活用したメッセージの並行表示という4つのアプローチで待ち時間の不安を解消する方法をいくつか紹介します。
//}

== 背景

LLM の精度向上とツールによる機能拡張が進むにつれ、1回の応答にかかる時間が長くなる傾向があります。処理の過程を何も表示しないと、ユーザーは「本当に動いているのか？」という不安を抱くことがあります。また、長時間待った後に返ってきた結果が意図するものと大きく異なるケースも、ユーザー体験としてあまりよくありません（@<img>{kotsubo/no_step}）。本章では、過程をわかりやすく表示する工夫と、待ち時間を有効活用する工夫を紹介します。

//image[kotsubo/no_step][思考の過程を表示しない例][scale=0.8]{
//}

== 本章でわかること

 * Chainlit の @<code>{cl.Step} の基本的な使い方
 * Step の出力を Markdown でリッチに表示する方法
 * @<code>{cl.TaskList} による全体進捗のサイドバー表示
 * @<code>{cl.Plotly} によるインタラクティブなチャート表示
 * @<code>{asyncio.gather} を使った並行処理で待ち時間を有効活用する方法

== 本章で利用するアプリケーション

本章では、複数のトピックを検索するリサーチアプリケーション @<fn>{support} を例に、Chainlit の @<code>{cl.Step} 機能を中心に説明します。このアプリケーションは、ユーザーがブラウザから質問を送ると、OpenAI の @<code>{web_search_preview} ツールを使って複数のトピックをウェブ検索し、結果を集約して回答を返すチャットボットです。処理の流れは次のとおりです。

//footnote[support][本章のソースコードは以下から参照できます。@<href>{https://github.com/takuto0831/chainlit-playground}]

 1. ユーザーのクエリから調査トピックを3つ生成する
 2. 各トピックについてウェブ検索を行い、ソースごとに要約・信頼度を付与する
 3. 全調査結果を集約して最終回答をストリーミング表示する

1つのメッセージに対して複数回の LLM 呼び出しとウェブ検索が走るため、応答までに数十秒かかることもあります。この待ち時間をどう扱うかが本章のテーマです。

== リサーチアプリケーションの実装

=== @<code>{cl.Step} とは？

Chainlit には @<b>{Step（ステップ）} という概念があります。処理の「中間状態」を UI に表示するための仕組みで、LLM が裏で何をしているかをユーザーに見せたいときに使います。

@<code>{cl.Step} はコンテキストマネージャとして使用します。@<code>{async with} ブロックに入ると UI 上にステップが開始され、ブロックを抜けると完了状態になります。

//emlist[@<code>{cl.Step}の基本的な使い方]{
async with cl.Step(name="処理名", type="tool") as step:
    step.input = "入力内容"
    # ... 何らかの処理を実行 ...
    result = await some_process()
    step.output = result
//}

@<code>{type} パラメータには @<code>{"tool"}・@<code>{"llm"}・@<code>{"retrieval"} などを指定でき、UI 上のアイコン表示に影響します。各値の使い分けは次のとおりです。

 * @<code>{"tool"} --- 関数呼び出しや外部 API など、ツールの実行を表します。本章のリサーチツールでは、ウェブ検索全体やトピックごとの調査ステップに使用しています。
 * @<code>{"llm"} --- LLM への推論リクエストを表します。プロンプトの送信や回答の生成など、モデルそのものへの呼び出しを示したいときに使います。
 * @<code>{"retrieval"} --- ドキュメントや URL からの情報取得を表します。本章ではウェブ検索の各ソースへのアクセスに使用しています。

また、@<code>{async with} をネストすることで階層構造を表現できます。

//emlist[入れ子にした@<code>{cl.Step}の使い方]{
async with cl.Step(name="親", type="tool") as parent:
    async with cl.Step(name="子", type="retrieval") as child:
        child.output = "子の結果"
    parent.output = "親の結果"
//}

=== ベース実装

本章のアプリケーションでは、次のような3段階の入れ子構造で @<code>{cl.Step} を構成しています。

//emlist[リサーチアプリケーションのベース実装]{
@cl.on_message
async def main(message: cl.Message) -> None:
    query = message.content
    all_findings: list[str] = []

    async with cl.Step(
            name="ウェブを調査しています", type="tool"
        ) as root_step:
        root_step.input = query

        topics = await generate_topics(query)

        for topic in topics:
            async with cl.Step(
                name=f"「{topic}」を調査中", type="tool"
            ) as topic_step:
                topic_step.input = f"「{topic}」の観点で調査"

                sites = await research_topic(query, topic)

                for site in sites:
                    async with cl.Step(
                        name=f"{site['name']}", type="retrieval"
                    ) as site_step:
                        site_step.input = site["url"]
                        site_step.output = site["summary"]

                    all_findings.append(
                        f"**[{topic}｜{site['name']}]** {site['summary']}"
                    )

                topic_step.output = f"{len(sites)} 件のソースを確認しました"

        root_step.output = f"合計{len(all_findings)} 件のソースを調査しました"
//}

@<code>{generate_topics} は LLM にユーザーのクエリを渡し、調査すべき観点を複数の文字列（@<code>{topic}）として返す関数です。たとえば「Chainlit とは何か」というクエリに対して @<code>{["基本概念", "主な機能", "活用事例"]} のようなリストが返ります。各 @<code>{topic} に対してトピック Step が1つ生成され、その中でウェブ検索が実行されます。

//emlist[@<code>{generate_topics}で利用しているプロンプト]{
messages=[
    {
        "role": "system",
        "content": (
            "あなたはリサーチアシスタントです。JSONのみを返してください。"
        ),
    },
    {
        "role": "user",
        "content": (
            f"「{query}」を調査するための主要なトピックを3つ挙げてください。"
            '{"topics": ["トピック1", "トピック2", "トピック3"]}'
            " の形式で返してください。"
        ),
    },
],
//}

@<code>{cl.Step} のツリー構造は次のようになっています。

//emlist[@<code>{cl.Step} のツリー構造]{
▼ ウェブを調査          ← root_step（type="tool"）
  ▼ 「基本概念」を調査      ← topic_step（type="tool"）
    ▼ Wikipedia              ← site_step（type="retrieval"）
    ▼ 公式ドキュメント         ← site_step（type="retrieval"）
  ▼ 「応用例」を調査        ← topic_step（type="tool"）
    ...
[最終回答]          ← Step の外で送信
//}

実行すると @<img>{kotsubo/ui_base} のように、処理の階層が折りたたみ UI として表示されます。「何かが動いている」ことが視覚的にわかるようになりました。

//image[kotsubo/ui_base][基本的な Step 利用時の表示例][scale=0.8]{
//}

== Markdown によるステップ出力の構造化

=== ベース実装からの変更内容

ベース実装では、トピック Step の @<code>{output} に「2 件のソースを確認しました」という単純なテキストを設定していました。ここを Markdown のテーブル形式に変更します。

//emlist[Markdown テーブルを生成するフォーマッター]{
def fmt_topic_output(_topic: str, sites: list[dict]) -> str:
    rows = "\n".join(
        f"| [{s['name']}]({s['url']}) | `{urlparse(s['url']).netloc}`"
        f" | {s['reliability']} |"
        for s in sites
    )
    return (
        f"| ソース | ドメイン | 信頼度 |\n"
        f"|--------|----------|--------|\n"
        f"{rows}\n\n"
        f"> **{len(sites)} 件**のソースを確認しました"
    )
//}

@<code>{topic_step.output = fmt_topic_output(topic, sites)} と差し替えるだけで適用できます（@<img>{kotsubo/ui_markdown2}）。なお信頼度は、LLM に対して★の数（1〜5個）で返すよう指示することで付与しています。

//image[kotsubo/ui_markdown2][Markdown フォーマットを導入した Step 表示の例][scale=0.8]{
//}

//emlist[@<code>{research_topic}での文章の要約と信頼度を返すプロンプト]{
{
    "role": "user",
    "content": (
        f"以下の調査結果と参考URL一覧をもとに情報ソースを最大3件まとめてください。\n\n"
        f"調査結果:\n{output_text}\n\n"
        f"参考URL:\n{citations_text}\n\n"
        '{"sources": [{"name": "ソース名", "url": "実際のURL",'
        ' "summary": "そのソースの要約", "reliability": "★～★★★★★"}]}'
        " の形式で返してください。"
        "urlは参考URL一覧にある実際のURLをそのまま使ってください。"
        "reliabilityは情報源の信頼度を★の数（1〜5個）で表してください。"
    ),
},
//}

=== メリット

 * @<b>{1クリックで全ソースを表示}：サイト Step を個別に展開しなくても、トピック Step の output 一覧だけで調査先と信頼度が把握できます
 * @<b>{ソース名がリンク化}：クリックで実際の URL に飛べます
 * @<b>{ドメインをコードブロックで表示}：@<code>{`ja.wikipedia.org`} の形式で情報源の出所が一目で判断できます
 * @<b>{信頼度が視覚的}：★の数でスキャン等が可能になります

== TaskList によるサイドバー進捗表示

=== ベース実装からの変更内容

Step のツリー表示は処理の「詳細」を確認するためのものです。トピック数が増えると、どこまで処理が終わっているかが一目でわかりません。@<code>{cl.TaskList} と @<code>{cl.Task} を追加し、サイドバーに全体進捗を表示します。

//emlist[TaskList と Task の追加]{
@cl.on_message
async def main(message: cl.Message) -> None:
    task_list = cl.TaskList()
    task_list.status = "調査中..."
    await task_list.send()

    # ...（Step のネスト構造はそのまま）

    for topic in topics:
        task = cl.Task(title=f"「{topic}」を調査中",
                       status=cl.TaskStatus.RUNNING)
        await task_list.add_task(task)
        await task_list.update()

        async with cl.Step(...) as topic_step:
            # ... 処理 ...
            pass

        task.status = cl.TaskStatus.DONE
        await task_list.update()
//}

各トピックの処理開始時に @<code>{RUNNING} で追加し、完了時に @<code>{DONE} へ更新することで、リアルタイムな進捗が反映されます。

@<img>{kotsubo/ui_tasklist} は1つ目のトピック処理中の状態、@<img>{kotsubo/ui_tasklist2} は1つ目のトピックが完了した後の状態です。

//image[kotsubo/ui_tasklist][タスク一覧の表示例（1つ目のタスクの処理開始中）][scale=0.8]{
//}

//image[kotsubo/ui_tasklist2][タスク一覧の表示例（1つ目のタスクの処理終了後）][scale=0.8]{
//}

=== メリット

 * @<b>{スクロールせずに全体進捗を可視化s}：サイドバーは常時表示されるため、ユーザーが Step ツリーをスクロールしていても進捗を確認できます
 * @<b>{「今どのトピックを処理中か」が明確}：実行中のタスクがハイライト表示されます
 * @<b>{完了数の把握}：何件中何件が終わったかが一目でわかります

== Plotly ヒートマップによる信頼度の可視化

=== ベース実装からの変更内容

全トピックの調査完了後に、Plotly のインタラクティブなヒートマップを @<code>{cl.Plotly} で表示します。前の節で紹介したのと同様に信頼度（★の数）を数値化し、「トピック × ソース番号」のマトリクスとして色で表現します。

//emlist[Plotly ヒートマップの表示]{
all_sites: list[dict] = []  # チャート用：topic 付きで全ソースを蓄積

# ... 省略 ...

if all_sites:
    fig = make_reliability_chart(all_sites)
    await cl.Message(
        content="**情報ソース 信頼度チャート**",
        elements=[
            cl.Plotly(name="reliability_chart", figure=fig, display="inline")
        ],
    ).send()
//}

@<code>{make_reliability_chart} では @<code>{plotly.graph_objects.Heatmap} を使い、★の個数をセルの色として表現しています。セルにはソース名と信頼度が重ねて表示されます。（@<img>{kotsubo/ui_chart}）

//image[kotsubo/ui_chart][ヒートマップによる信頼度の可視化][scale=0.8]{
//}

=== メリット

 * @<b>{どのトピックのどのソースが高品質かを視覚的に把握}：テキストで並んだ★と異なり、色による比較は直感的です
 * @<b>{インタラクティブ}：Plotly のチャートはホバーでツールチップが表示され、ズームや拡大も可能です
 * @<b>{全体感の把握}：複数トピックにわたるソースの信頼度分布をまとめて確認できます


== @<code>{asyncio.gather}を活用したメッセージの並行表示

=== ベース実装からの変更内容

@<code>{asyncio.gather} を使って、「トピック生成」と「豆知識生成」を並行実行します。トピック生成が完了した時点で豆知識も揃っており、リサーチ開始と同時にユーザーへ関連情報を表示できます。

//emlist[並行実行と豆知識の即時表示]{
@cl.on_message
async def main(message: cl.Message) -> None:
    query = message.content

    # トピック生成と豆知識生成を並行実行
    topics, trivia = await asyncio.gather(
        generate_topics(query),
        generate_trivia(query),
    )

    # 豆知識を即時表示
    await cl.Message(
        content=(
            f"{trivia}\n\n"
            f"---\n"
            f"*リサーチを開始しました。結果が出るまでしばらくお待ちください...*"
        )
    ).send()

    # 以降は通常どおりリサーチを実行
    async with cl.Step(...):
        ...
//}

@<code>{generate_topics} は「リサーチアプリケーションの実装」節で紹介した関数です。@<code>{generate_trivia} はそれと並行して実行する新しい関数で、クエリに関連する豆知識を1件返します。

//emlist[@<code>{generate_trivia} で利用しているプロンプト]{
messages=[
    {
        "role": "system",
        "content": (
            "あなたは博識なアシスタントです。"
            "与えられたテーマに関連する、知っていると少し得する面白い豆知識を1つ、"
            "日本語で3〜5文程度で教えてください。"
            "「💡 豆知識：」で始めてください。"
        ),
    },
    {
        "role": "user",
        "content": f"「{query}」に関連する豆知識を1つ教えてください。",
    },
],
//}

@<code>{generate_topics} と並行して実行されるため、処理を待っている間に豆知識を読むことができます。（@<img>{kotsubo/ui_trivia}）

//image[kotsubo/ui_trivia][豆知識の表示例][scale=0.8]{
//}

=== メリット

 * @<b>{待ち時間を退屈させない}：ユーザーがリサーチ結果を待つ間、テーマに関連した読み物を提供できます
 * @<b>{追加のレイテンシがほぼゼロ}：豆知識生成はトピック生成と並行して走るため、シーケンシャルな場合と比べて待ち時間はほとんど増えません
 * @<b>{「処理が始まった」という安心感}：最初に何かが表示されることで、ユーザーはアプリが応答したと認識できます

== まとめ

本章では、LLM アプリケーションの待ち時間に対するユーザー体験を向上する4つのアプローチを紹介しました。以下に各内容とメリットをまとめます。

//table[summary][ベース実装からの変更内容とメリットの一覧]{
変更内容	主なメリット
----------------
Step 出力のテーブル形式化	ソース・信頼度が展開不要で一覧できる
サイドバーに進捗リストを表示	全体の何件中何件が完了したか一目でわかる
信頼度ヒートマップを表示	ソース品質の比較が色で直感的にできる
豆知識の並行表示	待ち時間を有効活用し初動表示を早める
//}

それぞれの修正は独立していて、組み合わせて使うことも単独で利用することも可能です。
4つのアプローチは「処理の過程を見せる工夫（@<code>{cl.Step}・@<code>{cl.TaskList}・@<code>{cl.Plotly}）」と「待ち時間そのものを有意義にする工夫（豆知識の並行表示）」という2つの方向性に整理できます。
過程の表示を工夫するだけでなく、待ち時間そのものを有意義なものに変える発想が、LLM アプリケーションの UX 改善に活用できます。ぜひ皆さんのアプリにも取り入れてみてください！
