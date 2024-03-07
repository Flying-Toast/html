#ifndef __HAVE_WALK_H
#define __HAVE_WALK_H

struct node *find_node(struct node *root, int (*pred)(struct node *));
void walk_document(struct node *root, void (*fn)(struct node *));

#endif
