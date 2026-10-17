namespace std::chrono {

template <class, class>
struct clock_time_conversion {};

// A correção (polyfill) para a libc++ agora é aplicada para todos os sistemas
// que utilizem a biblioteca LLVM, não apenas __APPLE__.
template <class DestClock, class SourceClock, class Duration>
auto clock_cast(const std::chrono::time_point<SourceClock, Duration>& t) {
  return clock_time_conversion<DestClock, SourceClock>{}(t);
}

template <>
struct clock_time_conversion<::rex::chrono::WinSystemClock, ::rex::chrono::XSystemClock> {
  using WClock_ = ::rex::chrono::WinSystemClock;
  using XClock_ = ::rex::chrono::XSystemClock;

  template <typename Duration>
  typename WClock_::time_point operator()(
      const std::chrono::time_point<XClock_, Duration>& t) const {
    // Consult chrono_steady_cast.h for explanation on this:
    std::atomic_thread_fence(std::memory_order_acq_rel);
    auto w_now = WClock_::now();
    auto x_now = XClock_::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    auto delta = (t - x_now);
    if (!REXCVAR_GET(clock_no_scaling)) {
      delta =
          std::chrono::floor<WClock_::duration>(delta * rex::chrono::Clock::guest_time_scalar());
    }
    return w_now + delta;
  }
};

template <>
struct clock_time_conversion<::rex::chrono::XSystemClock, ::rex::chrono::WinSystemClock> {
  using WClock_ = ::rex::chrono::WinSystemClock;
  using XClock_ = ::rex::chrono::XSystemClock;

  template <typename Duration>
  typename XClock_::time_point operator()(
      const std::chrono::time_point<WClock_, Duration>& t) const {
    // Consult chrono_steady_cast.h for explanation on this:
    std::atomic_thread_fence(std::memory_order_acq_rel);
    auto w_now = WClock_::now();
    auto x_now = XClock_::now();
    std::atomic_thread_fence(std::memory_order_acq_rel);

    rex::chrono::hundrednanoseconds delta = (t - w_now);
    if (!REXCVAR_GET(clock_no_scaling)) {
      delta =
          std::chrono::floor<WClock_::duration>(delta / rex::chrono::Clock::guest_time_scalar());
    }
    return x_now + delta;
  }
};

}  // namespace std::chrono
