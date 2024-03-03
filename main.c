#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define _STRINGIFY(X) # X
#define TRY(CALL) \
	({ \
		errno = 0; \
		typeof(CALL) __ret = CALL; \
		if (errno) { \
			perror("TRY(" __FILE__ ":" _STRINGIFY(__LINE__) ")"); \
			exit(1); \
		} \
		__ret; \
	})

enum node_kind {
	NODE_ELT,
	NODE_TEXT,
	NODE_WHITESPACE,
};

struct node {
	enum node_kind kind;
	union {
		struct {
			char *tagname;
			struct nodelist *children;
		} elt;

		struct {
			char *content;
		} text;
	};
};

struct nodelist {
	struct node node;
	struct nodelist *next;
}; 

static int parse_node(char *s, char **rest, struct node *out);
static void put_nodelist(struct nodelist *l);

static void
eatsp(char **s) {
	char *cp = *s;
	while (*cp && isspace(*cp))
		cp++;
	*s = cp;
}

/* caller must free returned string.
 * may return null.
 */
static char *
parse_alnum(char *s, char **rest) {
	char *start = s;
	while (*s && isalnum(*s))
		s++;
	size_t len = s - start;
	if (len == 0)
		return NULL;
	char *ret = malloc(len + 1);
	ret[len] = '\0';
	memcpy(ret, start, len);
	*rest = s;
	return ret;
}

static struct node *
new_node() {
	return malloc(sizeof(struct node));
}

static void
put_node_inner(struct node *n) {
	if (!n)
		return;

	if (n->kind == NODE_ELT) {
		free(n->elt.tagname);
		put_nodelist(n->elt.children);
	} else if (n->kind == NODE_TEXT) {
		free(n->text.content);
	}
}

static void
put_nodelist(struct nodelist *l) {
	if (!l)
		return;
	struct nodelist *next = l->next;
	while (l != NULL) {
		put_node_inner(&l->node);
		free(l);
		l = next;
		if (l)
			next = l->next;
	}
}

/* returns a node with kind = NODE_TEXT or NODE_WHITESPACE */
static int
parse_text(char *s, char **rest, struct node *out) {
	*rest = s;
	if (*s == '\0')
		return -1;
	char *start = s;
	while (*s && *s != '<')
		s++;
	size_t rawlen = s - start;
	if (rawlen == 0)
		return -1;

	int isallspace = 1;
	// # of chars shorter the content will be,
	// due to collapsed spaces:
	int ndupspaces = 0;

	int spacerun = 0;
	for (size_t i = 0; i < rawlen; i++) {
		if (isspace(start[i])) {
			spacerun++;
		}

		if (!isspace(start[i]) || i >= rawlen) {
			isallspace = 0;
			if (spacerun)
				ndupspaces += spacerun - 1;
			spacerun = 0;
		}
	}
	
	if (isallspace) {
		out->kind = NODE_WHITESPACE;
	} else {
		size_t len = rawlen - ndupspaces;
		char *content = malloc(len + 1);
		content[len] = '\0';
		size_t cidx = 0;
		for (size_t i = 0; i < rawlen; i++) {
			if (isspace(start[i])) {
				content[cidx++] = ' ';
				while (isspace(start[i + 1]))
					i++;
			} else {
				content[cidx++] = start[i];
			}
		}
		out->kind = NODE_TEXT;
		out->text.content = content;
	}

	*rest = s;
	return 0;
}

static int
parse_elt(char *s, char **rest, struct node *out) {
	*rest = s;
	if (!*s || *s++ != '<')
		return -1;
	eatsp(&s);
	char *tagname = parse_alnum(s, rest);
	s = *rest;
	if (!tagname)
		return -1;
	eatsp(&s); // eat post-tagname whitespace

	// TODO: actually parse attrs.
	// for now we just skip them.
	while (*s && *s != '>')
		s++;
	if (!*s++)
		goto err_freetag;

	out->kind = NODE_ELT;
	out->elt.tagname = tagname;
	out->elt.children = NULL;
	struct nodelist **next = &out->elt.children;
	while (*s && strncmp("</", s, 2)) {
		struct nodelist *new = malloc(sizeof(struct nodelist));
		new->next = NULL;
		if (parse_node(s, rest, &new->node))
			goto err_freechildren;
		s = *rest;
		*next = new;
		next = &new->next;
	}
	if (strncmp(s, "</", 2))
		goto err_freechildren;
	s += strlen("</");
	eatsp(&s);
	if (strncasecmp(s, tagname, strlen(tagname)))
		goto err_freechildren;
	s += strlen(tagname);
	if (*s++ != '>')
		goto err_freechildren;
	*rest = s;
	return 0;

err_freechildren:
	put_nodelist(out->elt.children);
err_freetag:
	free(tagname);
	return -1;
}

static int
parse_node(char *s, char **rest, struct node *out) {
	*rest = s;
	if (!*s)
		return -1;

	if (*s == '<')
		return parse_elt(s, rest, out);
	else
		return parse_text(s, rest, out);
}

static void
print_node(struct node *n, int nindent) {
	if (!n)
		return;

	char *indentstr;
	if (nindent) {
		indentstr = malloc(nindent + 1);
		memset(indentstr, '\t', nindent);
		indentstr[nindent] = '\0';
	} else {
		indentstr = "";
	}

	if (n->kind == NODE_TEXT) {
		printf("%s#Text \"%s\"\n", indentstr, n->text.content);
	} else if (n->kind == NODE_ELT) {
		printf("%s#%s\n", indentstr, n->elt.tagname);
		struct nodelist *i;
		for (i = n->elt.children; i != NULL; i = i->next) {
			print_node(&i->node, nindent + 1);
		}
	} else if (n->kind == NODE_WHITESPACE) {
		//printf("%s#Whitespace\n", indentstr);
	}

	if (nindent)
		free(indentstr);
}

int
main(int argc, char **argv) {
	if (argc != 2) {
		fputs("bad usage\n", stderr);
		exit(1);
	}
	int f = TRY(open(argv[1], O_RDONLY));
	struct stat st;
	TRY(fstat(f, &st));
	size_t buflen = st.st_size + 1;
	char *buf = malloc(buflen);
	buf[buflen - 1] = '\0';
	read(f, buf, st.st_size);
	struct node *htmlnode = new_node();
	char *rest;
	if (parse_node(buf, &rest, htmlnode))
		goto err;
	
	print_node(htmlnode, 0);

	goto ok;
err:
	fputs("error :(\n", stderr);
	goto err_free;
ok:
	put_node_inner(htmlnode);
err_free:
	free(htmlnode);
	free(buf);
}
