import React, { useState } from 'react';
import { Book, Calendar, BarChart3, Sparkles, Loader2, X } from 'lucide-react';

const App = () => {
    const [currentIndex, setCurrentIndex] = useState(0);
    const [activeTab, setActiveTab] = useState('book'); // 底部导航栏状态
    const [touchStart, setTouchStart] = useState(null);
    const [touchEnd, setTouchEnd] = useState(null);

    // AI 功能状态
    const [showModal, setShowModal] = useState(false);
    const [isGenerating, setIsGenerating] = useState(false);
    const [aiMessage, setAiMessage] = useState("");

    // 模拟三本书的数据
    const books = [
        { id: 1, title: "Oxford 5000", progress: 35, color: "bg-gray-900", tag: "CORE" },
        { id: 2, title: "GRE Essential", progress: 12, color: "bg-indigo-950", tag: "ADVANCED" },
        { id: 3, title: "IELTS Vocab", progress: 85, color: "bg-zinc-800", tag: "EXAM" },
    ];

    const realCurrentIndex = ((currentIndex % books.length) + books.length) % books.length;

    // --- 滑动手势逻辑 ---
    const minSwipeDistance = 40;

    const onTouchStart = (e) => {
        setTouchEnd(null);
        setTouchStart(e.targetTouches[0].clientX);
    };

    const onTouchMove = (e) => setTouchEnd(e.targetTouches[0].clientX);

    const onTouchEnd = () => {
        if (!touchStart || !touchEnd) return;
        const distance = touchStart - touchEnd;

        if (distance > minSwipeDistance) {
            setCurrentIndex(prev => prev + 1);
        }
        if (distance < -minSwipeDistance) {
            setCurrentIndex(prev => prev - 1);
        }
    };

    // --- Gemini API 调用逻辑 ---
    const handleStartStudy = async (bookTitle) => {
        setShowModal(true);
        setIsGenerating(true);
        setAiMessage("");

        try {
            const apiKey = ""; // 执行环境会自动提供 apiKey
            const url = `https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-preview-09-2025:generateContent?key=${apiKey}`;

            const response = await fetch(url, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    contents: [{
                        parts: [{ text: `Please generate a very short, inspiring 1-sentence English quote (with Chinese translation) to motivate me to study the vocabulary book: ${bookTitle}.` }]
                    }],
                    systemInstruction: {
                        parts: [{ text: "You are an encouraging AI study assistant. Keep responses extremely brief and energetic." }]
                    }
                })
            });

            const data = await response.json();
            const generatedText = data.candidates?.[0]?.content?.parts?.[0]?.text;
            setAiMessage(generatedText || "Knowledge is power! 知识就是力量！");
        } catch (error) {
            setAiMessage("Let's start today's journey! 今天也请加油学习！");
        } finally {
            setIsGenerating(false);
        }
    };

    return (
        <div className="flex flex-col items-center justify-center min-h-screen bg-gray-100 font-sans text-gray-900 p-4 sm:p-8">

            {/* 外部手机壳大框架 */}
            <div className="w-full max-w-[380px] aspect-[9/16] border border-gray-200/80 rounded-[2.5rem] shadow-2xl flex flex-col relative overflow-hidden bg-gray-50/50">

                {/* 顶部统计信息 */}
                <div className="pt-8 pb-2 text-center z-10">
                    <p className="text-[10px] tracking-[0.2em] text-gray-400 uppercase font-light">
                        今日已学 7 · 今日复习 0
                    </p>
                </div>

                {/* --- 核心画廊滑动区域 --- */}
                <div
                    className="flex-1 relative flex items-center justify-center overflow-hidden touch-pan-y"
                    onTouchStart={onTouchStart}
                    onTouchMove={onTouchMove}
                    onTouchEnd={onTouchEnd}
                >
                    {[-2, -1, 0, 1, 2].map((offset) => {
                        const virtualIndex = currentIndex + offset;
                        const realIndex = ((virtualIndex % books.length) + books.length) % books.length;
                        const book = books[realIndex];
                        const isCenter = offset === 0;

                        return (
                            <div
                                key={virtualIndex}
                                onClick={() => {
                                    if (!isCenter && Math.abs(offset) === 1) setCurrentIndex(virtualIndex);
                                }}
                                // 修改：将高度从 h-[78%] 缩小为 h-[66%]，腾出底部空间
                                className={`absolute w-[75%] h-[66%] transition-all duration-500 ease-[cubic-bezier(0.25,1,0.5,1)] flex flex-col ${isCenter ? 'cursor-default' : 'cursor-pointer'
                                    }`}
                                style={{
                                    transform: `translateX(${offset * 104}%) scale(${isCenter ? 1 : 0.86})`,
                                    opacity: isCenter ? 1 : (Math.abs(offset) > 1 ? 0 : 0.5),
                                    zIndex: 10 - Math.abs(offset),
                                    // 修改：移除 top: '8%'，使用负的 marginTop 将卡片整体微微向上提，避免碰到下方控件
                                    marginTop: '-2rem',
                                }}
                            >
                                {/* 内部模块矩形 */}
                                <div className={`flex-1 border border-gray-200/60 rounded-[1.5rem] bg-white p-6 flex flex-col transition-shadow duration-500 ${isCenter ? 'shadow-[0_20px_40px_-10px_rgb(0,0,0,0.1)]' : 'shadow-none'}`}>

                                    {/* 上部：Book封面 + 名称进度 */}
                                    <div className="flex gap-4 mb-8">
                                        <div className={`${book.color} w-[5.5rem] h-[7.5rem] rounded-md shadow flex items-center justify-center overflow-hidden flex-shrink-0 relative`}>
                                            <div className="absolute inset-0 opacity-10 bg-[radial-gradient(circle_at_50%_50%,#fff,transparent)]"></div>
                                            <div className="text-white text-[9px] font-bold rotate-[-90deg] tracking-[0.2em] opacity-40 whitespace-nowrap">
                                                {book.tag}
                                            </div>
                                        </div>

                                        <div className="flex-1 pt-1.5">
                                            <h2 className="text-lg font-semibold tracking-tight text-gray-800 mb-4 leading-tight">
                                                {book.title}
                                            </h2>
                                            <div className="space-y-3">
                                                <div className="w-full h-[2px] bg-gray-100 rounded-full overflow-hidden">
                                                    <div
                                                        className="h-full bg-gray-900 transition-all duration-700 ease-out"
                                                        style={{ width: `${book.progress}%` }}
                                                    ></div>
                                                </div>
                                                <p className="text-[9px] text-gray-400 font-mono tracking-widest uppercase">
                                                    Progress {book.progress}%
                                                </p>
                                            </div>
                                        </div>
                                    </div>

                                    <div className="flex-1"></div>

                                    {/* 底部按钮 B1, B2 */}
                                    <div className="flex gap-3 mt-auto">
                                        <button
                                            className={`flex-1 py-3.5 border border-gray-200 rounded-xl text-[13px] font-medium transition-all ${isCenter ? 'text-gray-500 hover:bg-gray-50 active:scale-95' : 'text-gray-300 pointer-events-none'}`}
                                        >
                                            复习
                                        </button>
                                        <button
                                            onClick={() => handleStartStudy(book.title)}
                                            className={`flex-1 py-3.5 rounded-xl text-[13px] font-medium transition-all flex items-center justify-center gap-1.5 ${isCenter ? 'bg-gray-900 text-white shadow-lg shadow-gray-900/20 hover:bg-black active:scale-95' : 'bg-gray-100 text-gray-400 pointer-events-none'}`}
                                        >
                                            <Sparkles size={14} className={isCenter ? "text-yellow-300" : "text-gray-300"} />
                                            学习
                                        </button>
                                    </div>
                                </div>
                            </div>
                        );
                    })}
                </div>

                {/* 翻页指示点 (稍微下移一点点，使其刚好处于卡片和导航栏的正中间) */}
                <div className="absolute bottom-[6.5rem] left-0 right-0 flex justify-center gap-1.5 z-10 pointer-events-none">
                    {books.map((_, i) => (
                        <div
                            key={i}
                            className={`h-1 rounded-full transition-all duration-500 ${i === realCurrentIndex ? 'w-4 bg-gray-800' : 'w-1.5 bg-gray-300/60'}`}
                        />
                    ))}
                </div>

                {/* --- 悬浮底部导航栏 --- */}
                {/* 使用 absolute 定位，脱离文档流，增加阴影和毛玻璃背景 */}
                <div className="absolute bottom-6 left-1/2 -translate-x-1/2 w-[82%] h-[4.25rem] border border-white/60 bg-white/70 backdrop-blur-xl shadow-[0_12px_40px_-12px_rgba(0,0,0,0.15)] rounded-full flex items-center justify-around px-6 z-20">
                    <button
                        onClick={() => setActiveTab('book')}
                        className={`p-3 transition-all duration-300 ${activeTab === 'book' ? 'text-gray-900 scale-110 drop-shadow-sm' : 'text-gray-400 hover:text-gray-600'}`}
                    >
                        <Book size={22} strokeWidth={activeTab === 'book' ? 2 : 1.5} />
                    </button>
                    <button
                        onClick={() => setActiveTab('calendar')}
                        className={`p-3 transition-all duration-300 ${activeTab === 'calendar' ? 'text-gray-900 scale-110 drop-shadow-sm' : 'text-gray-400 hover:text-gray-600'}`}
                    >
                        <Calendar size={22} strokeWidth={activeTab === 'calendar' ? 2 : 1.5} />
                    </button>
                    <button
                        onClick={() => setActiveTab('stats')}
                        className={`p-3 transition-all duration-300 ${activeTab === 'stats' ? 'text-gray-900 scale-110 drop-shadow-sm' : 'text-gray-400 hover:text-gray-600'}`}
                    >
                        <BarChart3 size={22} strokeWidth={activeTab === 'stats' ? 2 : 1.5} />
                    </button>
                </div>

                {/* AI 模态弹窗 */}
                {showModal && (
                    <div className="absolute inset-0 z-50 flex items-center justify-center bg-white/40 backdrop-blur-md p-6 animate-in fade-in duration-200">
                        <div className="bg-white border border-gray-100 shadow-[0_30px_80px_rgb(0,0,0,0.15)] rounded-3xl w-full p-8 relative flex flex-col items-center text-center">
                            <button
                                onClick={() => setShowModal(false)}
                                className="absolute top-5 right-5 text-gray-400 hover:text-gray-900 transition-colors bg-gray-50 p-1.5 rounded-full"
                            >
                                <X size={16} />
                            </button>

                            <div className="w-12 h-12 bg-gray-900 rounded-full flex items-center justify-center mb-5 shadow-lg shadow-gray-900/20">
                                <Sparkles size={20} className="text-yellow-300" />
                            </div>

                            <h3 className="text-[15px] font-semibold text-gray-900 mb-2">
                                AI 学习助理
                            </h3>

                            <div className="text-[13px] text-gray-600 min-h-[70px] flex items-center justify-center mt-2">
                                {isGenerating ? (
                                    <div className="flex flex-col items-center gap-3">
                                        <Loader2 className="animate-spin text-gray-400" size={18} />
                                        <span className="text-[10px] text-gray-400 tracking-widest uppercase">生成专属格言中...</span>
                                    </div>
                                ) : (
                                    <p className="leading-relaxed font-medium">{aiMessage}</p>
                                )}
                            </div>

                            <button
                                onClick={() => setShowModal(false)}
                                className="mt-6 w-full py-4 bg-gray-900 text-white rounded-xl text-[13px] font-medium hover:bg-black transition-colors active:scale-95 shadow-md"
                            >
                                开始本次专注
                            </button>
                        </div>
                    </div>
                )}

            </div>

            {/* 底部格言 */}
            <div className="mt-8 flex flex-col items-center gap-2">
                <div className="w-1 h-1 bg-gray-300 rounded-full"></div>
                <p className="text-gray-400 text-[10px] tracking-[0.3em] font-light">
                    长期主义的核心是无视中断
                </p>
            </div>
        </div>
    );
};

export default App;