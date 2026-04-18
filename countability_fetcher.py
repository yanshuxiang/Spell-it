import pandas as pd
import os
import json
import time
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from openai import OpenAI
from tqdm import tqdm

# --- 模型配置驱动 ---
# 支持多 Key 轮询，大幅提升采集并发效率
MODEL_CONFIGS = {
    "1": {
        "name": "Qwen-Flash (推荐: 快速且稳定)",
        "api_keys": ["sk-35266b1d300a465a87d78cb000ab38d4"],
        "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "model": "qwen-flash"
    },
    "2": {
        "name": "Qwen-Plus (高精度语义分析)",
        "api_keys": ["sk-35266b1d300a465a87d78cb000ab38d4"],
        "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "model": "qwen-plus"
    },
    "3": {
        "name": "DeepSeek-Chat (逻辑性强)",
        "api_keys": [
            "sk-dae1bc4b18034ecdbd5365c1348234ad",
            "sk-31b092afea204db4b07f1570e7042fff",
            "sk-9df1e5bc83d64142916751dee3c7b7cd",
            "sk-af0218aef6944fe6ae23e2962074470a",
            "sk-bb44ce68cdcb426da287685fe432f869"
        ],
        "base_url": "https://api.deepseek.com",
        "model": "deepseek-chat"
    }
}

# --- 全局设定 ---
INPUT_FILE = "test.csv"
OUTPUT_FILE = "test_return.csv"
BATCH_SIZE = 15  # 适当增加批次大小
MAX_WORKERS = 15  # 增加并发线程数 (对应多 Key 负载)
MAX_RETRIES = 3

csv_lock = threading.Lock()

def get_system_prompt():
    """构建基于“考点优先”的深度解析指令集 (V7: 精简考点版)"""
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

def synthesize_explanation(res):
    """数据合成器：精简输出，突出考点"""
    if not res.get('is_noun', True):
        return "None"
    
    parts = []
    
    # 1. 核心考点 (Exam Points) - 优先级最高
    ep = res.get('exam_points')
    if ep:
        parts.append(f"【核心考点】{ep}")

    # 2. 语义分析
    shifts = res.get('shifts')
    if shifts:
        parts.append("【用法解析】")
        for s in shifts:
            parts.append(f"({s['type']}) {s['sense']}: {s['example']}")
            
    # 3. 注意/陷阱
    trap = res.get('chinese_trap')
    if trap:
        parts.append(f"【注意】{trap}")
        
    # 4. 主谓一致 (仅针对特殊规则)
    ag = res.get('agreement')
    if ag and ag.get('rule'):
        parts.append(f"【特殊语法】{ag['rule']} 例: {ag['example']}")
        
    # 5. 搭配例句 (Collocations)
    cl = res.get('collocations')
    if cl:
        parts.append("【常用搭配】")
        for c in cl:
            parts.append(f"· {c['phrase']} (例: {c['example']})")
        
    return "\n".join(parts) if parts else "暂无详细用法说明。"

def get_countability_batch(word_list, client, model_name):
    """批量 API 调取"""
    prompt_words = ", ".join(word_list)
    user_prompt = f"Words to process: {prompt_words}"
    
    for attempt in range(MAX_RETRIES):
        try:
            response = client.chat.completions.create(
                model=model_name,
                messages=[
                    {"role": "system", "content": get_system_prompt()},
                    {"role": "user", "content": user_prompt},
                ],
                response_format={'type': 'json_object'},
                temperature=0.1
            )
            data = json.loads(response.choices[0].message.content)
            results = data.get('results', [])
            return {item['word'].lower(): item for item in results}
        except Exception as e:
            if attempt < MAX_RETRIES - 1:
                time.sleep(1 * (attempt + 1)) # 缩短重试延迟
            else:
                return {}

def process_batch(batch, df_original, pbar, client, model_name):
    """单线程批处理单元"""
    results_map = get_countability_batch(batch, client, model_name)
    batch_data = []
    
    for word in batch:
        res = results_map.get(word.lower(), {})
        matching_rows = df_original[df_original['word'].str.lower() == word.lower()]
        if matching_rows.empty: continue
        
        row = matching_rows.iloc[0].to_dict()
        
        countability = res.get('countability', 'NA')
        plural = res.get('plural', '')
        
        if not res.get('is_noun', True):
            explanation = "None"
        else:
            explanation = json.dumps(res, ensure_ascii=False)
            
        row['countability'] = countability
        row['plural'] = plural
        row['explanation'] = explanation
        batch_data.append(row)

    if batch_data:
        with csv_lock:
            temp_df = pd.DataFrame(batch_data)
            cols = list(df_original.columns)
            for c in ['countability', 'plural', 'explanation']:
                if c not in cols: cols.append(c)
            
            final_cols = [c for c in cols if c in temp_df.columns]
            temp_df = temp_df[final_cols]
            
            header = not os.path.exists(OUTPUT_FILE)
            temp_df.to_csv(OUTPUT_FILE, mode='a', index=False, header=header, encoding='utf-8-sig')
    
    pbar.update(len(batch))

def main():
    print("\n" + "="*50)
    print("      VibeSpeller 可数性深度采集助手 (Speed Edition)")
    print("="*50)
    
    # 动态模型选择
    print("\n请选择 AI 引擎 (输入数字):")
    for k, v in MODEL_CONFIGS.items():
        print(f" [{k}] {v['name']} ({len(v['api_keys'])} Keys)")
    
    choice = input("\n您的选择 (默认 3): ").strip()
    config = MODEL_CONFIGS.get(choice, MODEL_CONFIGS["3"])
    print(f"\n🚀 已切换至: {config['name']} | 正在初始化 {len(config['api_keys'])} 个并行线程池...\n")
    
    # 初始化客户端池
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

    to_process = df[~df['word'].str.lower().isin(processed_words)].copy()
    
    # 自动过滤：如果 meaning 列中不包含 'n.' 或 '名词'，则视为非名词，直接跳过。
    if 'meaning' in to_process.columns:
        initial_count = len(to_process)
        to_process = to_process[to_process['meaning'].str.contains(r'n\.|名词|名$', na=False, case=False, regex=True)]
        skipped = initial_count - len(to_process)
        if skipped > 0:
            print(f"✂️ 自动过滤掉 {skipped} 个非名词单词 (基于释义中未检测到 n. 或 名词)。")

    word_queue = to_process['word'].tolist()
    
    if not word_queue:
        print("🎉 所有任务已完成！")
        return

    batches = [word_queue[i : i + BATCH_SIZE] for i in range(0, len(word_queue), BATCH_SIZE)]
    
    with tqdm(total=len(word_queue), desc="极速采集进行中") as pbar:
        with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
            # 使用简单的轮询将任务分配给不同的 Client
            futures = [
                executor.submit(process_batch, b, df, pbar, clients[i % len(clients)], config['model']) 
                for i, b in enumerate(batches)
            ]
            for future in as_completed(futures):
                try:
                    future.result()
                except Exception as e:
                    pass

    print(f"\n✅ 任务圆满完成！增强数据已保存至: {OUTPUT_FILE}")

if __name__ == "__main__":
    main()
