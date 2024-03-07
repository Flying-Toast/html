#include <stddef.h>

#include "parse.h"
#include "walk.h"

struct node *
find_node(struct node *root, int (*pred)(struct node *)) {
	if ((*pred)(root))
		return root;
	if (root->kind != NODE_ELT)
		return NULL;

	for (struct nodelist *i = root->elt.children; i; i = i->next) {
		struct node *ret = find_node(&i->node, pred);
		if (ret)
			return ret;
	}

	return NULL;
}

void
walk_document(struct node *root, void (*fn)(struct node *)) {
	(*fn)(root);

	if (root->kind != NODE_ELT)
		return;

	for (struct nodelist *i = root->elt.children; i; i = i->next)
		walk_document(&i->node, fn);
}
