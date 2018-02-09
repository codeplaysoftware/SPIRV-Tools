// Copyright (c) 2018 Google LLC.
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

#ifndef FILER_ITERATOR_H
#define FILER_ITERATOR_H

namespace spvtools {
namespace ir {
// Wrapping iterator that skips element based on a predicate.
template <typename Iterator, typename Predicate>
class FilterIterator {
 public:
  using iterator_category = typename Iterator::iterator_category;
  using difference_type = typename Iterator::difference_type;
  using value_type = typename Iterator::value_type;
  using pointer = typename Iterator::pointer;
  using reference = typename Iterator::reference;

  // using Predicate = std::function<bool(const Iterator&)>;

  // FIXME: check Predicate compiles to void(const Iterator&)

  FilterIterator(/*Predicate predicate, */ Iterator it, Iterator end)
      : internal_iterator_(it), end_(end) {
    if (PredicateIsNotValid()) operator++();
  }

  explicit FilterIterator(Iterator end) : FilterIterator(end, end) {}

  FilterIterator& operator++() {
    if (!IsEnd()) {
      do {
        ++internal_iterator_;
      } while (PredicateIsNotValid());
    }
    return *this;
  }

  FilterIterator operator++(int) {
    FilterIterator tmp(internal_iterator_);
    operator++();
    return tmp;
  }

  template <typename ItCategory = typename Iterator::iterator_category,
            typename std::enable_if<std::is_base_of<
                ItCategory, std::bidirectional_iterator_tag>::value>::type = 0>
  FilterIterator& operator--() {
    if (!IsEnd()) {
      do {
        --internal_iterator_;
      } while (PredicateIsNotValid());
    }
    return *this;
  }

  template <typename ItCategory = typename Iterator::iterator_category,
            typename std::enable_if<std::is_base_of<
                ItCategory, std::bidirectional_iterator_tag>::value>::type = 0>
  FilterIterator operator--(int) {
    FilterIterator tmp(internal_iterator_);
    operator--();
    return tmp;
  }

  FilterIterator& operator=(const FilterIterator& i) {
    internal_iterator_ = i.internal_iterator_;
    end_ = i.end_;
    return *this;
  }

  reference operator*() const { return *internal_iterator_; }
  pointer operator->() const { return &*internal_iterator_; }

  inline bool operator==(const FilterIterator& rhs) const {
    return internal_iterator_ == rhs.internal_iterator_ && end_ == rhs.end_;
  }
  inline bool operator!=(const FilterIterator& rhs) const {
    return !(*this == rhs);
  }

  // Returns true if we reached the end of the iterator.
  bool IsEnd() const { return internal_iterator_ == end_; }

 protected:
  // Returns true if the iterator did not reach the end and the predicate is
  // not satisfied.
  bool PredicateIsNotValid() {
    return !IsEnd() && !Predicate()(internal_iterator_);
  }

  Iterator internal_iterator_;
  Iterator end_;
};
}  // namespace ir
}  // namespace spvtools

#endif
