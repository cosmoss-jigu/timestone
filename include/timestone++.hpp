#pragma once
#include <timestone.h>
#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

namespace ts {

class ts_thread;
template <typename T>
class ts_cached_ptr;

enum ts_isolation {
  snapshot = TS_SNAPSHOT,
  serializability = TS_SERIALIZABILITY,
  linearizability = TS_LINEARIZABILITY,
};

enum ts_rw {
  read_only = 0,
  read_write,
  unknown = read_write,
};

class ts_thread : public std::thread {
 public:
  template <class F, class... A>
  explicit ts_thread(F &&f, A &&... args)
      : thread(thread_wrapper(f, args...)) {}

  void flush_log(void) { ::ts_flush_log(self); }

 private:
  template <class F, class... A>
  auto thread_wrapper(F &&f, A &&... args) {
    auto w = [&]() {
      assert(self == nullptr);

      // Initialize a timestone thread struct
      self = ::ts_thread_alloc();
      ::ts_thread_init(self);

      // Run a user-provided thread main
      f(args...);

      // Clean up the timestone thread struct
      ::ts_thread_finish(self);
      ::ts_thread_free(self);
      self = nullptr;
    };
    return w;
  }

 public:
  static thread_local ::ts_thread_struct_t *self;

 private:
  static thread_local std::jmp_buf bots;
  friend class ts_txn;
  template <typename Y>
  friend void ts_lock(ts_cached_ptr<Y> &ptr);
  template <typename Y>
  friend void ts_lock_const(ts_cached_ptr<Y> const &ptr);
};

class ts_system {
 public:
  static int init(::ts_conf_t *conf = nullptr) noexcept {
    // Use default configuration if conf is not specified
    ::ts_conf_t def_conf;
    if (!conf) {
      init_conf(def_conf);
      conf = &def_conf;
    }

    // Initialize timestone
    auto rc = ::ts_init(conf);

    // Initialize the main thread
    init_main_thread();
    return rc;
  }

  static void finish(void) noexcept {
    // Finish the main thread
    finish_main_thread();

    // Finish timestone
    ::ts_finish();
  }

  static void reset_stats(void) noexcept { ::ts_reset_stats(); }
  static void print_stats(void) noexcept { ::ts_print_stats(); }

  static bool is_supported(ts_isolation iso) noexcept {
    return ts_isolation_supported(iso);
  };

 private:
  static void init_main_thread(void) {
    // Initialize the main thread
    assert(ts_thread::self == nullptr);

    // Initialize a timestone thread struct
    ts_thread::self = ::ts_thread_alloc();
    ::ts_thread_init(ts_thread::self);
  }

  static void finish_main_thread(void) {
    // Clean up the timestone thread struct
    ::ts_thread_finish(ts_thread::self);
    ::ts_thread_free(ts_thread::self);
    ts_thread::self = nullptr;
  }

  static void init_conf(::ts_conf_t &conf) {
    std::strcpy(conf.nvheap_path, NVHEAP_POOL_PATH);
    conf.nvheap_size = NVHEAP_POOL_SIZE;
  }

  ts_system() = delete;
};

class ts_txn {
 public:
  template <class F, class... A>
  static void run(ts_isolation iso, ts_rw rw, F &&f, A &&... args) {
    if (rw == read_write) {
      // Serialize operation for a write operation
      serialize_op(f, args...);

      // NOTE: We intentionally use setjump()-longjump()
      // because C++ exception handling is not scalable.
      // Ref: https://github.com/scylladb/seastar/issues/73
      setjmp(ts_thread::bots);

      // NOTE: rw would not be read_write
      // if an application calls ts_lock()
      // in a read-only mode.
      assert(rw == read_write);
    }

    // Retry loop until succeed
    for (;;) {
      // Transaction begin
      ts_begin(ts_thread::self, iso);
      // Execute a given function
      f(args...);
      // Transaction end if it succeeds.
      if (ts_end(ts_thread::self)) {
        return;
      }
      // Transaction abort if it fails.
      ::ts_abort(ts_thread::self);
    }
    assert(0 && "Never be here!");
  }

  template <class F, class... A>
  static void run_ro(ts_isolation iso, F &&f, A &&... args) {
    run(iso, read_only, f, args...);
  }

  template <class F, class... A>
  static void run_rw(ts_isolation iso, F &&f, A &&... args) {
    run(iso, read_write, f, args...);
  }

 private:
  template <class F, class... A>
  static void serialize_op(F &&f, A &&... args) {
    // TODO: implement
  }

  ts_txn() = delete;
};

class ts_object {
 public:
  void *operator new(std::size_t size) { return ::ts_alloc(size); }

  void operator delete(void *ptr) { ::ts_free(ts_thread::self, ptr); }

  /**
   * Do not allow creating an array.
   */
  void *operator new[](std::size_t size) = delete;
  void operator delete[](void *ptr) = delete;
};

template <typename T>
class ts_persistent_ptr {
 public:
  /**
   * Default constructor
   */
  ts_persistent_ptr() noexcept : act_ptr(nullptr) {}

  /**
   * Constructor with a raw pointer
   */
  ts_persistent_ptr(T *raw_ptr) noexcept {
    ::ts_assign_ptr(ts_thread::self, &act_ptr, raw_ptr);
  }

  /**
   * Constructor with a cached pointer
   */
  ts_persistent_ptr(const ts_cached_ptr<T> &cch_ptr) noexcept
      : ts_persistent_ptr(cch_ptr.raw()) {}

  /**
   * function for array to update index and offset
   */
  void update_index(ts_persistent_ptr<T> p_ptr, unsigned int _index) noexcept {
    T *ptr = p_ptr.raw();
    ::ts_update_index(ptr, _index);
  }

  ts_persistent_ptr<T> &operator+(unsigned int _index) {
    static ts_persistent_ptr<T> offset;
    T *raw;
    unsigned int curr_index = ::ts_get_index(act_ptr);
    unsigned int target_index = curr_index + _index;
    raw = (T *)::ts_get_offset(act_ptr, target_index);
    offset = raw;
    return offset;
  }

  ts_persistent_ptr<T> &operator-(unsigned int _index) {
    static ts_persistent_ptr<T> offset;
    T *raw;
    unsigned int curr_index = ::ts_get_index(act_ptr);
    unsigned int target_index = curr_index - _index;
    raw = (T *)::ts_get_offset(act_ptr, target_index);
    offset = raw;
    return offset;
  }

  friend ts_persistent_ptr<T> &operator+=(ts_persistent_ptr<T> &pst_ptr,
                                          unsigned int _index) {
    T *raw;
    unsigned int curr_index, target_index;

    curr_index = ::ts_get_index(pst_ptr.raw());
    target_index = curr_index + _index;
    raw = (T *)::ts_get_offset(pst_ptr.raw(), target_index);
    pst_ptr = raw;
    return pst_ptr;
  }

  friend ts_persistent_ptr<T> &operator-=(ts_persistent_ptr<T> &pst_ptr,
                                          unsigned int _index) {
    T *raw;
    unsigned int curr_index, target_index;

    curr_index = ::ts_get_index(pst_ptr.raw());
    target_index = curr_index - _index;
    raw = (T *)::ts_get_offset(pst_ptr.raw(), target_index);
    pst_ptr = raw;
    return pst_ptr;
  }

  ts_persistent_ptr<T> &operator++() {
    static ts_persistent_ptr<T> offset;
    T *raw;
    unsigned int curr_index = ::ts_get_index(act_ptr);
    unsigned int target_index = curr_index + 1;
    raw = (T *)::ts_get_offset(act_ptr, target_index);
    offset = raw;
    return offset;
  }

  ts_persistent_ptr<T> operator++(int) {
    unsigned int temp = this->index;
    // this->index += 1;
    // TODO: need to recheck with changwoo
    return *(this->offset + temp);
  }

  ts_persistent_ptr<T> &operator--() {
    static ts_persistent_ptr<T> offset;
    T *raw;
    unsigned int curr_index = ::ts_get_index(act_ptr);
    unsigned int target_index = curr_index - 1;
    raw = (T *)::ts_get_offset(act_ptr, target_index);
    offset = raw;
    return offset;
  }

  ts_persistent_ptr<T> operator--(int) {
    unsigned int temp = this->index;
    // this->index -= 1;
    // TODO: need to recheck with changwoo
    return *(this->offset + temp);
  }
  /**
   * Assignment operator with a raw pointer
   */
  ts_persistent_ptr<T> &operator=(T *raw_ptr) noexcept {
    ::ts_assign_ptr(ts_thread::self, &act_ptr, raw_ptr);
    return *this;
  }

  /**
   * Assignment operator with a cached pointer
   */
  ts_persistent_ptr<T> &operator=(const ts_cached_ptr<T> &cch_ptr) noexcept {
    return operator=(cch_ptr.raw());
  }

  /**
   * Deference operator
   */
  inline T &operator*() const noexcept { return *deref(act_ptr); }

  /**
   * Member access operator
   */
  inline T *operator->() const noexcept { return deref(act_ptr); };

  /**
   * Raw pointer
   */
  inline T *raw() const noexcept { return act_ptr; }

  /**
   * Type conversion to raw pointer
   */
  inline operator T *() const { return raw(); }

 private:
  static inline T *deref(T *ptr) {
    //	std::cout << "ptp: deref ptr called for" << (void *) ptr << std::endl;
    return reinterpret_cast<T *>(
        ::ts_deref(ts_thread::self, reinterpret_cast<void *>(ptr)));
  }

  template <typename Y>
  friend class ts_cached_ptr;

 private:
  T *act_ptr;
};

template <typename T>
class ts_cached_ptr {
 public:
  /**
   * Default constructor
   */
  ts_cached_ptr() noexcept : copy_ptr(nullptr) {}

  /**
   * Constructor with a raw pointer
   */
  ts_cached_ptr(T *raw_ptr) noexcept
      : copy_ptr(ts_persistent_ptr<T>::deref(raw_ptr)) {}

  /**
   * Constructor with a persistent pointer
   */
  ts_cached_ptr(const ts_persistent_ptr<T> &pst_ptr) noexcept
      : ts_cached_ptr(pst_ptr.raw()) {}

  /**
   * Assignment operator with a raw pointer
   */
  ts_cached_ptr<T> &operator=(T *raw_ptr) noexcept {
    //	std::cout << "ccp: raw ptr assign called for" << (void *) raw_ptr <<
    // std::endl;
    copy_ptr = ts_persistent_ptr<T>::deref(raw_ptr);
    return *this;
  }

  /**
   * Assignment operator with a void raw pointer
   */
  ts_cached_ptr<T> &operator=(void *raw_ptr) noexcept {
    T *_raw_ptr;

    //	std::cout << "ccp: void ptr conv called for" << (void *) raw_ptr <<
    // std::endl;
    _raw_ptr = reinterpret_cast<T *>(raw_ptr);
    return operator=(_raw_ptr);
  }

  /**
   * Assignment operator with a persistent pointer
   */
  ts_cached_ptr<T> &operator=(const ts_persistent_ptr<T> &pst_ptr) noexcept {
    return operator=(pst_ptr.raw());
  }

  ts_cached_ptr<T> &operator=(const ts_persistent_ptr<T> *pst_ptr) noexcept {
    return operator=(pst_ptr->raw());
  }

  /**
   * Deference operator
   */
  inline T &operator*() const noexcept { return *copy_ptr; }

  /**
   * Member access operator
   */
  inline T *operator->() const noexcept { return copy_ptr; };

  /**
   * Raw pointer
   */
  inline T *raw() const noexcept { return copy_ptr; }

  /**
   * Type conversion to raw pointer
   */
  inline operator T *() const { return raw(); }

 private:
  /**
   * Do not allow array access operations.
   */
  ts_cached_ptr<T> &operator[](std::ptrdiff_t i) = delete;
  ts_cached_ptr<T> &operator++() = delete;
  ts_cached_ptr<T> operator++(int) = delete;
  ts_cached_ptr<T> &operator--() = delete;
  ts_cached_ptr<T> operator--(int) = delete;
  ts_cached_ptr<T> &operator+=(std::ptrdiff_t s) = delete;
  ts_cached_ptr<T> &operator-=(std::ptrdiff_t s) = delete;

 private:
  T *copy_ptr;

 private:
  template <typename X>
  friend void ts_lock(ts_cached_ptr<X> &ptr);

  template <typename X>
  friend void ts_lock_const(ts_cached_ptr<X> const &ptr);

  template <typename X, typename Y>
  friend ts_cached_ptr<X> static_pointer_cast(
      const ts_cached_ptr<Y> &r) noexcept;

  template <typename Y>
  friend ts_cached_ptr<void *> static_void_pointer_cast(
      const ts_cached_ptr<Y> r) noexcept;

  template <typename X, typename Y>
  friend ts_cached_ptr<X> dynamic_pointer_cast(
      const ts_cached_ptr<Y> &r) noexcept;

  template <typename X, typename Y>
  friend ts_cached_ptr<X> const_pointer_cast(
      const ts_cached_ptr<Y> &r) noexcept;

  template <typename X, typename Y>
  friend ts_cached_ptr<X> reinterpret_pointer_cast(
      const ts_cached_ptr<Y> &r) noexcept;
};

/*
 * */
template <class T, size_t N>
class ts_persistent_array : public ts_persistent_ptr<T> {
 public:
  ts_persistent_array() {
    for (auto i = 0; i < N; ++i) {
      // _array[i] = new T;
      _array[i] = (T *)::ts_alloc(sizeof(T));
      /* TODO: need flush?*/
      _array[i].update_index(_array[i], i);
    }
  }

  ts_persistent_ptr<T> &operator[](int index) { return _array[index]; }

  ts_persistent_ptr<T> &operator=(T *raw_ptr) noexcept {
    return ts_persistent_ptr<T>::operator=(raw_ptr);
  }

  ts_persistent_ptr<T> &operator=(const ts_cached_ptr<T> &cch_ptr) noexcept {
    return ts_persistent_ptr<T>::operator=(cch_ptr);
  }

 private:
  ts_persistent_ptr<T> _array[N];
};

template <class T>
class ts_persistent_dynamic_array : public ts_persistent_ptr<T> {
 public:
  ts_persistent_dynamic_array() {}
  void init(int n_elem) {
    _array = (ts_persistent_dynamic_array<T> *)::ts_alloc(sizeof(T) * n_elem);
    for (auto i = 0; i < n_elem; ++i) {
      _array[i] = new T;
      _array[i].update_index(_array[i], i);
    }
  }

  ts_persistent_ptr<T> &operator[](int index) { return _array[index]; }

  ts_persistent_ptr<T> &operator=(T *raw_ptr) noexcept {
    return ts_persistent_ptr<T>::operator=(raw_ptr);
  }

  ts_persistent_ptr<T> &operator=(const ts_cached_ptr<T> &cch_ptr) noexcept {
    return ts_persistent_ptr<T>::operator=(cch_ptr);
  }

 private:
  ts_persistent_ptr<T> *_array;
};

/**
 * Static pointer casting
 * : We support type casting only for ts_cached_ptr<>
 * because ts_persistent_ptr<> is designed to define
 * data not its manipulation.
 *
 * ref: https://en.cppreference.com/w/cpp/memory/shared_ptr/pointer_cast
 */

template <typename T, typename Y>
ts_cached_ptr<T> static_pointer_cast(const ts_cached_ptr<Y> &r) noexcept {
  ts_cached_ptr<T> cch_ptr;
  cch_ptr.copy_ptr = static_cast<T *>(r.raw());
  return cch_ptr;
}

template <typename T>
ts_cached_ptr<void *> static_void_pointer_cast(
    const ts_cached_ptr<T> r) noexcept {
  ts_cached_ptr<void *> cch_ptr;
  cch_ptr.copy_ptr = static_cast<void *>(r.raw());
  return cch_ptr;
}

template <typename T, typename Y>
ts_cached_ptr<T> dynamic_pointer_cast(const ts_cached_ptr<Y> &r) noexcept {
  ts_cached_ptr<T> cch_ptr;
  cch_ptr.copy_ptr = dynamic_cast<T *>(r.raw());
  return cch_ptr;
}

template <typename T, typename Y>
ts_cached_ptr<T> const_pointer_cast(const ts_cached_ptr<Y> &r) noexcept {
  ts_cached_ptr<T> cch_ptr;
  cch_ptr.copy_ptr = const_cast<T *>(r.raw());
  return cch_ptr;
}

template <typename T, typename Y>
ts_cached_ptr<T> reinterpret_pointer_cast(const ts_cached_ptr<Y> &r) noexcept {
  ts_cached_ptr<T> cch_ptr;
  cch_ptr.copy_ptr = reinterpret_cast<T *>(r.raw());
  return cch_ptr;
}

/**
 * Locking
 */
template <typename T>
inline void ts_lock(ts_cached_ptr<T> &ptr) {
  T *raw = ptr.raw();
  if (::ts_try_lock(ts_thread::self, &raw)) {
    ptr.copy_ptr = raw;
  } else {
    ::ts_abort(ts_thread::self);
    std::longjmp(ts_thread::bots, 0);
  }
}

template <typename T>
inline void ts_lock_const(ts_cached_ptr<T> const &ptr) {
  if (!::ts_try_lock_const(ts_thread::self, ptr.raw())) {
    ::ts_abort(ts_thread::self);
    std::longjmp(ts_thread::bots, 0);
  }
}

/**
 * Swap
 */
template <typename T>
inline void swap(ts_persistent_ptr<T> &a, ts_persistent_ptr<T> &b) {
  a.swap(b);
}

template <typename T>
inline void swap(ts_cached_ptr<T> &a, ts_cached_ptr<T> &b) {
  a.swap(b);
}

/**
 * Equality operator.
 */
template <typename T, typename Y>
inline bool operator==(ts_persistent_ptr<T> const &lhs,
                       ts_persistent_ptr<Y> const &rhs) noexcept {
  return lhs.raw() == rhs.raw();
}

template <typename T, typename Y>
inline bool operator==(ts_cached_ptr<T> const &cch_lhs,
                       ts_cached_ptr<Y> const &cch_rhs) noexcept {
  ts_persistent_ptr<T> const lhs(cch_lhs);
  ts_persistent_ptr<Y> const rhs(cch_rhs);
  return lhs == rhs;
}

template <typename T, typename Y>
inline bool operator==(ts_persistent_ptr<T> const &lhs,
                       Y const *p_rhs) noexcept {
  ts_persistent_ptr<Y> const rhs(p_rhs);
  return lhs == rhs;
}

template <typename T, typename Y>
inline bool operator==(ts_cached_ptr<T> const &cch_lhs,
                       Y const *p_rhs) noexcept {
  ts_persistent_ptr<T> const lhs(cch_lhs);
  ts_persistent_ptr<Y> const rhs(p_rhs);
  return lhs == rhs;
}

template <typename T>
inline bool operator==(ts_persistent_ptr<T> const &lhs,
                       std::nullptr_t) noexcept {
  return lhs.raw() == nullptr;
}

template <typename T>
inline bool operator==(ts_cached_ptr<T> const &lhs, std::nullptr_t) noexcept {
  return lhs.raw() == nullptr;
}

template <typename T, typename Y>
inline bool operator==(T const *p_lhs,
                       ts_persistent_ptr<Y> const &rhs) noexcept {
  ts_persistent_ptr<Y> const lhs(p_lhs);
  return lhs == rhs;
}

template <typename T, typename Y>
inline bool operator==(T const *p_lhs,
                       ts_cached_ptr<Y> const &cch_rhs) noexcept {
  ts_persistent_ptr<T> const lhs(p_lhs);
  ts_persistent_ptr<Y> const rhs(cch_rhs);
  return lhs == rhs;
}

template <typename T>
inline bool operator==(std::nullptr_t,
                       ts_persistent_ptr<T> const &rhs) noexcept {
  return rhs.raw() == nullptr;
}

template <typename T>
inline bool operator==(std::nullptr_t, ts_cached_ptr<T> const &rhs) noexcept {
  return rhs.raw() == nullptr;
}

/**
 * Inequality operator.
 */
template <typename T, typename Y>
inline bool operator!=(ts_persistent_ptr<T> const &lhs,
                       ts_persistent_ptr<Y> const &rhs) noexcept {
  return !(lhs == rhs);
}

template <typename T, typename Y>
inline bool operator!=(ts_cached_ptr<T> const &lhs,
                       ts_cached_ptr<Y> const &rhs) noexcept {
  return !(lhs == rhs);
}

template <typename T, typename Y>
inline bool operator!=(ts_persistent_ptr<T> const &lhs,
                       Y const *p_rhs) noexcept {
  ts_persistent_ptr<Y> const rhs(p_rhs);
  return lhs != rhs;
}

template <typename T, typename Y>
inline bool operator!=(ts_cached_ptr<T> const &cch_lhs,
                       Y const *p_rhs) noexcept {
  ts_persistent_ptr<T> const lhs(cch_lhs);
  ts_persistent_ptr<Y> const rhs(p_rhs);
  return lhs != rhs;
}

template <typename T>
inline bool operator!=(ts_persistent_ptr<T> const &lhs,
                       std::nullptr_t) noexcept {
  return lhs.raw() != nullptr;
}

template <typename T>
inline bool operator!=(ts_cached_ptr<T> const &lhs, std::nullptr_t) noexcept {
  return lhs.raw() != nullptr;
}

template <typename T, typename Y>
inline bool operator!=(T const *p_lhs,
                       ts_persistent_ptr<Y> const &rhs) noexcept {
  ts_persistent_ptr<T> const lhs(p_lhs);
  return lhs != rhs;
}

template <typename T, typename Y>
inline bool operator!=(T const *p_lhs,
                       ts_cached_ptr<Y> const &cch_rhs) noexcept {
  ts_persistent_ptr<T> const lhs(p_lhs);
  ts_persistent_ptr<Y> const rhs(cch_rhs);
  return lhs != rhs;
}

template <typename T>
inline bool operator!=(std::nullptr_t,
                       ts_persistent_ptr<T> const &rhs) noexcept {
  return rhs.raw() != nullptr;
}

template <typename T>
inline bool operator!=(std::nullptr_t, ts_cached_ptr<T> const &rhs) noexcept {
  return rhs.raw() != nullptr;
}

/**
 * Less than operator.
 */
template <typename T, typename Y>
inline bool operator<(ts_persistent_ptr<T> const &lhs,
                      ts_persistent_ptr<Y> const &rhs) noexcept {
  return lhs.raw() < rhs.raw();
}

template <typename T, typename Y>
inline bool operator<(ts_cached_ptr<T> const &cch_lhs,
                      ts_cached_ptr<Y> const &cch_rhs) noexcept {
  ts_persistent_ptr<T> const lhs(cch_lhs);
  ts_persistent_ptr<Y> const rhs(cch_rhs);
  return lhs < rhs;
}

template <typename T, typename Y>
inline bool operator<(ts_persistent_ptr<T> const &lhs,
                      Y const *p_rhs) noexcept {
  ts_persistent_ptr<Y> const rhs(p_rhs);
  return lhs < rhs;
}

template <typename T, typename Y>
inline bool operator<(ts_cached_ptr<T> const &cch_lhs,
                      Y const *p_rhs) noexcept {
  ts_persistent_ptr<T> const lhs(cch_lhs);
  ts_persistent_ptr<Y> const rhs(p_rhs);
  return lhs < rhs;
}

template <typename T>
inline bool operator<(ts_persistent_ptr<T> const &lhs,
                      std::nullptr_t) noexcept {
  return std::less<typename ts_persistent_ptr<T>::element_type *>()(lhs.raw(),
                                                                    nullptr);
}

template <typename T>
inline bool operator<(ts_cached_ptr<T> const &lhs, std::nullptr_t) noexcept {
  return std::less<typename ts_persistent_ptr<T>::element_type *>()(lhs.raw(),
                                                                    nullptr);
}

template <typename T, typename Y>
inline bool operator<(T const *p_lhs,
                      ts_persistent_ptr<Y> const &rhs) noexcept {
  ts_persistent_ptr<T> const lhs(p_lhs);
  return lhs < rhs;
}

template <typename T, typename Y>
inline bool operator<(T const *p_lhs,
                      ts_cached_ptr<Y> const &cch_rhs) noexcept {
  ts_persistent_ptr<T> const lhs(p_lhs);
  ts_persistent_ptr<Y> const rhs(cch_rhs);
  return lhs < rhs;
}

template <typename T>
inline bool operator<(std::nullptr_t,
                      ts_persistent_ptr<T> const &rhs) noexcept {
  return std::less<typename ts_persistent_ptr<T>::element_type *>()(nullptr,
                                                                    rhs.raw());
}

template <typename T>
inline bool operator<(std::nullptr_t, ts_cached_ptr<T> const &rhs) noexcept {
  return std::less<typename ts_persistent_ptr<T>::element_type *>()(nullptr,
                                                                    rhs.raw());
}

/**
 * Less or equal than operator.
 */
template <typename T, typename Y>
inline bool operator<=(ts_persistent_ptr<T> const &lhs,
                       ts_persistent_ptr<Y> const &rhs) noexcept {
  return !(rhs < lhs);
}

template <typename T, typename Y>
inline bool operator<=(ts_cached_ptr<T> const &lhs,
                       ts_cached_ptr<Y> const &rhs) noexcept {
  return !(rhs < lhs);
}

template <typename T>
inline bool operator<=(ts_persistent_ptr<T> const &lhs,
                       std::nullptr_t) noexcept {
  return !(nullptr < lhs);
}

template <typename T>
inline bool operator<=(ts_cached_ptr<T> const &lhs, std::nullptr_t) noexcept {
  return !(nullptr < lhs);
}

template <typename T>
inline bool operator<=(std::nullptr_t,
                       ts_persistent_ptr<T> const &rhs) noexcept {
  return !(rhs < nullptr);
}

template <typename T>
inline bool operator<=(std::nullptr_t, ts_cached_ptr<T> const &rhs) noexcept {
  return !(rhs < nullptr);
}

/**
 * Greater than operator.
 */
template <typename T, typename Y>
inline bool operator>(ts_persistent_ptr<T> const &lhs,
                      ts_persistent_ptr<Y> const &rhs) noexcept {
  return (rhs < lhs);
}

template <typename T, typename Y>
inline bool operator>(ts_cached_ptr<T> const &lhs,
                      ts_cached_ptr<Y> const &rhs) noexcept {
  return (rhs < lhs);
}

template <typename T>
inline bool operator>(ts_persistent_ptr<T> const &lhs,
                      std::nullptr_t) noexcept {
  return nullptr < lhs;
}

template <typename T>
inline bool operator>(ts_cached_ptr<T> const &lhs, std::nullptr_t) noexcept {
  return nullptr < lhs;
}

template <typename T>
inline bool operator>(std::nullptr_t,
                      ts_persistent_ptr<T> const &rhs) noexcept {
  return rhs < nullptr;
}

template <typename T>
inline bool operator>(std::nullptr_t, ts_cached_ptr<T> const &rhs) noexcept {
  return rhs < nullptr;
}

/**
 * Greater or equal than operator.
 */
template <typename T, typename Y>
inline bool operator>=(ts_persistent_ptr<T> const &lhs,
                       ts_persistent_ptr<Y> const &rhs) noexcept {
  return !(lhs < rhs);
}

template <typename T, typename Y>
inline bool operator>=(ts_cached_ptr<T> const &lhs,
                       ts_cached_ptr<Y> const &rhs) noexcept {
  return !(lhs < rhs);
}

template <typename T>
inline bool operator>=(ts_persistent_ptr<T> const &lhs,
                       std::nullptr_t) noexcept {
  return !(lhs < nullptr);
}

template <typename T>
inline bool operator>=(ts_cached_ptr<T> const &lhs, std::nullptr_t) noexcept {
  return !(lhs < nullptr);
}

template <typename T>
inline bool operator>=(std::nullptr_t,
                       ts_persistent_ptr<T> const &rhs) noexcept {
  return !(nullptr < rhs);
}

template <typename T>
inline bool operator>=(std::nullptr_t, ts_cached_ptr<T> const &rhs) noexcept {
  return !(nullptr < rhs);
}

/**
 * Ostream operator for the timestone pointer.
 */
template <typename T>
std::ostream &operator<<(std::ostream &os, ts_persistent_ptr<T> const &ptr) {
  os << ptr.raw();
  return os;
}

template <typename T>
std::ostream &operator<<(std::ostream &os, ts_cached_ptr<T> const &ptr) {
  os << ptr.raw();
  return os;
}

}  // end of namespace ts
