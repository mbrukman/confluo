#ifndef MONOLOG_MONOLOG_LINEAR_BLOCK_H_
#define MONOLOG_MONOLOG_LINEAR_BLOCK_H_

#include "atomic.h"
#include "io_utils.h"
#include "ptr.h"
#include "storage.h"

namespace dialog {
namespace monolog {

using namespace ::utils;

template<typename T, size_t BUFFER_SIZE = 1048576>
class monolog_block {
 public:
  typedef storage::swappable_ptr<T> __atomic_block_ref;
  typedef storage::read_only_ptr<T> __atomic_block_copy_ref;

  typedef bool block_state;
  static const block_state UNINIT = false;
  static const block_state INIT = true;

  monolog_block()
      : path_(""),
        state_(UNINIT),
        data_(),
        size_(0),
        storage_(storage::IN_MEMORY) {
  }

  monolog_block(const std::string& path, size_t size,
                const storage::storage_mode& storage)
      : path_(path),
        state_(UNINIT),
        data_(nullptr),
        size_(size),
        storage_(storage) {
  }

  monolog_block(const monolog_block<T, BUFFER_SIZE>& other)
      : path_(other.path_),
        state_(other.state_),
        size_(other.size_),
        storage_(other.storage_) {
    __atomic_block_copy_ref copy;
    other.data_.atomic_copy(copy);
    data_.atomic_init(copy.ptr_);
  }

  void init(const std::string& path, const size_t size,
            const storage::storage_mode& storage) {
    path_ = path;
    size_ = size;
    storage_ = storage;
  }

  size_t storage_size() const {
    if (data_.atomic_load() != nullptr)
      return (size_ + BUFFER_SIZE) * sizeof(T);
    return 0;
  }

  void flush(size_t offset, size_t len) {
    __atomic_block_copy_ref copy;
    data_.atomic_copy(copy);
    storage_.flush(copy.get() + offset, len * sizeof(T));
  }

  void set(size_t i, const T& val) {
    if (!data_.atomic_set(i, val)) {
      try_allocate();
      data_.atomic_set(i, val);
    }
  }

  void set_unsafe(size_t i, const T& val) {
    __atomic_block_copy_ref copy;
    data_.atomic_copy(copy);
    copy.get()[i] = val;
  }

  void write(size_t offset, const T* data, size_t len) {
    __atomic_block_copy_ref copy;
    data_.atomic_copy(copy);
    if (copy.get() == nullptr)
      try_allocate(copy);
    memcpy(copy.get() + offset, data, len * sizeof(T));
  }

  void write_unsafe(size_t offset, const T* data, size_t len) {
    __atomic_block_copy_ref copy;
    data_.atomic_copy(copy);
    memcpy(copy.get() + offset, data, len * sizeof(T));
  }

  const T& at(size_t i) const {
    return data_.atomic_get(i);
  }

  void read(size_t offset, T* data, size_t len) const {
    memcpy(data, data_.atomic_copy()->get() + offset, len * sizeof(T));
  }

  T& operator[](size_t i) {
    __atomic_block_copy_ref copy;
    data_.atomic_copy(copy);
    if (copy.get() == nullptr)
      try_allocate(copy);
    return copy.get()[i];
  }

  void ptr(size_t offset, __atomic_block_copy_ref& data_ptr) {
    data_.atomic_copy(data_ptr, offset);
    if (data_ptr.get() == nullptr) {
      try_allocate(data_ptr);
      data_ptr.set_offset(offset);
    }
  }

  void cptr(size_t offset, __atomic_block_copy_ref& data_ptr) const {
    data_.atomic_copy(data_ptr, offset);
  }

  monolog_block& operator=(const monolog_block<T, BUFFER_SIZE>& other) {
    path_ = other.path_;
    atomic::init(&state_, atomic::load(&other.state_));
    __atomic_block_copy_ref copy;
    other.data_.atomic_copy(copy);
    data_.atomic_init(copy.ptr_);
    return *this;
  }

  void ensure_alloc() {
    if (data_.atomic_load() == nullptr) {
      __atomic_block_copy_ref copy;
      try_allocate(copy);
    }
  }

 private:
  void try_allocate(__atomic_block_copy_ref& copy) {
    block_state state = UNINIT;
    if (atomic::strong::cas(&state_, &state, INIT)) {
      size_t file_size = (size_ + BUFFER_SIZE) * sizeof(T);
      T* ptr = storage_.allocate_block(path_, file_size);
      data_.atomic_init(ptr);
      data_.atomic_copy(copy);
      return;
    }

    // Someone else is initializing, stall until initialized
    while (data_.atomic_load() == nullptr)
      ;

    data_.atomic_copy(copy);
  }

  void try_allocate() {
    block_state state = UNINIT;
    if (atomic::strong::cas(&state_, &state, INIT)) {
      size_t file_size = (size_ + BUFFER_SIZE) * sizeof(T);
      T* ptr = storage_.allocate_block(path_, file_size);
      data_.atomic_init(ptr);
      return;
    }

    // Someone else is initializing, stall until initialized
    while (data_.atomic_load() == nullptr)
      ;
  }

  std::string path_;
  atomic::type<block_state> state_;
  __atomic_block_ref data_;
  size_t size_;
  storage::storage_mode storage_;
};

template<typename T, size_t BUFFER_SIZE>
const bool monolog_block<T, BUFFER_SIZE>::INIT;

template<typename T, size_t BUFFER_SIZE>
const bool monolog_block<T, BUFFER_SIZE>::UNINIT;

}
}

#endif /* MONOLOG_MONOLOG_LINEAR_BLOCK_H_ */
