#ifndef MAP_H_
#define MAP_H_

#include "d3d.h"
#include "loader.h"
#include "ent.h"
#include "team.h"
#include <stdbool.h>
#include <stdint.h>

// A specification of where some entity should begin.
struct map_ent_start {
	// The starting position.
	d3d_vec_s pos;
	// The type of the entitiy.
	struct ent_type *type;
	// The team of the entity.
	enum team team;
	// The starting frame of the entity.
	size_t frame;
};

struct map {
	// The allocated map name.
	char *name;
	// The board of visual blocks from the blocks array.
	d3d_board *board;
	// The grid of the same width and height as the board, documenting the
	// walls. If the bit (1 << direction) is set in a tile, there is a wall
	// in that direction.
	uint8_t *walls;
	// The blocks used in the board. These refer to textures in the table
	// passed to load_map(s).
	d3d_block_s *blocks;
	// The starting number of entities.
	size_t n_ents;
	// The list of entity types and corresponding starting positions. This
	// refers to data in the ents table passed to load_map(s).
	struct map_ent_start *ents;
	// The starting information of the player in the map.
	struct map_ent_start player;
};

// Move an object's position so as not to conflict with the map's walls. The
// object's malleable position is stored in pos. The object is a square with
// side length (2 * radius).
void map_check_walls(struct map *map, d3d_vec_s *pos, double radius);

// Load a map with the name or use one previously loaded. Allocate the map.
struct map *load_map(struct loader *ldr, const char *name);

// Free a loaded map. Does nothing when given NULL.
void map_free(struct map *map);

#endif /* MAP_H_ */
