#define VK_LAYER_EXPORT

/* Copyright (c) 2015-2017, 2019-2021 The Khronos Group Inc.
 * Copyright (c) 2015-2017, 2019-2021 Valve Corporation
 * Copyright (c) 2015-2017, 2019-2021 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Tobin Ehlis <tobine@google.com>
 * Author: Jeff Bolz <jbolz@nvidia.com>
 * Author: John Zulauf <jzulauf@lunarg.com>

*/

#ifndef LAYER_DATA_H
#define LAYER_DATA_H

#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>

#ifdef USE_ROBIN_HOOD_HASHING
#include "robin_hood.h"
#else
#include <unordered_set>
#endif

// namespace aliases to allow map and set implementations to easily be swapped out
namespace layer_data {

#ifdef USE_ROBIN_HOOD_HASHING
template <typename T>
using hash = robin_hood::hash<T>;

template <typename Key, typename Hash = robin_hood::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using unordered_set = robin_hood::unordered_set<Key, Hash, KeyEqual>;

template <typename Key, typename T, typename Hash = robin_hood::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using unordered_map = robin_hood::unordered_map<Key, T, Hash, KeyEqual>;

template <typename Key, typename T>
using map_entry = robin_hood::pair<Key, T>;

// robin_hood-compatible insert_iterator (std:: uses the wrong insert method)
template <typename T>
class insert_iterator : public std::iterator<std::output_iterator_tag, void, void, void, void> {
  public:
    typedef typename T::value_type value_type;
    typedef typename T::iterator iterator;
    insert_iterator(T &t, iterator i) : container(&t), iter(i) {}

    insert_iterator &operator=(const value_type &value) {
        auto result = container->insert(value);
        iter = result.first;
        ++iter;
        return *this;
    }

    insert_iterator &operator=(value_type &&value) {
        auto result = container->insert(std::move(value));
        iter = result.first;
        ++iter;
        return *this;
    }

    insert_iterator &operator*() { return *this; }

    insert_iterator &operator++() { return *this; }

    insert_iterator &operator++(int) { return *this; }

  private:
    T *container;
    typename T::iterator iter;
};
#else
template <typename T>
using hash = std::hash<T>;

template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using unordered_set = std::unordered_set<Key, Hash, KeyEqual>;

template <typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using unordered_map = std::unordered_map<Key, T, Hash, KeyEqual>;

template <typename Key, typename T>
using map_entry = std::pair<Key, T>;

template <typename T>
using insert_iterator = std::insert_iterator<T>;
#endif

#if __cplusplus < 201402L
// Temporary workaround for c++11. Remove with std >= c++14.
template <typename T, typename... Args>
constexpr std::unique_ptr<T> make_unique(Args &&...args) { return std::unique_ptr<T>(new T(std::forward<Args>(args)...)); }
#else
template <typename T>
constexpr auto make_unique = std::make_unique<T>;
#endif

} // namespace layer_data

// A vector class with "small string optimization" -- meaning that the class contains a fixed working store for N elements.
// Useful in in situations where the needed size is unknown, but the typical size is known  If size increases beyond the
// fixed capacity, a dynamically allocated working store is created.
//
// NOTE: Unlike std::vector which only requires T to be CopyAssignable and CopyConstructable, small_vector requires T to be
//       MoveAssignable and MoveConstructable
// NOTE: Unlike std::vector, iterators are invalidated by move assignment between small_vector objects effectively the
//       "small string" allocation functions as an incompatible allocator.
template <typename T, size_t N, typename SizeType = uint8_t>
class small_vector {
  public:
    using value_type = T;
    using reference = value_type &;
    using const_reference = const value_type &;
    using pointer = value_type *;
    using const_pointer = const value_type *;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using size_type = SizeType;
    static const size_type kSmallCapacity = N;
    static const size_type kMaxCapacity = std::numeric_limits<size_type>::max();
    static_assert(N <= kMaxCapacity, "size must be less than size_type::max");

    small_vector() : size_(0), capacity_(N) {}

    small_vector(const small_vector &other) : size_(0), capacity_(N) {
        reserve(other.size_);
        auto dest = GetWorkingStore();
        for (const auto &value : other) {
            new (dest) value_type(value);
            ++dest;
        }
        size_ = other.size_;
    }

    small_vector(small_vector &&other) : size_(0), capacity_(N) {
        if (other.large_store_) {
            // Can just take ownership of the other large store
            large_store_ = std::move(other.large_store_);
            capacity_ = other.capacity_;
            other.capacity_ = kSmallCapacity;
        } else {
            auto dest = GetWorkingStore();
            for (auto &value : other) {
                new (dest) value_type(std::move(value));
                value.~value_type();
                ++dest;
            }
        }
        size_ = other.size_;
        other.size_ = 0;
    }

    bool operator==(const small_vector &rhs) const {
        if (size_ != rhs.size_)
            return false;
        auto value = begin();
        for (const auto &rh_value : rhs) {
            if (!(*value == rh_value)) {
                return false;
            }
            ++value;
        }
        return true;
    }

    small_vector &operator=(const small_vector &other) {
        if (this != &other) {
            reserve(other.size_); // reserve doesn't shrink!
            auto dest = GetWorkingStore();
            auto source = other.GetWorkingStore();

            const auto overlap = std::min(size_, other.size_);
            // Copy assign anywhere we have objects in this
            for (size_type i = 0; i < overlap; i++) {
                dest[i] = source[i];
            }

            // Copy construct anywhere we *don't* have objects in this
            for (size_type i = overlap; i < other.size_; i++) {
                new (dest + i) value_type(source[i]);
            }

            // Any entries in this past other_size_ must be cleaned up...
            for (size_type i = other.size_; i < size_; i++) {
                dest[i].~value_type();
            }
            size_ = other.size_;
        }
        return *this;
    }

    small_vector &operator=(small_vector &&other) {
        if (this != &other) {
            if (other.large_store_) {
                clear(); // need to clean up any objects this owns.
                // Can just take ownership of the other large store
                large_store_ = std::move(other.large_store_);
                capacity_ = other.capacity_;
                size_ = other.size_;

                other.capacity_ = kSmallCapacity;
            } else {
                // Other is using the small_store
                auto source = other.begin();
                iterator dest;
                if (large_store_) {
                    // If this is using large store do a wholesale clobber of it.
                    ClearAndReset();
                    dest = GetWorkingStore();
                } else {
                    // This is also using small store, so move assign where both have valid values
                    dest = GetWorkingStore();
                    // Move values where both vectors have valid values
                    for (size_type i = 0; i < std::min(size_, other.size_); i++) {
                        *dest = std::move(*source);
                        source->~value_type();
                        ++dest;
                        ++source;
                    }
                }

                // Other is bigger, placement new into the working store
                // NOTE: this loop only runs when other is bigger
                for (size_type i = size_; i < other.size_; i++) {
                    new (dest) value_type(std::move(*source));
                    source->~value_type();
                    ++dest;
                    ++source;
                }
                // Other is smaller, clean up the excess entries
                // NOTE: this loop only runs when this is bigger
                for (size_type i = other.size_; i < size_; i++) {
                    dest->~value_type();
                    ++dest;
                }

                size_ = other.size_;
            }

            // When we're done other has no valid contents (all are moved or destructed)
            other.size_ = 0;
        }
        return *this;
    }

    reference operator[](size_type pos) {
        assert(pos < size_);
        return GetWorkingStore()[pos];
    }
    const_reference operator[](size_type pos) const {
        assert(pos < size_);
        return GetWorkingStore()[pos];
    }

    // Like std::vector::back, calling back on an empty container causes undefined behavior
    reference back() {
        assert(size_ > 0);
        return GetWorkingStore()[size_ - 1];
    }
    const_reference back() const {
        assert(size_ > 0);
        return GetWorkingStore()[size_ - 1];
    }

    bool empty() const { return size_ == 0; }

    template <class... Args>
    void emplace_back(Args &&...args) {
        assert(size_ < kMaxCapacity);
        reserve(size_ + 1);
        new (GetWorkingStore() + size_) value_type(args...);
        size_++;
    }

    void reserve(size_type new_cap) {
        // Since this can't shrink, if we're growing we're newing
        if (new_cap > capacity_) {
            assert(capacity_ >= kSmallCapacity);
            auto new_store = std::unique_ptr<BackingStore[]>(new BackingStore[new_cap]);
            auto new_values = reinterpret_cast<pointer>(new_store.get());
            auto working_store = GetWorkingStore();
            for (size_type i = 0; i < size_; i++) {
                new (new_values + i) value_type(std::move(working_store[i]));
                working_store[i].~value_type();
            }
            large_store_ = std::move(new_store);
        }
        // No shrink here.
    }

    void clear() {
        auto working_store = GetWorkingStore();
        for (size_type i = 0; i < size_; i++) {
            working_store[i].~value_type();
        }
        size_ = 0;
    }

    inline iterator begin() { return GetWorkingStore(); }
    inline const_iterator cbegin() const { return GetWorkingStore(); }
    inline const_iterator begin() const { return GetWorkingStore(); }

    inline iterator end() { return GetWorkingStore() + size_; }
    inline const_iterator cend() const { return GetWorkingStore() + size_; }
    inline const_iterator end() const { return GetWorkingStore() + size_; }
    inline size_type size() const { return size_; }

  protected:
    inline const_pointer GetWorkingStore() const {
        const BackingStore *store = large_store_ ? large_store_.get() : small_store_;
        return reinterpret_cast<const_pointer>(store);
    }
    inline pointer GetWorkingStore() {
        BackingStore *store = large_store_ ? large_store_.get() : small_store_;
        return reinterpret_cast<pointer>(store);
    }

    void ClearAndReset() {
        clear();
        large_store_.reset();
        capacity_ = kSmallCapacity;
    }

    struct alignas(alignof(value_type)) BackingStore {
        uint8_t data[sizeof(value_type)];
    };
    size_type size_;
    size_type capacity_;
    BackingStore small_store_[N];
    std::unique_ptr<BackingStore[]> large_store_;
};

// This is a wrapper around unordered_map that optimizes for the common case
// of only containing a small number of elements. The first N elements are stored
// inline in the object and don't require hashing or memory (de)allocation.

template <typename Key, typename value_type, typename inner_container_type, typename value_type_helper, int N>
class small_container {
  protected:
    bool small_data_allocated[N];
    value_type small_data[N];

    inner_container_type inner_cont;

    value_type_helper helper;

  public:
    small_container() {
        for (int i = 0; i < N; ++i) {
            small_data_allocated[i] = false;
        }
    }

    class iterator {
        typedef typename inner_container_type::iterator inner_iterator;
        friend class small_container<Key, value_type, inner_container_type, value_type_helper, N>;

        small_container<Key, value_type, inner_container_type, value_type_helper, N> *parent;
        int index;
        inner_iterator it;

      public:
        iterator() : parent(nullptr), index(0) {}

        iterator operator++() {
            if (index < N) {
                index++;
                while (index < N && !parent->small_data_allocated[index]) {
                    index++;
                }
                if (index < N) {
                    return *this;
                }
                it = parent->inner_cont.begin();
                return *this;
            }
            ++it;
            return *this;
        }

        bool operator==(const iterator &other) const {
            if ((index < N) != (other.index < N)) {
                return false;
            }
            if (index < N) {
                return (index == other.index);
            }
            return it == other.it;
        }

        bool operator!=(const iterator &other) const { return !(*this == other); }

        value_type &operator*() const {
            if (index < N) {
                return parent->small_data[index];
            }
            return *it;
        }
        value_type *operator->() const {
            if (index < N) {
                return &parent->small_data[index];
            }
            return &*it;
        }
    };

    class const_iterator {
        typedef typename inner_container_type::const_iterator inner_iterator;
        friend class small_container<Key, value_type, inner_container_type, value_type_helper, N>;

        const small_container<Key, value_type, inner_container_type, value_type_helper, N> *parent;
        int index;
        inner_iterator it;

      public:
        const_iterator() : parent(nullptr), index(0) {}

        const_iterator operator++() {
            if (index < N) {
                index++;
                while (index < N && !parent->small_data_allocated[index]) {
                    index++;
                }
                if (index < N) {
                    return *this;
                }
                it = parent->inner_cont.begin();
                return *this;
            }
            ++it;
            return *this;
        }

        bool operator==(const const_iterator &other) const {
            if ((index < N) != (other.index < N)) {
                return false;
            }
            if (index < N) {
                return (index == other.index);
            }
            return it == other.it;
        }

        bool operator!=(const const_iterator &other) const { return !(*this == other); }

        const value_type &operator*() const {
            if (index < N) {
                return parent->small_data[index];
            }
            return *it;
        }
        const value_type *operator->() const {
            if (index < N) {
                return &parent->small_data[index];
            }
            return &*it;
        }
    };

    iterator begin() {
        iterator it;
        it.parent = this;
        // If index 0 is allocated, return it, otherwise use operator++ to find the first
        // allocated element.
        it.index = 0;
        if (small_data_allocated[0]) {
            return it;
        }
        ++it;
        return it;
    }

    iterator end() {
        iterator it;
        it.parent = this;
        it.index = N;
        it.it = inner_cont.end();
        return it;
    }

    const_iterator begin() const {
        const_iterator it;
        it.parent = this;
        // If index 0 is allocated, return it, otherwise use operator++ to find the first
        // allocated element.
        it.index = 0;
        if (small_data_allocated[0]) {
            return it;
        }
        ++it;
        return it;
    }

    const_iterator end() const {
        const_iterator it;
        it.parent = this;
        it.index = N;
        it.it = inner_cont.end();
        return it;
    }

    bool contains(const Key &key) const {
        for (int i = 0; i < N; ++i) {
            if (small_data_allocated[i] && helper.compare_equal(small_data[i], key)) {
                return true;
            }
        }
        // check size() first to avoid hashing key unnecessarily.
        if (inner_cont.size() == 0) {
            return false;
        }
        return inner_cont.find(key) != inner_cont.end();
    }

    typename inner_container_type::size_type count(const Key &key) const { return contains(key) ? 1 : 0; }

    std::pair<iterator, bool> insert(const value_type &value) {
        for (int i = 0; i < N; ++i) {
            if (small_data_allocated[i] && helper.compare_equal(small_data[i], value)) {
                iterator it;
                it.parent = this;
                it.index = i;
                return std::make_pair(it, false);
            }
        }
        // check size() first to avoid hashing key unnecessarily.
        auto iter = inner_cont.size() > 0 ? inner_cont.find(helper.get_key(value)) : inner_cont.end();
        if (iter != inner_cont.end()) {
            iterator it;
            it.parent = this;
            it.index = N;
            it.it = iter;
            return std::make_pair(it, false);
        } else {
            for (int i = 0; i < N; ++i) {
                if (!small_data_allocated[i]) {
                    small_data_allocated[i] = true;
                    helper.assign(small_data[i], value);
                    iterator it;
                    it.parent = this;
                    it.index = i;
                    return std::make_pair(it, true);
                }
            }
            iter = inner_cont.insert(value).first;
            iterator it;
            it.parent = this;
            it.index = N;
            it.it = iter;
            return std::make_pair(it, true);
        }
    }

    typename inner_container_type::size_type erase(const Key &key) {
        for (int i = 0; i < N; ++i) {
            if (small_data_allocated[i] && helper.compare_equal(small_data[i], key)) {
                small_data_allocated[i] = false;
                return 1;
            }
        }
        return inner_cont.erase(key);
    }

    typename inner_container_type::size_type size() const {
        auto size = inner_cont.size();
        for (int i = 0; i < N; ++i) {
            if (small_data_allocated[i]) {
                size++;
            }
        }
        return size;
    }

    bool empty() const {
        for (int i = 0; i < N; ++i) {
            if (small_data_allocated[i]) {
                return false;
            }
        }
        return inner_cont.size() == 0;
    }

    void clear() {
        for (int i = 0; i < N; ++i) {
            small_data_allocated[i] = false;
        }
        inner_cont.clear();
    }
};

// Helper function objects to compare/assign/get keys in small_unordered_set/map.
// This helps to abstract away whether value_type is a Key or a pair<Key, T>.
template <typename MapType>
class value_type_helper_map {
    using PairType = typename MapType::value_type;
    using Key = typename std::remove_const<typename PairType::first_type>::type;

  public:
    bool compare_equal(const PairType &lhs, const Key &rhs) const { return lhs.first == rhs; }
    bool compare_equal(const PairType &lhs, const PairType &rhs) const { return lhs.first == rhs.first; }

    void assign(PairType &lhs, const PairType &rhs) const {
        // While the const_cast may be unsatisfactory, we are using small_data as
        // stand-in for placement new and a small-block allocator, so the const_cast
        // is minimal, contained, valid, and allows operators * and -> to avoid copies
        const_cast<Key &>(lhs.first) = rhs.first;
        lhs.second = rhs.second;
    }

    Key get_key(const PairType &value) const { return value.first; }
};

template <typename Key>
class value_type_helper_set {
  public:
    bool compare_equal(const Key &lhs, const Key &rhs) const { return lhs == rhs; }

    void assign(Key &lhs, const Key &rhs) const { lhs = rhs; }

    Key get_key(const Key &value) const { return value; }
};

template <typename Key, typename T, int N = 1>
class small_unordered_map
    : public small_container<Key, typename layer_data::unordered_map<Key, T>::value_type, layer_data::unordered_map<Key, T>, value_type_helper_map<layer_data::unordered_map<Key, T>>, N> {
  public:
    T &operator[](const Key &key) {
        for (int i = 0; i < N; ++i) {
            if (this->small_data_allocated[i] && this->helper.compare_equal(this->small_data[i], key)) {
                return this->small_data[i].second;
            }
        }
        auto iter = this->inner_cont.find(key);
        if (iter != this->inner_cont.end()) {
            return iter->second;
        } else {
            for (int i = 0; i < N; ++i) {
                if (!this->small_data_allocated[i]) {
                    this->small_data_allocated[i] = true;
                    this->helper.assign(this->small_data[i], {key, T()});

                    return this->small_data[i].second;
                }
            }
            return this->inner_cont[key];
        }
    }
};

template <typename Key, int N = 1>
class small_unordered_set : public small_container<Key, Key, layer_data::unordered_set<Key>, value_type_helper_set<Key>, N> {};

// For the given data key, look up the layer_data instance from given layer_data_map
template <typename DATA_T>
DATA_T *GetLayerDataPtr(void *data_key, small_unordered_map<void *, DATA_T *, 2> &layer_data_map) {
    // TODO: should lock here, or have caller lock
    DATA_T *&got = layer_data_map[data_key];

    if (got == nullptr) {
        got = new DATA_T;
    }

    return got;
}

template <typename DATA_T>
void FreeLayerDataPtr(void *data_key, small_unordered_map<void *, DATA_T *, 2> &layer_data_map) {
    delete layer_data_map[data_key];
    layer_data_map.erase(data_key);
}

// For the given data key, look up the layer_data instance from given layer_data_map
template <typename DATA_T>
DATA_T *GetLayerDataPtr(void *data_key, std::unordered_map<void *, DATA_T *> &layer_data_map) {
    DATA_T *debug_data;
    // TODO: should lock here, or have caller lock
    auto got = layer_data_map.find(data_key);

    if (got == layer_data_map.end()) {
        debug_data = new DATA_T;
        layer_data_map[(void *)data_key] = debug_data;
    } else {
        debug_data = got->second;
    }

    return debug_data;
}

template <typename DATA_T>
void FreeLayerDataPtr(void *data_key, std::unordered_map<void *, DATA_T *> &layer_data_map) {
    auto got = layer_data_map.find(data_key);
    assert(got != layer_data_map.end());

    delete got->second;
    layer_data_map.erase(got);
}

namespace layer_data {

struct in_place_t {};
static constexpr in_place_t in_place{};

// A C++11 approximation of std::optional
template <typename T>
class optional {
  protected:
    union Store {
        Store() {};  // Do nothing.  That's the point.
        ~Store() {}; // Not safe to destroy this object outside of its stateful container to clean up T if any.
        typename std::aligned_storage<sizeof(T), alignof(T)>::type backing;
        T obj;
    };

  public:
    optional() : init_(false) {}

    template <typename... Args>
    explicit optional(in_place_t, const Args &...args) { emplace(args...); }
    optional(const optional &other) : init_(false) { *this = other; }
    optional(optional &&other) : init_(false) { *this = std::move(other); }

    ~optional() { DeInit(); }

    template <typename... Args>
    T &emplace(const Args &...args) {
        init_ = true;
        new (&store_.backing) T(args...);
        return store_.obj;
    }
    T *operator&() {
        if (init_)
            return &store_.obj;
        return nullptr;
    }
    const T *operator&() const {
        if (init_)
            return &store_.obj;
        return nullptr;
    }
    T *operator->() {
        if (init_)
            return &store_.obj;
        return nullptr;
    }
    const T *operator->() const {
        if (init_)
            return &store_.obj;
        return nullptr;
    }
    operator bool() const { return init_; }
    bool has_value() const { return init_; }

    optional &operator=(const optional &other) {
        if (other.has_value()) {
            if (has_value()) {
                store_.obj = other.store_.obj;
            } else {
                emplace(other.store_.obj);
            }
        } else {
            DeInit();
        }
        return *this;
    }

    optional &operator=(optional &&other) {
        if (other.has_value()) {
            if (has_value()) {
                store_.obj = std::move(other.store_.obj);
            } else {
                emplace(std::move(other.store_.obj));
            }
        } else {
            DeInit();
        }
        return *this;
    }

    T &operator*() & {
        assert(init_);
        return store_.obj;
    }
    const T &operator*() const & {
        assert(init_);
        return store_.obj;
    }
    T &&operator*() && {
        assert(init_);
        return std::move(store_.obj);
    }
    const T &&operator*() const && {
        assert(init_);
        return std::move(store_.obj);
    }

  protected:
    inline void DeInit() {
        if (init_) {
            store_.obj.~T();
            init_ = false;
        }
    }
    Store store_;
    bool init_;
};
} // namespace layer_data
#endif // LAYER_DATA_H

/* Copyright (c) 2015-2021 The Khronos Group Inc.
 * Copyright (c) 2015-2021 Valve Corporation
 * Copyright (c) 2015-2021 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Mark Lobodzinski <mark@lunarg.com>
 * Author: Dave Houlton <daveh@lunarg.com>
 *
 */

// #include "vk_layer_data.h"
#include "vulkan/vulkan.h"
#include <map>
#include <set>
#include <string.h>
#include <string>
#include <vector>

#include <vvv/vk/format_utils.hpp>

struct VULKAN_FORMAT_INFO {
    uint32_t size;
    uint32_t component_count;
    VkFormatCompatibilityClass format_class;
};

// Disable auto-formatting for this large table
// clang-format off

// Set up data structure with size(bytes) and number of components for each Vulkan format
// For compressed and multi-plane formats, size is bytes per compressed or shared block
const std::map<VkFormat, VULKAN_FORMAT_INFO> kVkFormatTable = {
   {VK_FORMAT_UNDEFINED,                   {0, 0, VK_FORMAT_COMPATIBILITY_CLASS_NONE_BIT }},
   {VK_FORMAT_R4G4_UNORM_PACK8,            {1, 2, VK_FORMAT_COMPATIBILITY_CLASS_8_BIT}},
   {VK_FORMAT_R4G4B4A4_UNORM_PACK16,       {2, 4, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_B4G4R4A4_UNORM_PACK16,       {2, 4, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT,   {2, 4, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT,   {2, 4, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R5G6B5_UNORM_PACK16,         {2, 3, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_B5G6R5_UNORM_PACK16,         {2, 3, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R5G5B5A1_UNORM_PACK16,       {2, 4, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_B5G5R5A1_UNORM_PACK16,       {2, 4, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_A1R5G5B5_UNORM_PACK16,       {2, 4, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R8_UNORM,                    {1, 1, VK_FORMAT_COMPATIBILITY_CLASS_8_BIT}},
   {VK_FORMAT_R8_SNORM,                    {1, 1, VK_FORMAT_COMPATIBILITY_CLASS_8_BIT}},
   {VK_FORMAT_R8_USCALED,                  {1, 1, VK_FORMAT_COMPATIBILITY_CLASS_8_BIT}},
   {VK_FORMAT_R8_SSCALED,                  {1, 1, VK_FORMAT_COMPATIBILITY_CLASS_8_BIT}},
   {VK_FORMAT_R8_UINT,                     {1, 1, VK_FORMAT_COMPATIBILITY_CLASS_8_BIT}},
   {VK_FORMAT_R8_SINT,                     {1, 1, VK_FORMAT_COMPATIBILITY_CLASS_8_BIT}},
   {VK_FORMAT_R8_SRGB,                     {1, 1, VK_FORMAT_COMPATIBILITY_CLASS_8_BIT}},
   {VK_FORMAT_R8G8_UNORM,                  {2, 2, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R8G8_SNORM,                  {2, 2, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R8G8_USCALED,                {2, 2, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R8G8_SSCALED,                {2, 2, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R8G8_UINT,                   {2, 2, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R8G8_SINT,                   {2, 2, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R8G8_SRGB,                   {2, 2, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R8G8B8_UNORM,                {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_R8G8B8_SNORM,                {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_R8G8B8_USCALED,              {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_R8G8B8_SSCALED,              {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_R8G8B8_UINT,                 {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_R8G8B8_SINT,                 {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_R8G8B8_SRGB,                 {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_B8G8R8_UNORM,                {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_B8G8R8_SNORM,                {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_B8G8R8_USCALED,              {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_B8G8R8_SSCALED,              {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_B8G8R8_UINT,                 {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_B8G8R8_SINT,                 {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_B8G8R8_SRGB,                 {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_24_BIT}},
   {VK_FORMAT_R8G8B8A8_UNORM,              {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R8G8B8A8_SNORM,              {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R8G8B8A8_USCALED,            {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R8G8B8A8_SSCALED,            {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R8G8B8A8_UINT,               {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R8G8B8A8_SINT,               {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R8G8B8A8_SRGB,               {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_B8G8R8A8_UNORM,              {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_B8G8R8A8_SNORM,              {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_B8G8R8A8_USCALED,            {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_B8G8R8A8_SSCALED,            {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_B8G8R8A8_UINT,               {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_B8G8R8A8_SINT,               {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_B8G8R8A8_SRGB,               {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A8B8G8R8_UNORM_PACK32,       {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A8B8G8R8_SNORM_PACK32,       {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A8B8G8R8_USCALED_PACK32,     {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A8B8G8R8_SSCALED_PACK32,     {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A8B8G8R8_UINT_PACK32,        {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A8B8G8R8_SINT_PACK32,        {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A8B8G8R8_SRGB_PACK32,        {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2R10G10B10_UNORM_PACK32,    {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2R10G10B10_SNORM_PACK32,    {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2R10G10B10_USCALED_PACK32,  {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2R10G10B10_SSCALED_PACK32,  {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2R10G10B10_UINT_PACK32,     {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2R10G10B10_SINT_PACK32,     {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2B10G10R10_UNORM_PACK32,    {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2B10G10R10_SNORM_PACK32,    {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2B10G10R10_USCALED_PACK32,  {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2B10G10R10_SSCALED_PACK32,  {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2B10G10R10_UINT_PACK32,     {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_A2B10G10R10_SINT_PACK32,     {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R16_UNORM,                   {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R16_SNORM,                   {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R16_USCALED,                 {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R16_SSCALED,                 {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R16_UINT,                    {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R16_SINT,                    {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R16_SFLOAT,                  {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R16G16_UNORM,                {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R16G16_SNORM,                {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R16G16_USCALED,              {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R16G16_SSCALED,              {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R16G16_UINT,                 {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R16G16_SINT,                 {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R16G16_SFLOAT,               {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R16G16B16_UNORM,             {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_48_BIT}},
   {VK_FORMAT_R16G16B16_SNORM,             {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_48_BIT}},
   {VK_FORMAT_R16G16B16_USCALED,           {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_48_BIT}},
   {VK_FORMAT_R16G16B16_SSCALED,           {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_48_BIT}},
   {VK_FORMAT_R16G16B16_UINT,              {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_48_BIT}},
   {VK_FORMAT_R16G16B16_SINT,              {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_48_BIT}},
   {VK_FORMAT_R16G16B16_SFLOAT,            {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_48_BIT}},
   {VK_FORMAT_R16G16B16A16_UNORM,          {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R16G16B16A16_SNORM,          {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R16G16B16A16_USCALED,        {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R16G16B16A16_SSCALED,        {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R16G16B16A16_UINT,           {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R16G16B16A16_SINT,           {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R16G16B16A16_SFLOAT,         {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R32_UINT,                    {4, 1, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R32_SINT,                    {4, 1, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R32_SFLOAT,                  {4, 1, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R32G32_UINT,                 {8, 2, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R32G32_SINT,                 {8, 2, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R32G32_SFLOAT,               {8, 2, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R32G32B32_UINT,              {12, 3, VK_FORMAT_COMPATIBILITY_CLASS_96_BIT}},
   {VK_FORMAT_R32G32B32_SINT,              {12, 3, VK_FORMAT_COMPATIBILITY_CLASS_96_BIT}},
   {VK_FORMAT_R32G32B32_SFLOAT,            {12, 3, VK_FORMAT_COMPATIBILITY_CLASS_96_BIT}},
   {VK_FORMAT_R32G32B32A32_UINT,           {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_128_BIT}},
   {VK_FORMAT_R32G32B32A32_SINT,           {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_128_BIT}},
   {VK_FORMAT_R32G32B32A32_SFLOAT,         {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_128_BIT}},
   {VK_FORMAT_R64_UINT,                    {8, 1, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R64_SINT,                    {8, 1, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R64_SFLOAT,                  {8, 1, VK_FORMAT_COMPATIBILITY_CLASS_64_BIT}},
   {VK_FORMAT_R64G64_UINT,                 {16, 2, VK_FORMAT_COMPATIBILITY_CLASS_128_BIT}},
   {VK_FORMAT_R64G64_SINT,                 {16, 2, VK_FORMAT_COMPATIBILITY_CLASS_128_BIT}},
   {VK_FORMAT_R64G64_SFLOAT,               {16, 2, VK_FORMAT_COMPATIBILITY_CLASS_128_BIT}},
   {VK_FORMAT_R64G64B64_UINT,              {24, 3, VK_FORMAT_COMPATIBILITY_CLASS_192_BIT}},
   {VK_FORMAT_R64G64B64_SINT,              {24, 3, VK_FORMAT_COMPATIBILITY_CLASS_192_BIT}},
   {VK_FORMAT_R64G64B64_SFLOAT,            {24, 3, VK_FORMAT_COMPATIBILITY_CLASS_192_BIT}},
   {VK_FORMAT_R64G64B64A64_UINT,           {32, 4, VK_FORMAT_COMPATIBILITY_CLASS_256_BIT}},
   {VK_FORMAT_R64G64B64A64_SINT,           {32, 4, VK_FORMAT_COMPATIBILITY_CLASS_256_BIT}},
   {VK_FORMAT_R64G64B64A64_SFLOAT,         {32, 4, VK_FORMAT_COMPATIBILITY_CLASS_256_BIT}},
   {VK_FORMAT_B10G11R11_UFLOAT_PACK32,     {4, 3, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,      {4, 3, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_D16_UNORM,                   {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_NONE_BIT}},
   {VK_FORMAT_X8_D24_UNORM_PACK32,         {4, 1, VK_FORMAT_COMPATIBILITY_CLASS_NONE_BIT}},
   {VK_FORMAT_D32_SFLOAT,                  {4, 1, VK_FORMAT_COMPATIBILITY_CLASS_NONE_BIT}},
   {VK_FORMAT_S8_UINT,                     {1, 1, VK_FORMAT_COMPATIBILITY_CLASS_NONE_BIT}},
   {VK_FORMAT_D16_UNORM_S8_UINT,           {3, 2, VK_FORMAT_COMPATIBILITY_CLASS_NONE_BIT}},
   {VK_FORMAT_D24_UNORM_S8_UINT,           {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_NONE_BIT}},
   {VK_FORMAT_D32_SFLOAT_S8_UINT,          {8, 2, VK_FORMAT_COMPATIBILITY_CLASS_NONE_BIT}},
   {VK_FORMAT_BC1_RGB_UNORM_BLOCK,         {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC1_RGB_BIT}},
   {VK_FORMAT_BC1_RGB_SRGB_BLOCK,          {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC1_RGB_BIT}},
   {VK_FORMAT_BC1_RGBA_UNORM_BLOCK,        {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC1_RGBA_BIT}},
   {VK_FORMAT_BC1_RGBA_SRGB_BLOCK,         {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC1_RGBA_BIT}},
   {VK_FORMAT_BC2_UNORM_BLOCK,             {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC2_BIT}},
   {VK_FORMAT_BC2_SRGB_BLOCK,              {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC2_BIT}},
   {VK_FORMAT_BC3_UNORM_BLOCK,             {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC3_BIT}},
   {VK_FORMAT_BC3_SRGB_BLOCK,              {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC3_BIT}},
   {VK_FORMAT_BC4_UNORM_BLOCK,             {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC4_BIT}},
   {VK_FORMAT_BC4_SNORM_BLOCK,             {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC4_BIT}},
   {VK_FORMAT_BC5_UNORM_BLOCK,             {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC5_BIT}},
   {VK_FORMAT_BC5_SNORM_BLOCK,             {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC5_BIT}},
   {VK_FORMAT_BC6H_UFLOAT_BLOCK,           {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC6H_BIT}},
   {VK_FORMAT_BC6H_SFLOAT_BLOCK,           {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC6H_BIT}},
   {VK_FORMAT_BC7_UNORM_BLOCK,             {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC7_BIT}},
   {VK_FORMAT_BC7_SRGB_BLOCK,              {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_BC7_BIT}},
   {VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,     {8, 3, VK_FORMAT_COMPATIBILITY_CLASS_ETC2_RGB_BIT}},
   {VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,      {8, 3, VK_FORMAT_COMPATIBILITY_CLASS_ETC2_RGB_BIT}},
   {VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,   {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_ETC2_RGBA_BIT}},
   {VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,    {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_ETC2_RGBA_BIT}},
   {VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,   {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ETC2_EAC_RGBA_BIT}},
   {VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,    {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ETC2_EAC_RGBA_BIT}},
   {VK_FORMAT_EAC_R11_UNORM_BLOCK,         {8, 1, VK_FORMAT_COMPATIBILITY_CLASS_EAC_R_BIT}},
   {VK_FORMAT_EAC_R11_SNORM_BLOCK,         {8, 1, VK_FORMAT_COMPATIBILITY_CLASS_EAC_R_BIT}},
   {VK_FORMAT_EAC_R11G11_UNORM_BLOCK,      {16, 2, VK_FORMAT_COMPATIBILITY_CLASS_EAC_RG_BIT}},
   {VK_FORMAT_EAC_R11G11_SNORM_BLOCK,      {16, 2, VK_FORMAT_COMPATIBILITY_CLASS_EAC_RG_BIT}},
   {VK_FORMAT_ASTC_4x4_UNORM_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_4X4_BIT}},
   {VK_FORMAT_ASTC_4x4_SRGB_BLOCK,         {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_4X4_BIT}},
   {VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT,   {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_4X4_BIT}},
   {VK_FORMAT_ASTC_5x4_UNORM_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_5X4_BIT}},
   {VK_FORMAT_ASTC_5x4_SRGB_BLOCK,         {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_5X4_BIT}},
   {VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT,   {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_5X4_BIT}},
   {VK_FORMAT_ASTC_5x5_UNORM_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_5X5_BIT}},
   {VK_FORMAT_ASTC_5x5_SRGB_BLOCK,         {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_5X5_BIT}},
   {VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT,   {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_5X5_BIT}},
   {VK_FORMAT_ASTC_6x5_UNORM_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_6X5_BIT}},
   {VK_FORMAT_ASTC_6x5_SRGB_BLOCK,         {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_6X5_BIT}},
   {VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT,   {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_6X5_BIT}},
   {VK_FORMAT_ASTC_6x6_UNORM_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_6X6_BIT}},
   {VK_FORMAT_ASTC_6x6_SRGB_BLOCK,         {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_6X6_BIT}},
   {VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT,   {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_6X6_BIT}},
   {VK_FORMAT_ASTC_8x5_UNORM_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_8X5_BIT}},
   {VK_FORMAT_ASTC_8x5_SRGB_BLOCK,         {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_8X5_BIT}},
   {VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT,   {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_8X5_BIT}},
   {VK_FORMAT_ASTC_8x6_UNORM_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_8X6_BIT}},
   {VK_FORMAT_ASTC_8x6_SRGB_BLOCK,         {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_8X6_BIT}},
   {VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT,   {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_8X6_BIT}},
   {VK_FORMAT_ASTC_8x8_UNORM_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_8X8_BIT}},
   {VK_FORMAT_ASTC_8x8_SRGB_BLOCK,         {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_8X8_BIT}},
   {VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT,   {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_8X8_BIT}},
   {VK_FORMAT_ASTC_10x5_UNORM_BLOCK,       {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X5_BIT}},
   {VK_FORMAT_ASTC_10x5_SRGB_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X5_BIT}},
   {VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT,  {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X5_BIT}},
   {VK_FORMAT_ASTC_10x6_UNORM_BLOCK,       {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X6_BIT}},
   {VK_FORMAT_ASTC_10x6_SRGB_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X6_BIT}},
   {VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT,  {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X6_BIT}},
   {VK_FORMAT_ASTC_10x8_UNORM_BLOCK,       {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X8_BIT}},
   {VK_FORMAT_ASTC_10x8_SRGB_BLOCK,        {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X8_BIT}},
   {VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT,  {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X8_BIT}},
   {VK_FORMAT_ASTC_10x10_UNORM_BLOCK,      {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X10_BIT}},
   {VK_FORMAT_ASTC_10x10_SRGB_BLOCK,       {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X10_BIT}},
   {VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT, {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_10X10_BIT}},
   {VK_FORMAT_ASTC_12x10_UNORM_BLOCK,      {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_12X10_BIT}},
   {VK_FORMAT_ASTC_12x10_SRGB_BLOCK,       {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_12X10_BIT}},
   {VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT, {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_12X10_BIT}},
   {VK_FORMAT_ASTC_12x12_UNORM_BLOCK,      {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_12X12_BIT}},
   {VK_FORMAT_ASTC_12x12_SRGB_BLOCK,       {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_12X12_BIT}},
   {VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT, {16, 4, VK_FORMAT_COMPATIBILITY_CLASS_ASTC_12X12_BIT}},
   {VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG, {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_PVRTC1_2BPP_BIT}},
   {VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG, {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_PVRTC1_4BPP_BIT}},
   {VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG, {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_PVRTC2_2BPP_BIT}},
   {VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG, {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_PVRTC2_4BPP_BIT}},
   {VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG,  {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_PVRTC1_2BPP_BIT}},
   {VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG,  {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_PVRTC1_4BPP_BIT}},
   {VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG,  {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_PVRTC2_2BPP_BIT}},
   {VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG,  {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_PVRTC2_4BPP_BIT}},
   // KHR_sampler_YCbCr_conversion extension - single-plane variants
   // 'PACK' formats are normal, uncompressed
   {VK_FORMAT_R10X6_UNORM_PACK16,                          {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R10X6G10X6_UNORM_2PACK16,                    {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16,          {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64BIT_R10G10B10A10}},
   {VK_FORMAT_R12X4_UNORM_PACK16,                          {2, 1, VK_FORMAT_COMPATIBILITY_CLASS_16_BIT}},
   {VK_FORMAT_R12X4G12X4_UNORM_2PACK16,                    {4, 2, VK_FORMAT_COMPATIBILITY_CLASS_32_BIT}},
   {VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16,          {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64BIT_R12G12B12A12}},
   // _422 formats encode 2 texels per entry with B, R components shared - treated as compressed w/ 2x1 block size
   {VK_FORMAT_G8B8G8R8_422_UNORM,                          {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32BIT_G8B8G8R8}},
   {VK_FORMAT_B8G8R8G8_422_UNORM,                          {4, 4, VK_FORMAT_COMPATIBILITY_CLASS_32BIT_B8G8R8G8}},
   {VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16,      {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64BIT_G10B10G10R10}},
   {VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16,      {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64BIT_B10G10R10G10}},
   {VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16,      {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64BIT_G12B12G12R12}},
   {VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16,      {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64BIT_B12G12R12G12}},
   {VK_FORMAT_G16B16G16R16_422_UNORM,                      {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64BIT_G16B16G16R16}},
   {VK_FORMAT_B16G16R16G16_422_UNORM,                      {8, 4, VK_FORMAT_COMPATIBILITY_CLASS_64BIT_B16G16R16G16}},
   // KHR_sampler_YCbCr_conversion extension - multi-plane variants
   // Formats that 'share' components among texels (_420 and _422), size represents total bytes for the smallest possible texel block
   // _420 share B, R components within a 2x2 texel block
   {VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,                   {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_8BIT_3PLANE_420}},
   {VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,                    {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_8BIT_2PLANE_420}},
   {VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16,  {12, 3, VK_FORMAT_COMPATIBILITY_CLASS_10BIT_3PLANE_420}},
   {VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16,   {12, 3, VK_FORMAT_COMPATIBILITY_CLASS_10BIT_2PLANE_420}},
   {VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16,  {12, 3, VK_FORMAT_COMPATIBILITY_CLASS_12BIT_3PLANE_420}},
   {VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16,   {12, 3, VK_FORMAT_COMPATIBILITY_CLASS_12BIT_2PLANE_420}},
   {VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM,                {12, 3, VK_FORMAT_COMPATIBILITY_CLASS_16BIT_3PLANE_420}},
   {VK_FORMAT_G16_B16R16_2PLANE_420_UNORM,                 {12, 3, VK_FORMAT_COMPATIBILITY_CLASS_16BIT_2PLANE_420}},
   // _422 share B, R components within a 2x1 texel block
   {VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM,                   {4, 3, VK_FORMAT_COMPATIBILITY_CLASS_8BIT_3PLANE_422}},
   {VK_FORMAT_G8_B8R8_2PLANE_422_UNORM,                    {4, 3, VK_FORMAT_COMPATIBILITY_CLASS_8BIT_2PLANE_422}},
   {VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16,  {8, 3, VK_FORMAT_COMPATIBILITY_CLASS_10BIT_3PLANE_422}},
   {VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16,   {8, 3, VK_FORMAT_COMPATIBILITY_CLASS_10BIT_2PLANE_422}},
   {VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16,  {8, 3, VK_FORMAT_COMPATIBILITY_CLASS_12BIT_3PLANE_422}},
   {VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16,   {8, 3, VK_FORMAT_COMPATIBILITY_CLASS_12BIT_2PLANE_422}},
   {VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM,                {8, 3, VK_FORMAT_COMPATIBILITY_CLASS_16BIT_3PLANE_422}},
   {VK_FORMAT_G16_B16R16_2PLANE_422_UNORM,                 {8, 3, VK_FORMAT_COMPATIBILITY_CLASS_16BIT_2PLANE_422}},
   // _444 do not share
   {VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM,                   {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_8BIT_3PLANE_444}},
   {VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16,  {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_10BIT_3PLANE_444}},
   {VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16,  {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_12BIT_3PLANE_444}},
   {VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM,                {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_16BIT_3PLANE_444}},
   //{VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT,                {3, 3, VK_FORMAT_COMPATIBILITY_CLASS_8BIT_2PLANE_444}},
   //{VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT, {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_10BIT_2PLANE_444}},
   //{VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT, {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_12BIT_2PLANE_444}},
   //{VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT,             {6, 3, VK_FORMAT_COMPATIBILITY_CLASS_16BIT_2PLANE_444}}
};

// Renable formatting
// clang-format on

// Return true if format is an ETC2 or EAC compressed texture format
VK_LAYER_EXPORT bool FormatIsCompressed_ETC2_EAC(VkFormat format) {
    bool found = false;

    switch (format) {
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
    case VK_FORMAT_EAC_R11_UNORM_BLOCK:
    case VK_FORMAT_EAC_R11_SNORM_BLOCK:
    case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
    case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
        found = true;
        break;
    default:
        break;
    }
    return found;
}

// Return true if format is either a LDR or HDR ASTC compressed textyre format
VK_LAYER_EXPORT bool FormatIsCompressed_ASTC(VkFormat format) { return (FormatIsCompressed_ASTC_LDR(format) || FormatIsCompressed_ASTC_HDR(format)); }

// Return true if format is an LDR ASTC compressed texture format
VK_LAYER_EXPORT bool FormatIsCompressed_ASTC_LDR(VkFormat format) {
    bool found = false;

    switch (format) {
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
    case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
    case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
    case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
    case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
    case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
    case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
    case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
    case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
        found = true;
        break;
    default:
        break;
    }
    return found;
}

// Return true if format is an HDR ASTC compressed texture format
VK_LAYER_EXPORT bool FormatIsCompressed_ASTC_HDR(VkFormat format) {
    bool found = false;

    switch (format) {
    case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT:
    case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT:
        found = true;
        break;
    default:
        break;
    }
    return found;
}

// Return true if format is a BC compressed texture format
VK_LAYER_EXPORT bool FormatIsCompressed_BC(VkFormat format) {
    bool found = false;

    switch (format) {
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        found = true;
        break;
    default:
        break;
    }
    return found;
}

// Return true if format is a PVRTC compressed texture format
VK_LAYER_EXPORT bool FormatIsCompressed_PVRTC(VkFormat format) {
    bool found = false;

    switch (format) {
    case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG:
    case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG:
        found = true;
        break;
    default:
        break;
    }
    return found;
}

// Single-plane "_422" formats are treated as 2x1 compressed (for copies)
VK_LAYER_EXPORT bool FormatIsSinglePlane_422(VkFormat format) {
    bool found = false;

    switch (format) {
    case VK_FORMAT_G8B8G8R8_422_UNORM:
    case VK_FORMAT_B8G8R8G8_422_UNORM:
    case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
    case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
    case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
    case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
    case VK_FORMAT_G16B16G16R16_422_UNORM:
    case VK_FORMAT_B16G16R16G16_422_UNORM:
        found = true;
        break;
    default:
        break;
    }
    return found;
}

// Return true if format is compressed
VK_LAYER_EXPORT bool FormatIsCompressed(VkFormat format) {
    return (FormatIsCompressed_ASTC(format) || FormatIsCompressed_BC(format) || FormatIsCompressed_ETC2_EAC(format) || FormatIsCompressed_PVRTC(format));
}
// Return true if format is packed
VK_LAYER_EXPORT bool FormatIsPacked(VkFormat format) {
    bool found = false;

    switch (format) {
    case VK_FORMAT_R4G4_UNORM_PACK8:
    case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
    case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
    case VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT:
    case VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT:
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
    case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_UINT_PACK32:
    case VK_FORMAT_A2R10G10B10_SINT_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
    case VK_FORMAT_A2B10G10R10_SINT_PACK32:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_R10X6_UNORM_PACK16:
    case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
    case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16:
    case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
    case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
    case VK_FORMAT_R12X4_UNORM_PACK16:
    case VK_FORMAT_R12X4G12X4_UNORM_2PACK16:
    case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16:
    case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
    case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
        // case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT:
        // case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT:
        found = true;
        break;
    default:
        break;
    }
    return found;
}

// Return true if format is 'normal', with one texel per format element
VK_LAYER_EXPORT bool FormatElementIsTexel(VkFormat format) {
    if (FormatIsPacked(format) || FormatIsCompressed(format) || FormatIsSinglePlane_422(format) || FormatIsMultiplane(format)) {
        return false;
    } else {
        return true;
    }
}

// Return true if format is a depth or stencil format
VK_LAYER_EXPORT bool FormatIsDepthOrStencil(VkFormat format) { return (FormatIsDepthAndStencil(format) || FormatIsDepthOnly(format) || FormatIsStencilOnly(format)); }

// Return true if format contains depth and stencil information
VK_LAYER_EXPORT bool FormatIsDepthAndStencil(VkFormat format) {
    bool is_ds = false;

    switch (format) {
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        is_ds = true;
        break;
    default:
        break;
    }
    return is_ds;
}

// Return true if format is a stencil-only format
VK_LAYER_EXPORT bool FormatIsStencilOnly(VkFormat format) { return (format == VK_FORMAT_S8_UINT); }

// Return true if format is a depth-only format
VK_LAYER_EXPORT bool FormatIsDepthOnly(VkFormat format) {
    bool is_depth = false;

    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        is_depth = true;
        break;
    default:
        break;
    }

    return is_depth;
}

// Return true if format is of type NORM
VK_LAYER_EXPORT bool FormatIsNorm(VkFormat format) { return (FormatIsUNorm(format) || FormatIsSNorm(format)); }

// Return true if format is of type UNORM
VK_LAYER_EXPORT bool FormatIsUNorm(VkFormat format) {
    bool is_unorm = false;

    switch (format) {
    case VK_FORMAT_R4G4_UNORM_PACK8:
    case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
    case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
    case VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT:
    case VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT:
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16B16_UNORM:
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
    case VK_FORMAT_EAC_R11_UNORM_BLOCK:
    case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
    case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
    case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
    case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
    case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
    case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
    case VK_FORMAT_B8G8R8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_G8B8G8R8_422_UNORM:
    case VK_FORMAT_B8G8R8G8_422_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
    case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16:
    case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
    case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
    case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16:
    case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
    case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
    case VK_FORMAT_G16B16G16R16_422_UNORM:
    case VK_FORMAT_B16G16R16G16_422_UNORM:
    case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
    case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
    case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_R10X6_UNORM_PACK16:
    case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
    case VK_FORMAT_R12X4_UNORM_PACK16:
    case VK_FORMAT_R12X4G12X4_UNORM_2PACK16:
        // case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT:
        // case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT:
        // case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT:
        // case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT:
        is_unorm = true;
        break;
    default:
        break;
    }

    return is_unorm;
}

// Return true if format is of type SNORM
VK_LAYER_EXPORT bool FormatIsSNorm(VkFormat format) {
    bool is_snorm = false;

    switch (format) {
    case VK_FORMAT_R8_SNORM:
    case VK_FORMAT_R8G8_SNORM:
    case VK_FORMAT_R8G8B8_SNORM:
    case VK_FORMAT_R8G8B8A8_SNORM:
    case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
    case VK_FORMAT_R16_SNORM:
    case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16B16_SNORM:
    case VK_FORMAT_R16G16B16A16_SNORM:
    case VK_FORMAT_BC4_SNORM_BLOCK:
    case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_EAC_R11_SNORM_BLOCK:
    case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
    case VK_FORMAT_B8G8R8_SNORM:
    case VK_FORMAT_B8G8R8A8_SNORM:
    case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
        is_snorm = true;
        break;
    default:
        break;
    }

    return is_snorm;
}

// Return true if format is an integer format
VK_LAYER_EXPORT bool FormatIsInt(VkFormat format) { return (FormatIsSInt(format) || FormatIsUInt(format)); }

// Return true if format is an unsigned integer format
VK_LAYER_EXPORT bool FormatIsUInt(VkFormat format) {
    bool is_uint = false;

    switch (format) {
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8B8_UINT:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16B16_UINT:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32B32_UINT:
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R64_UINT:
    case VK_FORMAT_R64G64_UINT:
    case VK_FORMAT_R64G64B64_UINT:
    case VK_FORMAT_R64G64B64A64_UINT:
    case VK_FORMAT_B8G8R8_UINT:
    case VK_FORMAT_B8G8R8A8_UINT:
    case VK_FORMAT_A2R10G10B10_UINT_PACK32:
        is_uint = true;
        break;
    default:
        break;
    }

    return is_uint;
}

// Return true if format is a signed integer format
VK_LAYER_EXPORT bool FormatIsSInt(VkFormat format) {
    bool is_sint = false;

    switch (format) {
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8B8_SINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_A2B10G10R10_SINT_PACK32:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16B16_SINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R64_SINT:
    case VK_FORMAT_R64G64_SINT:
    case VK_FORMAT_R64G64B64_SINT:
    case VK_FORMAT_R64G64B64A64_SINT:
    case VK_FORMAT_B8G8R8_SINT:
    case VK_FORMAT_B8G8R8A8_SINT:
    case VK_FORMAT_A2R10G10B10_SINT_PACK32:
        is_sint = true;
        break;
    default:
        break;
    }

    return is_sint;
}

// Return true if format is a floating-point format
VK_LAYER_EXPORT bool FormatIsFloat(VkFormat format) {
    bool is_float = false;

    switch (format) {
    case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R16G16B16_SFLOAT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_R32G32B32_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R64_SFLOAT:
    case VK_FORMAT_R64G64_SFLOAT:
    case VK_FORMAT_R64G64B64_SFLOAT:
    case VK_FORMAT_R64G64B64A64_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        is_float = true;
        break;
    default:
        break;
    }

    return is_float;
}

// Return true if format is in the SRGB colorspace
VK_LAYER_EXPORT bool FormatIsSRGB(VkFormat format) {
    bool is_srgb = false;

    switch (format) {
    case VK_FORMAT_R8_SRGB:
    case VK_FORMAT_R8G8_SRGB:
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
    case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
    case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
    case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
    case VK_FORMAT_B8G8R8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
        is_srgb = true;
        break;
    default:
        break;
    }

    return is_srgb;
}

// Return true if format is a USCALED format
VK_LAYER_EXPORT bool FormatIsUScaled(VkFormat format) {
    bool is_uscaled = false;

    switch (format) {
    case VK_FORMAT_R8_USCALED:
    case VK_FORMAT_R8G8_USCALED:
    case VK_FORMAT_R8G8B8_USCALED:
    case VK_FORMAT_B8G8R8_USCALED:
    case VK_FORMAT_R8G8B8A8_USCALED:
    case VK_FORMAT_B8G8R8A8_USCALED:
    case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
    case VK_FORMAT_R16_USCALED:
    case VK_FORMAT_R16G16_USCALED:
    case VK_FORMAT_R16G16B16_USCALED:
    case VK_FORMAT_R16G16B16A16_USCALED:
        is_uscaled = true;
        break;
    default:
        break;
    }

    return is_uscaled;
}

// Return true if format is a SSCALED format
VK_LAYER_EXPORT bool FormatIsSScaled(VkFormat format) {
    bool is_sscaled = false;

    switch (format) {
    case VK_FORMAT_R8_SSCALED:
    case VK_FORMAT_R8G8_SSCALED:
    case VK_FORMAT_R8G8B8_SSCALED:
    case VK_FORMAT_B8G8R8_SSCALED:
    case VK_FORMAT_R8G8B8A8_SSCALED:
    case VK_FORMAT_B8G8R8A8_SSCALED:
    case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
    case VK_FORMAT_R16_SSCALED:
    case VK_FORMAT_R16G16_SSCALED:
    case VK_FORMAT_R16G16B16_SSCALED:
    case VK_FORMAT_R16G16B16A16_SSCALED:
        is_sscaled = true;
        break;
    default:
        break;
    }

    return is_sscaled;
}

// Types from "Interpretation of Numeric Format" table
VK_LAYER_EXPORT bool FormatIsSampledInt(VkFormat format) { return FormatIsInt(format); }
VK_LAYER_EXPORT bool FormatIsSampledFloat(VkFormat format) {
    return (FormatIsUNorm(format) || FormatIsSNorm(format) || FormatIsUScaled(format) || FormatIsSScaled(format) || FormatIsFloat(format) || FormatIsSRGB(format));
}

// Return texel block sizes for all formats
// Uncompressed formats return {1, 1, 1}
// Compressed formats return the compression block extents
// Multiplane formats return the 'shared' extent of their low-res component(s)
VK_LAYER_EXPORT VkExtent3D FormatTexelBlockExtent(VkFormat format) {
    VkExtent3D block_size = {1, 1, 1};
    switch (format) {
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
    case VK_FORMAT_EAC_R11_UNORM_BLOCK:
    case VK_FORMAT_EAC_R11_SNORM_BLOCK:
    case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
    case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
    case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT:
        block_size = {4, 4, 1};
        break;
    case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
    case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
    case VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT:
        block_size = {5, 4, 1};
        break;
    case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT:
        block_size = {5, 5, 1};
        break;
    case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT:
        block_size = {6, 5, 1};
        break;
    case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT:
        block_size = {6, 6, 1};
        break;
    case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT:
        block_size = {8, 5, 1};
        break;
    case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
    case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT:
        block_size = {8, 6, 1};
        break;
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT:
        block_size = {8, 8, 1};
        break;
    case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT:
        block_size = {10, 5, 1};
        break;
    case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT:
        block_size = {10, 6, 1};
        break;
    case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT:
        block_size = {10, 8, 1};
        break;
    case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
    case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
    case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT:
        block_size = {10, 10, 1};
        break;
    case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
    case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
    case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT:
        block_size = {12, 10, 1};
        break;
    case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
    case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
    case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT:
        block_size = {12, 12, 1};
        break;
    case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG:
        block_size = {8, 4, 1};
        break;
    case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG:
    case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG:
    case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG:
        block_size = {4, 4, 1};
        break;
    // (KHR_sampler_ycbcr_conversion) _422 single-plane formats are treated as 2x1 compressed (for copies)
    case VK_FORMAT_G8B8G8R8_422_UNORM:
    case VK_FORMAT_B8G8R8G8_422_UNORM:
    case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
    case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
    case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
    case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
    case VK_FORMAT_G16B16G16R16_422_UNORM:
    case VK_FORMAT_B16G16R16G16_422_UNORM:
        block_size = {2, 1, 1};
        break;
    // _422 multi-plane formats are not considered compressed, but shared components form a logical 2x1 block
    case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
        block_size = {2, 1, 1};
        break;
    // _420 formats are not considered compressed, but shared components form a logical 2x2 block
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        block_size = {2, 2, 1};
        break;
    // _444 multi-plane formats do not share components, default to 1x1
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
    case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
    case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
    default:
        break;
    }
    return block_size;
}

VK_LAYER_EXPORT uint32_t FormatDepthSize(VkFormat format) {
    uint32_t depth_size = 0;
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D16_UNORM_S8_UINT:
        depth_size = 16;
        break;
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        depth_size = 24;
        break;
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        depth_size = 32;
        break;

    default:
        break;
    }
    return depth_size;
}

VK_LAYER_EXPORT VkFormatNumericalType FormatDepthNumericalType(VkFormat format) {
    VkFormatNumericalType numerical_type = VK_FORMAT_NUMERICAL_TYPE_NONE;
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        numerical_type = VK_FORMAT_NUMERICAL_TYPE_UNORM;
        break;
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        numerical_type = VK_FORMAT_NUMERICAL_TYPE_SFLOAT;
        break;

    default:
        break;
    }
    return numerical_type;
}

VK_LAYER_EXPORT uint32_t FormatStencilSize(VkFormat format) {
    uint32_t stencil_size = 0;
    switch (format) {
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        stencil_size = 8;
        break;

    default:
        break;
    }
    return stencil_size;
}

VK_LAYER_EXPORT VkFormatNumericalType FormatStencilNumericalType(VkFormat format) {
    VkFormatNumericalType numerical_type = VK_FORMAT_NUMERICAL_TYPE_NONE;
    switch (format) {
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        numerical_type = VK_FORMAT_NUMERICAL_TYPE_UINT;
        break;

    default:
        break;
    }
    return numerical_type;
}

VK_LAYER_EXPORT uint32_t FormatPlaneCount(VkFormat format) {
    switch (format) {
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
    case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
    case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
    case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
        return 3;
        break;
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
        // case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT:
        // case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT:
        // case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT:
        // case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT:
        return 2;
        break;
    default:
        return 1;
        break;
    }
}

// Return format class of the specified format
VK_LAYER_EXPORT VkFormatCompatibilityClass FormatCompatibilityClass(VkFormat format) {
    auto item = kVkFormatTable.find(format);
    if (item != kVkFormatTable.end()) {
        return item->second.format_class;
    }
    return VK_FORMAT_COMPATIBILITY_CLASS_NONE_BIT;
}

// Return size, in bytes, of one element of the specified format
// For uncompressed this is one texel, for compressed it is one block
VK_LAYER_EXPORT uint32_t FormatElementSize(VkFormat format, VkImageAspectFlags aspectMask) {
    // Handle special buffer packing rules for specific depth/stencil formats
    if (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
        format = VK_FORMAT_S8_UINT;
    } else if (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
        switch (format) {
        case VK_FORMAT_D16_UNORM_S8_UINT:
            format = VK_FORMAT_D16_UNORM;
            break;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return 3;
            break;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            format = VK_FORMAT_D32_SFLOAT;
            break;
        default:
            break;
        }
    } else if (FormatIsMultiplane(format)) {
        format = FindMultiplaneCompatibleFormat(format, aspectMask);
    }

    auto item = kVkFormatTable.find(format);
    if (item != kVkFormatTable.end()) {
        return item->second.size;
    }
    return 0;
}

// Return the size in bytes of one texel of given foramt
// For compressed or multi-plane, this may be a fractional number
VK_LAYER_EXPORT double FormatTexelSize(VkFormat format, VkImageAspectFlags aspectMask) {
    double texel_size = static_cast<double>(FormatElementSize(format, aspectMask));
    VkExtent3D block_extent = FormatTexelBlockExtent(format);
    uint32_t texels_per_block = block_extent.width * block_extent.height * block_extent.depth;
    if (1 < texels_per_block) {
        texel_size /= static_cast<double>(texels_per_block);
    }
    return texel_size;
}

// Return the number of components for a given format
uint32_t FormatComponentCount(VkFormat format) {
    auto item = kVkFormatTable.find(format);
    if (item != kVkFormatTable.end()) {
        return item->second.component_count;
    }
    return 0;
}

struct VULKAN_PER_PLANE_COMPATIBILITY {
    uint32_t width_divisor;
    uint32_t height_divisor;
    VkFormat compatible_format;
};

struct VULKAN_MULTIPLANE_COMPATIBILITY {
    VULKAN_PER_PLANE_COMPATIBILITY per_plane[VK_MULTIPLANE_FORMAT_MAX_PLANES];
};

// Source: Vulkan spec Table 47. Plane Format Compatibility Table
// clang-format off
static const std::map<VkFormat, VULKAN_MULTIPLANE_COMPATIBILITY>kVkMultiplaneCompatibilityMap {
   { VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,                  { { { 1, 1, VK_FORMAT_R8_UNORM },
                                                               { 2, 2, VK_FORMAT_R8_UNORM },
                                                               { 2, 2, VK_FORMAT_R8_UNORM } } } },
   { VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,                   { { { 1, 1, VK_FORMAT_R8_UNORM },
                                                               { 2, 2, VK_FORMAT_R8G8_UNORM },
                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } },
   { VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM,                  { { { 1, 1, VK_FORMAT_R8_UNORM },
                                                               { 2, 1, VK_FORMAT_R8_UNORM },
                                                               { 2, 1, VK_FORMAT_R8_UNORM } } } },
   { VK_FORMAT_G8_B8R8_2PLANE_422_UNORM,                   { { { 1, 1, VK_FORMAT_R8_UNORM },
                                                               { 2, 1, VK_FORMAT_R8G8_UNORM },
                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } },
   { VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM,                  { { { 1, 1, VK_FORMAT_R8_UNORM },
                                                               { 1, 1, VK_FORMAT_R8_UNORM },
                                                               { 1, 1, VK_FORMAT_R8_UNORM } } } },
   { VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16, { { { 1, 1, VK_FORMAT_R10X6_UNORM_PACK16 },
                                                               { 2, 2, VK_FORMAT_R10X6_UNORM_PACK16 },
                                                               { 2, 2, VK_FORMAT_R10X6_UNORM_PACK16 } } } },
   { VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16,  { { { 1, 1, VK_FORMAT_R10X6_UNORM_PACK16 },
                                                               { 2, 2, VK_FORMAT_R10X6G10X6_UNORM_2PACK16 },
                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } },
   { VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16, { { { 1, 1, VK_FORMAT_R10X6_UNORM_PACK16 },
                                                               { 2, 1, VK_FORMAT_R10X6_UNORM_PACK16 },
                                                               { 2, 1, VK_FORMAT_R10X6_UNORM_PACK16 } } } },
   { VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16,  { { { 1, 1, VK_FORMAT_R10X6_UNORM_PACK16 },
                                                               { 2, 1, VK_FORMAT_R10X6G10X6_UNORM_2PACK16 },
                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } },
   { VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16, { { { 1, 1, VK_FORMAT_R10X6_UNORM_PACK16 },
                                                               { 1, 1, VK_FORMAT_R10X6_UNORM_PACK16 },
                                                               { 1, 1, VK_FORMAT_R10X6_UNORM_PACK16 } } } },
   { VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16, { { { 1, 1, VK_FORMAT_R12X4_UNORM_PACK16 },
                                                               { 2, 2, VK_FORMAT_R12X4_UNORM_PACK16 },
                                                               { 2, 2, VK_FORMAT_R12X4_UNORM_PACK16 } } } },
   { VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16,  { { { 1, 1, VK_FORMAT_R12X4_UNORM_PACK16 },
                                                               { 2, 2, VK_FORMAT_R12X4G12X4_UNORM_2PACK16 },
                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } },
   { VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16, { { { 1, 1, VK_FORMAT_R12X4_UNORM_PACK16 },
                                                               { 2, 1, VK_FORMAT_R12X4_UNORM_PACK16 },
                                                               { 2, 1, VK_FORMAT_R12X4_UNORM_PACK16 } } } },
   { VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16,  { { { 1, 1, VK_FORMAT_R12X4_UNORM_PACK16 },
                                                               { 2, 1, VK_FORMAT_R12X4G12X4_UNORM_2PACK16 },
                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } },
   { VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16, { { { 1, 1, VK_FORMAT_R12X4_UNORM_PACK16 },
                                                               { 1, 1, VK_FORMAT_R12X4_UNORM_PACK16 },
                                                               { 1, 1, VK_FORMAT_R12X4_UNORM_PACK16 } } } },
   { VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM,               { { { 1, 1, VK_FORMAT_R16_UNORM },
                                                               { 2, 2, VK_FORMAT_R16_UNORM },
                                                               { 2, 2, VK_FORMAT_R16_UNORM } } } },
   { VK_FORMAT_G16_B16R16_2PLANE_420_UNORM,                { { { 1, 1, VK_FORMAT_R16_UNORM },
                                                               { 2, 2, VK_FORMAT_R16G16_UNORM },
                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } },
   { VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM,               { { { 1, 1, VK_FORMAT_R16_UNORM },
                                                               { 2, 1, VK_FORMAT_R16_UNORM },
                                                               { 2, 1, VK_FORMAT_R16_UNORM } } } },
   { VK_FORMAT_G16_B16R16_2PLANE_422_UNORM,                { { { 1, 1, VK_FORMAT_R16_UNORM },
                                                               { 2, 1, VK_FORMAT_R16G16_UNORM },
                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } },
   { VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM,               { { { 1, 1, VK_FORMAT_R16_UNORM },
                                                               { 1, 1, VK_FORMAT_R16_UNORM },
                                                               { 1, 1, VK_FORMAT_R16_UNORM } } } },
//   { VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT,               { { { 1, 1, VK_FORMAT_R8_UNORM },
//                                                               { 2, 1, VK_FORMAT_R8G8_UNORM },
//                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } },
//   { VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT, { { { 1, 1, VK_FORMAT_R10X6_UNORM_PACK16 },
//                                                                  { 2, 1, VK_FORMAT_R10X6G10X6_UNORM_2PACK16 },
//                                                                  { 1, 1, VK_FORMAT_UNDEFINED } } } },
//   { VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT, { { { 1, 1, VK_FORMAT_R12X4_UNORM_PACK16 },
//                                                                  { 2, 1, VK_FORMAT_R12X4G12X4_UNORM_2PACK16 },
//                                                                  { 1, 1, VK_FORMAT_UNDEFINED } } } },
//   { VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT,            { { { 1, 1, VK_FORMAT_R16_UNORM },
//                                                               { 2, 1, VK_FORMAT_R16G16_UNORM },
//                                                               { 1, 1, VK_FORMAT_UNDEFINED } } } }
};
// clang-format on

uint32_t GetPlaneIndex(VkImageAspectFlags aspect) {
    // Returns an out of bounds index on error
    switch (aspect) {
    case VK_IMAGE_ASPECT_PLANE_0_BIT:
        return 0;
        break;
    case VK_IMAGE_ASPECT_PLANE_1_BIT:
        return 1;
        break;
    case VK_IMAGE_ASPECT_PLANE_2_BIT:
        return 2;
        break;
    default:
        // If more than one plane bit is set, return error condition
        return VK_MULTIPLANE_FORMAT_MAX_PLANES;
        break;
    }
}

VK_LAYER_EXPORT VkFormat FindMultiplaneCompatibleFormat(VkFormat mp_fmt, VkImageAspectFlags plane_aspect) {
    uint32_t plane_idx = GetPlaneIndex(plane_aspect);
    auto it = kVkMultiplaneCompatibilityMap.find(mp_fmt);
    if ((it == kVkMultiplaneCompatibilityMap.end()) || (plane_idx >= VK_MULTIPLANE_FORMAT_MAX_PLANES)) {
        return VK_FORMAT_UNDEFINED;
    }

    return it->second.per_plane[plane_idx].compatible_format;
}

VK_LAYER_EXPORT VkExtent2D FindMultiplaneExtentDivisors(VkFormat mp_fmt, VkImageAspectFlags plane_aspect) {
    VkExtent2D divisors = {1, 1};
    uint32_t plane_idx = GetPlaneIndex(plane_aspect);
    auto it = kVkMultiplaneCompatibilityMap.find(mp_fmt);
    if ((it == kVkMultiplaneCompatibilityMap.end()) || (plane_idx >= VK_MULTIPLANE_FORMAT_MAX_PLANES)) {
        return divisors;
    }

    divisors.width = it->second.per_plane[plane_idx].width_divisor;
    divisors.height = it->second.per_plane[plane_idx].height_divisor;
    return divisors;
}

// Source: Vulkan spec Table 69. Formats requiring sampler YCBCR conversion for VK_IMAGE_ASPECT_COLOR_BIT image views
const std::set<VkFormat> kVkFormatsRequiringYcbcrConversion{
    VK_FORMAT_G8B8G8R8_422_UNORM,
    VK_FORMAT_B8G8R8G8_422_UNORM,
    VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,
    VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
    VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM,
    VK_FORMAT_G8_B8R8_2PLANE_422_UNORM,
    VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM,
    VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16,
    VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16,
    VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16,
    VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16,
    VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16,
    VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16,
    VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16,
    VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16,
    VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16,
    VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16,
    VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16,
    VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16,
    VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16,
    VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16,
    VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16,
    VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16,
    VK_FORMAT_G16B16G16R16_422_UNORM,
    VK_FORMAT_B16G16R16G16_422_UNORM,
    VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM,
    VK_FORMAT_G16_B16R16_2PLANE_420_UNORM,
    VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM,
    VK_FORMAT_G16_B16R16_2PLANE_422_UNORM,
    VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM,
    // VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT,
    // VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT,
    // VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT,
    // VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT
};

VK_LAYER_EXPORT bool FormatRequiresYcbcrConversion(VkFormat format) {
    auto it = kVkFormatsRequiringYcbcrConversion.find(format);
    return (it != kVkFormatsRequiringYcbcrConversion.end());
}

VK_LAYER_EXPORT bool FormatIsXChromaSubsampled(VkFormat format) {
    bool is_x_chroma_subsampled = false;

    switch (format) {
    case VK_FORMAT_G8B8G8R8_422_UNORM:
    case VK_FORMAT_B8G8R8G8_422_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
    case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
    case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
    case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G16B16G16R16_422_UNORM:
    case VK_FORMAT_B16G16R16G16_422_UNORM:
    case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
    case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
        is_x_chroma_subsampled = true;
        break;
    default:
        break;
    }

    return is_x_chroma_subsampled;
}

VK_LAYER_EXPORT bool FormatIsYChromaSubsampled(VkFormat format) {
    bool is_y_chroma_subsampled = false;

    switch (format) {
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        is_y_chroma_subsampled = true;
        break;

    default:
        break;
    }

    return is_y_chroma_subsampled;
}
