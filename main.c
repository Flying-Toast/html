#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define _STRINGIFY1(X) #X
#define _STRINGIFY2(X) _STRINGIFY1(X)
#define TRY(CALL) \
	({ \
		errno = 0; \
		typeof(CALL) __ret = CALL; \
		if (errno) { \
			perror("TRY(" __FILE__ ":" _STRINGIFY2(__LINE__) ")"); \
			exit(1); \
		} \
		__ret; \
	})

#define ENUMERATE_SELFCLOSING_TAGS(X) \
	X(area) \
	X(base) \
	X(br) \
	X(col) \
	X(embed) \
	X(hr) \
	X(img) \
	X(input) \
	X(link) \
	X(meta) \
	X(param) \
	X(source) \
	X(track) \
	X(wbr)

enum node_kind {
	NODE_ELT,
	NODE_TEXT,
	NODE_COMMENT,
	NODE_WHITESPACE,
};

struct attr {
	char *name;
	char *val;
};

struct attrlist {
	struct attr attr;
	struct attrlist *next;
};


struct node {
	enum node_kind kind;
	union {
		struct {
			char *tagname;
			struct attrlist *attrs;
			struct nodelist *children;
		} elt;

		struct {
			char *content;
		} text;

		struct {
			char *content;
		} comment;
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

static int
is_selfclose(char *tagname) {
#define X(ARG) if (!strcasecmp(#ARG, tagname)) return 1;
	ENUMERATE_SELFCLOSING_TAGS(X)
#undef X
	return 0;
}

/* caller must free returned string.
 * may return null.
 */
static char *
parse_strwhile(char *s, char **rest, int (*pred)(int)) {
	char *start = s;
	while (*s && (*pred)(*s))
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

static int
_attrname_pred(int ch) {
	return ch == '-' || ch == '_' || isalnum(ch);
}

static char *
parse_attrname(char *s, char **rest) {
	return parse_strwhile(s, rest, _attrname_pred);
}

static struct node *
new_node() {
	return malloc(sizeof(struct node));
}

static void
put_attrlist(struct attrlist *l) {
	if (!l)
		return;
	struct attrlist *next = l->next;
	while (l != NULL) {
		free(l->attr.name);
		free(l->attr.val);
		free(l);
		l = next;
		if (l)
			next = l->next;
	}
}

static void
put_node_inner(struct node *n) {
	if (!n)
		return;

	if (n->kind == NODE_ELT) {
		free(n->elt.tagname);
		put_nodelist(n->elt.children);
		put_attrlist(n->elt.attrs);
	} else if (n->kind == NODE_TEXT) {
		free(n->text.content);
	} else if (n->kind == NODE_COMMENT) {
		free(n->comment.content);
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
		//return -1;
		__builtin_trap();
	char *start = s;
	while (*s && *s != '<')
		s++;
	size_t rawlen = s - start;
	if (rawlen == 0)
		//return -1;
		__builtin_trap();

	int isallspace = 1;
	// # of chars shorter the content will be,
	// due to collapsed spaces:
	int ndupspaces = 0;

	int spacerun = 0;
	for (size_t i = 0; i < rawlen; i++) {
		if (isspace(start[i])) {
			spacerun++;
		} else {
			isallspace = 0;
		}

		if (!isspace(start[i]) || i == rawlen - 1) {
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

/* parses a NODE_ELT or NODE_COMMENT */
static int
parse_elt(char *s, char **rest, struct node *out) {
	// handle comment
	const char *cstart = "<!--";
	const char *cend = "-->";
	if (!strncmp(s, cstart, strlen(cstart))) {
		s += strlen(cstart);
		char *start = s;
		while (*s && strncmp(s, cend, strlen(cend)))
			s++;
		size_t clen = s - start;
		char *content = malloc(clen + 1);
		content[clen] = '\0';
		memcpy(content, start, clen);
		if (*s)
			s += strlen(cend);
		eatsp(&s);
		*rest = s;
		out->kind = NODE_COMMENT;
		out->comment.content = content;
		return 0;
	}

	*rest = s;
	if (!*s || *s++ != '<')
		//return -1;
		__builtin_trap();
	eatsp(&s);
	char *tagname = parse_attrname(s, rest);
	s = *rest;
	if (!tagname)
		//return -1;
		__builtin_trap();
	eatsp(&s); // eat post-tagname whitespace

	struct attrlist **nextatt = &out->elt.attrs;
	while (*s && *s != '>' && *s != '/') {
		struct attrlist *new = malloc(sizeof(struct attrlist));
		new->next = NULL;
		new->attr.name = NULL;
		new->attr.val = NULL;
		*nextatt = new;
		nextatt = &new->next;
		if ((new->attr.name = parse_attrname(s, rest)) == NULL)
			// goto err_freeattrs;
			__builtin_trap();
		s = *rest;
		eatsp(&s);
		if (*s && *s != '=') {
			// boolean attr, no `="value"`
			new->attr.val = calloc(1, 1);
			continue;
		}
		if (!*s++)
			// goto err_freeattrs;
			__builtin_trap();
		eatsp(&s);
		if (!*s)
			// goto err_freeattrs;
			__builtin_trap();

		if (*s == '"' || *s == '\'') { // attr value is quoted
			char quot = *s++;

			char *valstart = s;
			while (*s && *s != quot)
				s++;
			size_t vlen = s - valstart;
			char *buf = malloc(vlen + 1);
			buf[vlen] = '\0';
			memcpy(buf, valstart, vlen);
			new->attr.val = buf;

			if (!*s++)
				// goto err_freeattrs;
				__builtin_trap();
		} else { // attr value is unquoted
			char *valstart = s;
			while (*s && !isspace(*s) && *s != '/' && *s != '>')
				s++;
			size_t vlen = s - valstart;
			char *buf = malloc(vlen + 1);
			buf[vlen] = '\0';
			memcpy(buf, valstart, vlen);
			new->attr.val = buf;
		}
		eatsp(&s);
	}
	int selfclose = is_selfclose(tagname);
	if (*s && *s == '/') {
		s++;
		selfclose = 1;
	}
	if (!*s || *s++ != '>')
		//goto err_freetag;
		__builtin_trap();

	out->kind = NODE_ELT;
	out->elt.tagname = tagname;
	out->elt.children = NULL;
	if (!selfclose) {
		// NOTE: we don't actually store the contents
		// of <script>s
		if (!strcasecmp(tagname, "script")) {
			while (*s && strncasecmp(s, "</script>", 8))
				s++;
		}

		struct nodelist **next = &out->elt.children;
		while (*s && strncmp("</", s, 2)) {
			struct nodelist *new = malloc(sizeof(struct nodelist));
			new->next = NULL;
			if (parse_node(s, rest, &new->node))
				//goto err_freechildren;
				__builtin_trap();
			s = *rest;
			*next = new;
			next = &new->next;
		}
		if (strncmp(s, "</", 2))
			//goto err_freechildren;
			__builtin_trap();
		s += strlen("</");
		eatsp(&s);
		if (strncasecmp(s, tagname, strlen(tagname)))
			//goto err_freechildren;
			__builtin_trap();
		s += strlen(tagname);
		if (*s++ != '>')
			//goto err_freechildren;
			__builtin_trap();
	}
	*rest = s;
	return 0;

err_freechildren:
	put_nodelist(out->elt.children);
err_freeattrs:
	put_attrlist(out->elt.attrs);
err_freetag:
	free(tagname);
	return -1;
}

static int
parse_node(char *s, char **rest, struct node *out) {
	*rest = s;
	if (!*s)
		//return -1;
		__builtin_trap();

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
		printf("%s#Element %s\n", indentstr, n->elt.tagname);
		struct attrlist *ai;
		for (ai = n->elt.attrs; ai != NULL; ai = ai->next) {
			printf(
				"%s  %s=\"%s\"\n",
				indentstr,
				ai->attr.name,
				ai->attr.val
			);
		}
		struct nodelist *ni;
		for (ni = n->elt.children; ni != NULL; ni = ni->next) {
			print_node(&ni->node, nindent + 1);
		}
	} else if (n->kind == NODE_WHITESPACE) {
		//printf("%s#Whitespace\n", indentstr);
	} else if (n->kind == NODE_COMMENT) {
		printf("%s#Comment \"%s\"\n", indentstr, n->comment.content);
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
	char *nodestart = buf;
	eatsp(&nodestart);
	// skip doctype
	if (!strncasecmp(nodestart, "<!doctype", 9)) {
		while (*nodestart && *nodestart++ != '>')
			;
	}

	struct node *htmlnode = new_node();
	char *rest;
	for (;;) {
		if (parse_node(nodestart, &rest, htmlnode))
			//goto err;
			__builtin_trap();
		nodestart = rest;
		if (htmlnode->kind == NODE_COMMENT || htmlnode->kind == NODE_WHITESPACE)
			put_node_inner(htmlnode);
		else
			break;
	}
	eatsp(&rest);
	if (*rest != '\0')
		fprintf(
			stdout,
			"OOPS: non-empty rest:\n%s\n==========\n",
			rest
		);

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
