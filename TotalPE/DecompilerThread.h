#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

//
// The decompiler's work, on a thread of its own: one piece at a time, in order, so that the user interface is responsive
// while a function is decompiled (a large one takes seconds). Ghidra's decompiler is not thread safe, and its instances
// share state: they are all made, used and destroyed on this thread.
//
class DecompilerThread {
public:
	using Work = std::function<void()>;

	// the thread (started on first use)
	static DecompilerThread& Get();
	void Post(Work work);
	// At the end of the process: the work that was not started is not done. False if the work in progress did not end in
	// 'timeout' milliseconds (the decompiler cannot be stopped): the thread is still running.
	static bool Shutdown(DWORD timeout);

private:
	DecompilerThread();
	void Run();

	std::mutex m_Lock;
	std::condition_variable m_Ready;
	std::deque<Work> m_Queue;
	bool m_Stop{ false };
	bool m_Done{ false };	// the thread ended
	std::thread m_Thread;		// (last: it runs with the members above)
};
