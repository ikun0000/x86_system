#include "list.h"
#include "interrupt.h"

/* 初始化双向链表 */
void list_init(struct list* list)
{
    list->head.prev = NULL;
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
    list->tail.next = NULL;   
}

/* 把elem插入到元素before之前 */
void list_insert_before(struct list_elem *before, struct list_elem *elem)
{
    enum intr_status old_status = intr_disable();

    before->prev->next = elem;
    
    elem->prev = before->prev;
    elem->next = before;   
    
    before->prev = elem;

    intr_set_status(old_status);
}

/* 添加元素到列表队前 */
void list_push(struct list *plist, struct list_elem *elem) 
{
    list_insert_before(plist->head.next, elem);
}

/* 
void list_iterate(struct list *plist);
*/

/* 把元素追加到链表尾部 */
void list_append(struct list *plist, struct list_elem *elem) 
{
    list_insert_before(&plist->tail, elem);
}

/* 把元素从链表中移除 */
void list_remove(struct list_elem *pelem)
{
    enum intr_status old_status = intr_disable();

    pelem->prev->next = pelem->next;
    pelem->next->prev = pelem->prev;

    intr_set_status(old_status);
}

/* 将链表第一个元素弹出并返回 */
struct list_elem *list_pop(struct list *plist)
{
    struct list_elem *elem = plist->head.next;
    list_remove(elem);
    return elem;
}

/* 判断链表是否为空 */
int list_empty(struct list *plist)
{
    return (plist->head.next == &plist->tail ? 1 : 0);
}

/* 返回链表长度 */
uint32_t list_len(struct list *plist)
{
    struct list_elem *elem = plist->head.next;
    uint32_t length = 0;

    while (elem != &plist->tail)
    {
        length++;
        elem = elem->next;
    }

    return length;
}

/* 把链表中每个元素和arg传递给func, 
   arg给func用来判断链表每个元素是否符合条件
   找到符合条件的元素返回该元素的地址，否则返回NULL */
struct list_elem *list_traversal(struct list *plist, function *func, int arg)
{
    struct list_elem *elem = plist->head.next;

    if (list_empty(plist)) return NULL;
    
    while (elem != &plist->tail)
    {
        if (func(elem, arg)) return elem;
        elem = elem->next;
    }
    
    return NULL;
}

/* 在链表中查找指定元素，成功返回1，否则返回0 */
int elem_find(struct list *plist, struct list_elem *obj_elem)
{
    struct list_elem *elem = plist->head.next;   
    while (elem != &plist->tail)
    {
        if (elem == obj_elem) return 1;
        elem = elem->next;
    }
    return 0;
}
