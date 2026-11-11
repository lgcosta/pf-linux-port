/*
 * PFLINUX: userland implementation of the out-of-line generic red-black
 * tree API that tools/include/sys/tree.h (copied verbatim from
 * _vendor/upstream/sys/sys/tree.h) declares and the RBT_* macros call:
 * _rb_insert/_rb_find/_rb_remove/_rb_min/_rb_next/...  OpenBSD ships
 * these as libc symbols (sys/kern/subr_rbtree.c is linked into libc);
 * glibc has none, so pfctl links this file.  Behavior follows OpenBSD:
 *   _rb_insert  returns NULL on success, the existing item on duplicate.
 *   _rb_remove  unlinks and returns the removed item.
 *   Colors use the tree.h RB_RED/RB_BLACK values; struct rb_entry holds
 *   plain parent/left/right pointers plus an unsigned color.
 *   t_augment (when non-NULL) is called on ancestors after mutations.
 */

#include <sys/types.h>
#include <stddef.h>
#include <errno.h>

#include <sys/tree.h>

static __inline struct rb_entry *
rbt_entry(const struct rb_type *t, void *item)
{
	return ((struct rb_entry *)((char *)item + t->t_offset));
}

static __inline void *
rbt_item(const struct rb_type *t, const struct rb_entry *rbe)
{
	return (rbe == NULL ? NULL :
	    (void *)((char *)(struct rb_entry *)rbe - t->t_offset));
}

#define RBE_PARENT(rbe)		((rbe)->rbt_parent)
#define RBE_LEFT(rbe)		((rbe)->rbt_left)
#define RBE_RIGHT(rbe)		((rbe)->rbt_right)
#define RBE_COLOR(rbe)		((rbe)->rbt_color)

static void
rbt_augment(const struct rb_type *t, struct rb_entry *rbe)
{
	for (; rbe != NULL && t->t_augment != NULL; rbe = RBE_PARENT(rbe))
		t->t_augment(rbt_item(t, rbe));
}

/*
 * Replace the subtree rooted at `u' with `v' (CLRS TRANSPLANT).
 */
static void
rbt_transplant(struct rb_entry **root, struct rb_entry *u,
    struct rb_entry *v)
{
	if (RBE_PARENT(u) == NULL)
		*root = v;
	else if (u == RBE_LEFT(RBE_PARENT(u)))
		RBE_PARENT(u)->rbt_left = v;
	else
		RBE_PARENT(u)->rbt_right = v;
	if (v != NULL)
		v->rbt_parent = RBE_PARENT(u);
}

/*
 * Replace the subtree rooted at `x' with `y'; y takes x's parent link.
 * (CLRS RB-INSERT-FIXUP/DELETE-FIXUP rotations.)
 */
static void
rbt_rotate_left(struct rb_entry **root, struct rb_entry *x)
{
	struct rb_entry *y = RBE_RIGHT(x);

	x->rbt_right = RBE_LEFT(y);
	if (RBE_LEFT(y) != NULL)
		RBE_LEFT(y)->rbt_parent = x;
	y->rbt_parent = RBE_PARENT(x);
	if (RBE_PARENT(x) == NULL)
		*root = y;
	else if (x == RBE_LEFT(RBE_PARENT(x)))
		RBE_PARENT(x)->rbt_left = y;
	else
		RBE_PARENT(x)->rbt_right = y;
	y->rbt_left = x;
	x->rbt_parent = y;
}

static void
rbt_rotate_right(struct rb_entry **root, struct rb_entry *x)
{
	struct rb_entry *y = RBE_LEFT(x);

	x->rbt_left = RBE_RIGHT(y);
	if (RBE_RIGHT(y) != NULL)
		RBE_RIGHT(y)->rbt_parent = x;
	y->rbt_parent = RBE_PARENT(x);
	if (RBE_PARENT(x) == NULL)
		*root = y;
	else if (x == RBE_RIGHT(RBE_PARENT(x)))
		RBE_PARENT(x)->rbt_right = y;
	else
		RBE_PARENT(x)->rbt_left = y;
	y->rbt_right = x;
	x->rbt_parent = y;
}

void *
_rb_insert(const struct rb_type *t, struct rb_tree *rbt, void *item)
{
	struct rb_entry **root = &rbt->rbt_root;
	struct rb_entry *rbe = rbt_entry(t, item);
	struct rb_entry *current = *root, *parent = NULL;

	/* [1] Find the insertion spot. */
	while (current != NULL) {
		int result = t->t_compare(item, rbt_item(t, current));

		parent = current;
		if (result < 0)
			current = RBE_LEFT(current);
		else if (result > 0)
			current = RBE_RIGHT(current);
		else
			return (rbt_item(t, current));	/* duplicate */
	}

	/* [2] Insert red. */
	rbe->rbt_left = NULL;
	rbe->rbt_right = NULL;
	rbe->rbt_color = RB_RED;
	rbe->rbt_parent = parent;
	if (parent == NULL)
		*root = rbe;
	else if (t->t_compare(item, rbt_item(t, parent)) < 0)
		parent->rbt_left = rbe;
	else
		parent->rbt_right = rbe;

	/* [3] Fix red-black violations (CLRS RB-INSERT-FIXUP). */
	if (parent == NULL)
		rbe->rbt_color = RB_BLACK;
	else {
		struct rb_entry *z = rbe, *p, *g, *u;

		while ((p = RBE_PARENT(z)) != NULL &&
		    RBE_COLOR(p) == RB_RED) {
			g = RBE_PARENT(p);
			if (p == RBE_LEFT(g)) {
				u = RBE_RIGHT(g);
				if (u != NULL && RBE_COLOR(u) == RB_RED) {
					p->rbt_color = RB_BLACK;
					u->rbt_color = RB_BLACK;
					g->rbt_color = RB_RED;
					z = g;
				} else {
					if (z == RBE_RIGHT(p)) {
						z = p;
						rbt_rotate_left(root, p);
						p = RBE_PARENT(z);
					}
					g = RBE_PARENT(p);
					p->rbt_color = RB_BLACK;
					g->rbt_color = RB_RED;
					rbt_rotate_right(root, g);
				}
			} else {
				u = RBE_LEFT(g);
				if (u != NULL && RBE_COLOR(u) == RB_RED) {
					p->rbt_color = RB_BLACK;
					u->rbt_color = RB_BLACK;
					g->rbt_color = RB_RED;
					z = g;
				} else {
					if (z == RBE_LEFT(p)) {
						z = p;
						rbt_rotate_right(root, p);
						p = RBE_PARENT(z);
					}
					g = RBE_PARENT(p);
					p->rbt_color = RB_BLACK;
					g->rbt_color = RB_RED;
					rbt_rotate_left(root, g);
				}
			}
		}
		(*root)->rbt_color = RB_BLACK;
	}

	/* [4] Augment. */
	rbt_augment(t, RBE_PARENT(rbe));

	return (NULL);
}

/*
 * CLRS RB-DELETE-FIXUP with x possibly NULL; x_parent threaded explicitly.
 */
static void
rbt_delete_fixup(struct rb_entry **root, struct rb_entry *x,
    struct rb_entry *x_parent)
{
	while (x != *root && (x == NULL || RBE_COLOR(x) == RB_BLACK)) {
		struct rb_entry *sibling;

		if (x_parent == NULL)
			break;
		if (x == RBE_LEFT(x_parent)) {
			sibling = RBE_RIGHT(x_parent);
			if (sibling != NULL &&
			    RBE_COLOR(sibling) == RB_RED) {
				sibling->rbt_color = RB_BLACK;
				x_parent->rbt_color = RB_RED;
				rbt_rotate_left(root, x_parent);
				sibling = RBE_RIGHT(x_parent);
			}
			if (sibling == NULL) {
				x = x_parent;
				x_parent = RBE_PARENT(x);
				continue;
			}
			if ((RBE_LEFT(sibling) == NULL ||
			    RBE_COLOR(RBE_LEFT(sibling)) == RB_BLACK) &&
			    (RBE_RIGHT(sibling) == NULL ||
			    RBE_COLOR(RBE_RIGHT(sibling)) == RB_BLACK)) {
				sibling->rbt_color = RB_RED;
				x = x_parent;
				x_parent = RBE_PARENT(x);
			} else {
				if (RBE_RIGHT(sibling) == NULL ||
				    RBE_COLOR(RBE_RIGHT(sibling)) ==
				    RB_BLACK) {
					if (RBE_LEFT(sibling) != NULL)
						RBE_LEFT(sibling)->rbt_color =
						    RB_BLACK;
					sibling->rbt_color = RB_RED;
					rbt_rotate_right(root, sibling);
					sibling = RBE_RIGHT(x_parent);
				}
				sibling->rbt_color = RBE_COLOR(x_parent);
				x_parent->rbt_color = RB_BLACK;
				if (RBE_RIGHT(sibling) != NULL)
					RBE_RIGHT(sibling)->rbt_color =
					    RB_BLACK;
				rbt_rotate_left(root, x_parent);
				x = *root;
			}
		} else {
			sibling = RBE_LEFT(x_parent);
			if (sibling != NULL &&
			    RBE_COLOR(sibling) == RB_RED) {
				sibling->rbt_color = RB_BLACK;
				x_parent->rbt_color = RB_RED;
				rbt_rotate_right(root, x_parent);
				sibling = RBE_LEFT(x_parent);
			}
			if (sibling == NULL) {
				x = x_parent;
				x_parent = RBE_PARENT(x);
				continue;
			}
			if ((RBE_RIGHT(sibling) == NULL ||
			    RBE_COLOR(RBE_RIGHT(sibling)) == RB_BLACK) &&
			    (RBE_LEFT(sibling) == NULL ||
			    RBE_COLOR(RBE_LEFT(sibling)) == RB_BLACK)) {
				sibling->rbt_color = RB_RED;
				x = x_parent;
				x_parent = RBE_PARENT(x);
			} else {
				if (RBE_LEFT(sibling) == NULL ||
				    RBE_COLOR(RBE_LEFT(sibling)) == RB_BLACK) {
					if (RBE_RIGHT(sibling) != NULL)
						RBE_RIGHT(sibling)->rbt_color =
						    RB_BLACK;
					sibling->rbt_color = RB_RED;
					rbt_rotate_left(root, sibling);
					sibling = RBE_LEFT(x_parent);
				}
				sibling->rbt_color = RBE_COLOR(x_parent);
				x_parent->rbt_color = RB_BLACK;
				if (RBE_LEFT(sibling) != NULL)
					RBE_LEFT(sibling)->rbt_color =
					    RB_BLACK;
				rbt_rotate_right(root, x_parent);
				x = *root;
			}
		}
	}

	if (x != NULL)
		x->rbt_color = RB_BLACK;
}

void *
_rb_remove(const struct rb_type *t, struct rb_tree *rbt, void *item)
{
	struct rb_entry **root = &rbt->rbt_root;
	struct rb_entry *z = rbt_entry(t, item);
	struct rb_entry *y = z, *x, *x_parent;
	int y_color;

	y_color = RBE_COLOR(y);

	if (RBE_LEFT(z) == NULL) {		/* [1] no left child */
		x = RBE_RIGHT(z);
		x_parent = RBE_PARENT(z);
		rbt_transplant(root, z, x);
	} else if (RBE_RIGHT(z) == NULL) {	/* [2] no right child */
		x = RBE_LEFT(z);
		x_parent = RBE_PARENT(z);
		rbt_transplant(root, z, x);
	} else {				/* [3] two children */
		/* in-order successor: leftmost of z's right subtree */
		for (y = RBE_RIGHT(z); RBE_LEFT(y) != NULL; y = RBE_LEFT(y))
			;
		y_color = RBE_COLOR(y);
		x = RBE_RIGHT(y);
		if (RBE_PARENT(y) == z) {
			x_parent = y;
		} else {
			x_parent = RBE_PARENT(y);
			rbt_transplant(root, y, x);
			y->rbt_right = RBE_RIGHT(z);
			RBE_RIGHT(z)->rbt_parent = y;
		}
		rbt_transplant(root, z, y);
		y->rbt_left = RBE_LEFT(z);
		RBE_LEFT(z)->rbt_parent = y;
		y->rbt_color = RBE_COLOR(z);
	}

	/* detach z */
	z->rbt_parent = NULL;
	z->rbt_left = NULL;
	z->rbt_right = NULL;
	z->rbt_color = RB_BLACK;

	if (y_color == RB_BLACK)
		rbt_delete_fixup(root, x, x_parent);

	/* augment from the changed point up to the root */
	rbt_augment(t, x_parent);

	return (item);
}

void *
_rb_find(const struct rb_type *t, struct rb_tree *rbt, const void *item)
{
	struct rb_entry *current = rbt->rbt_root;

	while (current != NULL) {
		int result = t->t_compare(item, rbt_item(t, current));

		if (result < 0)
			current = RBE_LEFT(current);
		else if (result > 0)
			current = RBE_RIGHT(current);
		else
			return (rbt_item(t, current));
	}
	return (NULL);
}

void *
_rb_nfind(const struct rb_type *t, struct rb_tree *rbt, const void *item)
{
	struct rb_entry *current = rbt->rbt_root;
	struct rb_entry *best = NULL;

	while (current != NULL) {
		if (t->t_compare(item, rbt_item(t, current)) <= 0) {
			best = current;
			current = RBE_LEFT(current);
		} else
			current = RBE_RIGHT(current);
	}
	return (best == NULL ? NULL : rbt_item(t, best));
}

void *
_rb_root(const struct rb_type *t, struct rb_tree *rbt)
{
	return (rbt_item(t, rbt->rbt_root));
}

void *
_rb_min(const struct rb_type *t, struct rb_tree *rbt)
{
	struct rb_entry *current = rbt->rbt_root;

	if (current == NULL)
		return (NULL);
	while (RBE_LEFT(current) != NULL)
		current = RBE_LEFT(current);
	return (rbt_item(t, current));
}

void *
_rb_max(const struct rb_type *t, struct rb_tree *rbt)
{
	struct rb_entry *current = rbt->rbt_root;

	if (current == NULL)
		return (NULL);
	while (RBE_RIGHT(current) != NULL)
		current = RBE_RIGHT(current);
	return (rbt_item(t, current));
}

void *
_rb_next(const struct rb_type *t, void *item)
{
	struct rb_entry *rbe = rbt_entry(t, item);
	struct rb_entry *parent;

	if (RBE_RIGHT(rbe) != NULL) {
		rbe = RBE_RIGHT(rbe);
		while (RBE_LEFT(rbe) != NULL)
			rbe = RBE_LEFT(rbe);
		return (rbt_item(t, rbe));
	}
	for (parent = RBE_PARENT(rbe); parent != NULL;
	    parent = RBE_PARENT(parent)) {
		if (rbe == RBE_LEFT(parent))
			return (rbt_item(t, parent));
		rbe = parent;
	}
	return (NULL);
}

void *
_rb_prev(const struct rb_type *t, void *item)
{
	struct rb_entry *rbe = rbt_entry(t, item);
	struct rb_entry *parent;

	if (RBE_LEFT(rbe) != NULL) {
		rbe = RBE_LEFT(rbe);
		while (RBE_RIGHT(rbe) != NULL)
			rbe = RBE_RIGHT(rbe);
		return (rbt_item(t, rbe));
	}
	for (parent = RBE_PARENT(rbe); parent != NULL;
	    parent = RBE_PARENT(parent)) {
		if (rbe == RBE_RIGHT(parent))
			return (rbt_item(t, parent));
		rbe = parent;
	}
	return (NULL);
}

void *
_rb_left(const struct rb_type *t, void *item)
{
	return (rbt_item(t, RBE_LEFT(rbt_entry(t, item))));
}

void *
_rb_right(const struct rb_type *t, void *item)
{
	return (rbt_item(t, RBE_RIGHT(rbt_entry(t, item))));
}

void *
_rb_parent(const struct rb_type *t, void *item)
{
	return (rbt_item(t, RBE_PARENT(rbt_entry(t, item))));
}

void
_rb_set_left(const struct rb_type *t, void *item, void *left)
{
	RBE_LEFT(rbt_entry(t, item)) =
	    left == NULL ? NULL : rbt_entry(t, left);
}

void
_rb_set_right(const struct rb_type *t, void *item, void *right)
{
	RBE_RIGHT(rbt_entry(t, item)) =
	    right == NULL ? NULL : rbt_entry(t, right);
}

void
_rb_set_parent(const struct rb_type *t, void *item, void *parent)
{
	RBE_PARENT(rbt_entry(t, item)) =
	    parent == NULL ? NULL : rbt_entry(t, parent);
}

void
_rb_poison(const struct rb_type *t, void *item, unsigned long poison)
{
	struct rb_entry *rbe = rbt_entry(t, item);

	rbe->rbt_parent = (void *)poison;
	rbe->rbt_left = (void *)poison;
	rbe->rbt_right = (void *)poison;
}

int
_rb_check(const struct rb_type *t, void *item, unsigned long poison)
{
	struct rb_entry *rbe = rbt_entry(t, item);

	if (rbe->rbt_parent == (void *)poison ||
	    rbe->rbt_left == (void *)poison ||
	    rbe->rbt_right == (void *)poison) {
		errno = EINVAL;
		return (-1);
	}
	return (0);
}
