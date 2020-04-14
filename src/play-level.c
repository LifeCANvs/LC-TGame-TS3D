#include "play-level.h"
#include "ent.h"
#include "map.h"
#include "loader.h"
#include "logger.h"
#include "pixel.h"
#include "player.h"
#include "save-state.h"
#include "ticker.h"
#include "ui-util.h"
#include "util.h"
#include <ctype.h>
#include <math.h>

// The number of ticks a turn lasts, triggered by a single key press. This is to
// smooth out key detection speeds for different terminals, as some wait longer
// before registering another press.
#define TURN_DURATION 5

// Create all the entities specified by the entity start specifications
// (struct map_ent_start) in the given map.
static void init_entities(struct ents *ents, struct map *map)
{
	ents_init(ents, map->n_ents * 2);
	for (size_t i = 0; i < map->n_ents; ++i) {
		ent_id e = ents_add(ents, map->ents[i].type, map->ents[i].team,
			&map->ents[i].pos);
		*ents_worth(ents, e) = map->ents[i].team == TEAM_ENEMY;
	}
}

// Move the player based on the input key. translation and turn_duration are
// used as persistent state. translation is the last translation key pressed
// ('w', 'a', etc.) turn_duration is a number whose absolute value specifies
// the remaining turn ticks and whose sign indicates the turn direction.
static void move_player(struct player *player,
	int *translation, int *turn_duration, int key)
{
	key = tolower(key);
	switch (key) {
	case 'w': // Forward
	case 's': // Backward
	case 'a': // Left
	case 'd': // Right
		*translation = *translation != key ? key : '\0';
		break;
	case 'q': // Turn CCW
		*turn_duration = +TURN_DURATION;
		break;
	case 'e': // Turn CW
		*turn_duration = -TURN_DURATION;
		break;
	}
	switch (*translation) {
	case 'w': // Forward
		player_walk(player, 0);
		break;
	case 's': // Backward
		player_walk(player, PI);
		break;
	case 'a': // Left
		player_walk(player, PI / 2);
		break;
	case 'd': // Right
		player_walk(player, -PI / 2);
		break;
	default:
		break;
	}
	if (*turn_duration > 0) {
		player_turn_ccw(player);
		--*turn_duration;
	} else if (*turn_duration < 0) {
		player_turn_cw(player);
		++*turn_duration;
	}
}

// Move the given entities on the map. The entities will approach the player if
// they can turn and move. Entities that die upon hitting the wall will do so.
static void move_ents(struct ents *ents, struct map *map, struct player *player)
{
	map_check_walls(map, &player->body.pos, player->body.radius);
	ENTS_FOR_EACH(ents, e) {
		const struct ent_type *type = ents_type(ents, e);
		d3d_vec_s *epos = ents_pos(ents, e);
		d3d_vec_s *evel = ents_vel(ents, e);
		epos->x += evel->x;
		epos->y += evel->y;
		d3d_vec_s disp;
		if (chance_decide(type->turn_chance)) {
			disp.x = epos->x - player->body.pos.x;
			disp.y = epos->y - player->body.pos.y;
			vec_norm_mul(&disp, -type->speed);
		} else {
			disp.x = disp.y = 0;
		}
		d3d_vec_s move = *epos; // Movement due to wall collision.
		map_check_walls(map, &move, ents_body(ents, e)->radius);
		if (type->wall_die && (move.x != epos->x || move.y != epos->y))
		{
			ents_kill(ents, e);
		} else if (type->wall_block) {
			disp.x += move.x - epos->x;
			disp.y += move.y - epos->y;
			*epos = move;
			// The entity will move from the wall later.
			if (disp.x != 0.0) evel->x = disp.x;
			if (disp.y != 0.0) evel->y = disp.y;
		}
	}
}

// Have entities collide with each other.
static void hit_ents(struct ents *ents)
{
	ENTS_FOR_EACH_PAIR(ents, ea, eb) {
		enum team ta = ents_team(ents, ea), tb = ents_team(ents, eb);
		if (teams_can_collide(ta, tb)) {
			if (bodies_collide(ents_body(ents, ea),
				ents_body(ents, eb))
			 && (ta == TEAM_ALLY || tb == TEAM_ALLY))
				beep();
		}
	}
}

// Shoot the bullets of the entities who want to shoot, adding them to the
// entity pool.
static void shoot_bullets(struct ents *ents)
{
	ENTS_FOR_EACH(ents, e) {
		struct ent_type *type = ents_type(ents, e);
		if (type->bullet && chance_decide(type->shoot_chance)) {
			d3d_vec_s pos = *ents_pos(ents, e);
			ent_id bullet = ents_add(ents, type->bullet,
				ents_team(ents, e), &pos);
			d3d_vec_s *bvel = ents_vel(ents, bullet), d_bvel;
			d_bvel = *bvel = *ents_vel(ents, e);
			vec_norm_mul(&d_bvel, ents_type(ents, bullet)->speed);
			bvel->x += d_bvel.x;
			bvel->y += d_bvel.y;
		}
	}
}

// Count the remaining number of targets standing in the way of level winning.
static int get_remaining(struct ents *ents)
{
	int remaining = 0;
	ENTS_FOR_EACH(ents, e) {
		remaining += *ents_worth(ents, e);
	}
	return remaining;
}

int play_level(const char *root_dir, struct save_state *save,
	const char *map_name, struct ticker *timer, struct logger *log)
{
	struct loader ldr;
	loader_init(&ldr, root_dir);
	logger_free(loader_set_logger(&ldr, log));
	struct map *map = load_map(&ldr, map_name);
	if (!map) {
		logger_printf(loader_logger(&ldr), LOGGER_ERROR,
			"Failed to load map \"%s\"\n", map_name);
		goto error_map;
	}
	if (map->prereq && !save_state_is_complete(save, map->prereq))
		goto error_map; // Map not unlocked.
	loader_print_summary(&ldr);
	srand(time(NULL)); // For random_start_frame
	struct ents ents;
	init_entities(&ents, map);
	d3d_board *board = map->board;
	struct meter health_meter = {
		.label = "HEALTH",
		.style = pixel_style(pixel(PC_BLACK, PC_GREEN)),
		// Position and size not initialized yet.
	};
	struct meter reload_meter = {
		.label = "RELOAD",
		.style = pixel_style(pixel(PC_BLACK, PC_RED)),
		// Position and size not initialized yet.
	};
	WINDOW *dead_popup = NULL;
	WINDOW *pause_popup = NULL;
	WINDOW *quit_popup = NULL;
	d3d_camera *cam = NULL;
	struct player player;
	player_init(&player, map);
	timeout(0);
	keypad(stdscr, TRUE);
	int translation = '\0'; // No initial translation
	int turn_duration = 0; // No initial turning
	bool won = false;
	bool paused = false;
	bool quitting = false;
	bool do_redraw = true;
	int known_lines, known_cols;
	clear();
	for (;;) {
		static const char dead_msg[] =
			"You died.\n"
			"Press Y to return to the menu.";
		static const char pause_msg[] =
			"Game paused.\n"
			"Press P to resume.";
		static const char quit_msg[] =
			"Are you sure you want to quit?\n"
			"Press Y to confirm or N to cancel.";
		bool resized = !cam ||
			sync_screen_size(known_lines, known_cols);
		if (resized) {
			known_lines = LINES;
			known_cols = COLS;
			d3d_free_camera(cam);
			// LINES - 1 so that one is reserved for the health and
			// reload meters:
			cam = camera_with_dims(COLS, LINES > 0 ? LINES - 1 : 0);
			// Takes up the left half of the bottom:
			health_meter.x = 0;
			health_meter.y = LINES - 1;
			health_meter.width = COLS / 2;
			health_meter.win = stdscr;
			// Takes up the right half of the bottom:
			reload_meter.x = health_meter.width;
			reload_meter.y = LINES - 1;
			reload_meter.width = COLS - health_meter.width;
			reload_meter.win = stdscr;
			if (dead_popup) {
				delwin(dead_popup);
				dead_popup = popup_window(dead_msg);
			}
			if (pause_popup) {
				delwin(pause_popup);
				pause_popup = popup_window(pause_msg);
			}
			if (quit_popup) {
				delwin(quit_popup);
				quit_popup = popup_window(quit_msg);
			}
			do_redraw = true;
		}
		int remaining = get_remaining(&ents);
		// Player wins if all targets gone and they are not, or if they
		// won already:
		won = won || (remaining <= 0 && !player_is_dead(&player));
		bool lost = !won && player_is_dead(&player);
		if (do_redraw) {
			player_move_camera(&player, cam);
			d3d_draw_walls(cam, board);
			d3d_draw_sprites(cam, ents_num(&ents),
					ents_sprites(&ents));
			display_frame(cam, stdscr);
			health_meter.fraction = player_health_fraction(&player);
			meter_draw(&health_meter);
			reload_meter.fraction = player_reload_fraction(&player);
			meter_draw(&reload_meter);
			attron(A_BOLD);
			if (won) {
				mvaddstr(0, 0,
					"YOU WIN! Press Y to return to menu.");
			} else {
				mvprintw(0, 0, "TARGETS LEFT: %d", remaining);
			}
			attroff(A_BOLD);
			refresh();
		}
		do_redraw = resized;
		int key = getch();
		int lowkey = tolower(key);
		if (dead_popup) {
			touchwin(dead_popup);
			wrefresh(dead_popup);
		} else if (!lost && quitting) {
			if (quit_popup) {
				touchwin(quit_popup);
				wrefresh(quit_popup);
			}
			switch (lowkey) {
			case 'y':
				goto quit;
			case 'n':
				if (quit_popup) {
					delwin(quit_popup);
					quit_popup = NULL;
					do_redraw = true;
				}
				quitting = false;
				break;
			}
			continue;
		} else if (!lost && paused) {
			if (pause_popup) {
				touchwin(pause_popup);
				wrefresh(pause_popup);
			}
			switch (lowkey) {
			case 'p':
				if (pause_popup) {
					delwin(pause_popup);
					pause_popup = NULL;
					do_redraw = true;
				}
				paused = false;
				break;
			case 'x':
				quit_popup = popup_window(quit_msg);
				quitting = true;
				break;
			}
			continue;
		}
		do_redraw = true;
		if (won && lowkey == 'y') {
			// Player quits after winning.
			goto quit;
		} else if (lost) {
			// Player lost, entities still simulated. Y to quit.
			if (lowkey == 'y') goto quit;
			if (!dead_popup) dead_popup = popup_window(dead_msg);
		} else if (lowkey == 'p') {
			paused = true;
			pause_popup = popup_window(pause_msg);
			continue;
		} else if (lowkey == 'x' || key == ESC) {
			quitting = true;
			quit_popup = popup_window(quit_msg);
			continue;
		} else {
			// Otherwise, let the player be controlled.
			move_player(&player,
				&translation, &turn_duration, key);
		}
		move_ents(&ents, map, &player);
		player_collide(&player, &ents);
		hit_ents(&ents);
		// Let the player shoot. This is blocked by player_try_shoot if
		// the player is dead:
		if (isupper(key) || key == ' ')
			player_try_shoot(&player, &ents);
		shoot_bullets(&ents);
		player_tick(&player);
		ents_tick(&ents);
		ents_clean_up_dead(&ents);
		tick(timer);
	}
quit:
	clear();
	refresh();
	if (pause_popup) delwin(pause_popup);
	if (quit_popup) delwin(quit_popup);
	if (dead_popup) delwin(dead_popup);
	d3d_free_camera(cam);
	// Record the player's winning:
	if (won) save_state_mark_complete(save, map_name);
	ents_destroy(&ents);
	loader_free(&ldr);
	return 0;

error_map:
	loader_free(&ldr);
	return -1;
}

