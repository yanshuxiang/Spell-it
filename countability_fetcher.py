import pandas as pd
import os
import json
import time
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from openai import OpenAI
from tqdm import tqdm

# --- 模型配置驱动 ---
# 支持动态选择模型，内置已有的 API Key 和配置
MODEL_CONFIGS = {
    "1": {
        "name": "Qwen-Flash (推荐: 快速且稳定)",
        "api_key": "sk-35266b1d300a465a87d78cb000ab38d4",
        "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "model": "qwen-flash"
    },
    "2": {
        "name": "Qwen-Plus (高精度语义分析)",
        "api_key": "sk-35266b1d300a465a87d78cb000ab38d4",
        "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "model": "qwen-plus"
    },
    "3": {
        "name": "DeepSeek-Chat (逻辑性强)",
        "api_key": "sk-538530794c90430a9e72b452f554898e",
        "base_url": "https://api.deepseek.com",
        "model": "deepseek-chat"
    }
}

# --- 全局设定 ---
INPUT_FILE = "雅思词汇大纲.csv"
OUTPUT_FILE = "雅思词汇大纲_countability.csv"
BATCH_SIZE = 10  # 为了保证深度分析的质量，建议每批次处理 10 个词
MAX_WORKERS = 3  # 并发线程数
MAX_RETRIES = 3

csv_lock = threading.Lock()

def get_system_prompt():
    """构建基于“考点优先”的深度解析指令集 (V6: 雅思/四六级减负版)"""
    return (
        "You are a Senior Bilingual Exam Coach for IELTS, CET-4, and CET-6, specializing in helping Chinese students master countability with MINIMAL cognitive load.\n"
        "Your task is to provide countability data that is 100% practical for exam writing and speaking. ALWAYS prioritize the most frequent and exam-relevant usage.\n\n"
        
        "CORE DIRECTIVES:\n"
        "1. EXAM RELEVANCE PRIORITY: If a word has multiple countability types but 95% of exam contexts use only one (e.g., 'lack' is mostly U in exams), treat it as a single type (C or U) to reduce burden. Only use 'B' (Both) if both meanings are high-frequency exam points (e.g., 'Experience').\n"
        "2. FILTER OBSCURE USAGES: Strictly ignore dictionary-only technicalities (e.g., 'a curry' meaning a dish, or 'waters' meaning territory) unless they are SPECIFIC common traps in IELTS/CET-4/6.\n"
        "3. CHINGLISH BUSTER: Focus heavily on 'Chinese Traps' where Chinese speakers often add 's' incorrectly (e.g., advice, information, equipment) or fail to distinguish between abstract (U) and concrete (C) meanings in core academic contexts.\n"
        "4. NO JARGON: Use simple, teaching-oriented Chinese. Avoid academic linguistic terms. Explain by 'usage scenario' (e.g., '讨论整体情况时' vs '指代某次具体事件时').\n"
        "5. ABSOLUTE LANGUAGE DIVIDE: \n"
        "   - Simplified Chinese: sense, chinese_trap, classifiers, agreement.rule.\n"
        "   - English: word, plural, example, agreement.example.\n\n"
        
        "ONE-SHOT EXAMPLE (High-Frequency Contrast):\n"
        "Word: Experience\n"
        "{\n"
        "  'word': 'Experience',\n"
        "  'is_noun': true,\n"
        "  'countability': 'B',\n"
        "  'plural': 'experiences',\n"
        "  'shifts': [\n"
        "    { 'sense': '[不可数] 抽象的“经验/知识”，指长期积累的技能（考点：工作经验不能加 s）。', 'type': 'U', 'example': 'He has a lot of experience in teaching.' },\n"
        "    { 'sense': '[可数] 具体的“经历/往事”，指某次发生的事件（考点：难忘的经历用可数）。', 'type': 'C', 'example': 'It was a very strange experience.' }\n"
        "  ],\n"
        "  'chinese_trap': '四六级和雅思常考：指工作经验时永远不可数，别受中文“一项经验”影响；指生活经历时才用可数。',\n"
        "  'classifiers': ['years of experience', 'a unique experience'],\n"
        "  'agreement': { 'rule': '作“经验”讲时谓语用单数；作“经历”讲时随单复数。', 'example': 'Relevant experience is required for this position.' }\n"
        "}\n\n"

        "JSON SCHEMA:\n"
        "{\n"
        "  'results': [{\n"
        "    'word': 'str',\n"
        "    'is_noun': bool,\n"
        "    'countability': 'C|U|B|NA', \n"
        "    'plural': 'str',\n"
        "    'shifts': [{ 'sense': 'str (Practical Focus)', 'type': 'C/U', 'example': 'str' }],\n"
        "    'chinese_trap': 'str (Exam Trap Advice)', \n"
        "    'classifiers': ['str'],\n"
        "    'agreement': { 'rule': 'str', 'example': 'str' }\n"
        "  }]\n"
        "}"
    )

def synthesize_explanation(res):
    """数据合成器：将结构化字段整理成 GUI 可直观显示的字符串"""
    if not res.get('is_noun', True):
        return "None"
    
    parts = []
    
    # 1. 语义对撞 (Sense Shift) - 对应要求：意思不同、可数性不同、两套例句
    shifts = res.get('shifts')
    if shifts and len(shifts) >= 2:
        parts.append("【语境分析】")
        for s in shifts:
            parts.append(f"({s['type']}) {s['sense']}: {s['example']}")
            
    # 2. 中式陷阱 (Chinese Trap)
    trap = res.get('chinese_trap')
    if trap:
        parts.append(f"【陷阱】{trap}")
        
    # 3. 地道搭配 (Classifiers)
    cl = res.get('classifiers')
    if cl:
        parts.append(f"【搭配】{' / '.join(cl)}")
        
    # 4. 主谓一致 (Verb Agreement)
    ag = res.get('agreement')
    if ag and ag.get('rule'):
        parts.append(f"【语法注意】{ag['rule']} 例: {ag['example']}")
        
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
                time.sleep(2 * (attempt + 1))
            else:
                print(f"API请求失败: {e}")
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
        
        # ── 核心逻辑修改：不再合并成字符串，而是直接保存原始 JSON ──
        # 这样 C++ 端可以解析出更丰富的信息（如陷阱提示、主谓一致等）
        countability = res.get('countability', 'NA')
        plural = res.get('plural', '')
        
        if not res.get('is_noun', True):
            explanation = "None"
        else:
            # 将整个结果对象转为 JSON 字符串存储
            explanation = json.dumps(res, ensure_ascii=False)
            
        row['countability'] = countability
        row['plural'] = plural
        row['explanation'] = explanation
        batch_data.append(row)

    if batch_data:
        with csv_lock:
            temp_df = pd.DataFrame(batch_data)
            cols = list(df_original.columns)
            for c in ['countability', 'plural', 'explanation', 'translation']:
                if c not in cols and c in temp_df.columns:
                    cols.append(c)
            
            # 确保只包含 DataFrame 中存在的列
            final_cols = [c for c in cols if c in temp_df.columns]
            temp_df = temp_df[final_cols]
            
            header = not os.path.exists(OUTPUT_FILE)
            temp_df.to_csv(OUTPUT_FILE, mode='a', index=False, header=header, encoding='utf-8-sig')
    
    pbar.update(len(batch))

def main():
    print("\n" + "="*50)
    print("      VibeSpeller 可数性深度采集助手 (Pro)")
    print("="*50)
    
    # 动态模型选择
    print("\n请选择 AI 引擎 (输入数字):")
    for k, v in MODEL_CONFIGS.items():
        print(f" [{k}] {v['name']}")
    
    choice = input("\n您的选择 (默认 1): ").strip()
    config = MODEL_CONFIGS.get(choice, MODEL_CONFIGS["1"])
    print(f"\n🚀 已切换至: {config['name']} | 准备开始深度挖掘...\n")
    
    client = OpenAI(api_key=config['api_key'], base_url=config['base_url'])
    
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
    word_queue = to_process['word'].tolist()
    
    if not word_queue:
        print("🎉 所有任务已完成！")
        return

    # 分割批次
    batches = [word_queue[i : i + BATCH_SIZE] for i in range(0, len(word_queue), BATCH_SIZE)]
    
    with tqdm(total=len(word_queue), desc="深度采集进行中") as pbar:
        with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
            futures = [executor.submit(process_batch, b, df, pbar, client, config['model']) for b in batches]
            for future in as_completed(futures):
                try:
                    future.result()
                except Exception as e:
                    print(f"线程任务失败: {e}")

    print(f"\n✅ 任务圆满完成！增强数据已保存至: {OUTPUT_FILE}")

if __name__ == "__main__":
    main()
