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
 *
 */



#include "precompiled.hpp"
#include "memory/metaspace/internStat.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/ostream.hpp"

#ifdef ASSERT

namespace metaspace {

#define CREATE_COUNTER(name)          uintx InternalStats::_##name;
#define CREATE_ATOMIC_COUNTER(name)   volatile uintx InternalStats::_##name;

  ALL_MY_COUNTERS(CREATE_COUNTER, CREATE_ATOMIC_COUNTER)

#undef MATERIALIZE_COUNTER
#undef MATERIALIZE_ATOMIC_COUNTER

void InternalStats::print_on(outputStream* st) {

#define xstr(s) str(s)
#define str(s) #s

#define PRINTER(name)  st->print_cr("%s: " UINTX_FORMAT ".", xstr(name), _##name);

  ALL_MY_COUNTERS(PRINTER, PRINTER)

#undef PRINTER

}

} // namespace metaspace

#endif // ASSERT