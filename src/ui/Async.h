#pragma once
#include "ui/Messages.h"
#include <Messenger.h>
#include <Alert.h>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <memory>
#include <string>
namespace tc {
// Owns both pending jobs and delivered results. Closing a window joins workers
// and releases queued results even when their notification messages are dropped.
class AsyncQueue {
public:
    using Callback=std::function<void()>;
    using Work=std::function<Callback(const std::atomic<bool>&)>;
    using Failure=std::function<void(const std::string&)>;
    explicit AsyncQueue(BMessenger target,int count=1):fTarget(target) {
        for(int i=0;i<count;++i) fThreads.emplace_back([this] {
            while(true) {
                std::shared_ptr<Job> job;
                {
                    std::unique_lock<std::mutex> lock(fMutex);
                    fWake.wait(lock,[&]{return fStop || !fWork.empty();});
                    if(fStop) return;
                    job=std::move(fWork.front());fWork.pop_front();fRunning.push_back(job);
                }
                Callback callback;
                try { if(!job->cancelled) callback=job->work(job->cancelled); }
                catch(const std::exception& error) {
                    std::string detail=error.what();auto failure=job->failure;
                    callback=[failure,detail] {
                        if(failure) failure(detail);
                        else (new BAlert("Background Operation",detail.c_str(),"OK"))->Go();
                    };
                }
                bool notify=false;
                {
                    std::lock_guard<std::mutex> lock(fMutex);
                    fRunning.erase(std::remove(fRunning.begin(),fRunning.end(),job),fRunning.end());
                    if(fStop) return;
                    if(callback && !job->cancelled) {
                        notify=fDone.empty();
                        fDone.push_back([job,callback=std::move(callback)] { if(!job->cancelled) callback(); });
                    }
                }
                // Never hold a worker indefinitely on a window's full message port.
                // Retrying a bounded send also lets shutdown cancel the wait.
                if(notify) {
                    BMessage message(kWorkDone);
                    while(!fStop) {
                        auto status=fTarget.SendMessage(&message,static_cast<BHandler*>(nullptr),100000);
                        if(status!=B_TIMED_OUT && status!=B_WOULD_BLOCK) break;
                    }
                }
            }
        });
    }
    ~AsyncQueue() { StopAndWait(); }
    void StopAndWait() {
        { std::lock_guard<std::mutex> lock(fMutex);fStop=true;
          for(auto& job:fWork) job->cancelled=true;
          for(auto& job:fRunning) job->cancelled=true; }
        fWake.notify_all();
        for(auto& thread:fThreads) if(thread.joinable()) thread.join();
    }
    void Submit(Work work,std::string key={},Failure failure={}) {
        auto job=std::make_shared<Job>();job->work=std::move(work);job->key=std::move(key);job->failure=std::move(failure);
        { std::lock_guard<std::mutex> lock(fMutex);
          if(!job->key.empty()) CancelLocked(job->key);
          fWork.push_back(std::move(job)); }
        fWake.notify_one();
    }
    void Cancel(const std::string& key) { std::lock_guard<std::mutex> lock(fMutex);CancelLocked(key); }
    // Deliver results from persistent services without tying up a worker.
    // The workspace's language tick also drains results if its port was full.
    void Post(Callback callback) {
        { std::lock_guard<std::mutex> lock(fMutex);if(fStop) return;fDone.push_back(std::move(callback)); }
        BMessage message(kWorkDone);fTarget.SendMessage(&message,static_cast<BHandler*>(nullptr),1000);
    }
    void Drain() {
        std::deque<Callback> done;
        { std::lock_guard<std::mutex> lock(fMutex);done.swap(fDone); }
        for(auto& callback:done) callback();
    }
private:
    struct Job { Work work;Failure failure;std::string key;std::atomic<bool> cancelled{false}; };
    void CancelLocked(const std::string& key) {
        for(auto& job:fWork) if(job->key==key) job->cancelled=true;
        for(auto& job:fRunning) if(job->key==key) job->cancelled=true;
        fWork.erase(std::remove_if(fWork.begin(),fWork.end(),[](const auto& job){return job->cancelled.load();}),fWork.end());
    }
    BMessenger fTarget;
    std::atomic<bool> fStop{false};
    std::mutex fMutex;
    std::condition_variable fWake;
    std::deque<std::shared_ptr<Job>> fWork;
    std::vector<std::shared_ptr<Job>> fRunning;
    std::deque<Callback> fDone;
    std::vector<std::thread> fThreads;
};
}
