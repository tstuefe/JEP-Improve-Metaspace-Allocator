/*
 * Copyright (c) 2019, SAP SE. All rights reserved.
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
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
 */


#include "precompiled.hpp"

#include "metaspace/metaspace_testhelper.hpp"

using namespace metaspace::chunklevel;

  // No reserve limit, and a commit limit.
MetaspaceTestHelper::MetaspaceTestHelper(size_t commit_limit_word_size)
: _rs(), // ignored for this case, see VirtualSpaceList
  _commit_limiter(commit_limit_word_size),
  _vs_list("test_vs_list", &_commit_limiter),
  _cm("test_cm", &_vs_list),
  _num_chunks_allocated(0)
{
}

  // Reserve limit and commit limit.
MetaspaceTestHelper::MetaspaceTestHelper(size_t reserve_limit_word_size, size_t commit_limit_word_size)
: _rs(reserve_limit_word_size * BytesPerWord, Metaspace::reserve_alignment(), false),
  _commit_limiter(commit_limit_word_size),
  _vs_list("test_vs_list", _rs, &_commit_limiter),
  _cm("test_cm", &_vs_list),
  _num_chunks_allocated(0)
{
}

// Default ctor should cause no limits to fire (within reason)
MetaspaceTestHelper::MetaspaceTestHelper()
// (no c++11) : MetachunkTestHelper(1024 * G)
: _rs(), // ignored for this case, see VirtualSpaceList
  _commit_limiter(max_uintx),
  _vs_list("test_vs_list", &_commit_limiter),
  _cm("test_cm", &_vs_list),
  _num_chunks_allocated(0)
{
}

MetaspaceTestHelper::~MetaspaceTestHelper() {
  DEBUG_ONLY(verify();)
}

void MetaspaceTestHelper::checked_alloc_chunk_0(Metachunk** p_return_value, chunklevel_t preferred_level, chunklevel_t max_level,
                                                      size_t min_committed_size) {

  *p_return_value = NULL;

  Metachunk* c = cm().get_chunk(preferred_level, max_level, min_committed_size);

  if (c != NULL) {

    ASSERT_LE(c->level(), max_level);
    ASSERT_GE(c->level(), preferred_level);
    ASSERT_GE(c->committed_words(), min_committed_size);
    ASSERT_EQ(c->committed_words(), c->free_below_committed_words());
    ASSERT_EQ(c->used_words(), (size_t)0);
    ASSERT_TRUE(c->is_in_use());
    ASSERT_FALSE(c->is_free());
    ASSERT_FALSE(c->is_dead());
    ASSERT_NULL(c->next());
    ASSERT_NULL(c->prev());
    if (c->level() == HIGHEST_CHUNK_LEVEL) {
      ASSERT_TRUE(c->is_leaf_chunk());
    } else {
      ASSERT_FALSE(c->is_leaf_chunk());
    }
    if (c->level() == LOWEST_CHUNK_LEVEL) {
      ASSERT_TRUE(c->is_root_chunk());
    } else {
      ASSERT_FALSE(c->is_root_chunk());
    }
    if (_num_chunks_allocated == 0) { // First chunk? We can make more assumptions
      ASSERT_EQ(c->level(), preferred_level);
      // Needs lock EXPECT_NULL(c->next_in_vs());
      // Needs lock EXPECT_NULL(c->prev_in_vs());
      ASSERT_TRUE(c->is_root_chunk() || c->is_leader());
    }
    _num_chunks_allocated ++;

  }

  DEBUG_ONLY(verify();)

  *p_return_value = c;

}

// Test pattern established when allocating from the chunk with allocate_from_chunk_with_tests().
void MetaspaceTestHelper::test_pattern(Metachunk* c, size_t word_size) {
  check_range_for_pattern(c->base(), word_size, (uintx)c);
}

void MetaspaceTestHelper::return_chunk(Metachunk* c) {
  test_pattern(c);
  DEBUG_ONLY(verify();)
  c->set_in_use(); // Forestall assert in cm
  _cm.return_chunk(c);
}

 void MetaspaceTestHelper::allocate_from_chunk(MetaWord** p_return_value, Metachunk* c, size_t word_size) {

  size_t used_before = c->used_words();
  size_t free_before = c->free_words();
  size_t free_below_committed_before = c->free_below_committed_words();
  const MetaWord* top_before = c->top();

  MetaWord* p = c->allocate(word_size);
  EXPECT_NOT_NULL(p);
  EXPECT_EQ(c->used_words(), used_before + word_size);
  EXPECT_EQ(c->free_words(), free_before - word_size);
  EXPECT_EQ(c->free_below_committed_words(), free_below_committed_before - word_size);
  EXPECT_EQ(c->top(), top_before + word_size);

  // Old content should be preserved
  test_pattern(c, used_before);

  // Fill newly allocated range too
  fill_range_with_pattern(p, word_size, (uintx)c);

  *p_return_value = p;
}

void MetaspaceTestHelper::commit_chunk_with_test(Metachunk* c, size_t additional_size) {

  size_t used_before = c->used_words();
  size_t free_before = c->free_words();
  const MetaWord* top_before = c->top();

  c->set_in_use();
  bool b = c->ensure_committed_additional(additional_size);
  EXPECT_TRUE(b);

  // We should have enough committed size now
  EXPECT_GE(c->free_below_committed_words(), additional_size);

  // used, free, top should be unchanged.
  EXPECT_EQ(c->used_words(), used_before);
  EXPECT_EQ(c->free_words(), free_before);
  EXPECT_EQ(c->top(), top_before);

  test_pattern(c, used_before);

}

void MetaspaceTestHelper::commit_chunk_expect_failure(Metachunk* c, size_t additional_size) {

  size_t used_before = c->used_words();
  size_t free_before = c->free_words();
  size_t free_below_committed_before = c->free_below_committed_words();
  const MetaWord* top_before = c->top();

  c->set_in_use();
  bool b = c->ensure_committed_additional(additional_size);
  EXPECT_FALSE(b);

  // Nothing should have changed
  EXPECT_EQ(c->used_words(), used_before);
  EXPECT_EQ(c->free_words(), free_before);
  EXPECT_EQ(c->free_below_committed_words(), free_below_committed_before);
  EXPECT_EQ(c->top(), top_before);

  test_pattern(c, used_before);

}

void MetaspaceTestHelper::uncommit_chunk_with_test(Metachunk* c) {
  if (c->word_size() >= Settings::commit_granule_words()) {
    c->set_free();  // Forestall assert in uncommit
    c->reset_used_words();
    c->uncommit();

    EXPECT_EQ(c->free_below_committed_words(), (size_t)0);
    EXPECT_EQ(c->used_words(), (size_t)0);
    EXPECT_EQ(c->free_words(), c->word_size());
    EXPECT_EQ(c->top(), c->base());
    EXPECT_TRUE(c->is_fully_uncommitted());
  }
}

#ifdef ASSERT
void MetaspaceTestHelper::verify() const {
  _cm.verify(true);
  _vs_list.verify(true);
}
#endif



/////// SparseArray<T> ////////////////


