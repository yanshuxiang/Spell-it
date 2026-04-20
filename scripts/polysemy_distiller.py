import pandas as pd
import os
import json
import time
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from openai import OpenAI
from tqdm import tqdm

# --- 模型配置 ---
# 直接沿用 Countability Fetcher 的多 Key 配置，确保并发效率
MODEL_CONFIGS = {
    "3": {
        "name": "DeepSeek-Chat (熟词僻义蒸馏专用)",
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
INPUT_FILE = "../data/test.csv"
OUTPUT_FILE = "../data/test_polysemy.csv"
BATCH_SIZE = 15  # 批量处理
MAX_WORKERS = 15 # 高并发
MAX_RETRIES = 3

csv_lock = threading.Lock()

def get_system_prompt():
    """熟词僻义蒸馏专用指令集 (V2: 极高门槛筛选版)"""
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

def get_polysemy_batch(words_with_meanings, client, model_name):
    """批量 API 调取"""
    prompt_content = "\n".join([f"Word: {w}, Known Meanings: {m}" for w, m in words_with_meanings])
    user_prompt = f"Distill gems for these words:\n{prompt_content}"
    
    for attempt in range(MAX_RETRIES):
        try:
            response = client.chat.completions.create(
                model=model_name,
                messages=[
                    {"role": "system", "content": get_system_prompt()},
                    {"role": "user", "content": f"{user_prompt}\n\nREMEMBER: I only want the RARE gems (approx 1 per 15 words). Most results should have 'has_gem': false."},
                ],
                response_format={'type': 'json_object'},
                temperature=0.1
            )
            data = json.loads(response.choices[0].message.content)
            results = data.get('results', [])
            return {item['word'].lower(): item for item in results}
        except Exception as e:
            if attempt < MAX_RETRIES - 1:
                time.sleep(1)
            else:
                return {}

def process_batch(batch_indices, df_original, pbar, client, model_name):
    """线程执行单元"""
    batch_df = df_original.iloc[batch_indices]
    words_info = []
    for _, row in batch_df.iterrows():
        words_info.append((str(row['word']), str(row.get('meaning', ''))))

    results_map = get_polysemy_batch(words_info, client, model_name)
    batch_data = []

    for _, row_orig in batch_df.iterrows():
        word_lower = str(row_orig['word']).lower()
        res = results_map.get(word_lower, {"has_gem": False})
        
        output_row = row_orig.to_dict()
        if res.get('has_gem', False):
            output_row['polysemy_data'] = json.dumps({
                "meaning": res.get('gem_meaning'),
                "value": res.get('exam_value'),
                "example": res.get('example')
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
    print("      VibeSpeller 熟词僻义“金蝉脱壳”蒸馏器")
    print("="*60)
    
    config = MODEL_CONFIGS["3"]
    print(f"\n🚀 引擎初始化: {config['name']} | 多 Key 并发池: {len(config['api_keys'])}\n")
    
    clients = [OpenAI(api_key=k, base_url=config['base_url']) for k in config['api_keys']]
    
    if not os.path.exists(INPUT_FILE):
        print(f"❌ 错误: 找不到输入数据 {INPUT_FILE}")
        return

    df = pd.read_csv(INPUT_FILE)
    processed_words = set()
    if os.path.exists(OUTPUT_FILE):
        try:
            processed_df = pd.read_csv(OUTPUT_FILE)
            processed_words = set(processed_df['word'].dropna().str.lower())
            print(f"🔄 自动读取进度：已完成 {len(processed_words)} 个。")
        except: pass

    to_process_df = df[~df['word'].str.lower().isin(processed_words)].copy()
    total_to_process = len(to_process_df)
    
    if total_to_process == 0:
        print("🎉 恭喜，所有单词已蒸馏完毕！")
        return

    indices = list(range(total_to_process))
    batches = [indices[i : i + BATCH_SIZE] for i in range(0, total_to_process, BATCH_SIZE)]
    
    with tqdm(total=total_to_process, desc="金蝉脱壳中") as pbar:
        with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
            futures = [
                executor.submit(process_batch, b, to_process_df, pbar, clients[i % len(clients)], config['model']) 
                for i, b in enumerate(batches)
            ]
            for future in as_completed(futures):
                try:
                    future.result()
                except:
                    pass

    print(f"\n✅ 蒸馏圆满完成！结果已保存至: {OUTPUT_FILE}")

if __name__ == "__main__":
    main()
