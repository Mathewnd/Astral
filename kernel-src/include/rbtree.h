#ifndef _RBTREE_H
#define _RBTREE_H

typedef struct rbtree_t {
	struct rbtree_t *left, *right, *parent;
} rbtree_t;

// return -1 when a < b
// return  0 when a = b
// return  1 when a > b
typedef int (*rbtree_compare_fn_t)(rbtree_t *a, rbtree_t *b);
typedef int (*rbtree_value_compare_fn_t)(void *a, rbtree_t *b);
typedef void (*rbtree_print_fn_t)(rbtree_t *node);

rbtree_t *rbtree_lookup(rbtree_t *rbtree, void *key, rbtree_value_compare_fn_t compare_fn);
void rbtree_insert(rbtree_t **rbtreep, rbtree_t *node, rbtree_compare_fn_t compare_fn);
void rbtree_remove(rbtree_t **rbtreep, rbtree_t *node);
void rbtree_check(rbtree_t *rbtree);
void rbtree_print(rbtree_t *rbtree, rbtree_print_fn_t print_fn);
rbtree_t *rbtree_first(rbtree_t *rbtree);
rbtree_t *rbtree_last(rbtree_t *rbtree);
rbtree_t *rbtree_successor(rbtree_t *node);
rbtree_t *rbtree_predecessor(rbtree_t *node);
rbtree_t *rbtree_find_first_larger_equal(rbtree_t *rbtree, void *key, rbtree_value_compare_fn_t compare_fn);

#endif
