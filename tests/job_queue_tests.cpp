#include "JobQueue.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			throw std::runtime_error(a_message);
		}
	}

	struct Job
	{
		virtual ~Job() = default;
		virtual bool Run() = 0;
	};

	struct Task : Job
	{
		Task(std::function<bool()> a_run, int& a_destroyed) :
			run(std::move(a_run)), destroyed(a_destroyed)
		{}
		~Task() override { ++destroyed; }
		bool Run() override { return run(); }
		std::function<bool()> run;
		int& destroyed;
	};

	void ResetCancelsRetainedAndRejectsWork()
	{
		std::atomic<std::uint64_t> epoch{ 0 };
		JobQueue<Job> queue(epoch);
		int calls = 0;
		int destroyed = 0;
		queue.Push<Task>([&] { ++calls; return false; }, destroyed);
		queue.Process([](const auto& a_job) { return a_job->Run(); });
		Require(calls == 1 && destroyed == 0, "unfinished job must survive a normal phase");
		epoch.store(1);
		queue.Push<Task>([&] { ++calls; return true; }, destroyed);
		queue.Process([](const auto& a_job) { return a_job->Run(); });
		Require(calls == 1 && destroyed == 0, "reset must suppress processing and new work");
		queue.Clear();
		Require(destroyed == 1, "reset must release retained jobs");
		epoch.store(2);
		queue.Process([](const auto& a_job) { return a_job->Run(); });
		Require(calls == 1, "cancelled jobs must not replay after reset");
		queue.Push<Task>([&] { ++calls; return true; }, destroyed);
		queue.Process([](const auto& a_job) { return a_job->Run(); });
		Require(calls == 2 && destroyed == 2, "new-world work must resume normally");
	}

	void ResetDrainsRunningPhaseAndStopsCrossPhaseEnqueue()
	{
		std::atomic<std::uint64_t> epoch{ 0 };
		JobQueue<Job> postHavok(epoch);
		JobQueue<Job> prePhysics(epoch);
		std::mutex mutex;
		std::condition_variable cv;
		bool entered = false;
		bool release = false;
		bool finished = false;
		int destroyed = 0;
		int impulses = 0;
		postHavok.Push<Task>([&] {
			{
				std::unique_lock lock(mutex);
				entered = true;
				cv.notify_all();
				cv.wait(lock, [&] { return release; });
			}
			prePhysics.Push<Task>([&] { ++impulses; return true; }, destroyed);
			finished = true;
			return false;
		},
			destroyed);
		std::thread processor([&] { postHavok.Process([](const auto& a_job) { return a_job->Run(); }); });
		{
			std::unique_lock lock(mutex);
			cv.wait(lock, [&] { return entered; });
		}
		epoch.store(1);
		prePhysics.Clear();
		{
			std::lock_guard lock(mutex);
			release = true;
		}
		cv.notify_all();
		postHavok.Clear();
		processor.join();
		Require(finished && destroyed == 1, "Clear must wait for active jobs and release retained ownership");
		epoch.store(2);
		prePhysics.Process([](const auto& a_job) { return a_job->Run(); });
		Require(impulses == 0 && destroyed == 1, "a draining old phase must not enqueue work into a cleared phase");
	}
}

int main()
{
	try {
		ResetCancelsRetainedAndRejectsWork();
		ResetDrainsRunningPhaseAndStopsCrossPhaseEnqueue();
		std::cout << "job queue reset tests passed\n";
		return 0;
	} catch (const std::exception& a_error) {
		std::cerr << a_error.what() << '\n';
		return 1;
	}
}
