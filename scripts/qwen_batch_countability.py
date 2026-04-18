import pandas as pd
import os
import json
import time
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from openai import OpenAI
from tqdm import tqdm

# --- 配置区 ---
# 使用用户驱动的 API Key 和 Qwen (DashScope) 兼容接口
API_KEY = "sk-35266b1d300a465a87d78cb000ab38d4"
BASE_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1"
MODEL = "qwen3.6-flash"  # 推荐使用 qwen-plus 或 qwen-max 以获得更准确的语言分析

INPUT_FILE = "雅思词汇大纲.csv"
OUTPUT_FILE = "雅思词汇大纲_countability.csv"
BATCH_SIZE = 30   # 每批 30 个单词
MAX_WORKERS = 3   # 并发线程数
MAX_RETRIES = 3   # 重试次数

# 初始化客户端
client = OpenAI(api_key=API_KEY, base_url=BASE_URL)
# 文件写入锁
csv_lock = threading.Lock()

def get_countability_batch(word_list):
    """通过 Qwen API 批量获取单词可数性"""
    prompt_words = ", ".join(word_list)
    system_prompt = (
        "You are a professional Academic English Lexicographer and IELTS Writing Coach. "
        "For the provided words, determine countability and plural forms with high precision.\n"
        "RULES:\n"
        "1. 'countability': Use 'C' (Countable), 'U' (Uncountable), 'B' (Both), or 'NA' (Not a noun).\n"
        "2. 'plural': \n"
        "   - Mandatory if 'C' or 'B'. \n"
        "   - If singular and plural are the same (e.g., sheep), write 'word (单复数同形)'.\n"
        "   - If a word is almost always plural (e.g., clothes, assets), write 'plural (常用复数)'.\n"
        "3. 'explanation': \n"
        "   - Brief definition or usage guide. \n"
        "   - IMPORTANT: If 'countability' is 'NA' (not a noun), set 'explanation' to 'None'.\n"
        "4. 'countable_example' & 'uncountable_example': \n"
        "   - Provide a short, academic example sentence for each applicable case (especially for 'B').\n"
        "   - Keep it concise (under 15 words).\n"
        "Return a JSON object with key 'results' containing an array of objects."
    )
    user_prompt = f"Words to process ({len(word_list)} in total): {prompt_words}"

    for attempt in range(MAX_RETRIES):
        try:
            response = client.chat.completions.create(
                model=MODEL,
                messages=[
                    {"role": "system", "content": system_prompt},
                    {"role": "user", "content": user_prompt},
                ],
                # Qwen Plus/Max 最新版本支持 json_object
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
                print(f"请求失败: {e}")
                return {}

def process_batch(batch, df_original, pbar):
    """一个线程循环处理的任务单元"""
    results_map = get_countability_batch(batch)
    
    batch_data = []
    for word in batch:
        res = results_map.get(word.lower(), {})
        
        # 寻找对应行
        matching_rows = df_original[df_original['word'].str.lower() == word.lower()]
        if matching_rows.empty:
            continue
        original_row = matching_rows.iloc[0].to_dict()
        
        # 提取结果
        countability = res.get('countability', 'NA')
        plural = res.get('plural', '')
        explanation = res.get('explanation', '')
        c_ex = res.get('countable_example', '')
        u_ex = res.get('uncountable_example', '')

        # 格式化例句进 explanation
        if countability == 'NA':
            formatted_notes = "None"
        elif countability == 'B':
            formatted_notes = f"{explanation} [例(U)]: {u_ex} [例(C)]: {c_ex}"
        elif countability == 'C' and c_ex:
            formatted_notes = f"{explanation} [例]: {c_ex}"
        elif countability == 'U' and u_ex:
            formatted_notes = f"{explanation} [例]: {u_ex}"
        else:
            formatted_notes = explanation

        # 填充结果
        original_row['countability'] = countability
        original_row['plural'] = plural
        original_row['explanation'] = formatted_notes
        batch_data.append(original_row)

    # 线程安全写入 CSV
    if batch_data:
        with csv_lock:
            temp_df = pd.DataFrame(batch_data)
            # 自动补全可能缺失的列并排序
            cols = list(df_original.columns)
            for c in ['countability', 'plural', 'explanation']:
                if c not in cols:
                    cols.append(c)
            temp_df = temp_df[cols]
            
            header = not os.path.exists(OUTPUT_FILE)
            temp_df.to_csv(OUTPUT_FILE, mode='a', index=False, header=header, encoding='utf-8-sig')
    
    pbar.update(len(batch))

def main():
    if not os.path.exists(INPUT_FILE):
        print(f"找不到输入文件: {INPUT_FILE}")
        return

    # 加载原始数据
    df = pd.read_csv(INPUT_FILE)
    if 'word' not in df.columns:
        print("错误：CSV 文件中找不到 'word' 列。")
        return

    # 断点续传逻辑
    if os.path.exists(OUTPUT_FILE):
        try:
            processed_df = pd.read_csv(OUTPUT_FILE)
            processed_words = set(processed_df['word'].dropna().str.lower())
            print(f"检测到进度：已处理 {len(processed_words)} 个单词，正在使用 Qwen 补全剩余内容...")
        except Exception:
            processed_words = set()
    else:
        processed_words = set()

    # 筛选未处理单词
    to_process = df[~df['word'].str.lower().isin(processed_words)].copy()
    word_queue = to_process['word'].tolist()
    
    if not word_queue:
        print("🎉 所有单词已处理完成。")
        return

    print(f"🚀 Qwen 启动 | 处理 {len(word_queue)} 个单词 | 批次: {BATCH_SIZE} | 线程: {MAX_WORKERS}")

    # 分割批次
    batches = [word_queue[i : i + BATCH_SIZE] for i in range(0, len(word_queue), BATCH_SIZE)]

    with tqdm(total=len(word_queue), desc="Qwen 正在处理") as pbar:
        with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
            futures = [executor.submit(process_batch, b, df, pbar) for b in batches]
            for future in as_completed(futures):
                try:
                    future.result()
                except Exception as e:
                    print(f"\n子线程执行出错: {e}")

    print(f"\n✅ 全部任务完成！结果已同步至: {OUTPUT_FILE}")

if __name__ == "__main__":
    main()
