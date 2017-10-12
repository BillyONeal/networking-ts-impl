#include <experimental/tp_context>

#include "udp_socket_test.h"
#include "tcp_socket_test.h"
//#include "tp_context.h"
//#include "null_context.h"
#include <chrono>
#include <thread>
//#include <experimental/io_context>
#include <experimental/timer>
//#include <experimental/executor>
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

#if 1
  auto ex = io.get_executor();

  for (int i = 0; i < 1'000'000; ++i)
    post(ex, [&] { count.fetch_add(1, std::memory_order_relaxed); });
#endif
#if 0
  //post(io, [] { puts("work1"); });
  post(io.get_executor(), [] {
    puts("work2");
  });
#endif
  run(io);

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
#if 1
  system_timer unused_timer1(io);
#endif

#if 0
  system_timer DEDUCE(IoContext) timer(io);
  timer.async_wait([](std::error_code ec) {
    printf("Hi %d\n", ec.value());
  });
  timer.async_wait([](std::error_code ec) {
    printf("Hi2 %d\n", ec.value());
  });
#endif
#if 0
  system_timer DEDUCE(IoContext) timer2(io, 1000ms);
  timer2.async_wait([](std::error_code ec) {
    printf("After sleep %d\n", ec.value());
  });
  timer2.expires_after(0ms);
#endif
#if 0
  post(io, [] {});

  auto ex = io.get_executor();

  for (int i = 0; i < 10'000'000; ++i)
    post(ex, [&] { count.fetch_add(1, std::memory_order_relaxed); });
#endif
#if 0
  //post(io, [] { puts("work1"); });
  post(io.get_executor(), [] {
    puts("work2");
  });
#endif


#if 1
  system_timer fast_timer(io, 100000ms);

  fast_timer.async_wait([&io](auto ec) {
    if (ec) {
      printf("fast-timer => %u\n", ec.value());
      return;
    }
    puts("stopping...");
    io.stop();
  });
  fast_timer.expires_after(1000ms);
  system_timer faster_timer(io, system_clock::now() + 30ms);
  faster_timer.async_wait([&io](auto) { puts("tick"); });
#endif
  run(io);

  duration<double> elapsed = high_resolution_clock::now() - start;
  printf("run completed in %g seconds (%u)\n", elapsed.count(), count.load());
}

using namespace std::experimental::net;

extern void not_inline_check();

void run(io_context& ioc, int thread_count) {
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
    //udp_socket_test<io_context>("io_context", [](auto& io) { run(io, 8); });
    //udp_socket_test<tp_context>("tp_context", [](auto& io) { io.join(); });
    //udp_socket_test<io_context>("io_context", [](auto& io) { run(io, 8); });
    //tcp_socket_test<io_context>("io_context", [](auto& io) { run(io, 8); });
    tcp_socket_test<tp_context>("tp_context", [](auto& io) { io.join(); });
    //post_test<io_context>("io_context", [](auto& io) { run(io, 8); });
    //post_test<tp_context>("tp_context", [](auto& io) { io.join(); });
    //timer_test<io_context>("io_context", [](auto& io) { run(io, 8);  });
    //timer_test<tp_context>("tp_context", [](auto& io) { io.join(); });
    //test<null_context>("null_context", [](auto& io) { io.join(); });
    //printf("%d\n", is_executor<tp_executor>::value);
  }
  catch (std::exception& e) {
    printf("caught: %s\n", e.what());
  }
  return 0;
}
