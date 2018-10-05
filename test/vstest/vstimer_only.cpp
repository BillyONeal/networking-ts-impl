#include "udp_socket_test.h"
#include "tcp_socket_test.h"
#include <chrono>
#include <thread>
#include <experimental/io_context>
#include <experimental/timer>
#include <experimental/executor>
#include <utility>

template <typename IoContext, typename F>
void post_test(const char* name, F run) {
  using namespace std::chrono;
  using namespace std::experimental::net;

  auto start = high_resolution_clock::now();
  std::atomic<unsigned> count = 0;

  printf("testing %s ...\n", name);
  IoContext io;
  post(io, [] { puts("work"); });

  auto ex = io.get_executor();
  auto targetCount = 1'000'000u;
  for (unsigned i = 0; i < targetCount; ++i)
    post(ex, [&] { count.fetch_add(1, std::memory_order_relaxed); });

  run(io);

  assert(count.load(std::memory_order_relaxed) == targetCount);

  duration<double> elapsed = high_resolution_clock::now() - start;
  printf("run completed in %g seconds (%u)\n", elapsed.count(), count.load());
}

template <typename IoContext, typename F>
void timer_test(const char* name, F run) {
  using namespace std::chrono;
  using namespace std::experimental::net;

  auto start = high_resolution_clock::now();
  std::atomic<unsigned> count = 0;

  printf("testing %s ...\n", name);
  IoContext io;
  system_timer slow_timer(io, 24h);

  slow_timer.async_wait([&io](auto ec) {
    if (ec) {
      printf("slow-timer => %u\n", ec.value());
      return;
    }
    puts("Slow timer fired. You either have a lot of patience or there is a "
         "bug");
  });

  system_timer fast_timer(io, 100ms);

  fast_timer.async_wait([&io](auto ec) {
    if (ec) {
      printf("fast-timer => %u\n", ec.value());
      return;
    }
    puts("stopping...");
    io.stop();
  });
  //fast_timer.expires_after(1000ms);
  system_timer faster_timer(io, system_clock::now() + 30ms);
  faster_timer.async_wait([&io](auto) { puts("tick"); });
  run(io);

  duration<double> elapsed = high_resolution_clock::now() - start;
  printf("run completed in %g seconds (%u)\n", elapsed.count(), count.load());
}

using namespace std::experimental::net;

extern void not_inline_check();

void run(manual_io_context& ioc, int thread_count) {
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  while (--thread_count > 0) {
    threads.emplace_back([&ioc] { ioc.run(); });
  }

  ioc.run();

  while (!threads.empty()) {
    threads.back().join();
    threads.pop_back();
  }
}

struct Noisy {
  Noisy() { printf("%x: thread ctor\n", GetCurrentThreadId()); }
  ~Noisy() { printf("%x: thread dtor\n", GetCurrentThreadId()); }
};

//thread_local Noisy noisy;
//4.65015 Mbytes written per second
//4.65002 Mbytes read per second
//31.4194 Mbytes written per second
//31.4193 Mbytes read per second

int main() {
  try {
    printf("%x: main\n", GetCurrentThreadId());
    not_inline_check();
    udp_socket_test<manual_io_context>("manual_io_context", [](auto& io) { run(io, 8); });
    tcp_socket_test<manual_io_context>("manual_io_context", [](auto& io) { run(io, 8); });
    post_test<manual_io_context>("manual_io_context", [](auto& io) { run(io, 8); });
    post_test<tp_context>("tp_context", [](auto& io) { io.cancel_all(); io.join(); });
    //timer_test<manual_io_context>("manual_io_context", [](auto& io) { run(io, 8); });
    //timer_test<tp_context>("tp_context", [](auto& io) { io.join(); });
  }
  catch (std::exception& e) {
    printf("caught: %s\n", e.what());
  }
  return 0;
}
