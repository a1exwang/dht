#pragma once

#include <mutex>
#include <memory>
#include <set>

namespace albert::common {

#ifdef ALBERT_DEBUG_MEMORY
template <typename T>
class wp;

template <typename T>
class sp {
 public:
  static std::set<sp<T>*> instances;
  static std::atomic<size_t> instance_count;
  static std::mutex instances_lock;
  sp();
  sp(T *p);
  sp(const sp<T> &rhs);
  sp<T> &operator=(const sp<T> &rhs);
  ~sp();

  const T *operator->() const {
    return impl_.get();
  }
  T *operator->() {
    return impl_.get();
  }
  operator bool() const {
    return impl_ != nullptr;
  }
  const T *get() const { return impl_.get(); }
  T *get() { return impl_.get(); }
 private:
  sp(std::shared_ptr<T> impl);
  template <typename U> friend class wp;

  std::shared_ptr<T> impl_;
};

template <typename T>
class wp {
 public:
  wp(sp<T> p) :impl_(p.impl_) { }

  sp<T> lock() {
    auto std_sp = impl_.lock();
    return sp<T>(std_sp);
  }
  sp<const T> lock() const {
    auto std_sp = impl_.lock();
    return sp<const T>(std_sp);
  }
 private:
  template <typename U> friend class sp;
  std::weak_ptr<T> impl_;
};

template<class T, class ...Args>
sp<T> make_shared(Args&& ...args) {
  return sp<T>(new T(std::forward<Args>(args)...));
}

template <typename T>
class enable_shared_from_this {
 public:
  sp<T> shared_from_this() {
    auto std_sp = impl_.shared_from_this();
  }
 private:
  std::enable_shared_from_this<T> impl_;
};

#else

template <typename T>
using sp = std::shared_ptr<T>;
template <typename T>
using wp = std::weak_ptr<T>;

template<class T, class ...Args>
auto make_shared(Args&&...args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T>
using enable_shared_from_this = std::enable_shared_from_this<T>;

#endif // ALBERT_DEBUG_MEMORY

template <typename T>
using up = std::unique_ptr<T>;

}