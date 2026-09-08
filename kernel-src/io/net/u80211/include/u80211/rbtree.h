#ifndef U80211_RBTREE_H
#define U80211_RBTREE_H

typedef struct u80211_rbtree_t {
	struct u80211_rbtree_t *left, *right, *parent;
} u80211_rbtree_t;

// return -1 when a < b
// return  0 when a = b
// return  1 when a > b
typedef int (*u80211_rbtree_compare_fn_t)(u80211_rbtree_t *a, u80211_rbtree_t *b);
typedef int (*u80211_rbtree_value_compare_fn_t)(void *a, u80211_rbtree_t *b);

u80211_rbtree_t *u80211_rbtree_lookup(u80211_rbtree_t *rbtree, void *key, u80211_rbtree_value_compare_fn_t compare_fn);
void u80211_rbtree_insert(u80211_rbtree_t **rbtreep, u80211_rbtree_t *node, u80211_rbtree_compare_fn_t compare_fn);
void u80211_rbtree_remove(u80211_rbtree_t **rbtreep, u80211_rbtree_t *node);
void u80211_rbtree_check(u80211_rbtree_t *rbtree);
u80211_rbtree_t *u80211_rbtree_first(u80211_rbtree_t *rbtree);
u80211_rbtree_t *u80211_rbtree_last(u80211_rbtree_t *rbtree);
u80211_rbtree_t *u80211_rbtree_successor(u80211_rbtree_t *node);
u80211_rbtree_t *u80211_rbtree_predecessor(u80211_rbtree_t *node);
u80211_rbtree_t *u80211_rbtree_find_first_larger_equal(u80211_rbtree_t *rbtree, void *key, u80211_rbtree_value_compare_fn_t compare_fn);

#endif
