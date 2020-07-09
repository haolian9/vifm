#include "test-utils.h"

#include <stic.h>

#include <sys/stat.h> /* chmod() */
#include <sys/time.h> /* timeval utimes() */
#include <unistd.h> /* access() rmdir() usleep() */

#ifdef _WIN32
#include <windows.h>
#endif

#include <locale.h> /* LC_ALL setlocale() */
#include <stddef.h> /* NULL */
#include <stdio.h> /* FILE fclose() fopen() fread() remove() */
#include <stdlib.h> /* free() */
#include <string.h> /* memset() strcpy() strdup() */

#include "../../src/cfg/config.h"
#include "../../src/compat/os.h"
#include "../../src/engine/options.h"
#include "../../src/ui/color_manager.h"
#include "../../src/ui/column_view.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/dynarray.h"
#include "../../src/utils/env.h"
#include "../../src/utils/fs.h"
#include "../../src/utils/macros.h"
#include "../../src/utils/matcher.h"
#include "../../src/utils/path.h"
#include "../../src/utils/str.h"
#include "../../src/utils/string_array.h"
#include "../../src/utils/utils.h"
#include "../../src/background.h"
#include "../../src/filelist.h"
#include "../../src/filtering.h"
#include "../../src/opt_handlers.h"
#include "../../src/status.h"
#include "../../src/undo.h"

static int exec_func(OPS op, void *data, const char *src, const char *dst);
static int op_avail(OPS op);
static void format_none(int id, const void *data, size_t buf_len, char buf[]);
static void init_list(view_t *view);
static int init_pair_stub(short pair, short f, short b);
static int pair_content_stub(short pair, short *f, short *b);
static int pair_in_use_stub(short int pair);
static void move_pair_stub(short int from, short int to);

void
fix_environ(void)
{
#ifdef _WIN32
	extern int _CRT_glob;
	extern void __wgetmainargs(int *, wchar_t ***, wchar_t ***, int, int *);

	wchar_t **envp, **argv;
	int argc, si = 0;
	__wgetmainargs(&argc, &argv, &envp, _CRT_glob, &si);
#endif
}

void
conf_setup(void)
{
	update_string(&cfg.slow_fs_list, "");
	update_string(&cfg.apropos_prg, "");
	update_string(&cfg.cd_path, "");
	update_string(&cfg.find_prg, "");
	update_string(&cfg.fuse_home, "");
	update_string(&cfg.time_format, "+");
	update_string(&cfg.vi_command, "");
	update_string(&cfg.vi_x_command, "");
	update_string(&cfg.ruler_format, "");
	update_string(&cfg.status_line, "");
	update_string(&cfg.grep_prg, "");
	update_string(&cfg.locate_prg, "");
	update_string(&cfg.media_prg, "");
	update_string(&cfg.border_filler, "");
	update_string(&cfg.tab_label, "");

#ifndef _WIN32
	replace_string(&cfg.shell, "/bin/sh");
	update_string(&cfg.shell_cmd_flag, "-c");
#else
	replace_string(&cfg.shell, "cmd");
	update_string(&cfg.shell_cmd_flag, "/C");
#endif
	stats_update_shell_type(cfg.shell);

	cfg.dot_dirs = DD_TREE_LEAFS_PARENT;
}

void
conf_teardown(void)
{
	update_string(&cfg.slow_fs_list, NULL);
	update_string(&cfg.apropos_prg, NULL);
	update_string(&cfg.cd_path, NULL);
	update_string(&cfg.find_prg, NULL);
	update_string(&cfg.fuse_home, NULL);
	update_string(&cfg.time_format, NULL);
	update_string(&cfg.vi_command, NULL);
	update_string(&cfg.vi_x_command, NULL);
	update_string(&cfg.ruler_format, NULL);
	update_string(&cfg.status_line, NULL);
	update_string(&cfg.grep_prg, NULL);
	update_string(&cfg.locate_prg, NULL);
	update_string(&cfg.media_prg, NULL);
	update_string(&cfg.border_filler, NULL);
	update_string(&cfg.tab_label, NULL);
	update_string(&cfg.shell, NULL);
	update_string(&cfg.shell_cmd_flag, NULL);

	cfg.dot_dirs = 0;
}

void
opt_handlers_setup(void)
{
	update_string(&lwin.view_columns, "");
	update_string(&lwin.view_columns_g, "");
	update_string(&lwin.sort_groups, "");
	update_string(&lwin.sort_groups_g, "");
	update_string(&lwin.preview_prg, "");
	update_string(&lwin.preview_prg_g, "");
	update_string(&rwin.view_columns, "");
	update_string(&rwin.view_columns_g, "");
	update_string(&rwin.sort_groups, "");
	update_string(&rwin.sort_groups_g, "");
	update_string(&rwin.preview_prg, "");
	update_string(&rwin.preview_prg_g, "");

	conf_setup();

	init_option_handlers();
}

void
opt_handlers_teardown(void)
{
	vle_opts_reset();

	conf_teardown();

	update_string(&lwin.view_columns, NULL);
	update_string(&lwin.view_columns_g, NULL);
	update_string(&lwin.sort_groups, NULL);
	update_string(&lwin.sort_groups_g, NULL);
	update_string(&lwin.preview_prg, NULL);
	update_string(&lwin.preview_prg_g, NULL);
	update_string(&rwin.view_columns, NULL);
	update_string(&rwin.view_columns_g, NULL);
	update_string(&rwin.sort_groups, NULL);
	update_string(&rwin.sort_groups_g, NULL);
	update_string(&rwin.preview_prg, NULL);
	update_string(&rwin.preview_prg_g, NULL);
}

void
undo_setup(void)
{
	static int max_undo_levels = 0;
	un_init(&exec_func, &op_avail, NULL, &max_undo_levels);
}

static int
exec_func(OPS op, void *data, const char *src, const char *dst)
{
	return 0;
}

static int
op_avail(OPS op)
{
	return 0;
}

void
undo_teardown(void)
{
	un_reset();
}

void
view_setup(view_t *view)
{
	char *error;

	view->list_rows = 0;
	view->filtered = 0;
	view->list_pos = 0;
	view->dir_entry = NULL;
	view->hide_dot = 0;
	view->hide_dot_g = 0;
	view->invert = 1;
	view->selected_files = 0;
	view->ls_view = 0;
	view->ls_view_g = 0;
	view->miller_view = 0;
	view->miller_view_g = 0;
	view->window_rows = 0;
	view->run_size = 1;

	assert_success(filter_init(&view->local_filter.filter, 1));
	assert_non_null(view->manual_filter = matcher_alloc("", 0, 0, "", &error));
	assert_success(filter_init(&view->auto_filter, 1));

	strcpy(view->curr_dir, "/path");
	update_string(&view->custom.orig_dir, NULL);

	view->sort[0] = SK_BY_NAME;
	memset(&view->sort[1], SK_NONE, sizeof(view->sort) - 1);
	memcpy(view->sort_g, view->sort, sizeof(view->sort_g));

	view->custom.entry_count = 0;
	view->custom.entries = NULL;

	view->local_filter.entry_count = 0;
	view->local_filter.entries = NULL;
}

void
view_teardown(view_t *view)
{
	flist_free_view(view);
}

void
columns_setup_column(int id)
{
	columns_add_column_desc(id, &format_none);
}

static void
format_none(int id, const void *data, size_t buf_len, char buf[])
{
	buf[0] = '\0';
}

void
columns_teardown(void)
{
	columns_clear_column_descs();
	columns_set_line_print_func(NULL);
}

void
histories_init(int size)
{
	cfg_resize_histories(0);
	cfg_resize_histories(size);
}

void
create_dir(const char path[])
{
	assert_success(os_mkdir(path, 0700));
	assert_true(is_dir(path));
}

void
create_file(const char path[])
{
	FILE *const f = fopen(path, "w");
	assert_non_null(f);
	if(f != NULL)
	{
		fclose(f);
	}
}

void
create_executable(const char path[])
{
	create_file(path);
	assert_success(access(path, F_OK));
	chmod(path, 0755);
	assert_success(access(path, X_OK));
}

void
make_file(const char path[], const char contents[])
{
	FILE *fp = fopen(path, "wb");
	assert_non_null(fp);

	if(fp != NULL)
	{
		fputs(contents, fp);
		fclose(fp);
	}
}

void
remove_dir(const char path[])
{
	assert_success(rmdir(path));
}

void
no_remove_dir(const char path[])
{
	assert_failure(rmdir(path));
}

void
remove_file(const char path[])
{
	assert_success(remove(path));
}

void
no_remove_file(const char path[])
{
	assert_failure(remove(path));
}

void
make_abs_path(char buf[], size_t buf_len, const char base[], const char sub[],
		const char cwd[])
{
	char local_buf[buf_len];

	if(is_path_absolute(base))
	{
		snprintf(local_buf, buf_len, "%s%s%s", base, (sub[0] == '\0' ? "" : "/"),
				sub);
	}
	else
	{
		char cwd_buf[PATH_MAX + 1];
		if(cwd == NULL)
		{
			assert_non_null(get_cwd(cwd_buf, sizeof(cwd_buf)));
			cwd = cwd_buf;
		}
		snprintf(local_buf, buf_len, "%s/%s%s%s", cwd, base,
				(sub[0] == '\0' ? "" : "/"), sub);
	}

	canonicalize_path(local_buf, buf, buf_len);
	if(!ends_with_slash(sub) && !is_root_dir(buf))
	{
		chosp(buf);
	}
}

void
copy_file(const char src[], const char dst[])
{
	char buf[4*1024];
	size_t nread;
	FILE *const in = os_fopen(src, "rb");
	FILE *const out = os_fopen(dst, "wb");

	assert_non_null(in);
	assert_non_null(out);

	while((nread = fread(&buf, 1, sizeof(buf), in)) != 0U)
	{
		assert_int_equal(nread, fwrite(&buf, 1, nread, out));
	}

	fclose(out);
	fclose(in);
}

int
windows(void)
{
#ifdef _WIN32
	return 1;
#else
	return 0;
#endif
}

int
not_windows(void)
{
#ifdef _WIN32
	return 0;
#else
	return 1;
#endif
}

void
try_enable_utf8_locale(void)
{
	(void)setlocale(LC_ALL, "");
	if(!utf8_locale())
	{
		(void)setlocale(LC_ALL, "en_US.utf8");
	}
}

int
utf8_locale(void)
{
	return (vifm_wcwidth(L'丝') == 2);
}

int
replace_matcher(matcher_t **matcher, const char expr[])
{
	char *error;

	matcher_free(*matcher);
	*matcher = matcher_alloc(expr, FILTER_DEF_CASE_SENSITIVITY, 0, "", &error);
	free(error);

	return (*matcher == NULL);
}

void
setup_grid(view_t *view, int column_count, int list_rows, int init)
{
	view->window_cols = column_count;
	view->ls_view = 1;
	view->miller_view = 0;
	view->ls_transposed = 0;
	view->list_rows = list_rows;
	view->column_count = column_count;
	view->run_size = column_count;
	view->window_cells = column_count*view->window_rows;

	if(init)
	{
		init_list(view);
	}
}

void
setup_transposed_grid(view_t *view, int column_count, int list_rows, int init)
{
	view->window_cols = column_count;
	view->ls_view = 1;
	view->miller_view = 0;
	view->ls_transposed = 1;
	view->list_rows = list_rows;
	view->column_count = column_count;
	view->run_size = view->window_rows;
	view->window_cells = column_count*view->window_rows;

	if(init)
	{
		init_list(view);
	}
}

void
init_view_list(view_t *view)
{
	view->list_rows = 1;
	init_list(view);
}

static void
init_list(view_t *view)
{
	int i;

	view->dir_entry = dynarray_cextend(NULL,
			view->list_rows*sizeof(*view->dir_entry));

	for(i = 0; i < view->list_rows; ++i)
	{
		view->dir_entry[i].name = strdup("");
		view->dir_entry[i].type = FT_REG;
		view->dir_entry[i].origin = view->curr_dir;
	}
}

void
wait_for_bg(void)
{
	int counter = 0;
	while(bg_has_active_jobs(0))
	{
		usleep(5000);
		if(++counter > 100)
		{
			assert_fail("Waiting for too long.");
			break;
		}
	}
}

void
file_is(const char path[], const char *lines[], int nlines)
{
	FILE *fp = fopen(path, "r");
	if(fp == NULL)
	{
		assert_non_null(fp);
		return;
	}

	int actual_nlines;
	char **actual_lines = read_file_lines(fp, &actual_nlines);
	fclose(fp);

	assert_int_equal(nlines, actual_nlines);

	int i;
	for(i = 0; i < MIN(nlines, actual_nlines); ++i)
	{
		assert_string_equal(lines[i], actual_lines[i]);
	}

	free_string_array(actual_lines, actual_nlines);
}

char *
mock_env(const char env[], const char with[])
{
	char *value = NULL;
	update_string(&value, env_get("TMPDIR"));
	env_set("TMPDIR", with);
	return value;
}

void
unmock_env(const char env[], char old_value[])
{
	if(old_value != NULL)
	{
		env_set("TMPDIR", old_value);
	}
	else
	{
		env_remove("TMPDIR");
	}
	free(old_value);
}

void
stub_colmgr(void)
{
	const colmgr_conf_t colmgr_conf = {
		.max_color_pairs = 256,
		.max_colors = 16,
		.init_pair = &init_pair_stub,
		.pair_content = &pair_content_stub,
		.pair_in_use = &pair_in_use_stub,
		.move_pair = &move_pair_stub,
	};
	colmgr_init(&colmgr_conf);
}

static int
init_pair_stub(short pair, short f, short b)
{
	return 0;
}

static int
pair_content_stub(short pair, short *f, short *b)
{
	*f = 0;
	*b = 0;
	return 0;
}

static int
pair_in_use_stub(short int pair)
{
	return 0;
}

static void
move_pair_stub(short int from, short int to)
{
}

void
reset_timestamp(const char path[])
{
#ifndef _WIN32
	struct timeval tvs[2] = {};
	assert_success(utimes(path, tvs));
#else
	HANDLE file = CreateFileA(path, GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);

	assert_true(file != INVALID_HANDLE_VALUE);
	if(file != INVALID_HANDLE_VALUE)
	{
		FILETIME time = { 1, 0 };
		assert_true(SetFileTime(file, &time, &time, &time));
		CloseHandle(file);
	}
#endif
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
