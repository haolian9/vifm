#include <stic.h>

#include "../../src/cfg/config.h"
#include "../../src/engine/keys.h"
#include "../../src/modes/modes.h"
#include "../../src/modes/visual.h"
#include "../../src/modes/wk.h"
#include "../../src/ui/ui.h"

#include "utils.h"

static view_t *const view = &lwin;

SETUP()
{
	init_modes();

	view_setup(view);
	view->window_rows = 8;

	cfg.scroll_off = 0;
}

TEARDOWN()
{
	view_teardown(view);

	vle_keys_reset();
}

/*        | file0  | file1  |
 *        | file2  | file3  |
 * 0 row----file4----file5-----
 * 1 row  | file6  | file7  |
 * 2 row  | file8  | file9  |
 * 3 row  | file10 | file11 |
 * 4 row----file12---file13----
 *        | file14 | file15 |
 *        | file16 | file17 |
 *        | file18 | file19 |
 *        | file20 |        |
 */

TEST(scrolling_in_ls_normal)
{
	view->window_rows = 5;
	setup_grid(view, 2, 21, 1);
	view->top_line = 4;
	view->list_pos = 5;

	(void)vle_keys_exec_timed_out(WK_C_d);
	assert_int_equal(8, view->top_line);
	assert_int_equal(9, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_u);
	assert_int_equal(4, view->top_line);
	assert_int_equal(5, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_f);
	assert_int_equal(10, view->top_line);
	assert_int_equal(11, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_b);
	assert_int_equal(4, view->top_line);
	assert_int_equal(13, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_y);
	assert_int_equal(2, view->top_line);
	assert_int_equal(11, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_e);
	assert_int_equal(4, view->top_line);
	assert_int_equal(11, view->list_pos);
}

TEST(scrolling_in_ls_visual)
{
	view->window_rows = 5;
	setup_grid(view, 2, 21, 1);
	view->top_line = 4;
	view->list_pos = 5;

	enter_visual_mode(VS_NORMAL);

	(void)vle_keys_exec_timed_out(WK_C_d);
	assert_int_equal(8, view->top_line);
	assert_int_equal(9, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_u);
	assert_int_equal(4, view->top_line);
	assert_int_equal(5, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_f);
	assert_int_equal(10, view->top_line);
	assert_int_equal(11, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_b);
	assert_int_equal(4, view->top_line);
	assert_int_equal(13, view->list_pos);

	leave_visual_mode(0, 1, 0);
}

/*                 |        |
 * 0 row----file0--|-file8--|-file16----
 * 1 row    file1  | file9  | file17
 * 2 row    file2  | file10 | file18
 * 3 row    file3  | file11 | file19
 * 4 row    file4  | file12 | file20
 * 5 row    file5  | file13 |
 * 6 row    file6  | file14 |
 * 7 row----file7--|-file15-|-----------
 *                 |        |
 */

TEST(scrolling_in_tls_normal)
{
	view->window_rows = 8;
	setup_transposed_grid(view, 1, 21, 1);
	view->top_line = 8;
	view->list_pos = 10;

	(void)vle_keys_exec_timed_out(WK_C_d);
	assert_int_equal(16, view->top_line);
	assert_int_equal(18, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_u);
	assert_int_equal(8, view->top_line);
	assert_int_equal(10, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_f);
	assert_int_equal(16, view->top_line);
	assert_int_equal(18, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_b);
	assert_int_equal(8, view->top_line);
	assert_int_equal(10, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_y);
	assert_int_equal(8, view->top_line);
	assert_int_equal(10, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_e);
	assert_int_equal(8, view->top_line);
	assert_int_equal(10, view->list_pos);
}

TEST(scrolling_in_tls_visual)
{
	view->window_rows = 8;
	setup_transposed_grid(view, 1, 21, 1);
	view->top_line = 8;
	view->list_pos = 10;

	enter_visual_mode(VS_NORMAL);

	(void)vle_keys_exec_timed_out(WK_C_d);
	assert_int_equal(16, view->top_line);
	assert_int_equal(18, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_u);
	assert_int_equal(8, view->top_line);
	assert_int_equal(10, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_f);
	assert_int_equal(16, view->top_line);
	assert_int_equal(18, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_b);
	assert_int_equal(8, view->top_line);
	assert_int_equal(10, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_y);
	assert_int_equal(8, view->top_line);
	assert_int_equal(10, view->list_pos);

	(void)vle_keys_exec_timed_out(WK_C_e);
	assert_int_equal(8, view->top_line);
	assert_int_equal(10, view->list_pos);

	leave_visual_mode(0, 1, 0);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
