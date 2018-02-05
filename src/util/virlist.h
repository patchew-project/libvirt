/*
 * virlist.h: methods for managing list, logic is copied from Linux Kernel
 *
 * Copyright (C) 2018 SUSE LINUX Products, Beijing, China.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __VIR_LIST_H
#define __VIR_LIST_H

#include <stdbool.h>

typedef struct _virListHead virListHead;
typedef virListHead *virListHeadPtr;

struct _virListHead {
    struct _virListHead *next, *prev;
};

static inline void
virListHeadInit(virListHeadPtr name)
{
    name->next = name;
    name->prev = name;
}

static inline void
__virListAdd(virListHeadPtr entry,
             virListHeadPtr prev, virListHeadPtr next)
{
    next->prev = entry;
    entry->next = next;
    entry->prev = prev;
    prev->next = entry;
}

static inline void
virListAdd(virListHeadPtr entry, virListHeadPtr head)
{
    __virListAdd(entry, head, head->next);
}

static inline void
virListAddTail(virListHeadPtr entry, virListHeadPtr head)
{
    __virListAdd(entry, head->prev, head);
}

static inline void
__virListDelete(virListHeadPtr prev, virListHeadPtr next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void
virListDelete(virListHeadPtr entry)
{
    __virListDelete(entry->prev, entry->next);
}

static inline bool
virListEmpty(virListHeadPtr head)
{
    return head->next == head;
}

#ifndef virContainerOf
#define virContainerOf(ptr, type, member) \
    (type *)((char *)(ptr) - (char *) &((type *)0)->member)
#endif

#define virListEntry(ptr, type, member) \
    virContainerOf(ptr, type, member)

#define virListFirstEntry(ptr, type, member) \
    virListEntry((ptr)->next, type, member)

#define virListLastEntry(ptr, type, member) \
    virListEntry((ptr)->prev, type, member)

#define __virContainerOf(ptr, sample, member)             \
    (void *)virContainerOf((ptr), typeof(*(sample)), member)

#define virListForEachEntry(pos, head, member)              \
    for (pos = __virContainerOf((head)->next, pos, member);       \
     &pos->member != (head);                    \
     pos = __virContainerOf(pos->member.next, pos, member))

#define virListForEachEntrySafe(pos, tmp, head, member)         \
    for (pos = __virContainerOf((head)->next, pos, member),       \
     tmp = __virContainerOf(pos->member.next, pos, member);       \
     &pos->member != (head);                    \
     pos = tmp, tmp = __virContainerOf(pos->member.next, tmp, member))

#endif
