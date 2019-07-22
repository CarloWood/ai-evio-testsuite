#include <iostream>
#include <functional>
#include <cstring>

void h(std::function<void(void*)>&& f, void* g)
{
  f(g);
}

template<size_t number_of_size_t>
void do_test()
{
  size_t a[number_of_size_t];
  std::memset(a, 0, sizeof(a));
  a[0] = sizeof(a);

  void* device = &a;
  uint32_t epoll_events = 42;
  int32_t epoll_fd = 13;

  if (number_of_size_t == 5)
  {
    std::function<void(void*)> g = [device, events = epoll_events, epoll_fd](void* ptr) {
      if (&device != ptr)
        std::cout << "malloc was called when capturing what EventLoopThread passes to the thread pool!" << std::endl;
      else
        std::cout << "No allocation took place when capturing what EventLoopThread passes to the thread pool: " << device << ", " << events << ", " << epoll_fd << '.' << std::endl;
    };
    h(std::move(g), &g);
  }
  else
  {
    std::function<void(void*)> g = [a](void* ptr) {
      if (&a != ptr)
        std::cout << "malloc was called when capturing " << a[0] << " bytes." << std::endl;
      else
        std::cout << "No allocation took place when capturing " << a[0] << " bytes." << std::endl;
    };
    h(std::move(g), &g);
  }
}

int main()
{
  do_test<1>();
  do_test<2>();
  do_test<3>();
  do_test<4>();
  do_test<5>();
}
