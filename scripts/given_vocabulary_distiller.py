import pandas as pd
import os
import json
import time
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from openai import OpenAI
from tqdm import tqdm
import re

# --- 模型配置 ---
# 集成了所有可用 API Key，支持大规模并发负载
MODEL_CONFIGS = {
    "3": {
        "name": "DeepSeek-Chat (语言学深度蒸馏专用)",
        "api_keys": [
            "sk-dae1bc4b18034ecdbd5365c1348234ad",
            "sk-31b092afea204db4b07f1570e7042fff",
            "sk-9df1e5bc83d64142916751dee3c7b7cd",
            "sk-af0218aef6944fe6ae23e2962074470a",
            "sk-bb44ce68cdcb426da287685fe432f869",
            "sk-c7dab615130548559426796b3984cae6",
            "sk-3a2c0a5540a048ea98a6058421ce6d19",
            "sk-a701d45a2c4a46e1a38ef1b5605815e8",
            "sk-a365304d6a204fe38f969d46ee65d64d"
        ],
        "base_url": "https://api.deepseek.com",
        "model": "deepseek-chat"
    }
}

# --- 全局设定 ---
INPUT_FILE = "../data/csv/CET4.csv"
OUTPUT_FILE = "../data/CET4_distilled.csv"
BATCH_SIZE = 15
MAX_WORKERS = 20 # 增加线程数以充分利用多 Key
MAX_RETRIES = 3

csv_lock = threading.Lock()

def get_countability_prompt():
    """原封不动保留的可数性提示词"""
    return (
        "You are a Senior Bilingual Exam Coach for IELTS, CET-4, and CET-6.\n"
        "Your task: Explain countability by focusing ONLY on what matters for the exams. MINIMIZE redundancy.\n\n"
        "CORE DIRECTIVES:\n"
        "1. EXAM POINTS (NEW): If a word has a specific, testable gimmick in IELTS/CET (e.g., 'equipment' is never plural), or if it is marked as 'Both' (which is always a key contrast point), put the distinction in 'exam_points'.\n"
        "2. SELECTIVE AGREEMENT: ONLY return 'agreement' if the noun has IRREGULAR subject-verb agreement (e.g., 'police are', 'news is'). For regular nouns, set to null.\n"
        "3. EXAM-CENTRIC BOTH (90/10 Rule): Categorize as 'Both' (B) ONLY if both C and U usages are highly frequent in exams (e.g., 'experience'). If one usage is dominant (90%+) and the other is niche/rare, ignore the niche one and classify as a single C or U. If marked as 'U', set 'plural' to null.\n"
        "4. CHINGLISH BUSTER (SELECTIVE): Use 'chinese_trap' ONLY for nouns that are exceptionally tricky (e.g., 'information'). For 95% of words, set to null.\n"
        "5. MANDATORY CONTRAST FOR BOTH: If marked 'Both', you MUST provide at least TWO items in 'shifts': one for 'C' and one for 'U'. The 'sense' and 'example' must clearly correspond to that specific usage. Example sentences must use markers like 'a/an/the...s' for C and no article/quantity for U.\n"
        "6. COMPLETE ENGLISH COLLOCATIONS: In 'collocations', provide full NATURAL ENGLISH phrases (phrase + example). Phrase must include the word itself. STRICTLY FORBIDDEN: Do not provide Chinese measure words or classifiers (e.g., NEVER return '个', '位', '只'). Return phrases like 'immediate superior' or 'report to a superior' instead.\n"
        "7. NO JARGON: Use simple, teaching-oriented Chinese for definitions/notes, but keep collocations/examples in English.\n\n"
        "JSON SCHEMA:\n"
        "{\n"
        "  'results': [{\n"
        "    'word': 'str',\n"
        "    'is_noun': bool,\n"
        "    'countability': 'C|U|B|NA', \n"
        "    'plural': 'str',\n"
        "    'shifts': [{ 'sense': 'str', 'type': 'C/U', 'example': 'str' }],\n"
        "    'chinese_trap': 'str or null',\n"
        "    'exam_points': 'str or null',\n"
        "    'collocations': [{ 'phrase': 'str', 'example': 'str' }],\n"
        "    'agreement': { 'rule': 'str', 'example': 'str' } or null\n"
        "  }]\n"
        "}"
    )

def get_polysemy_prompt():
    """原封不动保留的熟词生义提示词 (V2 极高门槛版)"""
    return (
        "You are a World-Class Lexicographer and Elite IELTS/TOEFL Examiner.\n"
        "Your goal: Identify ONLY the most 'Surprising & Exam-Critical' secondary meanings (金蝉脱壳) for common English words.\n\n"
        "ULTRA-STRICT FILTERING RULES:\n"
        "1. THE 1:15 RATIO GUIDELINE: Expect a yield of around 5-7% (approx. 1 out of 15 words). Most words should return 'has_gem': false.\n"
        "2. THE SURPRISE THRESHOLD: A 'Gem' must be a meaning that makes a learner say 'I didn't know the word could mean THAT!'. It must be a radical departure, not just a related nuance.\n"
        "   - BAD: 'bank' (n.) vs 'bank' (v. to deposit) -> NOT A GEM.\n"
        "   - BAD: 'book' (n.) vs 'book' (v. to reserve) -> NOT A GEM (too common).\n"
        "   - GOOD: 'check' (v. to stop/hinder), 'weather' (v. to survive), 'husband' (v. to conserve).\n"
        "3. NO PART-OF-SPEECH TRAPS: Simply changing from a noun to a related verb is ALMOST NEVER a gem unless the meaning changes significantly (like 'husband').\n"
        "4. EXAM FATALITY: Only include meanings that are known 'traps' in high-level reading (IELTS/TOEFL/GRE/SAT).\n"
        "5. NO FILLER: 'exam_value' must be a sharp 1-sentence analysis of WHY this is a trap. NEVER explain things like 'it's easy to confuse'.\n"
        "6. AGGRESSIVE NEGATION: When in doubt, return 'has_gem': false. It is much better to skip a word than to provide a mediocre result.\n\n"
        "JSON SCHEMA:\n"
        "{\n"
        "  'results': [{\n"
        "    'word': 'str',\n"
        "    'has_gem': bool,\n"
        "    'gem_meaning': '极简中文释义 (like \"v. 节制；节俭\")',\n"
        "    'exam_value': '考点说明 (like \"阅读常考其动词义，干扰考生往已知常识去脑补\")',\n"
        "    'example': 'A convincing, academic context example sentence.'\n"
        "  }]\n"
        "}"
    )

def call_api(prompt, content, client, model_name, temp=0.1):
    """通用的 API 调用函数"""
    for attempt in range(MAX_RETRIES):
        try:
            response = client.chat.completions.create(
                model=model_name,
                messages=[
                    {"role": "system", "content": prompt},
                    {"role": "user", "content": content},
                ],
                response_format={'type': 'json_object'},
                temperature=temp
            )
            data = json.loads(response.choices[0].message.content)
            return data.get('results', [])
        except Exception:
            if attempt < MAX_RETRIES - 1:
                time.sleep(1)
    return []

def process_batch(batch_indices, df_original, pbar, client, model_name):
    """核心处理单元：合并两个 API 的结果"""
    batch_df = df_original.iloc[batch_indices]
    words_info = []
    word_list = []
    
    for _, row in batch_df.iterrows():
        w = str(row['word'])
        m = str(row.get('meaning', ''))
        word_list.append(w)
        words_info.append((w, m))

    # --- 并行请求两个 AI 任务 ---
    # 任务 A: 可数性 (仅针对名词)
    # 任务 B: 熟词生义
    
    # 为了保持逻辑清晰，这里直接顺序调用，因为外面已经有 MAX_WORKERS 并发了
    # 如果追求更高极致性能，可以在这里套一层 ThreadPoolExecutor
    
    # 1. 获取可数性数据
    nouns_in_batch = [w for w, m in words_info if re.search(r'n\.|名词|名$', m, re.I)]
    count_results = {}
    if nouns_in_batch:
        res_list = call_api(get_countability_prompt(), f"Words to process: {', '.join(nouns_in_batch)}", client, model_name)
        count_results = {item['word'].lower(): item for item in res_list}

    # 2. 获取熟词生义数据
    polysemy_content = "\n".join([f"Word: {w}, Known Meanings: {m}" for w, m in words_info])
    user_polysemy = f"Distill gems for these words:\n{polysemy_content}\n\nREMEMBER: I only want the RARE gems (approx 1 per 15 words). Most results should have 'has_gem': false."
    poly_list = call_api(get_polysemy_prompt(), user_polysemy, client, model_name)
    poly_results = {item['word'].lower(): item for item in poly_list}

    # 3. 合并写入
    batch_data = []
    for _, row_orig in batch_df.iterrows():
        word_lower = str(row_orig['word']).lower()
        output_row = row_orig.to_dict()

        # 处理可数性
        c_res = count_results.get(word_lower, {})
        output_row['countability'] = c_res.get('countability', 'NA')
        output_row['plural'] = c_res.get('plural', '')
        if c_res:
            output_row['explanation'] = json.dumps(c_res, ensure_ascii=False)
        else:
            output_row['explanation'] = "None"

        # 处理熟词生义
        p_res = poly_results.get(word_lower, {"has_gem": False})
        if p_res.get('has_gem', False):
            output_row['polysemy_data'] = json.dumps({
                "meaning": p_res.get('gem_meaning'),
                "value": p_res.get('exam_value'),
                "example": p_res.get('example')
            }, ensure_ascii=False)
        else:
            output_row['polysemy_data'] = "None"

        batch_data.append(output_row)

    if batch_data:
        with csv_lock:
            temp_df = pd.DataFrame(batch_data)
            header = not os.path.exists(OUTPUT_FILE)
            temp_df.to_csv(OUTPUT_FILE, mode='a', index=False, header=header, encoding='utf-8-sig')
    
    pbar.update(len(batch_indices))

def main():
    print("\n" + "="*60)
    print("      VibeSpeller 语言学数据联合蒸馏器 (Unified Distiller)")
    print("="*60)
    
    config = MODEL_CONFIGS["3"]
    print(f"\n🚀 引擎: {config['name']} | 并行池: {len(config['api_keys'])} Keys\n")
    
    clients = [OpenAI(api_key=k, base_url=config['base_url']) for k in config['api_keys']]
    
    if not os.path.exists(INPUT_FILE):
        print(f"❌ 找不到输入文件: {INPUT_FILE}")
        return

    df = pd.read_csv(INPUT_FILE)
    processed_words = set()
    if os.path.exists(OUTPUT_FILE):
        try:
            processed_df = pd.read_csv(OUTPUT_FILE)
            processed_words = set(processed_df['word'].dropna().str.lower())
            print(f"🔄 检测到进度：已处理 {len(processed_words)} 个单词。")
        except: pass

    to_process_df = df[~df['word'].str.lower().isin(processed_words)].copy()
    total = len(to_process_df)
    
    if total == 0:
        print("🎉 所有任务已圆满完成！")
        return

    indices = list(range(total))
    batches = [indices[i : i + BATCH_SIZE] for i in range(0, total, BATCH_SIZE)]
    
    with tqdm(total=total, desc="联合蒸馏中") as pbar:
        with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
            futures = [
                executor.submit(process_batch, b, to_process_df, pbar, clients[i % len(clients)], config['model']) 
                for i, b in enumerate(batches)
            ]
            for future in as_completed(futures):
                try:
                    future.result()
                except Exception:
                    pass

    print(f"\n✅ 联合蒸馏圆满完成！结果已保存至: {OUTPUT_FILE}")

if __name__ == "__main__":
    main()
