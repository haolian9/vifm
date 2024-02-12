/* vifm
 * Copyright (C) 2001 Ken Steen.
 * Copyright (C) 2011 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "sort.h"

#include <regex.h> /* regex_t regexec() regfree() */

#include <assert.h> /* assert() */
#include <ctype.h>
#include <stdlib.h> /* abs() */
#include <string.h> /* strcmp() strrchr() */

#include "cfg/config.h"
#include "compat/fs_limits.h"
#include "ui/ui.h"
#include "utils/dynarray.h"
#include "utils/fs.h"
#include "utils/fsdata.h"
#include "utils/macros.h"
#include "utils/path.h"
#include "utils/regexp.h"
#include "utils/str.h"
#include "utils/string_array.h"
#include "utils/test_helpers.h"
#include "utils/utils.h"
#include "filelist.h"
#include "filtering.h"
#include "status.h"
#include "types.h"

const char *sort_enum[] = {
	/* SK_* start with 1. */
	[0] = "",

	[SK_BY_EXTENSION]     = "ext",
	[SK_BY_NAME]          = "name",
	[SK_BY_SIZE]          = "size",
	[SK_BY_TIME_ACCESSED] = "atime",
	[SK_BY_TIME_CHANGED]  = "ctime",
	[SK_BY_TIME_MODIFIED] = "mtime",
	[SK_BY_INAME]         = "iname",
	[SK_BY_DIR]           = "dir",
	[SK_BY_TYPE]          = "type",
	[SK_BY_FILEEXT]       = "fileext",
	[SK_BY_NITEMS]        = "nitems",
	[SK_BY_GROUPS]        = "groups",
	[SK_BY_TARGET]        = "target",
	[SK_BY_ROOT]          = "root",
	[SK_BY_FILEROOT]      = "fileroot",
#ifndef _WIN32
	[SK_BY_GROUP_ID]      = "gid",
	[SK_BY_GROUP_NAME]    = "gname",
	[SK_BY_MODE]          = "mode",
	[SK_BY_OWNER_ID]      = "uid",
	[SK_BY_OWNER_NAME]    = "uname",
	[SK_BY_PERMISSIONS]   = "perms",
	[SK_BY_NLINKS]        = "nlinks",
	[SK_BY_INODE]         = "inode",
#endif

	[SK_NONE]             = "",
	[SK_BY_ID]            = "",
};
ARRAY_GUARD(sort_enum, SK_TOTAL);

static void sort_tree_slice(dir_entry_t *entries, const dir_entry_t *children,
		size_t nchildren, int root);
static int prepare_for_sorting(view_t *v, int local);
static void sort_sequence(dir_entry_t *entries, size_t nentries);
static void sort_by_groups(dir_entry_t *entries, signed char key,
		size_t nentries);
static void sort_by_key(dir_entry_t *entries, size_t nentries, signed char key,
		void *data);
static int sort_dir_list(const void *one, const void *two);
TSTATIC int strnumcmp(const char s[], const char t[]);
#if !defined(HAVE_STRVERSCMP_FUNC) || !HAVE_STRVERSCMP_FUNC
static int vercmp(const char s[], const char t[]);
#else
static char * skip_leading_zeros(const char str[]);
#endif
static int compare_file_names(const dir_entry_t *f, const dir_entry_t *s,
		SortingKey sort_type);
static int compare_file_exts(const dir_entry_t *f, int f_dir,
		const dir_entry_t *s, int s_dir, SortingKey sort_type);
static int compare_name_part(const char s[], const char t[]);
static int compare_file_sizes(const dir_entry_t *f, const dir_entry_t *s);
static int compare_item_count(const dir_entry_t *f, int fdir,
		const dir_entry_t *s, int sdir);
static int compare_group(const char f[], const char s[], regex_t *regex);
static int compare_targets(const dir_entry_t *f, const dir_entry_t *s);

/* The following variables are set by prepare_for_sorting(). */

/* View which is being sorted. */
static view_t *view;
/* Picked sort array of the view. */
static const signed char *view_sort;
/* Picked sort groups setting of the view. */
static const char *view_sort_groups;
/* Whether the view displays custom file list. */
static int custom_view;

/* The following variables are set by sort_by_key(). */

/* Whether it's descending sort. */
static int sort_descending;
/* Key used to sort entries in current sorting round. */
static SortingKey sort_type;
/* Sorting key specific data. */
static void *sort_data;

void
sort_view(view_t *v)
{
	dir_entry_t *unsorted_list;

	if(prepare_for_sorting(v, /*local=*/1) != 0)
	{
		return;
	}

	/* Tree sorting works fine for flat list, but requires a bit more
	 * resources, so skip it if we can. */
	if(!custom_view || !cv_tree(v->custom.type))
	{
		sort_sequence(&v->dir_entry[0], v->list_rows);
		return;
	}

	/* When local filter isn't empty, parent directories disappear and sorting
	 * stops being aware of tree structure to some degree.  Perform one more round
	 * of stable sorting of origins to group child nodes. */
	if(!filter_is_empty(&v->local_filter.filter))
	{
		flist_custom_uncompress_tree(v);
	}

	unsorted_list = v->dir_entry;
	v->dir_entry = dynarray_extend(NULL, v->list_rows*sizeof(*v->dir_entry));
	if(v->dir_entry != NULL)
	{
		sort_tree_slice(&v->dir_entry[0], unsorted_list, v->list_rows, 1);
	}
	else
	{
		/* Just do nothing on memory error. */
		v->dir_entry = unsorted_list;
		unsorted_list = NULL;
	}

	if(filter_is_empty(&v->local_filter.filter))
	{
		dynarray_free(unsorted_list);
	}
	else
	{
		/* This undoes effect of flist_custom_uncompress_tree() from above. */
		filters_drop_temporaries(v, unsorted_list);
	}
}

/* Sorts one level of a tree per invocation, recurring to sort all nested
 * trees. */
static void
sort_tree_slice(dir_entry_t *entries, const dir_entry_t *children,
		size_t nchildren, int root)
{
	int i = 0;
	size_t pos = 0U;
	/* Copy all first-level nodes of the current tree forming a sequence for
	 * sorting. */
	while(pos < nchildren)
	{
		entries[i] = children[pos];
		entries[i].child_pos = pos;
		pos += children[pos].child_count + 1;
		++i;
	}

	sort_sequence(entries, i);

	/* Finish sorting of this level by placing nodes at their corresponding
	 * position starting with the last one.  Each subtree is then sorted
	 * recursively. */
	pos = nchildren;
	while(--i >= 0)
	{
		pos -= entries[i].child_count + 1;
		entries[pos] = entries[i];
		if(entries[pos].child_count != 0)
		{
			sort_tree_slice(&entries[pos + 1U], &children[entries[pos].child_pos + 1],
					entries[pos].child_count, 0);
		}
		entries[pos].child_pos = root ? 0 : pos + 1;
	}
}

void
sort_entries(view_t *v, entries_t entries)
{
	if(prepare_for_sorting(v, /*local=*/0) == 0)
	{
		sort_sequence(entries.entries, entries.nentries);
	}
}

/* Prepares globals of this unit for performing sorting.  Returns non-zero if
 * there is no sorting to do. */
static int
prepare_for_sorting(view_t *v, int local)
{
	const signed char *sort = (local ? v->sort : v->sort_g);
	if(sort[0] > SK_LAST)
	{
		/* Completely skip sorting if primary key isn't set. */
		return 1;
	}

	view = v;
	view_sort = sort;
	view_sort_groups = (local ? v->sort_groups : v->sort_groups_g);
	custom_view = flist_custom_active(v);
	return 0;
}

/* Sorts sequence of file entries (plain list, not tree, although it can be some
 * part of a tree). */
static void
sort_sequence(dir_entry_t *entries, size_t nentries)
{
	int i = SK_COUNT;
	while(--i >= 0)
	{
		const signed char sorting_key = view_sort[i];
		const int sorting_type = abs(sorting_key);

		if(sorting_type > SK_LAST)
		{
			continue;
		}

		if(sorting_type == SK_BY_GROUPS)
		{
			sort_by_groups(entries, sorting_key, nentries);
			continue;
		}

		sort_by_key(entries, nentries, sorting_key, NULL);
	}

	if(!ui_view_sort_list_contains(view_sort, SK_BY_DIR))
	{
		sort_by_key(entries, nentries, SK_BY_DIR, NULL);
	}
}

/* Sorts specified range of entries according to sorting groups option. */
static void
sort_by_groups(dir_entry_t *entries, signed char key, size_t nentries)
{
	char **groups = NULL;
	int ngroups = 0;

	char *const copy = strdup(view_sort_groups);
	char *group = copy, *state = NULL;
	while((group = split_and_get(group, ',', &state)) != NULL)
	{
		ngroups = add_to_string_array(&groups, ngroups, group);
	}
	free(copy);

	/* Whether view->primary_group can be used to skip compiling regexp of the
	 * first group. */
	const int optimized = (view_sort_groups == view->sort_groups);

	int i;
	for(i = ngroups - 1; i >= (optimized ? 1 : 0); --i)
	{
		regex_t regex;
		(void)regexp_compile(&regex, groups[i], REG_EXTENDED | REG_ICASE);
		sort_by_key(entries, nentries, key, &regex);
		regfree(&regex);
	}
	if(optimized && ngroups != 0)
	{
		sort_by_key(entries, nentries, key, &view->primary_group);
	}

	free_string_array(groups, ngroups);
}

/* Sorts specified range of entries by the key in a stable way. */
static void
sort_by_key(dir_entry_t *entries, size_t nentries, signed char key, void *data)
{
	sort_descending = (key < 0);
	sort_type = (SortingKey)abs(key);
	sort_data = data;

	unsigned int i;
	for(i = 0U; i < nentries; ++i)
	{
		entries[i].tag = i;
	}

	safe_qsort(entries, nentries, sizeof(*entries), &sort_dir_list);
}

/* Compares file names containing numbers correctly. */
TSTATIC int
strnumcmp(const char s[], const char t[])
{
#if !defined(HAVE_STRVERSCMP_FUNC) || !HAVE_STRVERSCMP_FUNC
	return vercmp(s, t);
#else
	const char *new_s = skip_leading_zeros(s);
	const char *new_t = skip_leading_zeros(t);
	return strverscmp(new_s, new_t);
#endif
}

#if !defined(HAVE_STRVERSCMP_FUNC) || !HAVE_STRVERSCMP_FUNC
static int
vercmp(const char s[], const char t[])
{
	while(*s != '\0' && *t != '\0')
	{
		if(isdigit(*s) && isdigit(*t))
		{
			int num_a, num_b;
			const char *os = s, *ot = t;
			char *p;

			num_a = strtol(s, &p, 10);
			s = p;

			num_b = strtol(t, &p, 10);
			t = p;

			if(num_a != num_b)
				return SORT_CMP(num_a, num_b);
			else if(*os != *ot)
				return SORT_CMP((unsigned char)*os, (unsigned char)*ot);
		}
		else if(*s == *t)
		{
			s++;
			t++;
		}
		else
			break;
	}

	return SORT_CMP((unsigned char)*s, (unsigned char)*t);
}
#else
/* Skips all zeros in front of numbers (correctly handles zero).  Returns str, a
 * pointer to '0' or a pointer to non-zero digit. */
static char *
skip_leading_zeros(const char str[])
{
	while(str[0] == '0' && isdigit(str[1]))
	{
		str++;
	}
	return (char *)str;
}
#endif

static int
sort_dir_list(const void *one, const void *two)
{
	/* TODO: refactor this function sort_dir_list(). */

	int retval;
	const dir_entry_t *const first = one;
	const dir_entry_t *const second = two;

	const int first_is_dir = fentry_is_dir(first);
	const int second_is_dir = fentry_is_dir(second);

	if(first_is_dir && is_parent_dir(first->name))
	{
		return -1;
	}
	if(second_is_dir && is_parent_dir(second->name))
	{
		return 1;
	}

	retval = 0;
	switch(sort_type)
	{
		case SK_BY_NAME:
		case SK_BY_INAME:
			retval = compare_file_names(first, second, sort_type);
			break;

		case SK_BY_DIR:
			if(first_is_dir != second_is_dir)
			{
				retval = first_is_dir ? -1 : 1;
			}
			break;

		case SK_BY_TYPE:
			retval = strcmp(get_type_str(first->type), get_type_str(second->type));
			break;

		case SK_BY_FILEEXT:
		case SK_BY_EXTENSION:
			retval = compare_file_exts(first, first_is_dir, second, second_is_dir,
					sort_type);
			break;

		case SK_BY_SIZE:
			retval = compare_file_sizes(first, second);
			break;

		case SK_BY_NITEMS:
			retval = compare_item_count(first, first_is_dir, second, second_is_dir);
			break;

		case SK_BY_GROUPS:
			retval = compare_group(first->name, second->name, sort_data);
			break;

		case SK_BY_TARGET:
			retval = compare_targets(first, second);
			break;

		case SK_BY_TIME_MODIFIED:
			retval = SORT_CMP(first->mtime, second->mtime);
			break;

		case SK_BY_TIME_ACCESSED:
			retval = SORT_CMP(first->atime, second->atime);
			break;

		case SK_BY_TIME_CHANGED:
			retval = SORT_CMP(first->ctime, second->ctime);
			break;

#ifndef _WIN32
		case SK_BY_MODE:
			retval = SORT_CMP(first->mode, second->mode);
			break;

		case SK_BY_INODE:
			retval = SORT_CMP(first->inode, second->inode);
			break;

		case SK_BY_OWNER_NAME: /* FIXME */
		case SK_BY_OWNER_ID:
			retval = SORT_CMP(first->uid, second->uid);
			break;

		case SK_BY_GROUP_NAME: /* FIXME */
		case SK_BY_GROUP_ID:
			retval = SORT_CMP(first->gid, second->gid);
			break;

		case SK_BY_PERMISSIONS:
			{
				char first_perm[11], second_perm[11];
				get_perm_string(first_perm, sizeof(first_perm), first->mode);
				get_perm_string(second_perm, sizeof(second_perm), second->mode);
				retval = strcmp(first_perm, second_perm);
			}
			break;

		case SK_BY_NLINKS:
			retval = SORT_CMP(first->nlinks, second->nlinks);
			break;
#endif
	}

	if(retval == 0)
	{
		retval = SORT_CMP(first->tag, second->tag);
	}
	else if(sort_descending)
	{
		retval = -retval;
	}

	return retval;
}

/* Compares two file sizes.  Returns standard -1, 0, 1 for comparisons. */
static int
compare_file_sizes(const dir_entry_t *f, const dir_entry_t *s)
{
	const uint64_t fsize = fentry_get_size(view, f);
	const uint64_t ssize = fentry_get_size(view, s);
	return SORT_CMP(fsize, ssize);
}

/* Compares number of items in two directories (taken as zero for files).
 * Returns standard -1, 0, 1 for comparisons. */
static int
compare_item_count(const dir_entry_t *f, int fdir, const dir_entry_t *s,
		int sdir)
{
	/* We don't want to call fentry_get_nitems() for files as sorting huge lists
	 * of files can call this function a lot of times, thus even small extra
	 * performance overhead is not desirable. */
	const uint64_t fsize = fdir ? fentry_get_nitems(view, f) : 0U;
	const uint64_t ssize = sdir ? fentry_get_nitems(view, s) : 0U;
	return SORT_CMP(fsize, ssize);
}

/* Compares two file names according to grouping regular expression.  Returns
 * standard -1, 0, 1 for comparisons. */
static int
compare_group(const char f[], const char s[], regex_t *regex)
{
	char fname[NAME_MAX + 1], sname[NAME_MAX + 1];
	regmatch_t fmatch = get_group_match(regex, f);
	regmatch_t smatch = get_group_match(regex, s);

	copy_str(fname, MIN(sizeof(fname), (size_t)fmatch.rm_eo - fmatch.rm_so + 1U),
			f + fmatch.rm_so);
	copy_str(sname, MIN(sizeof(sname), (size_t)smatch.rm_eo - smatch.rm_so + 1U),
			s + smatch.rm_so);

	return strcmp(fname, sname);
}

/* Compares two file names according to symbolic link target.  Returns standard
 * -1, 0, 1 for comparisons. */
static int
compare_targets(const dir_entry_t *f, const dir_entry_t *s)
{
	char full_path[PATH_MAX + 1];
	char nlink[PATH_MAX + 1], plink[PATH_MAX + 1];

	if((f->type == FT_LINK) != (s->type == FT_LINK))
	{
		/* One of the entries is not a link. */
		return (f->type == FT_LINK) ? 1 : -1;
	}
	if(f->type != FT_LINK)
	{
		/* Both entries are not symbolic links. */
		return 0;
	}

	/* Both entries are symbolic links. */

	get_full_path_of(f, sizeof(full_path), full_path);
	if(get_link_target(full_path, nlink, sizeof(nlink)) != 0)
	{
		return 0;
	}
	get_full_path_of(s, sizeof(full_path), full_path);
	if(get_link_target(full_path, plink, sizeof(plink)) != 0)
	{
		return 0;
	}

	return stroscmp(nlink, plink);
}

/* Compares two file names (could include one or several components) assuming
 * that the leading dot character is smaller than any other character.  Returns
 * positive value if s is greater than t, zero if they are equal, otherwise
 * negative value is returned. */
static int
compare_file_names(const dir_entry_t *f, const dir_entry_t *s,
		SortingKey sort_type)
{
	const char *f_name = f->name;
	const char *s_name = s->name;

	char f_short[PATH_MAX + 1];
	char s_short[PATH_MAX + 1];

	if(custom_view)
	{
		get_short_path_of(view, f, NF_NONE, /*drop_prefix=*/0, sizeof(f_short),
				f_short);
		get_short_path_of(view, s, NF_NONE, /*drop_prefix=*/0, sizeof(s_short),
				s_short);

		f_name = f_short;
		s_name = s_short;
	}

	if(f_name[0] == '.' && s_name[0] != '.')
	{
		return -1;
	}
	if(f_name[0] != '.' && s_name[0] == '.')
	{
		return 1;
	}

	const char *f_prev = f_name, *s_prev = s_name;

	char f_ignorecase[NAME_MAX + 1];
	char s_ignorecase[NAME_MAX + 1];
	if(sort_type == SK_BY_INAME)
	{
		/* Ignore too small buffer errors by not caring about part that didn't
		 * fit. */
		(void)str_to_lower(f_name, f_ignorecase, sizeof(f_ignorecase));
		(void)str_to_lower(s_name, s_ignorecase, sizeof(s_ignorecase));

		f_name = f_ignorecase;
		s_name = s_ignorecase;
	}

	int result = compare_name_part(f_name, s_name);
	if(result == 0 && sort_type == SK_BY_INAME)
	{
		/* Resort to comparing original names when their normalized versions match
		 * to always solve ties in a deterministic way. */
		result = strcmp(f_prev, s_prev);
	}
	return result;
}

/* Compares files/directories by extensions.  Returns standard < 0, == 0, > 0
 * comparison result. */
static int
compare_file_exts(const dir_entry_t *f, int f_dir, const dir_entry_t *s,
		int s_dir, SortingKey sort_type)
{
	const char *f_name = f->name;
	const char *s_name = s->name;

	if(sort_type == SK_BY_FILEEXT)
	{
		if(f_dir && s_dir)
		{
			return compare_name_part(f_name, s_name);
		}

		if(f_dir || s_dir)
		{
			return (f_dir ? -1 : 1);
		}
	}

	const char *f_ext = strrchr(f_name, '.');
	const char *s_ext = strrchr(s_name, '.');

	if(f_ext != NULL && s_ext != NULL)
	{
		if(f_ext == f_name && s_ext != s_name)
		{
			return -1;
		}

		if(f_ext != f_name && s_ext == s_name)
		{
			return 1;
		}

		return compare_name_part(f_ext + 1, s_ext + 1);
	}

	if(f_ext != NULL || s_ext != NULL)
	{
		return (f_ext != NULL ? -1 : 1);
	}

	return compare_name_part(f_name, s_name);
}

/* Compares two file names or their parts (e.g. extensions).  Returns positive
 * value if s is greater than t, zero if they are equal, otherwise negative
 * value is returned. */
static int
compare_name_part(const char s[], const char t[])
{
	return cfg.sort_numbers ? strnumcmp(s, t) : strcmp(s, t);
}

SortingKey
get_secondary_key(SortingKey primary_key)
{
	switch(primary_key)
	{
#ifndef _WIN32
		case SK_BY_OWNER_NAME:
		case SK_BY_OWNER_ID:
		case SK_BY_GROUP_NAME:
		case SK_BY_GROUP_ID:
		case SK_BY_MODE:
		case SK_BY_INODE:
		case SK_BY_PERMISSIONS:
		case SK_BY_NLINKS:
#endif
		case SK_BY_TYPE:
		case SK_BY_NITEMS:
		case SK_BY_TARGET:
		case SK_BY_TIME_MODIFIED:
		case SK_BY_TIME_ACCESSED:
		case SK_BY_TIME_CHANGED:
			return primary_key;
		case SK_BY_NAME:
		case SK_BY_INAME:
		case SK_BY_EXTENSION:
		case SK_BY_FILEEXT:
		case SK_BY_SIZE:
		case SK_BY_GROUPS:
		case SK_BY_DIR:
			return SK_BY_SIZE;
	}
	assert(0 && "Unhandled sorting key?");
	return SK_BY_SIZE;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
