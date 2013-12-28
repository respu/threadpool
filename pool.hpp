#ifndef THREADPOOL_POOL_H
#define THREADPOOL_POOL_H

#include "pool_core.hpp"

namespace threadpool {

class threadpool
{
public:
  threadpool(unsigned int max_threads = std::thread::hardware_concurrency(),
            bool start_paused = false) :
    m_core(new pool_core(max_threads, start_paused)) {}

  inline void add_task(std::function<void(void)> const & func,
                      unsigned int priority = 0)
  {
    m_core->add_task(func, priority);
  }

  inline void pause()
  {
    m_core->pause();
  }

  inline void unpause()
  {
    m_core->unpause();
  }

  inline bool empty()
  {
    return m_core->empty();
  }

  inline void clear()
  {
    m_core->clear();
  }

  inline void wait(bool clear_tasks)
  {
    m_core->wait(clear_tasks);
  }

  inline unsigned int get_threads_running() const
  {
    return m_core->get_threads_running();
  }

  inline unsigned int get_threads_created() const
  {
    return m_core->get_threads_created();
  }

  inline void set_max_threads(unsigned int max_threads)
  {
    m_core->set_max_threads(max_threads);
  }

  inline unsigned int get_max_threads() const
  {
    return m_core->get_max_threads();
  }

private:
  std::shared_ptr<pool_core> m_core;
};

}

#endif //THREADPOOL_POOL_H
