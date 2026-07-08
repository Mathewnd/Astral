#include <rbtree.h>
#include <stddef.h>
#include <stdint.h>
#include <logging.h>

#define RBTREE_COLOUR_BLACK 0
#define RBTREE_COLOUR_RED 1

// helper macros as the least significant bit of the parent pointer is used for the colour
#define RBTREE_NODE_GET_PARENT(x) ((rbtree_t *)((uintptr_t)(x)->parent & ~1lu))
#define RBTREE_NODE_GET_COLOUR(x) ((x) == NULL ? RBTREE_COLOUR_BLACK : ((uintptr_t)(x)->parent & 1))
#define RBTREE_NODE_SET_COLOUR(x, c) ((x)->parent = (rbtree_t *)((uintptr_t)RBTREE_NODE_GET_PARENT(x) | (c)))
#define RBTREE_NODE_SET_PARENT(x, p) ((x)->parent = (rbtree_t *)((uintptr_t)(p) | RBTREE_NODE_GET_COLOUR(x)))
#define RBTREE_CONSTRUCT_PARENT(x, c) ((rbtree_t *)((uintptr_t)(x) | (c)))

static void rotate_right(rbtree_t **rootp, rbtree_t *node) {
	/*      G             G
	//      |             |
	//      P      ->     N
	//     / \           / \
	//    N   3         1   P
	//   / \               / \
	//  1  2              2   3 
	*/

	rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
	rbtree_t *grand_parent = RBTREE_NODE_GET_PARENT(parent);

	// N
	if (grand_parent == NULL)
		*rootp = node;
	else if (grand_parent->left == parent)
		grand_parent->left = node;
	else
		grand_parent->right = node;

	RBTREE_NODE_SET_PARENT(node, grand_parent);

	// P
	rbtree_t *node_right_subtree = node->right;

	node->right = parent;
	RBTREE_NODE_SET_PARENT(parent, node);

	// 2
	parent->left = node_right_subtree;
	if (node_right_subtree)
		RBTREE_NODE_SET_PARENT(node_right_subtree, parent);
}

static void rotate_left(rbtree_t **rootp, rbtree_t *node) {
	/*        G              G
	//        |              |
	//        P              N
	//       / \    ->      / \
	//      1   N          P   3
	//         / \        / \
	//        2   3      1   2
	*/

	rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
	rbtree_t *grand_parent = RBTREE_NODE_GET_PARENT(parent);

	// N
	if (grand_parent == NULL)
		*rootp = node;
	else if (grand_parent->left == parent)
		grand_parent->left = node;
	else
		grand_parent->right = node;

	RBTREE_NODE_SET_PARENT(node, grand_parent);

	// P
	rbtree_t *node_left_subtree = node->left;

	node->left = parent;
	RBTREE_NODE_SET_PARENT(parent, node);

	// 2
	parent->right = node_left_subtree;
	if (node_left_subtree)
		RBTREE_NODE_SET_PARENT(node_left_subtree, parent);
}

rbtree_t *rbtree_find_first_larger_equal(rbtree_t *rbtree, void *key, rbtree_value_compare_fn_t compare_fn) {
	rbtree_t *found = NULL;
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

rbtree_t *rbtree_lookup(rbtree_t *rbtree, void *key, rbtree_value_compare_fn_t compare_fn) {
	for (;;) {
		if (rbtree == NULL)
			return NULL;

		int comparison = compare_fn(key, rbtree);

		if (comparison == 0)
			return rbtree;

		rbtree = comparison > 0 ? rbtree->right : rbtree->left;
	}
}

void rbtree_insert(rbtree_t **rbtreep, rbtree_t *node, rbtree_compare_fn_t compare_fn) {
	rbtree_t *rbtree = *rbtreep;
	if (rbtree == NULL) {
		*rbtreep = node;
		node->right = node->left = node->parent = NULL; // NULL implicitly sets to black
		return;
	}

	// find last node
	int comparison;
	rbtree_t **next;
	for (;;) {
		comparison = compare_fn(node, rbtree);
		__assert(comparison); // no duplicates, please

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
	rbtree_t *parent, *grand_parent, *uncle;
	for (;;) {
		parent = RBTREE_NODE_GET_PARENT(node);
		// check for property 3 violation (red parent and red child)
		if (RBTREE_NODE_GET_COLOUR(parent) == RBTREE_COLOUR_BLACK)
			return; // no violation, stop here

		grand_parent = RBTREE_NODE_GET_PARENT(parent);
		__assert(grand_parent); // we will not have red roots in this red-black tree implementation
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

void rbtree_check(rbtree_t *rbtree) {
	if (rbtree == NULL)
		return;

	// check that the root is black (always will be in this implementation)
	rbtree_t *parent = RBTREE_NODE_GET_PARENT(rbtree);
	__assert(parent || RBTREE_NODE_GET_COLOUR(rbtree) == RBTREE_COLOUR_BLACK);

	// perform a preorder transversal and ensure the properties are followed
	rbtree_t *node = rbtree;
	int leaf_black_count = 0;
	int current_black_count = 0;
	int count_blacks = 1;
	int last_direction = 0; // 1 = left, 2 = right
	int going_up = 0;
	for (;;) {
		int node_colour = RBTREE_NODE_GET_COLOUR(node);
		if (node_colour == RBTREE_COLOUR_RED) {
			__assert(RBTREE_NODE_GET_COLOUR(parent) != RBTREE_COLOUR_RED);
		} else if (!going_up) {
			++current_black_count;
			leaf_black_count += count_blacks;
		}

		if (node == NULL) {
			__assert(going_up == 0);
			// this is a NULL node, do nescessary checks and go up
			count_blacks = 0;
			__assert(current_black_count == leaf_black_count);

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

rbtree_t *rbtree_first(rbtree_t *rbtree) {
	while (rbtree->left)
		rbtree = rbtree->left;

	return rbtree;
}

rbtree_t *rbtree_last(rbtree_t *rbtree) {
	while (rbtree->right)
		rbtree = rbtree->right;

	return rbtree;
}


rbtree_t *rbtree_successor(rbtree_t *node) {
	rbtree_t *iterator = node->right;
	if (iterator == NULL) {
		// no right, go up until we leave a left node
		iterator = node;
		rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
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

rbtree_t *rbtree_predecessor(rbtree_t *node) {
	rbtree_t *iterator = node->left;
	if (iterator == NULL) {
		// no left, go up until we leave a right node
		iterator = node;
		rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
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

static void rbtree_remove_leaf(rbtree_t **rbtreep, rbtree_t *node) {
	rbtree_t *og_node = node;
	if (*rbtreep == node) {
		// if root, set root to null
		*rbtreep = NULL;
		return;
	}

	if (RBTREE_NODE_GET_COLOUR(node) == RBTREE_COLOUR_RED) {
		// if red there is no rebalancing needed, just remove it
		goto do_remove;
	}

	rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);

	// black leaf node
	rbtree_t *sibling;
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

	rbtree_t *close_nephew, *far_nephew;
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

void rbtree_remove(rbtree_t **rbtreep, rbtree_t *node) {
	if (node->left == NULL && node->right == NULL) {
		// leaf node?
		rbtree_remove_leaf(rbtreep, node);
		return;
	} else if (!(node->left && node->right)) {
		// has one child
		// replace it with the child and colour the child black

		rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);
		rbtree_t *child = node->left ? node->left : node->right;
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
	rbtree_t *succ = rbtree_successor(node);
	rbtree_t *succ_parent = RBTREE_NODE_GET_PARENT(succ);
	rbtree_t *parent = RBTREE_NODE_GET_PARENT(node);

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
	rbtree_t *old_succ_right = succ->right;
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
	rbtree_remove(rbtreep, node);
}

void rbtree_print(rbtree_t *rbtree, rbtree_print_fn_t print_fn) {
	if (rbtree == NULL)
		return;

	rbtree_t *parent = RBTREE_NODE_GET_PARENT(rbtree);
	rbtree_t *node = rbtree;
	int depth = 0;
	int last_direction = 0; // 1 = left, 2 = right
	int going_up = 0;
	for (;;) {
		int node_colour = RBTREE_NODE_GET_COLOUR(node);

		if (node_colour == RBTREE_COLOUR_RED) {
			printf("\e[31m");
		} else {
			printf("\e[90m");
		}

		if (node && going_up == 0) {
			for (int i = 0; i < depth; ++i) {
				printf(" ");
			}
			print_fn(node);
		}

		printf("\e[0m");

		if (node && going_up == 0 && parent != RBTREE_NODE_GET_PARENT(node)) {
			printf("bad parent:");
			printf("expected: ");
			print_fn(parent);
			printf("got: ");
			print_fn(RBTREE_NODE_GET_PARENT(node));
			__assert(!"Bad parent");
		}

		if (node == NULL) {
			__assert(going_up == 0);
			// this is a NULL node, do nescessary checks and go up

			node = parent;
			parent = RBTREE_NODE_GET_PARENT(node);
			going_up = 1;
			--depth;
		} else if (going_up && last_direction == 1) {
			// we have gone up from the left node
			// go to the right
			last_direction = 2;
			parent = node;
			node = node->right;
			going_up = 0;
			++depth;
		} else if (going_up && last_direction == 2) {
			// we have gone up from the right
			// go up again
			if (parent == NULL)
				break; // end of tree

			last_direction = parent->left == node ? 1 : 2;
			node = parent;
			parent = RBTREE_NODE_GET_PARENT(parent);
			--depth;
		} else {
			// we have gone down, go to the left
			last_direction = 1;
			parent = node;
			node = node->left;
			++depth;
		}
	}

}
