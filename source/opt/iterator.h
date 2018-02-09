// Copyright (c) 2016 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LIBSPIRV_OPT_ITERATOR_H_
#define LIBSPIRV_OPT_ITERATOR_H_

#include <cstddef>  // for ptrdiff_t
#include <iterator>
#include <memory>
#include <type_traits>
#include <vector>

namespace spvtools {
namespace ir {

// An ad hoc iterator class for std::vector<std::unique_ptr<|ValueType|>>. The
// purpose of this iterator class is to provide transparent access to those
// std::unique_ptr managed elements in the vector, behaving like we are using
// std::vector<|ValueType|>.
template <typename ValueType,
          template <typename...> class ContainerType = std::vector>
class UptrContainerIterator
    : public std::iterator<std::random_access_iterator_tag, ValueType> {
 public:
  using super = std::iterator<std::random_access_iterator_tag, ValueType>;

  using pointer = typename super::pointer;
  using reference = typename super::reference;
  using difference_type = typename super::difference_type;

  // Type aliases. We need to apply constness properly if |IsConst| is true.
  using Uptr = std::unique_ptr<typename std::remove_const<ValueType>::type>;
  using UptrContainer =
      typename std::conditional<std::is_const<ValueType>::value,
                                const ContainerType<Uptr>,
                                ContainerType<Uptr>>::type;
  using UnderlyingIterator =
      typename std::conditional<std::is_const<ValueType>::value,
                                typename UptrContainer::const_iterator,
                                typename UptrContainer::iterator>::type;

  // Creates a new iterator from the given |container| and its raw iterator
  // |it|.
  UptrContainerIterator(UptrContainer* container, const UnderlyingIterator& it)
      : container_(container), iterator_(it) {}
  UptrContainerIterator(const UptrContainerIterator&) = default;
  UptrContainerIterator& operator=(const UptrContainerIterator&) = default;

  inline UptrContainerIterator& operator++();
  inline UptrContainerIterator operator++(int);
  inline UptrContainerIterator& operator--();
  inline UptrContainerIterator operator--(int);

  // Returns the underlying iterator.
  UnderlyingIterator get() { return iterator_; }
  // Return a valid end iterator for the underlying container.
  UptrContainerIterator end() {
    return UptrContainerIterator(container_, container_->end());
  }

  reference operator*() const { return **iterator_; }
  pointer operator->() { return (*iterator_).get(); }

  inline bool operator==(const UptrContainerIterator& that) const;
  inline bool operator!=(const UptrContainerIterator& that) const;

  // Inserts the given |value| to the position pointed to by this iterator
  // and returns an iterator to the newly iserted |value|.
  // If the underlying vector changes capacity, all previous iterators will be
  // invalidated. Otherwise, those previous iterators pointing to after the
  // insertion point will be invalidated.
  template <bool IsConstForMethod = std::is_const<ValueType>::value>
  inline typename std::enable_if<!IsConstForMethod, UptrContainerIterator>::type
  InsertBefore(Uptr value);

  // Erases the value at the position pointed to by this iterator
  // and returns an iterator to the following value.
  // If the underlying vector changes capacity, all previous iterators will be
  // invalidated. Otherwise, those previous iterators pointing to after the
  // erasure point will be invalidated.
  template <bool IsConstForMethod = std::is_const<ValueType>::value>
  inline typename std::enable_if<!IsConstForMethod, UptrContainerIterator>::type
  Erase();

 protected:
  UptrContainer* container_;     // The container we are manipulating.
  UnderlyingIterator iterator_;  // The raw iterator from the container.
};

// An ad hoc iterator class for std::vector<std::unique_ptr<|ValueType|>>. The
// purpose of this iterator class is to provide transparent access to those
// std::unique_ptr managed elements in the vector, behaving like we are using
// std::vector<|ValueType|>.
template <typename ValueType, bool IsConst = false>
class UptrVectorIterator
    : public UptrContainerIterator<
          typename std::conditional<IsConst, const ValueType, ValueType>::type,
          std::vector> {
 public:
  using super = UptrContainerIterator<
      typename std::conditional<IsConst, const ValueType, ValueType>::type,
      std::vector>;

  using pointer = typename super::pointer;
  using reference = typename super::reference;
  using difference_type = typename super::difference_type;

  // Type aliases. We need to apply constness properly if |IsConst| is true.
  using Uptr = typename super::Uptr;
  using UptrVector = typename super::UptrContainer;
  using UnderlyingIterator = typename super::UnderlyingIterator;

  // Creates a new iterator from the given |container| and its raw iterator
  // |it|.
  UptrVectorIterator(UptrVector* container, const UnderlyingIterator& it)
      : super(container, it) {}
  UptrVectorIterator(const UptrVectorIterator&) = default;
  UptrVectorIterator& operator=(const UptrVectorIterator&) = default;
  UptrVectorIterator(const super& it) : super(it) {}
  UptrVectorIterator& operator=(const super& it) {
    *static_cast<super*>(this) = it;
    return *this;
  }

  // Inserts the given |valueVector| to the position pointed to by this iterator
  // and returns an iterator to the first newly inserted value.
  // If the underlying vector changes capacity, all previous iterators will be
  // invalidated. Otherwise, those previous iterators pointing to after the
  // insertion point will be invalidated.
  template <bool IsConstForMethod = std::is_const<ValueType>::value>
  inline typename std::enable_if<!IsConstForMethod, UptrVectorIterator>::type
  InsertBefore(UptrVector* valueVector);
  using super::InsertBefore;

  reference operator[](ptrdiff_t index) { return **(super::iterator_ + index); }
  inline ptrdiff_t operator-(const UptrVectorIterator& that) const;
  inline bool operator<(const UptrVectorIterator& that) const;
};

// Handy class for a (begin, end) iterator pair.
template <typename IteratorType>
class IteratorRange {
 public:
  IteratorRange(const IteratorType& b, const IteratorType& e)
      : begin_(b), end_(e) {}
  IteratorRange(IteratorType&& b, IteratorType&& e)
      : begin_(std::move(b)), end_(std::move(e)) {}

  IteratorType begin() const { return begin_; }
  IteratorType end() const { return end_; }

  bool empty() const { return begin_ == end_; }
  size_t size() const { return end_ - begin_; }

 private:
  IteratorType begin_;
  IteratorType end_;
};

// Returns a (begin, end) iterator pair for the given iterators.
// The iterators must belong to the same container.
template <typename IteratorType>
inline IteratorRange<IteratorType> make_range(const IteratorType& begin,
                                              const IteratorType& end) {
  return {begin, end};
}

// Returns a (begin, end) iterator pair for the given iterators.
// The iterators must belong to the same container.
template <typename IteratorType>
inline IteratorRange<IteratorType> make_range(IteratorType&& begin,
                                              IteratorType&& end) {
  return {std::move(begin), std::move(end)};
}

// Returns a (begin, end) iterator pair for the given container.
template <typename ValueType,
          class IteratorType = UptrVectorIterator<ValueType>>
inline IteratorRange<IteratorType> make_range(
    std::vector<std::unique_ptr<ValueType>>& container) {
  return {IteratorType(&container, container.begin()),
          IteratorType(&container, container.end())};
}

// Returns a const (begin, end) iterator pair for the given container.
template <typename ValueType,
          class IteratorType = UptrVectorIterator<ValueType, true>>
inline IteratorRange<IteratorType> make_const_range(
    const std::vector<std::unique_ptr<ValueType>>& container) {
  return {IteratorType(&container, container.cbegin()),
          IteratorType(&container, container.cend())};
}

// Returns a (begin, end) iterator pair for the given container.
template <typename VT, template <typename...> class CT,
          class IteratorType = UptrContainerIterator<VT, CT>>
inline IteratorRange<IteratorType> make_range(
    CT<std::unique_ptr<VT>>& container) {
  return {IteratorType(&container, container.begin()),
          IteratorType(&container, container.end())};
}

// Returns a const (begin, end) iterator pair for the given container.
template <typename VT, template <typename...> class CT,
          class IteratorType = UptrContainerIterator<const VT, CT>>
inline IteratorRange<IteratorType> make_const_range(
    const CT<std::unique_ptr<VT>>& container) {
  return {IteratorType(&container, container.cbegin()),
          IteratorType(&container, container.cend())};
}

template <typename VT, template <typename...> class CT>
inline UptrContainerIterator<VT, CT>& UptrContainerIterator<VT, CT>::
operator++() {
  ++iterator_;
  return *this;
}

template <typename VT, template <typename...> class CT>
inline UptrContainerIterator<VT, CT> UptrContainerIterator<VT, CT>::operator++(
    int) {
  auto it = *this;
  ++(*this);
  return it;
}

template <typename VT, template <typename...> class CT>
inline UptrContainerIterator<VT, CT>& UptrContainerIterator<VT, CT>::
operator--() {
  --iterator_;
  return *this;
}

template <typename VT, template <typename...> class CT>
inline UptrContainerIterator<VT, CT> UptrContainerIterator<VT, CT>::operator--(
    int) {
  auto it = *this;
  --(*this);
  return it;
}

template <typename VT, template <typename...> class CT>
inline bool UptrContainerIterator<VT, CT>::operator==(
    const UptrContainerIterator& that) const {
  return container_ == that.container_ && iterator_ == that.iterator_;
}

template <typename VT, template <typename...> class CT>
inline bool UptrContainerIterator<VT, CT>::operator!=(
    const UptrContainerIterator& that) const {
  return !(*this == that);
}

template <typename VT, bool IC>
inline ptrdiff_t UptrVectorIterator<VT, IC>::operator-(
    const UptrVectorIterator& that) const {
  assert(super::container_ == that.container_);
  return super::iterator_ - that.iterator_;
}

template <typename VT, bool IC>
inline bool UptrVectorIterator<VT, IC>::operator<(
    const UptrVectorIterator& that) const {
  assert(super::container_ == that.container_);
  return super::iterator_ < that.iterator_;
}

template <typename VT, template <typename...> class CT>
template <bool IsConstForMethod>
inline typename std::enable_if<!IsConstForMethod,
                               UptrContainerIterator<VT, CT>>::type
UptrContainerIterator<VT, CT>::InsertBefore(Uptr value) {
  return UptrContainerIterator(container_,
                               container_->insert(iterator_, std::move(value)));
}

template <typename VT, bool IC>
template <bool IsConstForMethod>
inline
    typename std::enable_if<!IsConstForMethod, UptrVectorIterator<VT, IC>>::type
    UptrVectorIterator<VT, IC>::InsertBefore(UptrVector* values) {
  const auto pos = super::iterator_ - super::container_->begin();
  const auto origsz = super::container_->size();
  super::container_->resize(origsz + values->size());
  std::move_backward(super::container_->begin() + pos,
                     super::container_->begin() + origsz,
                     super::container_->end());
  std::move(values->begin(), values->end(), super::container_->begin() + pos);
  return UptrVectorIterator(super::container_,
                            super::container_->begin() + pos);
}

template <typename VT, template <typename...> class CT>
template <bool IsConstForMethod>
inline typename std::enable_if<!IsConstForMethod,
                               UptrContainerIterator<VT, CT>>::type
UptrContainerIterator<VT, CT>::Erase() {
  return UptrContainerIterator{container_, container_->erase(iterator_)};
}

}  // namespace ir
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_ITERATOR_H_
