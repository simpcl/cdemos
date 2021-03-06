
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define PHP_TAG_BUF_SIZE 1023

#define emalloc malloc
#define efree free
#define erealloc realloc
#define estrndup strndup

static char *php_strtolower(char *s, size_t len)
{
    unsigned char *c, *e;

    c = (unsigned char *)s;
    e = c+len;

    while (c < e) {
        *c = tolower(*c);
        c++;
    }
    return s;
}

static int php_tag_find(char *tag, int len, char *set)
{
	char c, *n, *t;
	int state=0, done=0;
	char *norm;

	if (len <= 0) {
		return 0;
	}

	norm = emalloc(len+1);

	n = norm;
	t = tag;
	c = tolower(*t);
	/*
	   normalize the tag removing leading and trailing whitespace
	   and turn any <a whatever...> into just <a> and any </tag>
	   into <tag>
	*/
	while (!done) {
		switch (c) {
			case '<':
				*(n++) = c;
				break;
			case '>':
				done =1;
				break;
			default:
				if (!isspace((int)c)) {
					if (state == 0) {
						state=1;
					}
					if (c != '/') {
						*(n++) = c;
					}
				} else {
					if (state == 1)
						done=1;
				}
				break;
		}
		c = tolower(*(++t));
	}
	*(n++) = '>';
	*n = '\0';
	if (strstr(set, norm)) {
		done=1;
	} else {
		done=0;
	}
	efree(norm);
	return done;
}

size_t php_strip_tags_ex(char *rbuf, int len, int *stateptr, char *allow, int allow_len, int allow_tag_spaces)
{
	char *tbuf, *buf, *p, *tp, *rp, c, lc;
	int br, i=0, depth=0, in_q = 0;
	int state = 0, pos;
	char *allow_free = NULL;

	if (stateptr)
		state = *stateptr;

	buf = estrndup(rbuf, len);
	c = *buf;
	lc = '\0';
	p = buf;
	rp = rbuf;
	br = 0;
	if (allow) {
		//if (IS_INTERNED(allow)) {
		//	allow_free = allow = zend_str_tolower_dup(allow, allow_len);
		//} else {
			allow_free = NULL;
			php_strtolower(allow, allow_len);
		//}
		tbuf = emalloc(PHP_TAG_BUF_SIZE + 1);
		tp = tbuf;
	} else {
		tbuf = tp = NULL;
	}

	while (i < len) {
		switch (c) {
			case '\0':
				break;
			case '<':
				if (in_q) {
					break;
				}
				if (isspace(*(p + 1)) && !allow_tag_spaces) {
					goto reg_char;
				}
				if (state == 0) {
					lc = '<';
					state = 1;
					if (allow) {
						if (tp - tbuf >= PHP_TAG_BUF_SIZE) {
							pos = tp - tbuf;
							tbuf = erealloc(tbuf, (tp - tbuf) + PHP_TAG_BUF_SIZE + 1);
							tp = tbuf + pos;
						}
						*(tp++) = '<';
				 	}
				} else if (state == 1) {
					depth++;
				}
				break;

			case '(':
				if (state == 2) {
					if (lc != '"' && lc != '\'') {
						lc = '(';
						br++;
					}
				} else if (allow && state == 1) {
					if (tp - tbuf >= PHP_TAG_BUF_SIZE) {
						pos = tp - tbuf;
						tbuf = erealloc(tbuf, (tp - tbuf) + PHP_TAG_BUF_SIZE + 1);
						tp = tbuf + pos;
					}
					*(tp++) = c;
				} else if (state == 0) {
					*(rp++) = c;
				}
				break;

			case ')':
				if (state == 2) {
					if (lc != '"' && lc != '\'') {
						lc = ')';
						br--;
					}
				} else if (allow && state == 1) {
					if (tp - tbuf >= PHP_TAG_BUF_SIZE) {
						pos = tp - tbuf;
						tbuf = erealloc(tbuf, (tp - tbuf) + PHP_TAG_BUF_SIZE + 1);
						tp = tbuf + pos;
					}
					*(tp++) = c;
				} else if (state == 0) {
					*(rp++) = c;
				}
				break;

			case '>':
				if (depth) {
					depth--;
					break;
				}

				if (in_q) {
					break;
				}

				switch (state) {
					case 1: /* HTML/XML */
						lc = '>';
						in_q = state = 0;
						if (allow) {
							if (tp - tbuf >= PHP_TAG_BUF_SIZE) {
								pos = tp - tbuf;
								tbuf = erealloc(tbuf, (tp - tbuf) + PHP_TAG_BUF_SIZE + 1);
								tp = tbuf + pos;
							}
							*(tp++) = '>';
							*tp='\0';
							if (php_tag_find(tbuf, tp-tbuf, allow)) {
								memcpy(rp, tbuf, tp-tbuf);
								rp += tp-tbuf;
							}
							tp = tbuf;
						}
						break;

					case 2: /* PHP */
						if (!br && lc != '\"' && *(p-1) == '?') {
							in_q = state = 0;
							tp = tbuf;
						}
						break;

					case 3:
						in_q = state = 0;
						tp = tbuf;
						break;

					case 4: /* JavaScript/CSS/etc... */
						if (p >= buf + 2 && *(p-1) == '-' && *(p-2) == '-') {
							in_q = state = 0;
							tp = tbuf;
						}
						break;

					default:
						*(rp++) = c;
						break;
				}
				break;

			case '"':
			case '\'':
				if (state == 4) {
					/* Inside <!-- comment --> */
					break;
				} else if (state == 2 && *(p-1) != '\\') {
					if (lc == c) {
						lc = '\0';
					} else if (lc != '\\') {
						lc = c;
					}
				} else if (state == 0) {
					*(rp++) = c;
				} else if (allow && state == 1) {
					if (tp - tbuf >= PHP_TAG_BUF_SIZE) {
						pos = tp - tbuf;
						tbuf = erealloc(tbuf, (tp - tbuf) + PHP_TAG_BUF_SIZE + 1);
						tp = tbuf + pos;
					}
					*(tp++) = c;
				}
				if (state && p != buf && (state == 1 || *(p-1) != '\\') && (!in_q || *p == in_q)) {
					if (in_q) {
						in_q = 0;
					} else {
						in_q = *p;
					}
				}
				break;

			case '!':
				/* JavaScript & Other HTML scripting languages */
				if (state == 1 && *(p-1) == '<') {
					state = 3;
					lc = c;
				} else {
					if (state == 0) {
						*(rp++) = c;
					} else if (allow && state == 1) {
						if (tp - tbuf >= PHP_TAG_BUF_SIZE) {
							pos = tp - tbuf;
							tbuf = erealloc(tbuf, (tp - tbuf) + PHP_TAG_BUF_SIZE + 1);
							tp = tbuf + pos;
						}
						*(tp++) = c;
					}
				}
				break;

			case '-':
				if (state == 3 && p >= buf + 2 && *(p-1) == '-' && *(p-2) == '!') {
					state = 4;
				} else {
					goto reg_char;
				}
				break;

			case '?':

				if (state == 1 && *(p-1) == '<') {
					br=0;
					state=2;
					break;
				}

			case 'E':
			case 'e':
				/* !DOCTYPE exception */
				if (state==3 && p > buf+6
						     && tolower(*(p-1)) == 'p'
					         && tolower(*(p-2)) == 'y'
						     && tolower(*(p-3)) == 't'
						     && tolower(*(p-4)) == 'c'
						     && tolower(*(p-5)) == 'o'
						     && tolower(*(p-6)) == 'd') {
					state = 1;
					break;
				}
				/* fall-through */

			case 'l':
			case 'L':

				/* swm: If we encounter '<?xml' then we shouldn't be in
				 * state == 2 (PHP). Switch back to HTML.
				 */

				if (state == 2 && p > buf+2 && strncasecmp(p-2, "xm", 2) == 0) {
					state = 1;
					break;
				}

				/* fall-through */
			default:
reg_char:
				if (state == 0) {
					*(rp++) = c;
				} else if (allow && state == 1) {
					if (tp - tbuf >= PHP_TAG_BUF_SIZE) {
						pos = tp - tbuf;
						tbuf = erealloc(tbuf, (tp - tbuf) + PHP_TAG_BUF_SIZE + 1);
						tp = tbuf + pos;
					}
					*(tp++) = c;
				}
				break;
		}
		c = *(++p);
		i++;
	}
	if (rp < rbuf + len) {
		*rp = '\0';
	}
	efree(buf);
	if (allow) {
		efree(tbuf);
		if (allow_free) {
			efree(allow_free);
		}
	}
	if (stateptr)
		*stateptr = state;

	return (size_t)(rp - rbuf);
}

