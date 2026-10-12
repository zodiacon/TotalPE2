#include "pch.h"
#include "DecompilerThread.h"

namespace {
	// (never deleted: the thread may still run when the process ends)
	DecompilerThread* s_Thread;
}

DecompilerThread& DecompilerThread::Get() {
	if (!s_Thread)
		s_Thread = new DecompilerThread;
	return *s_Thread;
}

DecompilerThread::DecompilerThread() : m_Thread([this] { Run(); }) {
}

void DecompilerThread::Post(Work work) {
	{
		std::lock_guard lock(m_Lock);
		m_Queue.push_back(std::move(work));
	}
	m_Ready.notify_one();
}

void DecompilerThread::Run() {
	::SetThreadDescription(::GetCurrentThread(), L"Decompiler");
	for (;;) {
		Work work;
		{
			std::unique_lock lock(m_Lock);
			m_Ready.wait(lock, [this] { return m_Stop || !m_Queue.empty(); });
			if (m_Stop) {
				// the work that was not started is not done (it goes here: what it holds is the thread's)
				auto left = std::move(m_Queue);
				lock.unlock();
				left.clear();
				break;
			}
			work = std::move(m_Queue.front());
			m_Queue.pop_front();
		}
		work();
	}
	std::lock_guard lock(m_Lock);
	m_Done = true;
	m_Ready.notify_all();
}

bool DecompilerThread::Shutdown(DWORD timeout) {
	if (!s_Thread)
		return true;
	auto& t = *s_Thread;
	std::unique_lock lock(t.m_Lock);
	t.m_Stop = true;
	t.m_Ready.notify_all();
	if (!t.m_Ready.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return t.m_Done; })) {
		t.m_Thread.detach();
		return false;
	}
	lock.unlock();
	t.m_Thread.join();
	return true;
}
