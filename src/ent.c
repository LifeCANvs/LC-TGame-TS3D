#include "ent.h"
#include "grow.h"
#include "json.h"
#include "json-util.h"
#include "pixel.h"
#include "load-texture.h"
#include "string.h"
#include "util.h"
#include "xalloc.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parse_frame(struct json_node *node, struct ent_frame *frame,
	struct loader *ldr)
{
	const char *txtr_name = "";
	long duration = 1;
	switch (node->kind) {
	case JN_STRING:
		txtr_name = node->d.str;
		node->d.str = NULL;
		break;
	case JN_LIST:
		if (node->d.list.n_vals < 1
		 || node->d.list.vals[0].kind != JN_STRING) break;
		txtr_name = node->d.list.vals[0].d.str;
		if (node->d.list.n_vals < 2
		 || node->d.list.vals[1].kind != JN_NUMBER) break;
		duration = node->d.list.vals[1].d.num;
		break;
	default:
		break;
	}
	d3d_texture *txtr = load_texture(ldr, txtr_name);
	frame->txtr = txtr ? txtr : loader_empty_texture(ldr);
	frame->duration = duration;
}

struct ent_type *load_ent_type(struct loader *ldr, const char *name)
{
	struct logger *log = loader_logger(ldr);
	FILE *file;
	struct ent_type *ent, **entp;
	entp = loader_ent(ldr, name, &file);
	if (!entp) return NULL;
	ent = *entp;
	if (ent) return ent;
	ent = malloc(sizeof(*ent));
	struct json_node jtree;
	ent->name = str_dup(name);
	ent->frames = NULL;
	ent->n_frames = 0;
	ent->turn_chance = 0;
	ent->shoot_chance = 0;
	ent->death_spawn = NULL;
	ent->bullet = NULL;
	ent->lifetime = -1;
	if (parse_json_tree(name, file, log, &jtree)) return NULL;
	if (jtree.kind != JN_MAP) {
		if (jtree.kind != JN_ERROR)
			logger_printf(log, LOGGER_WARNING,
				"Entity type \"%s\" is not a JSON dictionary\n",
				name);
		goto end;
	}
	union json_node_data *got;
	if ((got = json_map_get(&jtree, "name", JN_STRING))) {
		free(ent->name);
		ent->name = got->str;
		got->str = NULL;
	} else {
		logger_printf(log, LOGGER_WARNING, "Entity type \"%s\" does "
			"not have a \"name\" attribute\n", name);
	}
	ent->width = 1.0;
	if ((got = json_map_get(&jtree, "width", JN_NUMBER)))
		ent->width = got->num;
	ent->height = 1.0;
	if ((got = json_map_get(&jtree, "height", JN_NUMBER)))
		ent->height = got->num;
	ent->speed = 0.0;
	if ((got = json_map_get(&jtree, "speed", JN_NUMBER)))
		ent->speed = got->num;
	if ((got = json_map_get(&jtree, "turn_chance", JN_NUMBER)))
		// Convert from percent to an equivalent portion of RAND_MAX.
		ent->turn_chance = got->num * (RAND_MAX / 100.0);
	if ((got = json_map_get(&jtree, "shoot_chance", JN_NUMBER)))
		ent->shoot_chance = got->num * (RAND_MAX / 100.0);
	ent->transparent = EMPTY_PIXEL;
	if ((got = json_map_get(&jtree, "transparent", JN_STRING))
			&& *got->str)
		ent->height = *got->str;
	ent->random_start_frame =
		(got = json_map_get(&jtree, "random_start_frame", JN_BOOLEAN))
		&& got->boolean;
	if ((got = json_map_get(&jtree, "frames", JN_LIST))) {
		ent->n_frames = got->list.n_vals;
		ent->frames = xmalloc(ent->n_frames * sizeof(*ent->frames));
		for (size_t i = 0; i < ent->n_frames; ++i) {
			parse_frame(&got->list.vals[i], &ent->frames[i], ldr);
		}
	}
	// To prevent infinite recursion with infinite cycles.
	*entp = ent;
	if ((got = json_map_get(&jtree, "death_spawn", JN_STRING)))
		ent->death_spawn = load_ent_type(ldr, got->str);
	if ((got = json_map_get(&jtree, "bullet", JN_STRING)))
		ent->bullet = load_ent_type(ldr, got->str);
	if ((got = json_map_get(&jtree, "lifetime", JN_NUMBER)))
		ent->lifetime = got->num;
end:
	if (ent->n_frames == 0) {
		ent->frames = xrealloc(ent->frames, sizeof(*ent->frames));
		ent->frames[0].txtr = loader_empty_texture(ldr);
		ent->frames[0].duration = 0;
	}
	free_json_tree(&jtree);
	*entp = ent;
	return ent;
}

char *ent_type_to_string(const struct ent_type *ent)
{
	char fmt_buf[128];
	size_t cap = 64;
	struct string str;
	string_init(&str, cap);
	string_pushz(&str, &cap, "ent_type { name = \"");
	string_pushz(&str, &cap, ent->name);
	string_pushn(&str, &cap, fmt_buf, sbprintf(fmt_buf, sizeof(fmt_buf),
		"\", width = %f, height = %f", ent->width, ent->height));
	if (ent->transparent >= 0) {
		string_pushn(&str, &cap, fmt_buf, sbprintf(fmt_buf,
			sizeof(fmt_buf),
			", transparent = '%c'", ent->transparent));
	}
	string_pushz(&str, &cap, ", frames = ");
	if (ent->n_frames > 0) {
		const char *before = "[ ";
		for (size_t i = 0; i < ent->n_frames; ++i) {
			const d3d_texture *txtr = ent->frames[i].txtr;
			string_pushn(&str, &cap, before, 2);
			string_pushn(&str, &cap, fmt_buf, sbprintf(fmt_buf,
				sizeof(fmt_buf),
				"texture { width = %zu, height = %zu }",
				d3d_texture_width(txtr),
				d3d_texture_height(txtr)));
			before = ", ";
		}
		string_pushn(&str, &cap, " ]", 2);
	} else {
		string_pushn(&str, &cap, "[]", 2);
	}
	string_pushn(&str, &cap, " }", 3);
	string_shrink_to_fit(&str);
	return str.text;
}

void ent_type_free(struct ent_type *ent)
{
	if (!ent) return;
	free(ent->name);
	free(ent->frames);
	free(ent);
}

void ent_init(struct ent *ent, struct ent_type *type, d3d_sprite_s *sprite,
	const d3d_vec_s *pos)
{
	ent->vel.x = ent->vel.y = 0;
	ent->type = type;
	ent->sprite = sprite;
	ent->lifetime = type->lifetime;
	ent->frame = type->random_start_frame ? rand() % type->n_frames : 0;
	ent->frame_duration = type->frames[0].duration;
	sprite->txtr = type->frames[0].txtr;
	sprite->transparent = type->transparent;
	sprite->pos = *pos;
	sprite->scale.x = type->width;
	sprite->scale.y = type->height;
}

void ent_tick(struct ent *ent)
{
	if (ent->type->lifetime < 0 || --ent->lifetime > 0) {
		if (--ent->frame_duration <= 0) {
			if (++ent->frame >= ent->type->n_frames) ent->frame = 0;
			ent->frame_duration =
				ent->type->frames[ent->frame].duration;
			ent->sprite->txtr = ent->type->frames[ent->frame].txtr;
		}
	} else if (ent->type->death_spawn) {
		ent_destroy(ent);
		ent_init(ent, ent->type->death_spawn, ent->sprite,
			&ent->sprite->pos);
	} else {
		ent->lifetime = -1;
	}
}

void ent_relocate(struct ent *ent, struct ent *to_ent, d3d_sprite_s *to_sprite)
{
	*to_ent = *ent;
	*to_sprite = *to_ent->sprite;
	to_ent->sprite = to_sprite;
}

bool ent_is_dead(const struct ent *ent)
{
	return ent->type->lifetime >= 0 && ent->lifetime < 0;
}

d3d_vec_s *ent_pos(struct ent *ent)
{
	return &ent->sprite->pos;
}

d3d_vec_s *ent_vel(struct ent *ent)
{
	return &ent->vel;
}

void ent_destroy(struct ent *ent)
{
	(void)ent;
}
