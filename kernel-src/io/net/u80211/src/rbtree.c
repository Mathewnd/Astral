#include <u80211/rbtree.h>
#include <stddef.h>
#include <stdint.h>

#define RBTREE_COLOUR_BLACK 0
#define RBTREE_COLOUR_RED 1

// helper macros as the least significant bit of the parent pointer is used for the colour
#define RBTREE_NODE_GET_PARENT(x) ((u80211_rbtree_t *)((uintptr_t)(x)->parent & ~1lu))
#define RBTREE_NODE_GET_COLOUR(x) ((x) == NULL ? RBTREE_COLOUR_BLACK : ((uintptr_t)(x)->parent & 1))
#define RBTREE_NODE_SET_COLOUR(x, c) ((x)->parent = (u80211_rbtree_t *)((uintptr_t)RBTREE_NODE_GET_PARENT(x) | (c)))
#define RBTREE_NODE_SET_PARENT(x, p) ((x)->parent = (u80211_rbtree_t *)((uintptr_t)(p) | RBTREE_NODE_GET_COLOUR(x)))
#define RBTREE_CONSTRUCT_PARENT(x, c) ((u80211_rbtree_t *)((uintptr_t)(x) | (c)))

static void rotate_right(u80211_rbtree_t **rootp, u80211_rbtree_t *node) {
	/*      G             G
	//      |             |
	//      P      ->     N
	//     / \           / \
	//    N   3         1   P
	//   / \               / \
	//  1  2              2   3 
	*/

	u80211_rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
	u80211_rbtree_t *grand_parent = RBTREE_NODE_GET_PARENT(parent);

	// N
	if (grand_parent == NULL)
		*rootp = node;
	else if (grand_parent->left == parent)
		grand_parent->left = node;
	else
		grand_parent->right = node;

	RBTREE_NODE_SET_PARENT(node, grand_parent);

	// P
	u80211_rbtree_t *node_right_subtree = node->right;

	node->right = parent;
	RBTREE_NODE_SET_PARENT(parent, node);

	// 2
	parent->left = node_right_subtree;
	if (node_right_subtree)
		RBTREE_NODE_SET_PARENT(node_right_subtree, parent);
}

static void rotate_left(u80211_rbtree_t **rootp, u80211_rbtree_t *node) {
	/*        G              G
	//        |              |
	//        P              N
	//       / \    ->      / \
	//      1   N          P   3
	//         / \        / \
	//        2   3      1   2
	*/

	u80211_rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
	u80211_rbtree_t *grand_parent = RBTREE_NODE_GET_PARENT(parent);

	// N
	if (grand_parent == NULL)
		*rootp = node;
	else if (grand_parent->left == parent)
		grand_parent->left = node;
	else
		grand_parent->right = node;

	RBTREE_NODE_SET_PARENT(node, grand_parent);

	// P
	u80211_rbtree_t *node_left_subtree = node->left;

	node->left = parent;
	RBTREE_NODE_SET_PARENT(parent, node);

	// 2
	parent->right = node_left_subtree;
	if (node_left_subtree)
		RBTREE_NODE_SET_PARENT(node_left_subtree, parent);
}

u80211_rbtree_t *u80211_rbtree_find_first_larger_equal(u80211_rbtree_t *rbtree, void *key, u80211_rbtree_value_compare_fn_t compare_fn) {
	u80211_rbtree_t *found = NULL;
	for (;;) {
		if (rbtree == NULL)
			return found;

		int comparison = compare_fn(key, rbtree);

		if (comparison == 0)
			return rbtree;

		if (comparison == -1)
			found = rbtree;

		rbtree = comparison == 1 ? rbtree->right : rbtree->left;
	}
}

u80211_rbtree_t *u80211_rbtree_lookup(u80211_rbtree_t *rbtree, void *key, u80211_rbtree_value_compare_fn_t compare_fn) {
	for (;;) {
		if (rbtree == NULL)
			return NULL;

		int comparison = compare_fn(key, rbtree);

		if (comparison == 0)
			return rbtree;

		rbtree = comparison > 0 ? rbtree->right : rbtree->left;
	}
}

void u80211_rbtree_insert(u80211_rbtree_t **rbtreep, u80211_rbtree_t *node, u80211_rbtree_compare_fn_t compare_fn) {
	u80211_rbtree_t *rbtree = *rbtreep;
	if (rbtree == NULL) {
		*rbtreep = node;
		node->right = node->left = node->parent = NULL; // NULL implicitly sets to black
		return;
	}

	// find last node
	int comparison;
	u80211_rbtree_t **next;
	for (;;) {
		comparison = compare_fn(node, rbtree);
		next = comparison > 0 ? &rbtree->right : &rbtree->left;
		if (*next == NULL)
			break;

		rbtree = *next;
	}

	// set tree pointers
	*next = node;
	node->right = NULL;
	node->left = NULL;
	node->parent = RBTREE_CONSTRUCT_PARENT(rbtree, RBTREE_COLOUR_RED); // all inserted nodes are red

	// parent is red, iterate up the tree to fix any property 3 violations we find
	// node now means the node whose parent we are checking for a property 3 violation
	u80211_rbtree_t *parent, *grand_parent, *uncle;
	for (;;) {
		parent = RBTREE_NODE_GET_PARENT(node);
		// check for property 3 violation (red parent and red child)
		if (RBTREE_NODE_GET_COLOUR(parent) == RBTREE_COLOUR_BLACK)
			return; // no violation, stop here

		grand_parent = RBTREE_NODE_GET_PARENT(parent);
		uncle = grand_parent->left == parent ? grand_parent->right : grand_parent->left;

		// if the uncle is black, we can leave the loop as there are no more checks needed in this case
		if (RBTREE_NODE_GET_COLOUR(uncle) == RBTREE_COLOUR_BLACK)
			break;

		// uncle is red, recolour the tree such as:
		// parent = black, grandparent = red, uncle = black
		// and go up for another violation check, as changing the grandparent to red might have
		// violated it again
		RBTREE_NODE_SET_COLOUR(parent, RBTREE_COLOUR_BLACK);
		RBTREE_NODE_SET_COLOUR(uncle, RBTREE_COLOUR_BLACK);
		// SPECIAL CASE: if grandparent is root, do nothing. we always want the root to be black.
		if (RBTREE_NODE_GET_PARENT(grand_parent))
			RBTREE_NODE_SET_COLOUR(grand_parent, RBTREE_COLOUR_RED);
		node = grand_parent;
	}

	// black uncle, we have to do a rotation in order to be able to recolour it

	/* if grandparent -> parent and parent -> child are in opposite directions,
	// we will have to do an extra rotation to straighten it out first
	// Left rotation:                  Right rotation:
	//        G              G               G            G
	//       / \            / \             / \          / \
	//      P   U   ->     N   U           U   P  ->    U   N
	//       \            /                   /              \
	//        N          P                   N                P
	*/

	if (grand_parent->left == parent && parent->right == node) {
		rotate_left(rbtreep, node);
		node = parent;
		parent = grand_parent->left;
	} else if (grand_parent->right == parent && parent->left == node) {
		rotate_right(rbtreep, node);
		node = parent;
		parent = grand_parent->right;
	}

	/* now, we have to do a rotation so that we can recolour the nodes without
	// breaking any properties of the tree
	//        G              P                  G                P
	//       / \            / \                / \              / \
	//      P   U    ->    N   G       OR     U   P     ->     G   N
	//     /                    \                  \          /
	//    N                      U                  N        U
	*/

	if (grand_parent->left == parent)
		rotate_right(rbtreep, parent);
	else
		rotate_left(rbtreep, parent);

	/* finally, recolour the nodes:
	//         R             B                  R             B
	//        / \           / \                / \           / \
	//       R   B    ->   R   R      OR      B   R    ->   R   R
	//            \             \            /             /
	//             B             B          B             B
	*/

	// same operation on both cases
	RBTREE_NODE_SET_COLOUR(parent, RBTREE_COLOUR_BLACK);
	RBTREE_NODE_SET_COLOUR(grand_parent, RBTREE_COLOUR_RED);
	// no need to change node or uncle colour
}

void u80211_rbtree_check(u80211_rbtree_t *rbtree) {
	if (rbtree == NULL)
		return;

	// check that the root is black (always will be in this implementation)
	u80211_rbtree_t *parent = RBTREE_NODE_GET_PARENT(rbtree);

	// perform a preorder transversal and ensure the properties are followed
	u80211_rbtree_t *node = rbtree;
	int leaf_black_count = 0;
	int current_black_count = 0;
	int count_blacks = 1;
	int last_direction = 0; // 1 = left, 2 = right
	int going_up = 0;
	for (;;) {
		int node_colour = RBTREE_NODE_GET_COLOUR(node);
		if (node_colour == RBTREE_COLOUR_RED) {
		} else if (!going_up) {
			++current_black_count;
			leaf_black_count += count_blacks;
		}

		if (node == NULL) {
			// this is a NULL node, do nescessary checks and go up
			count_blacks = 0;

			--current_black_count;
			node = parent;
			parent = RBTREE_NODE_GET_PARENT(node);
			going_up = 1;
		} else if (going_up && last_direction == 1) {
			// we have gone up from the left node
			// go to the right
			last_direction = 2;
			parent = node;
			node = node->right;
			going_up = 0;
		} else if (going_up && last_direction == 2) {
			// we have gone up from the right
			// go up again
			if (parent == NULL)
				break; // end of tree

			if (node_colour == RBTREE_COLOUR_BLACK)
				--current_black_count;
			last_direction = parent->left == node ? 1 : 2;
			node = parent;
			parent = RBTREE_NODE_GET_PARENT(parent);
		} else {
			// we have gone down, go to the left
			last_direction = 1;
			parent = node;
			node = node->left;
		}
	}
}

u80211_rbtree_t *u80211_rbtree_first(u80211_rbtree_t *rbtree) {
	while (rbtree->left)
		rbtree = rbtree->left;

	return rbtree;
}

u80211_rbtree_t *u80211_rbtree_last(u80211_rbtree_t *rbtree) {
	while (rbtree->right)
		rbtree = rbtree->right;

	return rbtree;
}


u80211_rbtree_t *u80211_rbtree_successor(u80211_rbtree_t *node) {
	u80211_rbtree_t *iterator = node->right;
	if (iterator == NULL) {
		// no right, go up until we leave a left node
		iterator = node;
		u80211_rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
		while (parent) {
			if (parent->left == iterator)
				return parent;

			iterator = parent;
			parent = RBTREE_NODE_GET_PARENT(iterator);
		}

		// end of tree
		return NULL;
	}

	// we have gone right, go all the way down left
	while (iterator->left != NULL)
		iterator = iterator->left;

	return iterator;
}

u80211_rbtree_t *u80211_rbtree_predecessor(u80211_rbtree_t *node) {
	u80211_rbtree_t *iterator = node->left;
	if (iterator == NULL) {
		// no left, go up until we leave a right node
		iterator = node;
		u80211_rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
		while (parent) {
			if (parent->right == iterator)
				return parent;

			iterator = parent;
			parent = RBTREE_NODE_GET_PARENT(iterator);
		}

		// end of tree
		return NULL;
	}

	// we have gone right, go all the way down left
	while (iterator->right != NULL)
		iterator = iterator->right;

	return iterator;
}

static void u80211_rbtree_remove_leaf(u80211_rbtree_t **rbtreep, u80211_rbtree_t *node) {
	u80211_rbtree_t *og_node = node;
	if (*rbtreep == node) {
		// if root, set root to null
		*rbtreep = NULL;
		return;
	}

	if (RBTREE_NODE_GET_COLOUR(node) == RBTREE_COLOUR_RED) {
		// if red there is no rebalancing needed, just remove it
		goto do_remove;
	}

	u80211_rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);

	// black leaf node
	u80211_rbtree_t *sibling;
	for (;;) {
		if (parent == NULL)
			goto do_remove;

		sibling = parent->left == node ? parent->right : parent->left;

		// the only case where we have to loop upwards is when everyone is black
		if (RBTREE_NODE_GET_COLOUR(parent) == RBTREE_COLOUR_RED ||
				RBTREE_NODE_GET_COLOUR(sibling) == RBTREE_COLOUR_RED ||
				RBTREE_NODE_GET_COLOUR(sibling->left) == RBTREE_COLOUR_RED ||
				RBTREE_NODE_GET_COLOUR(sibling->right) == RBTREE_COLOUR_RED)
			break;

		// colour the sibling red and move upwards
		RBTREE_NODE_SET_COLOUR(sibling, RBTREE_COLOUR_RED);

		node = parent;
		parent = RBTREE_NODE_GET_PARENT(node);
	}

	// red sibling, do a rotation so that the sibling is now the grandparent
	// and switch the colours of the sibling and parent
	//     P            S   |       R          B
	//    / \          /    |      /          /
	//   N   S   ->   P     |     B    ->    R
	//               /      |    /          /
	//              N       |   B          B
	if (RBTREE_NODE_GET_COLOUR(sibling) == RBTREE_COLOUR_RED) {
		// set parent to red and sibling to black
		RBTREE_NODE_SET_COLOUR(parent, RBTREE_COLOUR_RED);
		RBTREE_NODE_SET_COLOUR(sibling, RBTREE_COLOUR_BLACK);

		// rotate
		if (parent->left == sibling) {
			rotate_right(rbtreep, sibling);
			sibling = parent->left;
		} else {
			rotate_left(rbtreep, sibling);
			sibling = parent->right;
		}
	}

	/* if all of the sibling nodes are black and parent is red, we can
	// recolour the tree so that the imbalance in black paths is fixed
	//   Example:
	//       R              B
	//      / \            / \
	//     B   B    ->    B   R
	//        / \            / \
	//       B   B          B   B
	*/
	if (RBTREE_NODE_GET_COLOUR(parent) == RBTREE_COLOUR_RED && 
			RBTREE_NODE_GET_COLOUR(sibling) == RBTREE_COLOUR_BLACK && 
			RBTREE_NODE_GET_COLOUR(sibling->left) == RBTREE_COLOUR_BLACK &&
			RBTREE_NODE_GET_COLOUR(sibling->right) == RBTREE_COLOUR_BLACK
	) {
		RBTREE_NODE_SET_COLOUR(parent, RBTREE_COLOUR_BLACK);
		RBTREE_NODE_SET_COLOUR(sibling, RBTREE_COLOUR_RED);
		goto do_remove;
	}

	u80211_rbtree_t *close_nephew, *far_nephew;
	if (parent->right == sibling) {
		close_nephew = sibling->left;
		far_nephew = sibling->right;
	} else {
		close_nephew = sibling->right;
		far_nephew = sibling->left;
	}

	/* if the nearest nephew is red and the farthest is black, rotate it into the new sibling
	// and exchange the colour between it and the old sibling
	// example:
	//
	//         P            P       |       X                X
	//        / \          / \      |      / \              / \
	//       N   S        N   C     |     B   R            B   B
	//          / \   ->       \    |          \     ->         \
	//         C   F            S   |           B                R
	//                           \  |            \                \
	//                            F |             B                B
	*/
	if (RBTREE_NODE_GET_COLOUR(close_nephew) == RBTREE_COLOUR_RED && RBTREE_NODE_GET_COLOUR(far_nephew) == RBTREE_COLOUR_BLACK) {
		// do rotation
		if (close_nephew == sibling->left)
			rotate_right(rbtreep, close_nephew);
		else
			rotate_left(rbtreep, close_nephew);

		// update for next step
		far_nephew = sibling;
		sibling = close_nephew;

		// recolour
		RBTREE_NODE_SET_COLOUR(far_nephew, RBTREE_COLOUR_RED);
		RBTREE_NODE_SET_COLOUR(sibling, RBTREE_COLOUR_BLACK);
	}

	/* in this case, the closest nephew is black and the distant nephew is red.
	// there is no need to check for this, as the result of the last operation, if it happened, guarantees it.
	// we need to rotate such that the sibling becomes the new grandparent and recolour such that:
	// sibling -> parent's colour
	// parent -> black
	// farthest nephew -> black
	// example:
	//    P             S      |     B           X
	//   / \           / \     |    / \         / \
	//  N   S   ->    P   F    |   X   R  ->   B   B
	//     / \       / \       |  / \         / \
	//    C   F     N   C      | B   B       B   B
	*/

	if (parent->right == sibling)
		rotate_left(rbtreep, sibling);
	else
		rotate_right(rbtreep, sibling);

	RBTREE_NODE_SET_COLOUR(sibling, RBTREE_NODE_GET_COLOUR(parent));
	RBTREE_NODE_SET_COLOUR(parent, RBTREE_COLOUR_BLACK);
	RBTREE_NODE_SET_COLOUR(far_nephew, RBTREE_COLOUR_BLACK);

	do_remove:
	// do the actual removal of the node
	parent = RBTREE_NODE_GET_PARENT(og_node);
	if (parent->left == og_node)
		parent->left = NULL;
	else
		parent->right = NULL;
}

void u80211_rbtree_remove(u80211_rbtree_t **rbtreep, u80211_rbtree_t *node) {
	if (node->left == NULL && node->right == NULL) {
		// leaf node?
		u80211_rbtree_remove_leaf(rbtreep, node);
		return;
	} else if (!(node->left && node->right)) {
		// has one child
		// replace it with the child and colour the child black

		u80211_rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
		u80211_rbtree_t *child = node->left ? node->left : node->right;
		if (parent == NULL) 
			*rbtreep = child;
		else if (parent->left == node)
			parent->left = child;
		else
			parent->right = child;

		child->parent = RBTREE_CONSTRUCT_PARENT(parent, RBTREE_COLOUR_BLACK);
		return;
	}

	// two children
	// swap itself with its successor and delete it again
	// doing this guarantees that it will only have, at most, one child when deleted again
	u80211_rbtree_t *succ = u80211_rbtree_successor(node);
	u80211_rbtree_t *succ_parent = RBTREE_NODE_GET_PARENT(succ);
	u80211_rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);

	// set the successor in place
	if (parent == NULL) {
		// root
		*rbtreep = succ;
	} else if (parent->left == node) {
		parent->left = succ;
	} else {
		parent->right = succ;
	}

	int succ_colour = RBTREE_NODE_GET_COLOUR(succ);
	succ->parent = node->parent; // copy both the parent and colour
	succ->left = node->left;
	u80211_rbtree_t *old_succ_right = succ->right;
	succ->right = node->right;

	RBTREE_NODE_SET_PARENT(succ->left, succ);
	if (succ_parent != node)
		RBTREE_NODE_SET_PARENT(succ->right, succ);

	// set the node in place
	if (succ_parent == node) {
		succ->right = node;
		node->parent = RBTREE_CONSTRUCT_PARENT(succ, succ_colour);
	} else {
		succ_parent->left = node;
		node->parent = RBTREE_CONSTRUCT_PARENT(succ_parent, succ_colour);
	}

	node->left = NULL; // sucessor has no left
	node->right = old_succ_right;
	if (node->right)
		RBTREE_NODE_SET_PARENT(node->right, node);

	// remove the node. this will not recurse again as the node will have only one child
	u80211_rbtree_remove(rbtreep, node);
}
