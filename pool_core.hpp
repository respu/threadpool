#ifndef THREADPOOL_POOLCORE_H
#define THREADPOOL_POOLCORE_H

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

#include "task.hpp"
#include "worker_thread.hpp"

namespace threadpool {

class pool_core : public std::enable_shared_from_this<pool_core>
{
 public:
  pool_core(unsigned int max_threads,
            bool start_paused,
            unsigned int despawn_time_ms)
    : m_max_threads(max_threads),
      m_despawn_time_ms(despawn_time_ms),
      m_threads_created(0),
      m_threads_running(0),
      m_join_requested(false)
  {
    m_threads.reserve(m_max_threads);
    if (start_paused)
    {
      pause();
    }
  }

  ~pool_core()
  {
    join(false);
  }
  
  template <typename T>
  std::future<T> add_task(std::function<T(void)> const & func,
                          unsigned int priority)
  {
    /*
     * If all created threads are executing tasks and we have not spawned the
     * maximum number of allowed threads, create a new thread.
     */
    if (m_threads_created == m_threads_running &&
        m_threads_created != m_max_threads)
    {
      m_threads.emplace_back(std::unique_ptr<worker_thread<pool_core>>(new
        worker_thread<pool_core>(shared_from_this())));
    }
    auto promise = std::make_shared<std::promise<T>>();
    std::lock_guard<std::mutex> task_lock(m_task_mutex);
    m_tasks.emplace(std::unique_ptr<task<T>>(
      new task<T>(func, priority, promise)));
    m_task_ready.notify_one();

    return promise->get_future();
  }

  void pause()
  {
    /*
     * First unlocks the pause mutex before locking it, ensuring that if the
     * current thread already owns the lock (i.e. pause() has been called prior
     * to the current call without a corresponding unpause()), no deadlock will
     * occur.
     */
    m_pause_mutex.unlock();
    m_pause_mutex.lock();
  }

  void unpause()
  {
    m_pause_mutex.unlock();
  }

  void run_task()
  {  
    while (m_threads_created <= m_max_threads)
    {
      m_pause_mutex.lock();
      m_pause_mutex.unlock();
      auto t = pop_task(m_despawn_time_ms);
      if (t)
      {
        ++m_threads_running;
        (*t)();
        --m_threads_running;
      }
      else if (m_join_requested.load())
      {
        return;
      }
    }
  }

  void wait()
  {
    std::unique_lock<std::mutex> task_lock(m_task_mutex);
    m_task_empty.wait(task_lock, [&]{ return m_tasks.empty(); });
  }

  std::unique_ptr<task_base> pop_task(unsigned int max_wait)
  {
    std::unique_ptr<task_base> ret;
    std::unique_lock<std::mutex> task_lock(m_task_mutex);
    while (m_tasks.empty())
    {
      if (m_join_requested || 
          m_task_ready.wait_for(task_lock, std::chrono::milliseconds(max_wait)) ==
            std::cv_status::timeout)
      {
        return ret;
      }
    }
    /*
     * This is OK because we immediately pop the task afterwards, so we don't
     * mess with the priority queue ordering invariant
     */
    ret = std::move(const_cast<std::unique_ptr<task_base>&>(m_tasks.top()));
    m_tasks.pop();

    if (m_tasks.empty())
    {
      m_task_empty.notify_all();
    }

    return ret;
  }

  bool empty()
  {
    std::lock_guard<std::mutex> task_lock(m_task_mutex);
    return m_tasks.empty();
  }

  void clear()
  {
    std::lock_guard<std::mutex> task_lock(m_task_mutex);
    while (!m_tasks.empty())
    {
      m_tasks.pop();
    }
  }

  void join(bool clear_tasks)
  {
    if (clear_tasks)
    {
      clear();
    }

    m_join_requested = true;

    for (auto&& thread : m_threads)
    {
      thread->join();
    }

    m_join_requested = false;

  }

  unsigned int get_threads_running() const
  {
    return m_threads_running.load();
  }

  unsigned int get_threads_created() const
  {
    return m_threads_created.load();
  }

  void set_max_threads(unsigned int max_threads)
  {
    m_max_threads = max_threads;
  }

  unsigned int get_max_threads() const
  {
    return m_max_threads;
  }

 private:
  std::vector<std::unique_ptr<worker_thread<pool_core>>> m_threads;
  std::priority_queue<std::unique_ptr<task_base>,
      std::vector<std::unique_ptr<task_base>>, task_comparator> m_tasks;

  std::mutex m_task_mutex, m_pause_mutex;
  std::condition_variable m_task_ready, m_task_empty;

  unsigned int m_max_threads, m_despawn_time_ms;

  std::atomic_uint m_threads_created, m_threads_running;
  std::atomic_bool m_join_requested;

  friend class worker_thread<pool_core>;
};

}

#endif //THREADPOOL_POOLCORE_H
