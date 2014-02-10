/*************************************************************************/
/* Copyright (c) 2004                                                    */
/* Daniel Sleator, David Temperley, and John Lafferty                    */
/* Copyright 2013 Linas Vepstas                                          */
/* All rights reserved                                                   */
/*                                                                       */
/* Use of the link grammar parsing system is subject to the terms of the */
/* license set forth in the LICENSE file included with this software,    */ 
/* and also available at http://www.link.cs.cmu.edu/link/license.html    */
/* This license allows free redistribution and use in source and binary  */
/* forms, with or without modification, subject to certain conditions.   */
/*                                                                       */
/*************************************************************************/


#include <stdarg.h>

#include "print.h"
#include "api-structures.h"
#include "corpus/corpus.h"
#include "idiom.h"
#include "print-util.h"
#include "string-set.h"
#include "utilities.h"
#include "word-utils.h"

#define LEFT_WALL_DISPLAY  ("LEFT-WALL")   /* the string to use to show the wall */
#define LEFT_WALL_SUPPRESS ("Wd") /* If this connector is used on the wall, */
                                  /* then suppress the display of the wall. */
#define RIGHT_WALL_DISPLAY  ("RIGHT-WALL") /* the string to use to show the wall */
#define RIGHT_WALL_SUPPRESS ("RW") /* If this connector is used on the wall, */

/* The Russian dictionary makes use of the empty word to deal with
 * the splitting of words into variable-length word-counts */
#define EMPTY_WORD ("=.zzz")   /* pure whitespace */
#define EMPTY_WORD_SUPPRESS ("ZZZ") /* link to pure whitespace */

#define SUFFIX_WORD ("=")      /* suffixes start with this */
#define SUFFIX_WORD_L 1        /* length of above */
#define SUFFIX_SUPPRESS ("LL") /* suffix links start with this */
#define SUFFIX_SUPPRESS_L 2    /* length of above */

#define HIDE_SUFFIX   (!display_suffixes)

/* XXX FIXME
 * There are several problems with the handling of suffixes here.
 * 1) Valid English constructions like "I think that 2 + 2 = 4" display
 *    bizarrely.  This needs fixing.
 * 2) Engish sentences like "this is a hey= =.zzz test" display
 *    unexpectely. (because the suffixes were contracted!)
 * In brief, the mechanism should be disabled for English.
 */

/**
 * Return TRUE if the word is a suffix.
 *
 * Suffixes have the form =asdf.asdf and "null" suffixes have the form =.asdf.
 * Ordinary equals signs appearing in regular text are either = or =[!].
 * In the English dict, equals signs appear as =.v and =.eq and are handled
 * specially below. This works only because these do not appear in the
 * Russian dict.  It will get nastier as more languages are added.
 * This is a bit of a mess, but I can't think of a better way ...
 * Perhaps a different character, perhaps some obscure UTF-8 marker?
 * At this time, suffixes are used only in the Russian dicts.
 */
static Boolean is_suffix(const char* w)
{
	if (0 != strncmp(SUFFIX_WORD, w, SUFFIX_WORD_L)) return FALSE;
	if (1 == strlen(w)) return FALSE;
	if (0 == strcmp("=[!]", w)) return FALSE;
	if (0 == strcmp("=.v", w)) return FALSE;
	if (0 == strcmp("=.eq", w)) return FALSE;
	return TRUE;
}

static void 
set_centers(const Linkage linkage, int center[],
            Boolean print_word_0, int N_words_to_print)
{
	int i, len, tot;
	Boolean display_suffixes = linkage->opts->display_suffixes;

	tot = 0;
	if (print_word_0) i=0; else i=1;
	for (; i < N_words_to_print; i++)
	{
		/* Ignore suffixes. */
		if (HIDE_SUFFIX && is_suffix(linkage->word[i]))
		{
			center[i] = tot;
			tot++;  // hack alert -- a trailing blank gets printed anyway ...
			continue;
		}

		/* Centers obtained by counting the characters, 
		 * not the bytes in the string.
		 * len = strlen(linkage->word[i]);
		 * len = mbsrtowcs(NULL, &linkage->word[i], 0, &mbss);
		 */
		len = utf8_strlen(linkage->word[i]);
		center[i] = tot + (len/2);
		tot += len+1;
	}
}

/* The following are all for generating postscript */
typedef struct
{
	int N_rows; /* N_rows -- the number of rows */
	/* tells the height of the links above the sentence */
	int link_heights[MAX_LINKS];
	/* the word beginning each row of the display */
	int row_starts[MAX_SENTENCE];
} ps_ctxt_t;

/**
 * prints s then prints the last |t|-|s| characters of t.
 * if s is longer than t, it truncates s.
 * multi-byte enabled.
 */
static void left_append_string(String * string, const char * s, const char * t)
{
	size_t i;
	size_t slen = strlen(s);
	size_t tlen = strlen(t);

	for (i = 0; i < tlen; )
	{
		if (i < slen)
		{
			i += append_utf8_char(string, &s[i]);
		}
		else
		{
			i += append_utf8_char(string, &t[i]);
		}
	}
}

static void print_a_link(String * s, const Linkage linkage, int link)
{
	Sentence sent = linkage_get_sentence(linkage);
	Dictionary dict = sent->dict;
	int l, r;
	const char *label, *llabel, *rlabel;
	
	l      = linkage_get_link_lword(linkage, link);
	r      = linkage_get_link_rword(linkage, link);
	label  = linkage_get_link_label(linkage, link);
	llabel = linkage_get_link_llabel(linkage, link);
	rlabel = linkage_get_link_rlabel(linkage, link);

	if ((l == 0) && dict->left_wall_defined) {
		left_append_string(s, LEFT_WALL_DISPLAY,"               ");
	} else if ((l == (linkage_get_num_words(linkage)-1)) && dict->right_wall_defined) {
		left_append_string(s, RIGHT_WALL_DISPLAY,"               ");	
	} else {
		left_append_string(s, linkage_get_word(linkage, l), "               ");
	}
	left_append_string(s, llabel, "     ");
	append_string(s, "   <---"); 
	left_append_string(s, label, "-----");
	append_string(s, "->  "); 
	left_append_string(s, rlabel, "     ");
	append_string(s, "     %s\n", linkage_get_word(linkage, r));
}


/**
 * To the left of each link, print the sequence of domains it is in.
 * Printing a domain means printing its type.
 * Takes info from pp_link_array and pp and chosen_words.
 */    
char * linkage_print_links_and_domains(const Linkage linkage)
{
	int link, longest, j;
	int N_links = linkage_get_num_links(linkage);
	String * s = string_new();
	char * links_string;
	const char ** dname;

	longest = 0;
	for (link=0; link<N_links; link++) {
		if (linkage_get_link_lword(linkage, link) == -1) continue;
		if (linkage_get_link_num_domains(linkage, link) > longest) 
			longest = linkage_get_link_num_domains(linkage, link);
	}
	for (link=0; link<N_links; link++) {
		if (linkage_get_link_lword(linkage, link) == -1) continue;
		dname = linkage_get_link_domain_names(linkage, link);
		for (j=0; j<linkage_get_link_num_domains(linkage, link); ++j) {
			append_string(s, " (%s)", dname[j]);
		}
		for (;j<longest; j++) {
			append_string(s, "    ");
		}
		append_string(s, "   ");
		print_a_link(s, linkage, link);
	}
	append_string(s, "\n");
	if (linkage_get_violation_name(linkage) != NULL) {
		append_string(s, "P.P. violations:\n");
		append_string(s, "        %s\n\n", linkage_get_violation_name(linkage));
	}
	
	links_string = string_copy(s);
	string_delete(s);
	return links_string; 
}

char * linkage_print_senses(Linkage linkage)
{
	String * s = string_new();
	char * sense_string;
#ifdef USE_CORPUS
	Linkage_info *lifo = linkage->info;
	Sense *sns;
	size_t w, nwords;

	lg_corpus_linkage_senses(linkage);

	nwords = lifo->nwords;
	for (w=0; w<nwords; w++)
	{
		sns = lg_get_word_sense(lifo, w);
		while (sns)
		{
			int idx = lg_sense_get_index(sns);
			const char * wd = lg_sense_get_subscripted_word(sns);
			const char * dj = lg_sense_get_disjunct(sns);
			const char * sense = lg_sense_get_sense(sns);
			double score = lg_sense_get_score(sns);
			append_string(s, "%d %s dj=%s sense=%s score=%f\n",
			              idx, wd, dj, sense, score);
			sns = lg_sense_next(sns);
		}
	}

#else
	append_string(s, "Corpus statstics is not enabled in this version\n");
#endif
	sense_string = string_copy(s);
	string_delete(s);
	return sense_string;
}

char * linkage_print_disjuncts(const Linkage linkage)
{
#ifdef USE_CORPUS
	double score;
#endif
	double cost;
	const char * infword;
	const char * dj;
	char * djs;
	int w;
	String * s = string_new();
	Sentence sent = linkage->sent;
	int nwords = sent->length;

	/* Decrement nwords, so as to ignore the RIGHT-WALL */
	nwords --;

	/* Loop over each word in the sentence (skipping LEFT-WALL, which is
	 * word 0. */
	for (w=1; w<nwords; w++)
	{
		Disjunct *disj = linkage->sent->parse_info->chosen_disjuncts[w];
		if (NULL == disj) continue;

		infword = disj->string;

		dj = linkage_get_disjunct_str(linkage, w);
		cost = linkage_get_disjunct_cost(linkage, w);

#ifdef USE_CORPUS
		score = linkage_get_disjunct_corpus_score(linkage, w);
		append_string(s, "%21s    %5.1f %6.3f %s\n", infword, cost, score, dj);
#else
		append_string(s, "%21s    %5.1f  %s\n", infword, cost, dj);
#endif
	}
	djs = string_copy(s);
	string_delete(s);
	return djs;
}

/**
 * XXX TODO: port over the suppression of ZZZ links, and also
 * the HIDE_SUFFIX stuff, fom the ascii printing code, way below.
 */
static char * build_linkage_postscript_string(const Linkage linkage, ps_ctxt_t *pctx)
{
	int link, i,j;
	int d;
	int print_word_0 = 0, print_word_N = 0, N_wall_connectors, suppressor_used;
#ifdef USE_FAT_LINKAGES
	Sublinkage *sublinkage = &(linkage->sublinkage[linkage->current]);
#else
	Sublinkage *sublinkage = &(linkage->sublinkage);
#endif /* USE_FAT_LINKAGES */
	int N_links = sublinkage->num_links;
	Link **ppla = sublinkage->link;
	String  * string;
	char * ps_string;
	Dictionary dict = linkage->sent->dict;
	Parse_Options opts = linkage->opts;
	int N_words_to_print;

	string = string_new();

	N_wall_connectors = 0;
	if (dict->left_wall_defined) {
		suppressor_used = FALSE;
		if (!opts->display_walls) 
			for (j=0; j<N_links; j++) {
				if (ppla[j]->l == 0) {
					if (ppla[j]->r == linkage->num_words-1) continue;
					N_wall_connectors ++;
					if (strcmp(ppla[j]->lc->string, LEFT_WALL_SUPPRESS)==0) {
						suppressor_used = TRUE;
					}
				}
			}
		print_word_0 = (((!suppressor_used) && (N_wall_connectors != 0)) 
						|| (N_wall_connectors > 1) || opts->display_walls);
	} else {
		print_word_0 = TRUE;
	}

	N_wall_connectors = 0;
	if (dict->right_wall_defined) {
		suppressor_used = FALSE;
		for (j=0; j<N_links; j++) {
			if (ppla[j]->r == linkage->num_words-1) {
				N_wall_connectors ++;
				if (strcmp(ppla[j]->lc->string, RIGHT_WALL_SUPPRESS)==0){
					suppressor_used = TRUE;
				}
			}
		}
		print_word_N = (((!suppressor_used) && (N_wall_connectors != 0)) 
						|| (N_wall_connectors > 1) || opts->display_walls);
	} 
	else {
		print_word_N = TRUE;
	}

	if (print_word_0) d=0; else d=1;

	i = 0;
	N_words_to_print = linkage->num_words;
	if (!print_word_N) N_words_to_print--;

	append_string(string, "[");
	for (j=d; j<N_words_to_print; j++) {
		if ((i%10 == 0) && (i>0)) append_string(string, "\n");
		i++;
		append_string(string, "(%s)", linkage->word[j]);
	}
	append_string(string,"]");
	append_string(string,"\n");

	append_string(string,"[");
	j = 0;
	for (link=0; link<N_links; link++) {
		if (!print_word_0 && (ppla[link]->l == 0)) continue;
		if (!print_word_N && (ppla[link]->r == linkage->num_words-1)) continue;
		if (ppla[link]->l == -1) continue;
		if ((j%7 == 0) && (j>0)) append_string(string,"\n");
		j++;
		append_string(string,"[%d %d %d",
				ppla[link]->l-d, ppla[link]->r-d, 
				pctx->link_heights[link]);
#ifdef USE_FAT_LINKAGES
		if (ppla[link]->lc->label < 0) {
			append_string(string," (%s)]", ppla[link]->name);
		} else {
			append_string(string," ()]");
		}
#else
		append_string(string," (%s)]", ppla[link]->name);
#endif /* USE_FAT_LINKAGES */

	}
	append_string(string,"]");
	append_string(string,"\n");
	append_string(string,"[");
	for (j=0; j < pctx->N_rows; j++ )
	{
		if (j>0) append_string(string, " %d", pctx->row_starts[j]);
		else append_string(string,"%d", pctx->row_starts[j]);
	}
	append_string(string,"]\n");

	ps_string = string_copy(string);
	string_delete(string);
	return ps_string; 
}

/**
 * This takes the current chosen_disjuncts array and uses it to
 * compute the chosen_words array.  "I.xx" suffixes are eliminated.
 */
void compute_chosen_words(Sentence sent, Linkage linkage)
{
	int i, l;
	char * s, *u;
	Parse_info pi = sent->parse_info;
	const char * chosen_words[MAX_SENTENCE];
	Parse_Options opts = linkage->opts;
	Boolean display_suffixes = opts->display_suffixes;

	for (i=0; i<sent->length; i++)
	{
		const char *t;
		/* If chosen_disjuncts is NULL, then this is an 'island' word
		 * that has not been linked to. */
		if (pi->chosen_disjuncts[i] == NULL)
		{
			/* The unsplit_word is the original word; if its been split
			 * into stem+suffix, and either one hasn't been choosen, then
			 * neither should be printed.  Do, however, put brackets around
			 * the original word, and print that.
			 */
			t = sent->word[i].unsplit_word;
			if (t)
			{
				l = strlen(t) + 2;
				s = (char *) xalloc(l+1);
				sprintf(s, "[%s]", t);
				t = string_set_add(s, sent->string_set);
				xfree(s, l+1);
			}
			else
				t = string_set_add("", sent->string_set);
		}
		else if (opts->display_word_subscripts)
		{
			t = pi->chosen_disjuncts[i]->string;        
			/* get rid of those ugly ".Ixx" */
			if (is_idiom_word(t)) {
				s = strdup(t);
				u = strrchr(s, SUBSCRIPT_MARK);
				*u = '\0';
				t = string_set_add(s, sent->string_set);
				free(s);
			}

			/* Suppress the empty word. */
			if (0 == strcmp(t, EMPTY_WORD))
			{
				t = string_set_add("", sent->string_set);
			}

			/* Concatenate the stem and the suffix together into one word */
			if (HIDE_SUFFIX)
			{
				if (is_suffix(t) && pi->chosen_disjuncts[i-1])
				{
					const char * stem = pi->chosen_disjuncts[i-1]->string;
					size_t len = strlen(stem) + strlen (t);
					char * join = (char *)malloc(len+1);
					strcpy(join, stem);
					u = strrchr(join, SUBSCRIPT_MARK);

					/* u can be null, if the the sentence happens to have
					 * an equals sign in it, for other reasons. */
					if (u)
					{
						*u = '\0';
						strcat(join, t + SUFFIX_WORD_L);
						t = string_set_add(join, sent->string_set);
					}
					free(join);
				}

				/* Suppress printing of the stem, if the next word is the suffix */
				if ((i+1 < sent->length) &&
				    (pi->chosen_disjuncts[i+1]))
				{
					const char * next = pi->chosen_disjuncts[i+1]->string;
					if (is_suffix(next) && 0 != strcmp(next, EMPTY_WORD))
					{
						t = string_set_add("", sent->string_set);
					}
				}
			}
		}
		else
		{
			/* XXX This is wrong, since it fails to indicate what
			 * was actally used for the parse, which might not actually 
			 * be alternative 0.  We should do like the above, and then
			 * manually strip the subscript.
			 * Except that this code is never ever reached, because 
			 * opts->display_word_subscripts is always true...
			 */
			t = sent->word[i].alternatives[0];
		}
		chosen_words[i] = t;
	}

	if (sent->dict->left_wall_defined) {
		chosen_words[0] = LEFT_WALL_DISPLAY;
	}
	if (sent->dict->right_wall_defined) {
		chosen_words[sent->length-1] = RIGHT_WALL_DISPLAY;
	}
	for (i=0; i<linkage->num_words; ++i) {
		s = (char *) exalloc(strlen(chosen_words[i])+1);
		strcpy(s, chosen_words[i]);
		linkage->word[i] = s;
	}
}


#define MAX_HEIGHT 30

/** 
 * Print the indicated linkage into a utf8-diagram.
 * Works fine for general utf8 multi-byte sentences.
 * Links and connectors are still mostly assumed to be ASCII, though;
 * to fix this, grep for "strlen" in the code below, replace by utf8 len.
 *
 * Returned string is allocated with exalloc.  
 * Needs to be freed with linkage_free_diagram()
 */
static char * linkage_print_diagram_ctxt(const Linkage linkage, ps_ctxt_t *pctx)
{
	unsigned int i, j, k, cl, cr, row, top_row, top_row_p1;
	const char *s;
	char *t;
	Boolean print_word_0 = 0, print_word_N = 0;
	int N_wall_connectors, suppressor_used;
	int center[MAX_SENTENCE];
	char connector[MAX_TOKEN_LENGTH];
	unsigned int line_len, link_length;
#ifdef USE_FAT_LINKAGES
	Sublinkage *sublinkage = &(linkage->sublinkage[linkage->current]);
#else
	Sublinkage *sublinkage = &(linkage->sublinkage);
#endif /* USE_FAT_LINKAGES */
	unsigned int N_links = sublinkage->num_links;
	Link **ppla = sublinkage->link;
	String * string;
	char * gr_string;
	Dictionary dict = linkage->sent->dict;
	Parse_Options opts = linkage->opts;
	size_t x_screen_width = parse_options_get_screen_width(opts);
	unsigned int N_words_to_print;

	char picture[MAX_HEIGHT][MAX_LINE];
	char xpicture[MAX_HEIGHT][MAX_LINE];
	size_t start[MAX_HEIGHT];

	Boolean display_suffixes = opts->display_suffixes;

	string = string_new();

	/* Do we want to print the left wall? */
	N_wall_connectors = 0;
	if (dict->left_wall_defined)
	{
		suppressor_used = FALSE;
		if (!opts->display_walls) 
		{
			for (j=0; j<N_links; j++)
			{
				if (0 == ppla[j]->l)
				{
					if (ppla[j]->r == linkage->num_words-1) continue;
					N_wall_connectors ++;
					if (0 == strcmp(ppla[j]->lc->string, LEFT_WALL_SUPPRESS))
					{
						suppressor_used = TRUE;
					}
				}
			}
		}
		print_word_0 = (((!suppressor_used) && (N_wall_connectors != 0)) 
						|| (N_wall_connectors > 1) || opts->display_walls);
	} 
	else
	{
		print_word_0 = TRUE;
	}

	/* Do we want to print the right wall? */
	N_wall_connectors = 0;
	if (dict->right_wall_defined)
	{
		suppressor_used = FALSE;
		for (j=0; j<N_links; j++)
		{
			if (ppla[j]->r == linkage->num_words-1)
			{
				N_wall_connectors ++;
				if (0 == strcmp(ppla[j]->lc->string, RIGHT_WALL_SUPPRESS))
				{
					suppressor_used = TRUE;
				}
			}
		}
		print_word_N = (((!suppressor_used) && (N_wall_connectors != 0)) 
						|| (N_wall_connectors > 1) || opts->display_walls);
	} 
	else
	{
		print_word_N = TRUE;
	}

	N_words_to_print = linkage->num_words;
	if (!print_word_N) N_words_to_print--;

	set_centers(linkage, center, print_word_0, N_words_to_print);
	line_len = center[N_words_to_print-1]+1;
	
	for (k=0; k<MAX_HEIGHT; k++) {
		for (j=0; j<line_len; j++) picture[k][j] = ' ';
		picture[k][line_len] = '\0';
	}
	top_row = 0;
	
	for (link_length = 1; link_length < N_words_to_print; link_length++) {
		for (j=0; j<N_links; j++) {
			if (ppla[j]->l == -1) continue;
			if (((unsigned int) (ppla[j]->r - ppla[j]->l)) != link_length)
			  continue;
			if (!print_word_0 && (ppla[j]->l == 0)) continue;
			/* gets rid of the irrelevant link to the left wall */
			if (!print_word_N && (ppla[j]->r == linkage->num_words-1)) continue;            
			/* Get rid of links to empty words */
			if (0 == strcmp(ppla[j]->name, EMPTY_WORD_SUPPRESS)) continue;

			if (HIDE_SUFFIX &&
			    0 == strncmp(ppla[j]->name, SUFFIX_SUPPRESS, SUFFIX_SUPPRESS_L))
				continue;

			/* gets rid of the irrelevant link to the right wall */
			/* ??? */

			/* put it into the lowest position */
			cl = center[ppla[j]->l];
			cr = center[ppla[j]->r];
			for (row=0; row < MAX_HEIGHT; row++) {
				for (k=cl+1; k<cr; k++) {
					if (picture[row][k] != ' ') break;
				}
				if (k == cr) break;
			}
			/* we know it fits, so put it in this row */

			pctx->link_heights[j] = row;
			
			if (2*row+2 > MAX_HEIGHT-1) {
				append_string(string, "The diagram is too high.\n");
				gr_string = string_copy(string);
				string_delete(string);
				return gr_string; 
			}
			if (row > top_row) top_row = row;
			
			picture[row][cl] = '+';
			picture[row][cr] = '+';
			for (k=cl+1; k<cr; k++) {
				picture[row][k] = '-';
			}
			s = ppla[j]->name;
			
			/* display_link_subscripts is always true ... hard-coded */
			if (opts->display_link_subscripts)
			{
#ifdef USE_FAT_LINKAGES
				if (!is_utf8_alpha(s))
				  s = "";  /* Don't print fat link connector name */
#endif /* USE_FAT_LINKAGES */
			}
			else
			{
				if (!is_utf8_upper(s)) {
				  s = "";
				}
			}
			strncpy(connector, s, MAX_TOKEN_LENGTH-1);
			connector[MAX_TOKEN_LENGTH-1] = '\0';
			k=0;
			if (opts->display_link_subscripts)
				k = strlen(connector);
			else
				for (t = connector; isupper((int)*t); t++) k++; /* uppercase len of conn*/
			if ((cl+cr-k)/2 + 1 <= cl) {
				t = picture[row] + cl + 1;
			} else {
				t = picture[row] + (cl + cr + 2 - k)/2;
			}
			s = connector;
			if (opts->display_link_subscripts)
				while ((*s != '\0') && (*t == '-')) *t++ = *s++; 
			else
				while (isupper((int)*s) && (*t == '-')) *t++ = *s++; 

			/* now put in the | below this one, where needed */
			for (k=0; k<row; k++) {
				if (picture[k][cl] == ' ') {
					picture[k][cl] = '|';
				}
				if (picture[k][cr] == ' ') {
					picture[k][cr] = '|';
				}
			}
		}
	}
	
	/* we have the link picture, now put in the words and extra "|"s */
	
	t = xpicture[0];
	if (print_word_0) k = 0; else k = 1;
	for (; k<N_words_to_print; k++) {
		s = linkage->word[k];
		i=0;
		while(*s != '\0') {
			*t++ = *s++;
			i++;
		}
		*t++ = ' ';
	}
	*t = '\0';
	
	if (opts->display_short) {
		for (k=0; picture[0][k] != '\0'; k++) {
			if ((picture[0][k] == '+') || (picture[0][k] == '|')) {
				xpicture[1][k] = '|';
			} else {
				xpicture[1][k] = ' ';
			}
		}
		xpicture[1][k] = '\0';
		for (row=0; row < top_row+1; row++) {
			strcpy(xpicture[row+2],picture[row]);
		}
		top_row += 2;
	} else {
		for (row=0; row < top_row+1; row++) {
			strcpy(xpicture[2*row+2],picture[row]);
			for (k=0; picture[row][k] != '\0'; k++) {
				if ((picture[row][k] == '+') || (picture[row][k] == '|')) {
					xpicture[2*row+1][k] = '|';
				} else {
					xpicture[2*row+1][k] = ' ';
				}
			}
			xpicture[2*row+1][k] = '\0';
		}
		top_row = 2*top_row + 2;
	}
	
	/* we've built the picture, now print it out */
	
	if (print_word_0) i = 0; else i = 1;

	/* start locations, for each row.  These may vary, due to different
	 * utf8 character widths */
	top_row_p1 = top_row + 1;
	for (row = 0; row < top_row_p1; row++)
		start[row] = 0;
	pctx->N_rows = 0;
	pctx->row_starts[pctx->N_rows] = 0;
	pctx->N_rows++;
	while (i < N_words_to_print)
	{
		unsigned int revrs;
		/* Count number of multi-byte chars in the words,
		 * up to the max screen width. */
		unsigned int uwidth = 0;
		do {
			uwidth += utf8_strlen(linkage->word[i]) + 1;
			i++;
		} while ((i < N_words_to_print) &&
			  (uwidth + utf8_strlen(linkage->word[i]) + 1 < x_screen_width));

		pctx->row_starts[pctx->N_rows] = i - (!print_word_0);    /* PS junk */
		if (i < N_words_to_print) pctx->N_rows++;     /* same */

		append_string(string, "\n");
		top_row_p1 = top_row + 1;
		for (revrs = 0; revrs < top_row_p1; revrs++)
		{
			/* print each row of the picture */
			/* 'blank' is used solely to detect blank lines */
			unsigned int mbcnt = 0;
			Boolean blank = TRUE;

			row = top_row - revrs;
			k = start[row];
			for (j = k; (mbcnt < uwidth) && (xpicture[row][j] != '\0'); )
			{
				size_t n = utf8_next(&xpicture[row][j]);
				blank = blank && (xpicture[row][j] == ' ');
				j += n;
				mbcnt ++;
			}
			start[row] = j;

			if (!blank)
			{
				mbcnt = 0;
				for (j = k; (mbcnt < uwidth) && (xpicture[row][j] != '\0'); )
				{
					/* Copy exactly one multi-byte character to buf */
					j += append_utf8_char(string, &xpicture[row][j]); 
					mbcnt ++;
				}
				append_string(string, "\n");
			}
		}
		append_string(string, "\n");
	}
	gr_string = string_copy(string);
	string_delete(string);
	return gr_string; 
}

/** 
 * Print the indicated linkage as utf8-art intp the given string.
 * The width of the diagram is given by the terminal width, taken
 * from the parse options.
 *
 * The returned string is malloced, and needs to be freed with
 * linkage_free_diagram()
 */
char * linkage_print_diagram(const Linkage linkage)
{
	ps_ctxt_t ctx;
	if (!linkage) return NULL;
	return linkage_print_diagram_ctxt(linkage, &ctx);
}

void linkage_free_diagram(char * s)
{
	exfree(s, strlen(s)+1);
}

void linkage_free_disjuncts(char * s)
{
	exfree(s, strlen(s)+1);
}

void linkage_free_links_and_domains(char * s)
{
	exfree(s, strlen(s)+1);
}

void linkage_free_senses(char * s)
{
	exfree(s, strlen(s)+1);
}

/* Forward declarations, the gunk is at the bottom. */
static const char * trailer(int mode);
static const char * header(int mode);

char * linkage_print_postscript(Linkage linkage, int mode)
{
	char * ps, * qs;
	int size;

	/* call the ascii printer to initialize the row size stuff. */
	ps_ctxt_t ctx;
	char * ascii = linkage_print_diagram_ctxt(linkage, &ctx);
	linkage_free_diagram(ascii);

	ps = build_linkage_postscript_string(linkage, &ctx);
	size = strlen(header(mode)) + strlen(ps) + strlen(trailer(mode)) + 1;
	
	qs = (char *) exalloc(sizeof(char)*size);
	sprintf(qs, "%s%s%s", header(mode), ps, trailer(mode));
	exfree(ps, strlen(ps)+1);
	 
	return qs;
}

void linkage_free_postscript(char * s)
{
		exfree(s, strlen(s)+1);
}

void print_disjunct_counts(Sentence sent)
{
	int i;
	int c;
	Disjunct *d;
	for (i=0; i<sent->length; i++) {
		c = 0;
		for (d=sent->word[i].d; d != NULL; d = d->next) {
			c++;
		}
		/* XXX alternatives[0] is not really correct, here .. */
		printf("%s(%d) ",sent->word[i].alternatives[0], c);
	}
	printf("\n\n");
}

void print_expression_sizes(Sentence sent)
{
	X_node * x;
	int w, size;
	for (w=0; w<sent->length; w++) {
		size = 0;
		for (x=sent->word[w].x; x!=NULL; x = x->next) {
			size += size_of_expression(x->exp);
		}
		/* XXX alternatives[0] is not really correct, here .. */
		printf("%s[%d] ",sent->word[w].alternatives[0], size);
	}
	printf("\n\n");
}

static const char * trailer(int mode)
{
	static const char * trailer_string=
		"diagram\n"
		"\n"
		"%%EndDocument\n"
		;

	if (mode==1) return trailer_string;
	else return "";
}

static const char * header(int mode)
{
	static const char * header_string=
		"%!PS-Adobe-2.0 EPSF-1.2\n"
		"%%Pages: 1\n"
		"%%BoundingBox: 0 -20 500 200\n"
		"%%EndComments\n"
		"%%BeginDocument: \n"
		"\n"
		"% compute size of diagram by adding\n"
		"% #rows x 8.5\n"
		"% (#rows -1) x 10\n"
		"% \\sum maxheight x 10\n"
		"/nulllink () def                     % The symbol of a null link\n"
		"/wordfontsize 11 def      % the size of the word font\n"
		"/labelfontsize 9 def      % the size of the connector label font\n"
		"/ex 10 def  % the horizontal radius of all the links\n"
		"/ey 10 def  % the height of the level 0 links\n"
		"/ed 10 def  % amount to add to this height per level\n"
		"/radius 10 def % radius for rounded arcs\n"
		"/row-spacing 10 def % the space between successive rows of the diagram\n"
		"\n"
		"/gap wordfontsize .5 mul def  % the gap between words\n"
		"/top-of-words wordfontsize .85 mul def\n"
		"             % the delta y above where the text is written where\n"
		"             % the major axis of the ellipse is located\n"
		"/label-gap labelfontsize .1 mul def\n"
		"\n"
		"/xwordfontsize 10 def      % the size of the word font\n"
		"/xlabelfontsize 10 def      % the size of the connector label font\n"
		"/xex 10 def  % the horizontal radius of all the links\n"
		"/xey 10 def  % the height of the level 0 links\n"
		"/xed 10 def  % amount to add to this height per level\n"
		"/xradius 10 def % radius for rounded arcs\n"
		"/xrow-spacing 10 def % the space between successive rows of the diagram\n"
		"/xgap wordfontsize .5 mul def  % the gap between words\n"
		"\n"
		"/centerpage 6.5 72 mul 2 div def\n"
		"  % this number of points from the left margin is the center of page\n"
		"\n"
		"/rightpage 6.5 72 mul def\n"
		"  % number of points from the left margin is the the right margin\n"
		"\n"
		"/show-string-centered-dict 5 dict def\n"
		"\n"
		"/show-string-centered {\n"
		"  show-string-centered-dict begin\n"
		"  /string exch def\n"
		"  /ycenter exch def\n"
		"  /xcenter exch def\n"
		"  xcenter string stringwidth pop 2 div sub\n"
		"  ycenter labelfontsize .3 mul sub\n"
		"  moveto\n"
		"  string show\n"
		"  end\n"
		"} def\n"
		"\n"
		"/clear-word-box {\n"
		"  show-string-centered-dict begin\n"
		"  /string exch def\n"
		"  /ycenter exch def\n"
		"  /xcenter exch def\n"
		"  newpath\n"
		"  /urx string stringwidth pop 2 div def\n"
		"  /ury labelfontsize .3 mul def\n"
		"  xcenter urx sub ycenter ury sub moveto\n"
		"  xcenter urx add ycenter ury sub lineto\n"
		"  xcenter urx add ycenter ury add lineto\n"
		"  xcenter urx sub ycenter ury add lineto\n"
		"  closepath\n"
		"  1 setgray fill\n"
		"  0 setgray\n"
		"  end\n"
		"} def\n"
		"\n"
		"/diagram-sentence-dict 20 dict def\n"
		"\n"
		"/diagram-sentence-circle\n"
		"{diagram-sentence-dict begin  \n"
		"   /links exch def\n"
		"   /words exch def\n"
		"   /n words length def\n"
		"   /Times-Roman findfont wordfontsize scalefont setfont\n"
		"   /x 0 def\n"
		"   /y 0 def\n"
		"\n"
		"   /left-ends [x dup words {stringwidth pop add gap add dup}\n"
		"	                     forall pop pop] def\n"
		"   /right-ends [x words {stringwidth pop add dup gap add} forall pop] def\n"
		"   /centers [0 1 n 1 sub {/i exch def\n"
		"		      left-ends i get\n"
		"		      right-ends i get\n"
		"		      add 2 div\n"
		"		    } for ] def\n"
		"\n"
		"   x y moveto\n"
		"   words {show gap 0 rmoveto} forall\n"
		"\n"
		"   .5 setlinewidth \n"
		"\n"
		"   links {dup 0 get /leftword exch def\n"
		"          dup 1 get /rightword exch def\n"
		"          dup 2 get /level exch def\n"
		"          3 get /string exch def\n"
		"          newpath\n"
		"          string nulllink eq {[2] 1 setdash}{[] 0 setdash} ifelse\n"
		"%          string nulllink eq {.8 setgray}{0 setgray} ifelse\n"
		"          centers leftword get\n"
		"	  y top-of-words add\n"
		"          moveto\n"
		"      \n"
		"          centers rightword get\n"
		"          centers leftword get\n"
		"          sub 2  div dup\n"
		"          radius \n"
		"          lt {/radiusx exch def}{pop /radiusx radius def} ifelse\n"
		"  \n"
		"          \n"
		" \n"
		"          centers leftword get\n"
		"	  y top-of-words add ey ed level mul add add\n"
		"          centers rightword get\n"
		"	  y top-of-words add ey ed level mul add add\n"
		"	  radiusx\n"
		"          arcto\n"
		"          4 {pop} repeat\n"
		"	  centers rightword get\n"
		"          y top-of-words add ey ed level mul add add\n"
		"	  centers rightword get\n"
		"	  y top-of-words add\n"
		"	  radiusx\n"
		"	  arcto\n"
		"          4 {pop} repeat\n"
		"	  centers rightword get\n"
		"	  y top-of-words add\n"
		"	  lineto\n"
		"\n"
		"	  stroke\n"
		"\n"
		"          /radius-y    ey ed level mul add	  def\n"
		"\n"
		"	  /center-arc-x\n"
		"	     centers leftword get centers rightword get add 2 div\n"
		"	  def\n"
		"	  \n"
		"          /center-arc-y\n"
		"             y top-of-words radius-y add add\n"
		"	  def\n"
		"\n"
		"          /Courier-Bold findfont labelfontsize scalefont setfont \n"
		"	  center-arc-x center-arc-y string clear-word-box\n"
		"	  center-arc-x center-arc-y string show-string-centered\n"
		"          } forall\n"
		"	  end\n"
		"  } def\n"
		"\n"
		"/diagramdict 20 dict def\n"
		"\n"
		"/diagram\n"
		"{diagramdict begin\n"
		"   /break-words exch def\n"
		"   /links exch def\n"
		"   /words exch def\n"
		"   /n words length def\n"
		"   /n-rows break-words length def\n"
		"   /Times-Roman findfont wordfontsize scalefont setfont\n"
		"\n"
		"   /left-ends [0 dup words {stringwidth pop add gap add dup}\n"
		"	                     forall pop pop] def\n"
		"   /right-ends [0 words {stringwidth pop add dup gap add} forall pop] def\n"
		"\n"
		"   /lwindows [ break-words {left-ends exch get gap 2 div sub } forall ] def\n"
		"   /rwindows [1 1 n-rows 1 sub {/i exch def\n"
		"		      lwindows i get } for\n"
		"	              right-ends n 1 sub get gap 2 div add\n"
		"	      ] def\n"
		"\n"
		"\n"
		"    /max 0 def\n"
		"    0 1 links length 1 sub {\n"
		"	/i exch def\n"
		"	/t links i get 2 get def\n"
		"	t max gt {/max t def} if\n"
		"      } for\n"
		"\n"
		"    /max-height ed max mul ey add top-of-words add row-spacing add def\n"
		"    /total-height n-rows max-height mul row-spacing sub def\n"
		"\n"
		"    /max-width 0 def            % compute the widest window\n"
		"    0 1 n-rows 1 sub {\n"
		"        /i exch def\n"
		"        /t rwindows i get lwindows i get sub def\n"
		"        t max-width gt {/max-width t def} if\n"
		"      } for\n"
		"\n"
		"    centerpage max-width 2 div sub 0 translate  % centers it\n"
		"   % rightpage max-width sub 0 translate      % right justified\n"
		"                        % Delete both of these to make it left justified\n"
		"\n"
		"   n-rows 1 sub -1 0\n"
		"     {/i exch def\n"
		"	gsave\n"
		"	newpath\n"
		"        %/centering centerpage rwindows i get lwindows i get sub 2 div sub def\n"
		"               % this line causes each row to be centered\n"
		"        /centering 0 def\n"
		"               % set centering to 0 to prevent centering of each row \n"
		"\n"
		"	centering -100 moveto  % -100 because some letters go below zero\n"
		"        centering max-height n-rows mul lineto\n"
		"        rwindows i get lwindows i get sub centering add\n"
		"                       max-height n-rows mul lineto\n"
		"        rwindows i get lwindows i get sub centering add\n"
		"                       -100 lineto\n"
		"	closepath\n"
		"        clip\n"
		"	lwindows i get neg n-rows i sub 1 sub max-height mul translate\n"
		"        centerpage centering 0 translate\n"
		"        words links diagram-sentence-circle\n"
		"	grestore\n"
		"     } for\n"
		"     end\n"
		"} def \n"
		"\n"
		"/diagramx\n"
		"{diagramdict begin\n"
		"   /break-words exch def\n"
		"   /links exch def\n"
		"   /words exch def\n"
		"   /n words length def\n"
		"   /n-rows break-words length def\n"
		"   /Times-Roman findfont xwordfontsize scalefont setfont\n"
		"\n"
		"   /left-ends [0 dup words {stringwidth pop add gap add dup}\n"
		"	                     forall pop pop] def\n"
		"   /right-ends [0 words {stringwidth pop add dup gap add} forall pop] def\n"
		"\n"
		"   /lwindows [ break-words {left-ends exch get gap 2 div sub } forall ] def\n"
		"   /rwindows [1 1 n-rows 1 sub {/i exch def\n"
		"		      lwindows i get } for\n"
		"	              right-ends n 1 sub get xgap 2 div add\n"
		"	      ] def\n"
		"\n"
		"\n"
		"    /max 0 def\n"
		"    0 1 links length 1 sub {\n"
		"	/i exch def\n"
		"	/t links i get 2 get def\n"
		"	t max gt {/max t def} if\n"
		"      } for\n"
		"\n"
		"    /max-height xed max mul xey add top-of-words add xrow-spacing add def\n"
		"    /total-height n-rows max-height mul xrow-spacing sub def\n"
		"\n"
		"    /max-width 0 def            % compute the widest window\n"
		"    0 1 n-rows 1 sub {\n"
		"        /i exch def\n"
		"        /t rwindows i get lwindows i get sub def\n"
		"        t max-width gt {/max-width t def} if\n"
		"      } for\n"
		"\n"
		"    centerpage max-width 2 div sub 0 translate  % centers it\n"
		"   % rightpage max-width sub 0 translate      % right justified\n"
		"                        % Delete both of these to make it left justified\n"
		"\n"
		"   n-rows 1 sub -1 0\n"
		"     {/i exch def\n"
		"	gsave\n"
		"	newpath\n"
		"        %/centering centerpage rwindows i get lwindows i get sub 2 div sub def\n"
		"               % this line causes each row to be centered\n"
		"        /centering 0 def\n"
		"               % set centering to 0 to prevent centering of each row \n"
		"\n"
		"	centering -100 moveto  % -100 because some letters go below zero\n"
		"        centering max-height n-rows mul lineto\n"
		"        rwindows i get lwindows i get sub centering add\n"
		"                       max-height n-rows mul lineto\n"
		"        rwindows i get lwindows i get sub centering add\n"
		"                       -100 lineto\n"
		"	closepath\n"
		"        clip\n"
		"	lwindows i get neg n-rows i sub 1 sub max-height mul translate\n"
		"        centerpage centering 0 translate\n"
		"        words links diagram-sentence-circle\n"
		"	grestore\n"
		"     } for\n"
		"     end\n"
		"} def \n"
		"\n"
		"/ldiagram\n"
		"{diagramdict begin\n"
		"   /break-words exch def\n"
		"   /links exch def\n"
		"   /words exch def\n"
		"   /n words length def\n"
		"   /n-rows break-words length def\n"
		"   /Times-Roman findfont wordfontsize scalefont setfont\n"
		"\n"
		"   /left-ends [0 dup words {stringwidth pop add gap add dup}\n"
		"	                     forall pop pop] def\n"
		"   /right-ends [0 words {stringwidth pop add dup gap add} forall pop] def\n"
		"\n"
		"   /lwindows [ break-words {left-ends exch get gap 2 div sub } forall ] def\n"
		"   /rwindows [1 1 n-rows 1 sub {/i exch def\n"
		"		      lwindows i get } for\n"
		"	              right-ends n 1 sub get gap 2 div add\n"
		"	      ] def\n"
		"\n"
		"\n"
		"    /max 0 def\n"
		"    0 1 links length 1 sub {\n"
		"	/i exch def\n"
		"	/t links i get 2 get def\n"
		"	t max gt {/max t def} if\n"
		"      } for\n"
		"\n"
		"    /max-height ed max mul ey add top-of-words add row-spacing add def\n"
		"    /total-height n-rows max-height mul row-spacing sub def\n"
		"\n"
		"    /max-width 0 def            % compute the widest window\n"
		"    0 1 n-rows 1 sub {\n"
		"        /i exch def\n"
		"        /t rwindows i get lwindows i get sub def\n"
		"        t max-width gt {/max-width t def} if\n"
		"      } for\n"
		"\n"
		"   % centerpage max-width 2 div sub 0 translate  % centers it\n"
		"   % rightpage max-width sub 0 translate      % right justified\n"
		"                        % Delete both of these to make it left justified\n"
		"\n"
		"   n-rows 1 sub -1 0\n"
		"     {/i exch def\n"
		"	gsave\n"
		"	newpath\n"
		"        %/centering centerpage rwindows i get lwindows i get sub 2 div sub def\n"
		"               % this line causes each row to be centered\n"
		"        /centering 0 def\n"
		"               % set centering to 0 to prevent centering of each row \n"
		"\n"
		"	centering -100 moveto  % -100 because some letters go below zero\n"
		"        centering max-height n-rows mul lineto\n"
		"        rwindows i get lwindows i get sub centering add\n"
		"                       max-height n-rows mul lineto\n"
		"        rwindows i get lwindows i get sub centering add\n"
		"                       -100 lineto\n"
		"	closepath\n"
		"        clip\n"
		"	lwindows i get neg n-rows i sub 1 sub max-height mul translate\n"
		"        centerpage centering 0 translate\n"
		"        words links diagram-sentence-circle\n"
		"	grestore\n"
		"     } for\n"
		"     end\n"
		"} def \n"
		;
	if (mode==1) return header_string;
	else return "";
}
