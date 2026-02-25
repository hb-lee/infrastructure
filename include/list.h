// SPDX-License-Identifier: GPL-2.0+
/*
 * Created by Hongbo Li <lihb2113@outlook.com>
 */
#ifndef __LIST_H__
#define __LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct list_head
{
    struct list_head *next;
    struct list_head *prev;
}list_head_t;

/*************************************************************************
*************************************************************************/

#define container_of(ptr, type, member)     \
    ((type *)(void *)((char *)(ptr) - __builtin_offsetof(type, member)))

#define list_first_entry(head, type, member)        \
    ((head)->next != (head) ? container_of((head)->next, type, member) : NULL)

#define list_last_entry(head, type, member)     \
    ((head)->prev != (head) ? container_of((head)->prev, type, member) : NULL)

#define list_foreach(pos, head)     \
    for ( pos = (head)->next;        \
            pos != (head);          \
            pos = pos->next )

#define list_foreach_safe(pos, n, head)     \
    for ( pos = (head)->next, n = pos->next;        \
            pos != (head);                  \
            pos = n, n = pos->next )

#define list_foreach_prev(pos, head)            \
    for ( pos = (head)->prev;               \
            pos != (head);                  \
            pos = pos->prev )

/*************************************************************************
*************************************************************************/

static inline void __list_init(list_head_t *head)
{
    head->prev = head;
    head->next = head;
}

static inline int __list_empty(const list_head_t *head)
{
    return (head->prev == head) ? 1 : 0;
}

static inline void __list_add(list_head_t *newnode,
                            list_head_t *prev,
                            list_head_t *next)
{
    next->prev  = newnode;
    newnode->next = next;
    newnode->prev = prev;
    prev->next  = newnode;
}

static inline void __list_del(list_head_t *prev, list_head_t *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void __list_splice(const list_head_t *list,
                                list_head_t *prev,
                                list_head_t *next)
{
    struct list_head *first = list->next;
    struct list_head *last = list->prev;

    first->prev = prev;
    prev->next = first;

    last->next = next;
    next->prev = last;
}

/*************************************************************************
*************************************************************************/

static inline void list_init(list_head_t *head)
{
    __list_init(head);
}

static inline int list_empty(const list_head_t *head)
{
    return __list_empty(head);
}

static inline void list_add(list_head_t *newnode, list_head_t *head)
{
    __list_add(newnode, head, head->next);
}

static inline void list_add_tail(list_head_t *newnode, list_head_t *head)
{
    __list_add(newnode, head->prev, head);
}

static inline void list_del(list_head_t *entry)
{
    __list_del(entry->prev, entry->next);
    __list_init(entry);
}

static inline void list_splice(list_head_t *list, list_head_t *head)
{
    if (!__list_empty(list))
    {
        __list_splice(list, head, head->next);
        __list_init(list);
    }
}

static inline void list_splice_tail(list_head_t *list, list_head_t *head)
{
    if (!__list_empty(list))
    {
        __list_splice(list, head->prev, head);
        __list_init(list);
    }
}

/*************************************************************************
*************************************************************************/

#ifdef __cplusplus
}
#endif

#endif