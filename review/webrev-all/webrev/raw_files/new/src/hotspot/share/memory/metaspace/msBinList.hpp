/*
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2020 SAP SE. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_MEMORY_METASPACE_MSBINLIST_HPP
#define SHARE_MEMORY_METASPACE_MSBINLIST_HPP

#include "memory/metaspace/msCommon.hpp"
#include "memory/metaspace/msCounter.hpp"
#include "utilities/align.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"

namespace metaspace {

// BinList is a data structure to manage small to very small memory blocks
// (only a few words). It is used to manage deallocated blocks - see
// class FreeBlocks.

// Memory blocks are kept in linked lists. Each list
// contains blocks of only one size. There is a list for blocks of two words,
// for blocks of three words, etc. The list heads are kept in a vector,
// ordered by block size.
//

// wordsize
//
//       +---+   +---+   +---+      +---+
//  1    |   |-->|   |-->|   |-...->|   |
//       +---+   +---+   +---+      +---+
//
//       +----+   +----+   +----+      +----+
//  2    |    |-->|    |-->|    |-...->|    |
//       +----+   +----+   +----+      +----+
//
//       +-----+   +-----+   +-----+      +-----+
//  3    |     |-->|     |-->|     |-...->|     |
//       +-----+   +-----+   +-----+      +-----+
//  .
//  .
//  .
//
//       +----------+   +----------+   +----------+      +----------+
//  n    |          |-->|          |-->|          |-...->|          |
//       +----------+   +----------+   +----------+      +----------+

// Insertion is of course fast, O(1).
//
// On retrieval, we attempt to find the closest fit to a given size, walking the
// list head vector (a bitmask is used to speed that part up).
//
// This structure is a bit expensive in memory costs (we pay one pointer per managed
// block size) so we only use it for a small number of sizes.

template <size_t smallest_word_size, int num_lists>
class BinListImpl {

  struct Block {
    Block* const _next;
    const size_t _word_size;
    Block(Block* next, size_t word_size)
      : _next(next), _word_size(word_size)
    {}
  };

  // a mask to speed up searching for populated lists.
  // 0 marks an empty list, 1 for a non-empty one.
  typedef uint32_t mask_t;
  STATIC_ASSERT(num_lists <= sizeof(mask_t) * 8);

  mask_t _mask;

  // Smallest block size must be large enough to hold a Block structure.
  STATIC_ASSERT(smallest_word_size * sizeof(MetaWord) >= sizeof(Block));

  STATIC_ASSERT(num_lists > 0);

public:

  // Minimal word size a block must have to be manageable by this structure.
  const static size_t MinWordSize = smallest_word_size;

  // Maximal (incl) word size a block can have to be manageable by this structure.
  const static size_t MaxWordSize = MinWordSize + num_lists - 1;

private:

  Block* _blocks[num_lists];

  MemRangeCounter _counter;

  static int index_for_word_size(size_t word_size) {
    int index = (int)(word_size - MinWordSize);
    assert(index >= 0 && index < num_lists, "Invalid index %d", index);
    return index;
  }

  static size_t word_size_for_index(int index) {
    assert(index >= 0 && index < num_lists, "Invalid index %d", index);
    return MinWordSize + index;
  }

  // Search the range [index, _num_lists) for the smallest non-empty list. Returns -1 on fail.
  int index_for_next_non_empty_list(int index) {
    assert(index >= 0 && index < num_lists, "Invalid index %d", index);
    int i2 = index;
    mask_t m = _mask >> i2;
    if (m > 0) {
      // count leading zeros would be helpful.
      while ((m & 1) == 0) {
        assert(_blocks[i2] == NULL, "mask mismatch");
        i2++;
        m >>= 1;
      }
      // We must have found something.
      assert(i2 < num_lists, "sanity.");
      assert(_blocks[i2] != NULL, "mask mismatch");
      return i2;
    }
    return -1;
  }

  void mask_set_bit(int bit) { _mask |= (((mask_t)1) << bit); }
  void mask_clr_bit(int bit) { _mask &= ~(((mask_t)1) << bit); }

public:

  BinListImpl() : _mask(0) {
    for (int i = 0; i < num_lists; i++) {
      _blocks[i] = NULL;
    }
  }

  void add_block(MetaWord* p, size_t word_size) {
    assert(word_size >= MinWordSize &&
           word_size <= MaxWordSize, "bad block size");
    const int index = index_for_word_size(word_size);
    Block* old_head = _blocks[index];
    Block* new_head = new(p)Block(old_head, word_size);
    _blocks[index] = new_head;
    _counter.add(word_size);
    mask_set_bit(index);
  }

  // Given a word_size, searches and returns a block of at least that size.
  // Block may be larger. Real block size is returned in *p_real_word_size.
  MetaWord* remove_block(size_t word_size, size_t* p_real_word_size) {
    assert(word_size >= MinWordSize &&
           word_size <= MaxWordSize, "bad block size " SIZE_FORMAT ".", word_size);
    int index = index_for_word_size(word_size);
    index = index_for_next_non_empty_list(index);
    if (index != -1) {
      assert(_blocks[index] != NULL &&
             _blocks[index]->_word_size >= word_size, "sanity");

      MetaWord* const p = (MetaWord*)_blocks[index];
      const size_t real_word_size = word_size_for_index(index);

      _blocks[index] = _blocks[index]->_next;
      if (_blocks[index] == NULL) {
        mask_clr_bit(index);
      }

      _counter.sub(real_word_size);
      *p_real_word_size = real_word_size;

      return p;

    } else {
      *p_real_word_size = 0;
      return NULL;
    }
  }

  // Returns number of blocks in this structure
  unsigned count() const { return _counter.count(); }

  // Returns total size, in words, of all elements.
  size_t total_size() const { return _counter.total_size(); }

  bool is_empty() const { return _mask == 0; }

#ifdef ASSERT
  void verify() const {
    MemRangeCounter local_counter;
    for (int i = 0; i < num_lists; i++) {
      assert(((_mask >> i) & 1) == ((_blocks[i] == 0) ? 0 : 1), "sanity");
      const size_t s = MinWordSize + i;
      for (Block* b = _blocks[i]; b != NULL; b = b->_next) {
        assert(b->_word_size == s, "bad block size");
        local_counter.add(s);
      }
    }
    local_counter.check(_counter);
  }
#endif // ASSERT

};

typedef BinListImpl<2, 32> BinList32;

} // namespace metaspace

#endif // SHARE_MEMORY_METASPACE_MSBINLIST_HPP
