/**
 * @file        rex/core/fiber_posix.cpp
 * @brief       POSIX backend for rex::thread::Fiber (makecontext/swapcontext)
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include <rex/platform.h>
#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC

#include <rex/thread/fiber.h>
#include <cassert>

#if defined(__ANDROID__)

// ============================================================================
// ANDROID EMULATION BACKEND (Fibers over Threads)
// O Android NDK removeu as funções POSIX ucontext (getcontext, makecontext)
// por questões de segurança. Esta é uma implementação substituta baseada em std::thread.
// ============================================================================

#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace rex::thread {

struct AndroidFiberState {
  std::mutex mtx;
  std::condition_variable cv;
  bool running = false;
  bool terminating = false;
  std::thread os_thread;
};

static std::unordered_map<Fiber*, AndroidFiberState*> s_fiber_states;
static std::mutex s_fiber_states_mtx;

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  auto* f = new Fiber();
  f->is_thread_fiber_ = true;
  tls_current_ = f;

  auto* state = new AndroidFiberState();
  state->running = true;

  std::lock_guard<std::mutex> lock(s_fiber_states_mtx);
  s_fiber_states[f] = state;

  return f;
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
  auto* f = new Fiber();
  f->entry_ = entry;
  f->arg_ = arg;
  f->stack_.resize(stack_size);

  auto* state = new AndroidFiberState();
  state->running = false;

  {
    std::lock_guard<std::mutex> lock(s_fiber_states_mtx);
    s_fiber_states[f] = state;
  }

  state->os_thread = std::thread([f, state]() {
    {
      std::unique_lock<std::mutex> lock(state->mtx);
      state->cv.wait(lock, [state]() { return state->running || state->terminating; });
    }

    if (!state->terminating) {
      tls_current_ = f;
      f->entry_(f->arg_);
    }
  });

  return f;
}

/*static*/ void Fiber::Trampoline() {
  // Não é utilizado no backend do Android
}

void Fiber::SwitchTo(Fiber* target) {
  Fiber* from = tls_current_;

  AndroidFiberState* from_state = nullptr;
  AndroidFiberState* target_state = nullptr;

  {
    std::lock_guard<std::mutex> lock(s_fiber_states_mtx);
    from_state = s_fiber_states[from];
    target_state = s_fiber_states[target];
  }

  if (!from_state || !target_state) return;

  // Acorda a thread de destino
  {
    std::lock_guard<std::mutex> lock(target_state->mtx);
    target_state->running = true;
  }
  target_state->cv.notify_one();

  // Suspende a thread atual
  {
    std::unique_lock<std::mutex> lock(from_state->mtx);
    from_state->running = false;
    from_state->cv.wait(lock, [from_state]() { return from_state->running || from_state->terminating; });
  }

  tls_current_ = from;
}

void Fiber::Destroy() {
  if (is_thread_fiber_) {
    tls_current_ = nullptr;
  } else {
    assert(this != tls_current_ && "Destroy called on the currently running fiber");
  }

  AndroidFiberState* state = nullptr;
  {
    std::lock_guard<std::mutex> lock(s_fiber_states_mtx);
    auto it = s_fiber_states.find(this);
    if (it != s_fiber_states.end()) {
      state = it->second;
      s_fiber_states.erase(it);
    }
  }

  if (state) {
    if (!is_thread_fiber_ && state->os_thread.joinable()) {
      {
        std::lock_guard<std::mutex> lock(state->mtx);
        state->terminating = true;
        state->running = true;
      }
      state->cv.notify_one();
      state->os_thread.join();
    }
    delete state;
  }

  delete this;
}

}  // namespace rex::thread

#else

// ============================================================================
// ORIGINAL POSIX IMPLEMENTATION (Linux/Mac)
// ============================================================================

namespace rex::thread {

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  auto* f = new Fiber();
  if (getcontext(&f->context_) == -1) {
    delete f;
    return nullptr;
  }
  f->is_thread_fiber_ = true;
  tls_current_ = f;
  return f;
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
  auto* f = new Fiber();
  f->entry_ = entry;
  f->arg_ = arg;
  f->stack_.resize(stack_size);

  if (getcontext(&f->context_) == -1) {
    delete f;
    return nullptr;
  }
  f->context_.uc_stack.ss_sp = f->stack_.data();
  f->context_.uc_stack.ss_size = f->stack_.size();
  f->context_.uc_link = nullptr;
  // Trampoline reads entry_/arg_ from tls_current_ — no pointer splitting needed.
  makecontext(&f->context_, &Fiber::Trampoline, 0);
  return f;
}

/*static*/ void Fiber::Trampoline() {
  // tls_current_ was updated by SwitchTo before swapcontext returned here.
  Fiber* f = tls_current_;
  f->entry_(f->arg_);
}

void Fiber::SwitchTo(Fiber* target) {
  Fiber* from = tls_current_;
  tls_current_ = target;
  swapcontext(&from->context_, &target->context_);
}

void Fiber::Destroy() {
  // Thread fibers are destroyed from the owning thread itself.
  if (is_thread_fiber_) {
    tls_current_ = nullptr;
  } else {
    assert(this != tls_current_ && "Destroy called on the currently running fiber");
  }
  // No POSIX equivalent of ConvertFiberToThread; stack_ is freed by the vector destructor.
  delete this;
}

}  // namespace rex::thread

#endif // defined(__ANDROID__)

#endif  // REX_PLATFORM_LINUX || REX_PLATFORM_MAC
