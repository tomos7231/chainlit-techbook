= ChainlitでLLMを観察しよう!

== ChainlitはLLMをクイックに「観察」できるツール
チャットUI付きのLLMアプリケーションを簡単に作るためのフレームワーク、というイメージが強いChainlit。
本章では、LLMの入出力を手軽にUI上に表示できるという強みを活かして、開発者がLLM/Agentの挙動を観察・デバッグするためのUIフレームワークとして利用する例を紹介します。

LLMアプリケーションの観察に特化したツールはいくつか存在しますが、セキュリティ面の制約などから、その環境の準備が難しいこともあります。そのような状況でもまずはクイックにLLMの挙動の観察を始めてみたい、という方には、今回紹介するChainlitを利用した方法が役に立つのではないかと思います。

==== ハンズオンの準備
ここから先のハンズオンでは、実際にChainlitを動かしていきます。

本章で取り扱うソースコードはサポートページ @<fn>{support} で公開していますので、ハンズオンに取り組みたい方はリポジトリを実行環境にクローンし、 @<code>{uv sync} で環境を構築してください。
//footnote[support][@<href>{https://github.com/y-uty/chainlit-observation}]
//emlist[ハンズオン環境構築][bash]{
git clone https://github.com/y-uty/chainlit-observation.git
cd chainlit-observation
uv sync
//}

ChainlitがLLMを呼び出すために必要な環境変数(APIキー)も設定しておいてください。
//emlist[][bash]{
cp .env.example .env
# .env ファイルを編集して OPENAI_API_KEY を設定
//}

== Chainlitの基本：観察可能性の観点から見る

まず、Chainlitの基本機能を使ってLLMを呼び出しつつ、それを観察可能にするためのシンプルな実装を見ていきます。

=== Chainlitの基本機能 
==== 1. @cl.on_message デコレータ
ユーザーからメッセージが送信されるたびに実行される関数を定義します。

//emlist[][python]{
@cl.on_message
async def main(message: cl.Message):
    # メッセージ処理のロジック
//}


==== 2. cl.Step で処理を構造化
cl.Step は、処理を構造化して観察可能にするための重要な機能です。
//emlist[][python]{
async with cl.Step(name="LLM呼び出し", type="llm") as step:
    # 処理内容
//}

cl.StepのパラメータにはUIに表示されるステップ名称 @<code>{name} や、ステップの種類を指定する @<code>{type} などがあります。

 :  @<code>{type="llm"}
 LLM呼び出し
 :  @<code>{type="tool"}
 ツール呼出し
 :  @<code>{type="run"}
 Agent実行
 :  @<code>{type="embedding"}
 Embedding生成
 :  @<code>{type="retrieval"}
 RAGなどからの情報検索

==== 3. 入出力の記録
@<code>{step.input} と @<code>{step.output} を使って、ステップの入出力を記録します。
//emlist[][python]{
step.input = message.content  # LLMに送る内容
# ... API呼び出し ...
step.output = response_text   # LLMから受け取った内容
//}

これにより、後から「何を入力して、何が出力されたか」を確認できます。


==== 4. メタデータの記録
@<code>{step.metadata} を使って、追加情報を記録します。
//emlist[][python]{
step.metadata = {
    "model": response.model,
    "prompt_tokens": response.usage.prompt_tokens,
    "completion_tokens": response.usage.completion_tokens,
    "total_tokens": response.usage.total_tokens,
    "finish_reason": response.choices[0].finish_reason,
}
//}
記録したメタデータは、トークン数の推移を追跡してコストを見積もる、異なるモデル間のパフォーマンスを比較する、@<code>{finish_reason} でエラーやトークン上限到達を検出する、などの活用方法があります。

=== ハンズオン: LLM呼び出しの観察
観察コード付きのChainlitを実際に動かして、LLMに何か質問して結果を確認してみましょう。
以下のコマンドでChainlitを起動し、ブラウザで @<href>{http://localhost:8000} を開いてください。
//emlist[][bash]{
uv run chainlit run examples/01_basic_observability/app.py
//}

下図を見ると、通常のLLMからの回答だけでなく、「使用済み LLM呼び出し」と表示され折りたたまれたブロックがあるのがわかります。ここを開くと、メタデータを含む、ステップに記録した情報が確認できます。
//image[ch2_response][質問への回答が表示された状態]{
//}

今回は、メタデータの文字列をJSON形式に変換し、@<code>{step.output}と結合して結果を表示しています。

//emlist[app.py抜粋][python]{
        step.metadata = metadata
        metadata_json = json.dumps(metadata, ensure_ascii=False, indent=2)

        # メタデータをStepの出力にも含める（UIで確実に表示されるように）
        step.output = "\n".join([
            response_text,
            "",
            "---",
            "**Metadata:**",
            "```json",
            metadata_json,
            "```",
        ])
//}

ChainlitはJSONなどの既定のデータ構造を受け取ると、見やすい形で描画してくれます。
//image[ch2_jsondisp][JSON形式で描画されたメタデータ]{
//}


== Multi-Step Agentの観察
この例では、Function Calling（Tool Calling）を使うAgentで、複数のツールを呼び出す様子を階層的なStepで観察します。

=== 階層的なステップの構造
以下のようにステップを階層的にネストすることで、複雑な処理の流れを可視化できます。
これによってAgent全体の実行フローが見やすくなり、どのLLM呼び出しがどのツール呼び出しを引き起こしたかが明確になります。

//emlist[cl.Stepのネスト][python]{
async with cl.Step(name="Agent実行", type="run") as agent_step:
    # 親ステップ

    async with cl.Step(
        name="LLM呼び出し",
        type="llm",
        parent_id=agent_step.id) as llm_step:
        # 子ステップ

    async with cl.Step(
        name="ツール実行",
        type="tool",
        parent_id=agent_step.id) as tool_step:
        # 別の子ステップ
//}

=== ハンズオン: ツール呼び出しの観察
実際のAgent実装を動かして、階層化されたステップでツール実行を観察できることを確認しましょう。
以下のコマンドでChainlitを起動し、ブラウザで @<href>{http://localhost:8000} を開いてください。
//emlist[][bash]{
uv run chainlit run examples/02_multi_step_agent/app.py
//}

Agentは以下の流れで動作します。
//emlist[][]{
1. ユーザーからのメッセージを受け取る
2. LLMにメッセージとツール定義を送る
3. LLMがツール呼び出しを返す場合:
   a. ツールを実行
   b. 実行結果をLLMに返す
   c. 2に戻る
4. LLMが最終回答を返したら終了
//}

天気予報APIからのデータ取得を模したダミー処理の @<code>{get_weather} 関数と、2つの数の加算値を返す @<code>{add} 関数をツールとして用意しています。

今回は「東京と大阪の最高気温の差は？」と質問することにします。LLMはそれぞれの最高気温を @<code>{get_weather} で、その差の計算を @<code>{add} で行ったあと、最終的な回答を出力します。

以下に示すように、ステップがネストして複数表示されていることと、各ステップでのメタデータが表示されていることが確認できるはずです。

今回は処理内で時間を計測してレイテンシも計算しています。複雑なMulti-Step Agentがレイテンシの課題を抱えているとき、どのツールがボトルネックになっているかを特定するのに役立ちます。

//image[ch301_nested_steps][ステップの階層. 各LLM呼び出しはさらにTool callの子ステップを持つ]{
//}

//image[ch302_llmcall01][LLM呼び出し1回目]{
//}

//image[ch303_getwether_tokyo][東京の天気情報を取得]{
//}

//image[ch304_getwether_osaka][大阪の天気情報を取得]{
//}

//image[ch305_llmcall02][LLM呼び出し2回目]{
//}

//image[ch306_add][加算]{
//}

//image[ch307_llmcall03][LLM呼び出し3回目(最終回答)]{
//}



== 応用例: プロンプトエンジニアリングのための観察
最後に、メタデータを用いた観察をプロンプトエンジニアリングに応用する例を紹介します。

テキストをLLMで要約する機能を考えます。要約自体はLLMを使えば比較的単純なタスクですが、要約処理にかけてよいコストや、どのような要約形式が良いのかは要件ごとに異なります。

そこで、以下の4つの要約スタイルをプロンプトとしてLLMに指示し、その要約結果と、トークン数・レイテンシを比較してみます。
//emlist[][]{
シンプル: 最小限の指示（ベースライン）
詳細指示: 具体的な要件や制約を含む
Few-shot: 例示を通じて期待する出力形式を示す
Chain-of-Thought: 段階的な思考プロセスを促す
//}

Few-shotとは、単に「〜〜してください」と指示するのではなく、数件程度の具体例を含んだプロンプトを用いて指示を与えることで、タスクの精度を向上させるプロンプトエンジニアリング手法です。たとえば、以下のようなプロンプトを用意します。
//emlist[Few-shotの例][]{
例1:
テキスト: 「人工知能は近年急速に発展しており、様々な分野で活用されています。特に自然言語処理の分野では、GPTなどの大規模言語モデルが登場し、人間のような文章生成が可能になりました。」
要約: AI技術、特に自然言語処理分野で大規模言語モデルが発展し、人間レベルの文章生成が実現。
//}


=== ハンズオン: 要約指示プロンプトの比較
以下のコマンドでChainlitを起動し、ブラウザで @<href>{http://localhost:8000} を開いてください。
//emlist[][bash]{
uv run chainlit run examples/03_prompt_comparison/app.py
//}

要約させたいテキストを自由に入力してください。要約性能を見るには、ある程度の長さがある方がよいでしょう。サンプルの実装では100字に満たない場合はエラーとなります。

以下に掲載する例では、「都市交通と自転車」をテーマとした1,700字程度のテキストを要約させています。サンプルコードに含めているので、適当なテキストがない場合はこちらを使ってください。
//emlist[summarize_sample.txt(冒頭抜粋)][]{
近年、都市部では自転車の利用が急速に増加している。背景には、環境問題への関心の高まりや健康志向の広がりに加え、都市交通の混雑を緩和する手段として自転車が注目されていることがある。特に大都市では、通勤時間帯の道路渋滞や公共交通機関の混雑が慢性的な問題となっており、短距離移動の代替手段として自転車を利用する人が増えている。また、スマートフォンアプリと連動したシェアサイクルサービスの普及により、自転車を所有していない人でも手軽に利用できる環境が整いつつある。 ... (以下略)
//}

テキストを入力すると、4つのスタイルそれぞれに対する要約結果と、パフォーマンス比較の表が表示されます。@<fn>{cond}
//footnote[cond][モデルの性能によって結果が大きく異なる可能性があります。掲載例では、OpenAIのモデル gpt-5.2 を使用しています。]

//image[ch401_res_detail][要約結果の例(詳細指示)]{
//}

//image[ch402_res_compare][スタイルごとの比較]{
//}

レイテンシはFew-shotが最速、トークン数は詳細指示が最小となったようです。Chain-of-Thoughtは、段階的なプロセスを踏ませている分、トークン数もレイテンシも他のスタイルより大きいことがわかります。実際の要約結果とあわせて見比べながら、どのスタイルを採用するかの意思決定の参考にしたり、使っているプロンプトの性能改善をねらったりするとよいでしょう。
