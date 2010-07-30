/* main.c
 *
 * Copyright (C) 2010 Christian Hergert <chris@dronelabs.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <sys/sysinfo.h>
#include <stdlib.h>

#include "uber.h"

typedef struct
{
	guint       len;
	gdouble    *total;
	gdouble    *freq;
	glong      *last_user;
	glong      *last_idle;
	glong      *last_system;
	glong      *last_nice;
	GtkWidget **labels;
} CpuInfo;

static guint        gdk_event_count  = 0;
static CpuInfo      cpu_info         = { 0 };
static const gchar *default_colors[] = { "#73d216",
                                         "#f57900",
                                         "#3465a4",
                                         "#ef2929",
                                         "#75507b",
                                         "#ce5c00",
                                         "#c17d11",
                                         "#ce5c00",
                                         NULL };

static void
gdk_event_hook (GdkEvent *event, /* IN */
                gpointer  data)  /* IN */
{
	gdk_event_count++;
	gtk_main_do_event(event);
}

static gboolean
get_xevent_info (UberLineGraph *graph,     /* IN */
                 guint          line,      /* IN */
                 gdouble       *value,     /* OUT */
                 gpointer       user_data) /* IN */
{
	switch (line) {
	case 1:
		*value = gdk_event_count;
		gdk_event_count = 0;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static gboolean
get_cpu_info (UberLineGraph *graph,     /* IN */
              guint          line,      /* IN */
              gdouble       *value,     /* OUT */
              gpointer       user_data) /* IN */
{
	gchar *text;
	gint i;

	g_assert_cmpint(line, >, 0);
	g_assert_cmpint(line, <=, cpu_info.len * 2);

	if ((line % 2) == 0) {
		*value = cpu_info.freq[((line - 1) / 2)];
	} else {
		i = (line - 1) / 2;
		*value = cpu_info.total[i];
		/*
		 * Update label text.
		 */
		text = g_strdup_printf("CPU%d  %0.1f %%", i + 1, *value);
		uber_label_set_text(UBER_LABEL(cpu_info.labels[i]), text);
		g_free(text);
	}
	return TRUE;
}

static void
next_cpu_info (void)
{
	gchar cpu[64] = { 0 };
	glong user;
	glong system;
	glong nice;
	glong idle;
	glong user_calc;
	glong system_calc;
	glong nice_calc;
	glong idle_calc;
	gchar *buf = NULL;
	glong total;
	gchar *line;
	gint ret;
	gint id;
	gint i;

	if (!cpu_info.len) {
#if __linux__
		cpu_info.len = get_nprocs();
#else
#error "Your platform is not supported"
#endif
		g_assert(cpu_info.len);
		/*
		 * Allocate data for sampling.
		 */
		cpu_info.total = g_new0(gdouble, cpu_info.len);
		cpu_info.last_user = g_new0(glong, cpu_info.len);
		cpu_info.last_idle = g_new0(glong, cpu_info.len);
		cpu_info.last_system = g_new0(glong, cpu_info.len);
		cpu_info.last_nice = g_new0(glong, cpu_info.len);
		cpu_info.labels = g_new0(GtkWidget*, cpu_info.len);
	}

	if (g_file_get_contents("/proc/stat", &buf, NULL, NULL)) {
		line = buf;
		for (i = 0; buf[i]; i++) {
			if (buf[i] == '\n') {
				buf[i] = '\0';
				if (g_str_has_prefix(line, "cpu") && isdigit(line[3])) {
					user = nice = system = idle = id = 0;
					ret = sscanf(line, "%s %ld %ld %ld %ld",
					             cpu, &user, &nice, &system, &idle);
					if (ret != 5) {
						goto next;
					}
					ret = sscanf(cpu, "cpu%d", &id);
					if (ret != 1 || id < 0 || id >= cpu_info.len) {
						goto next;
					}
					user_calc = user - cpu_info.last_user[id];
					nice_calc = nice - cpu_info.last_nice[id];
					system_calc = system - cpu_info.last_system[id];
					idle_calc = idle - cpu_info.last_idle[id];
					total = user_calc + nice_calc + system_calc + idle_calc;
					cpu_info.total[id] = (user_calc + nice_calc + system_calc) / (gfloat)total * 100.;
					cpu_info.last_user[id] = user;
					cpu_info.last_nice[id] = nice;
					cpu_info.last_idle[id] = idle;
					cpu_info.last_system[id] = system;
				}
			  next:
				line = &buf[i + 1];
			}
		}
	}

	g_free(buf);
}

static void
next_cpu_freq_info (void)
{
	glong max;
	glong cur;
	gboolean ret;
	gchar *buf;
	gchar *path;
	gint i;

	g_return_if_fail(cpu_info.len > 0);

	/*
	 * Initialize.
	 */
	if (!cpu_info.freq) {
		cpu_info.freq = g_new0(gdouble, cpu_info.len);
	}

	/*
	 * Get current frequencies.
	 */
	for (i = 0; i < cpu_info.len; i++) {
		/*
		 * Get max frequency.
		 */
		path = g_strdup_printf("/sys/devices/system/cpu/cpu%d"
		                       "/cpufreq/scaling_max_freq", i);
		ret = g_file_get_contents(path, &buf, NULL, NULL);
		g_free(path);
		if (!ret) {
			continue;
		}
		max = atoi(buf);
		g_free(buf);

		/*
		 * Get current frequency.
		 */
		path = g_strdup_printf("/sys/devices/system/cpu/cpu%d/"
		                       "cpufreq/scaling_cur_freq", i);
		ret = g_file_get_contents(path, &buf, NULL, NULL);
		g_free(path);
		if (!ret) {
			continue;
		}
		cur = atoi(buf);
		g_free(buf);

		/*
		 * Store frequency percentage.
		 */
		cpu_info.freq[i] = (gfloat)cur / (gfloat)max * 100.;
	}
}

static gpointer G_GNUC_NORETURN
sample_thread (gpointer data)
{
	while (TRUE) {
		g_usleep(G_USEC_PER_SEC);
		next_cpu_info();
		next_cpu_freq_info();
	}
}

static gboolean
has_freq_scaling (gint cpu)
{
	gboolean ret;
	gchar *path;

	path = g_strdup_printf("/sys/devices/system/cpu/cpu%d/cpufreq", cpu);
	ret = g_file_test(path, G_FILE_TEST_IS_DIR);
	g_free(path);
	return ret;
}

gint
main (gint   argc,   /* IN */
      gchar *argv[]) /* IN */
{
	gdouble dashes[] = { 1.0, 4.0 };
	UberRange cpu_range = { 0., 100., 100. };
	GtkWidget *window;
	GtkWidget *cpu;
	GtkWidget *line;
	GtkWidget *map;
	GtkWidget *scatter;
	GtkWidget *label;
	GdkColor color;
	gint lineno;
	gint nprocs;
	gint i;
	gint mod;

	gtk_init(&argc, &argv);
	nprocs = get_nprocs();
	/*
	 * Warm up differential samplers.
	 */
	next_cpu_info();
	next_cpu_freq_info();
	/*
	 * Install event hook to track how many X events we are doing.
	 */
	gdk_event_handler_set(gdk_event_hook, NULL, NULL);
	/*
	 * Create window and graphs.
	 */
	window = uber_window_new();
	cpu = g_object_new(UBER_TYPE_LINE_GRAPH, NULL);
	line = g_object_new(UBER_TYPE_LINE_GRAPH, NULL);
	map = g_object_new(UBER_TYPE_HEAT_MAP, NULL);
	scatter = g_object_new(UBER_TYPE_SCATTER, NULL);
	/*
	 * Add lines for CPU graph.
	 */
	for (i = 0; i < nprocs; i++) {
		mod = i % G_N_ELEMENTS(default_colors);
		gdk_color_parse(default_colors[mod], &color);
		label = uber_label_new();
		uber_label_set_color(UBER_LABEL(label), &color);
		uber_line_graph_add_line(UBER_LINE_GRAPH(cpu), &color,
		                         UBER_LABEL(label));
		cpu_info.labels[i] = label;
		/*
		 * XXX: Add the line regardless. Just dont populate it if we don't
		 *      have data.
		 */
		lineno = uber_line_graph_add_line(UBER_LINE_GRAPH(cpu), &color, NULL);
		if (has_freq_scaling(i)) {
			uber_line_graph_set_dash(UBER_LINE_GRAPH(cpu), lineno,
									 dashes, G_N_ELEMENTS(dashes), 0);
			uber_line_graph_set_alpha(UBER_LINE_GRAPH(cpu), lineno, 1.);
		}
	}
	/*
	 * Adjust graph settings.
	 */
	label = uber_label_new();
	uber_label_set_text(UBER_LABEL(label), "Gdk Events");
	gdk_color_parse("#729fcf", &color);
	uber_line_graph_add_line(UBER_LINE_GRAPH(line), &color, UBER_LABEL(label));
	uber_graph_set_format(UBER_GRAPH(cpu), UBER_GRAPH_FORMAT_PERCENT);
	uber_line_graph_set_range(UBER_LINE_GRAPH(cpu), &cpu_range);
	uber_line_graph_set_data_func(UBER_LINE_GRAPH(cpu),
	                              get_cpu_info, NULL, NULL);
	uber_line_graph_set_data_func(UBER_LINE_GRAPH(line),
	                              get_xevent_info, NULL, NULL);
	/*
	 * Add graphs.
	 */
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(cpu), "CPU");
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(line), "GDK Events");
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(map), "IO Latency");
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(scatter), "IOPS By Size");
	/*
	 * Set heat map and scatter color.
	 */
	gdk_color_parse(default_colors[0], &color);
	uber_heat_map_set_fg_color(UBER_HEAT_MAP(map), &color);
	gdk_color_parse(default_colors[3], &color);
	uber_scatter_set_fg_color(UBER_SCATTER(scatter), &color);
	/*
	 * Show widgets.
	 */
	gtk_widget_show(scatter);
	gtk_widget_show(map);
	gtk_widget_show(line);
	gtk_widget_show(cpu);
	gtk_widget_show(window);
	/*
	 * Attach signals.
	 */
	g_signal_connect(window,
	                 "delete-event",
	                 G_CALLBACK(gtk_main_quit),
	                 NULL);
	/*
	 * Start sampling thread.
	 */
	g_thread_create(sample_thread, NULL, FALSE, NULL);
	gtk_main();
	return 0;
}
