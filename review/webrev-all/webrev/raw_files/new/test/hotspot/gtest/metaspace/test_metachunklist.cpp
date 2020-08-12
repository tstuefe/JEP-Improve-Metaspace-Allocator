/*
 * Copyright (c) 2018, 2020, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2018, 2020 SAP SE. All rights reserved.
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

#include "precompiled.hpp"

//#define LOG_PLEASE
#include "metaspace/metaspaceTestsCommon.hpp"
#include "metaspace/metaspaceTestContexts.hpp"
#include "metaspace/metaspace_rangehelpers.hpp"

TEST_VM(metaspace, metachunklist) {

  ChunkTestsContext helper;

  MetachunkList lst;

  Metachunk* chunks[10];
  size_t total_size = 0;

  for (int i = 0; i < 10; i ++) {
    Metachunk* c = NULL;
    helper.alloc_chunk_expect_success(&c, ChunkLevelRanges::all_chunks().random_value());
    chunks[i] = c;
    total_size += c->committed_words();

    lst.add(c);
    EXPECT_EQ(lst.first(), c);

    Metachunk* c2 = lst.remove_first();
    EXPECT_EQ(c, c2);

    EXPECT_EQ(lst.count(), i);
    lst.add(c);
    EXPECT_EQ(lst.count(), i + 1);
    EXPECT_EQ(lst.calc_committed_word_size(), total_size);

  }

  for (int i = 0; i < 10; i ++) {
    DEBUG_ONLY(EXPECT_TRUE(lst.contains(chunks[i]));)
  }

  for (int i = 0; i < 10; i ++) {
    Metachunk* c = lst.remove_first();
    DEBUG_ONLY(EXPECT_FALSE(lst.contains(c));)
    helper.return_chunk(c);
  }

  EXPECT_EQ(lst.count(), 0);
  EXPECT_EQ(lst.calc_committed_word_size(), (size_t)0);

}


TEST_VM(metaspace, freechunklist) {

  ChunkTestsContext helper;

  FreeChunkListVector lst;

  MemRangeCounter cnt;
  MemRangeCounter committed_cnt;

  // Add random chunks to list and check the counter apis (word_size, commited_word_size, num_chunks)
  // Make every other chunk randomly uncommitted, and later we check that committed chunks are sorted in at the front
  // of the lists.
  for (int i = 0; i < 100; i ++) {
    Metachunk* c = NULL;
    helper.alloc_chunk_expect_success(&c, ChunkLevelRanges::all_chunks().random_value());
    bool uncommitted_chunk = i % 3;
    if (uncommitted_chunk) {
      helper.uncommit_chunk_with_test(c);
      c->set_in_use();
    }

    lst.add(c);

    LOG("->" METACHUNK_FULL_FORMAT, METACHUNK_FULL_FORMAT_ARGS(c));

    cnt.add(c->word_size());
    committed_cnt.add(c->committed_words());

    EXPECT_EQ(lst.num_chunks(), (int)cnt.count());
    EXPECT_EQ(lst.word_size(), cnt.total_size());
    EXPECT_EQ(lst.committed_word_size(), committed_cnt.total_size());
  }

  // Drain each list separately
  for (chunklevel_t lvl = LOWEST_CHUNK_LEVEL; lvl <= HIGHEST_CHUNK_LEVEL; lvl ++) {
    Metachunk* c = lst.remove_first(lvl);
    bool found_uncommitted = false;
    while (c != NULL) {

      LOG("<-" METACHUNK_FULL_FORMAT, METACHUNK_FULL_FORMAT_ARGS(c));

      // Within a level, no committed chunk should follow an uncommitted chunk:
      if (found_uncommitted) {
        EXPECT_TRUE(c->is_fully_uncommitted());
      } else {
        found_uncommitted = c->is_fully_uncommitted();
      }

      cnt.sub(c->word_size());
      committed_cnt.sub(c->committed_words());

      EXPECT_EQ(lst.num_chunks(), (int)cnt.count());
      EXPECT_EQ(lst.word_size(), cnt.total_size());
      EXPECT_EQ(lst.committed_word_size(), committed_cnt.total_size());

      helper.return_chunk(c);

      c = lst.remove_first(lvl);
    }
  }

  // TODO
}

