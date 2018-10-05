#include "udp_socket_test.h"
#include "tcp_socket_test.h"
#include <chrono>
#include <thread>
#include <experimental/io_context>
#include <experimental/timer>
#include <experimental/executor>
#include <utility>

template <typename Traits>
void post_test(Traits) {
  using namespace std::chrono;
  using namespace std::experimental::net;

  std::atomic<unsigned> count = 0;

  auto targetCount = 1'000'000u;
  printf("Posting and running %u increment tasks on %s ...\n", targetCount, Traits::name);
  auto start = steady_clock::now();
  typename Traits::IoContext io;
  auto ex = io.get_executor();
  for (unsigned i = 0; i < targetCount; ++i) {
    post(ex, [&] { count.fetch_add(1, std::memory_order_relaxed); });
  }

  auto afterPost = steady_clock::now();
  printf("Posting took %g seconds.\n", static_cast<duration<double>>(afterPost - start).count());

  Traits::run(io);

  assert(count.load(std::memory_order_relaxed) == targetCount);

  duration<double> elapsed = steady_clock::now() - start;
  printf("Completed in %g seconds.\n", elapsed.count());
}

template <typename Traits>
void timer_test(Traits) {
  using namespace std::chrono;
  using namespace std::experimental::net;

  printf("Retiring timers on %s ...\n", Traits::name);

  auto start = steady_clock::now();
  typename Traits::IoContext io;
  system_timer slow_timer(io, 24h);
  slow_timer.async_wait([&io](auto ec) {
    if (ec) {
      printf("slow-timer => %u\n", ec.value());
      return;
    }

    puts("Slow timer fired. You either have a lot of patience or there is a bug");
  });

  system_timer fast_timer(io, 100ms);
  fast_timer.async_wait([&io](auto ec) {
    if (ec) {
      printf("fast-timer => %u\n", ec.value());
      return;
    }

    puts("stopping...");
    Traits::stop(io);
  });

  system_timer faster_timer(io, system_clock::now() + 30ms);
  faster_timer.async_wait([&io](auto) { puts("tick"); });
  Traits::run(io);

  duration<double> elapsed = steady_clock::now() - start;
  printf("Timer test completed in %g seconds\n", elapsed.count());
}

using namespace std::experimental::net;

extern void not_inline_check();


struct Noisy {
  Noisy() { printf("%x: thread ctor\n", GetCurrentThreadId()); }
  ~Noisy() { printf("%x: thread dtor\n", GetCurrentThreadId()); }
};

//thread_local Noisy noisy;
//4.65015 Mbytes written per second
//4.65002 Mbytes read per second
//31.4194 Mbytes written per second
//31.4193 Mbytes read per second

struct manual_run_traits {
  using IoContext = manual_io_context;
  static constexpr const char * name = "manual_io_context";
  static constexpr size_t thread_count = 8;
  static void run(manual_io_context& ioc) {
    using namespace std;
    using namespace std::chrono;
    vector<thread> threads;
    threads.reserve(thread_count);
    printf("Starting %zu manual threads (totalling %zu) and running test.\n",
      thread_count - 1, thread_count);
    const auto start = steady_clock::now();
    for (size_t i = 1; i < thread_count; ++i) { // start at 1 to account for the current thread
      threads.emplace_back([&ioc] { ioc.run(); });
    }

    ioc.run();

    const auto stop = steady_clock::now();

    printf("Test completed in %g seconds, joining threads.\n",
      duration_cast<duration<double>>(stop - start).count());

    for (thread& t : threads) {
      t.join();
    }

    threads.clear();

    const auto joined = steady_clock::now();

    printf("Joined in %g seconds, total %g seconds.\n",
      duration_cast<duration<double>>(joined - stop).count(),
      duration_cast<duration<double>>(joined - start).count());
  }

  static void stop(manual_io_context& ioc) {
    ioc.stop();
  }
};

struct tp_run_traits {
  using IoContext = tp_context;
  static constexpr const char * name = "tp_context";
  static void run(tp_context& ioc) {
    puts("Starting tp_context join.");
    ioc.join();
    puts("Done with tp_context join.");
  }

  static void stop(tp_context& ioc) {
    ioc.cancel_all();
  }
};

int main() {
  try {
    not_inline_check();
    udp_socket_test(manual_run_traits{});
    tcp_socket_test(manual_run_traits{});
    post_test(manual_run_traits{});
    post_test(tp_run_traits{});
    timer_test(manual_run_traits{});
  }
  catch (std::exception& e) {
    printf("caught: %s\n", e.what());
  }
  return 0;
}
