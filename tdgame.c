/* tdgame.c */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>


#define LEVER_INDEX(r,c)   (8*(r)+(c))
#define COIN_INDEX(r,c,s)  (9*2*(r)+2*(c)+s)

#define max(a,b)  ((a) > (b) ? (a) : (b))
#define min(a,b)  ((a) < (b) ? (a) : (b))


typedef unsigned int uint_t;


typedef enum {
	LEVER_STATE_LEFT = 0,
	LEVER_STATE_RIGHT
} lever_state_t;

typedef struct {
	lever_state_t state;
	uint_t coin;
} lever_t;

typedef struct {
	uint_t round;
	uint_t round_scores[2];
	uint_t last_round_drop;

	uint_t scores[2];

	lever_t levers[5*8];

	uint_t last_round_halved;

	uint_t current_player;
} tdgame_t;

typedef union {
	/* Lever state: 2*30 bits,
	   Round: 3 bits,
	   Last round halved: 1 bit,
	   Scores and round scores: 4*16 bits. */
	uint8_t id[16];
	struct {
		uint64_t lever;
		uint32_t score;
		uint32_t rscore;
	} s;
} tdgame_id_t;


typedef uint8_t slot_t;

typedef struct {
	slot_t slot[8];
	float value[8];
} move_list_t;

typedef struct {
	int depth;
	float alpha;
	float beta;
	uint_t move_count;
	move_list_t moves;
} tdgame_ht_value_t;

typedef struct tdgame_ht_entry tdgame_ht_entry_t;

struct tdgame_ht_entry {
	tdgame_ht_entry_t *next;
	tdgame_id_t id;
	tdgame_ht_value_t value;
};

typedef struct {
	size_t size;
	uint_t entry_count;
	tdgame_ht_entry_t **entries;
} tdgame_ht_t;


static const uint_t round_scores[][16] = {
	{ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
        { 34, 21, 13, 8, 5, 3, 2, 1, 1, 2, 3, 5, 8, 13, 21, 34 },
        { 9, 8, 7, 6, 5, 4, 3, 2, 2, 3, 4, 5, 6, 7, 8, 9 },
        { 64, 49, 36, 25, 16, 9, 4, 1, 1, 4, 9, 16, 25, 36, 49, 64 }
};

static const uint_t round_target[] = { 10, 40, 20, 80 };


static void
tdgame_init(tdgame_t* game, uint_t last_round_halved)
{
	game->round = 0;
	game->round_scores[0] = 0;
	game->round_scores[1] = 0;
	game->last_round_drop = 0;

	game->scores[0] = 0;
	game->scores[1] = 0;

	for (int row = 0; row < 5; row++) {
		for (int col = 0; col < row+4; col++) {
			lever_t *lever = &game->levers[LEVER_INDEX(row, col)];
			lever->state = LEVER_STATE_LEFT;
			lever->coin = 0;
		}
	}

	game->last_round_halved = last_round_halved;

	game->current_player = 0;
}

static uint_t
tdgame_is_done(const tdgame_t *game)
{
	return game->round > 3;
}

static uint_t
score_from_slot(const tdgame_t *game, uint_t slot)
{
	uint_t score = round_scores[game->round][slot];
	if (game->last_round_halved && game->round == 3 &&
	    slot != 7 && slot != 8) {
		score /= 2;
	}
	return score;
}

static void
tdgame_print(const tdgame_t* game)
{
	if (!tdgame_is_done(game)) {
		printf("Round %i%s (player %i)\n",
		       game->round, game->last_round_drop ? "+" : "",
		       game->current_player+1);
	} else {
		printf("Game over\n");
	}

	printf("Score: %i, %i\n", game->scores[0], game->scores[1]);
	printf("Round score: %i, %i\n",
	       game->round_scores[0], game->round_scores[1]);

	fputs("        1 2 3 4 5 6 7 8\n", stdout);
	for (int row = 0; row < 5; row++) {
		int indent = 2*(4-row);

		for (int i = 0; i < indent; i++) fputs(" ", stdout);
		for (int col = 0; col < row+4; col++) {
			const lever_t *lever =
				&game->levers[LEVER_INDEX(row, col)];
			printf("%s %s|", (lever->coin && lever->state ==
					  LEVER_STATE_LEFT) ? "0" : " ",
			       (lever->coin && lever->state ==
				LEVER_STATE_RIGHT) ? "0" : " ");
		}
		fputs("\n", stdout);

		for (int i = 0; i < indent; i++) fputs(" ", stdout);
		for (int col = 0; col < row+4; col++) {
			const lever_t *lever =
				&game->levers[LEVER_INDEX(row, col)];
			printf("%s|", lever->state ==
			       LEVER_STATE_LEFT ? "\\  " : "  /");
		}
		fputs("\n", stdout);

		for (int i = 0; i < indent; i++) fputs(" ", stdout);
		for (int col = 0; col < row+4; col++) {
			fputs(" + |", stdout);
		}
		fputs("\n", stdout);

		for (int i = 0; i < indent; i++) fputs(" ", stdout);
		for (int col = 0; col < row+4; col++) {
			const lever_t *lever =
				&game->levers[LEVER_INDEX(row, col)];
			printf("%s ", lever->state ==
			       LEVER_STATE_LEFT ? " |\\" : "/| ");
		}
		fputs("\n", stdout);
	}

	if (!tdgame_is_done(game)) {
		for (int i = 0; i < 16; i++) {
			printf("%i ", score_from_slot(game, i));
		}
		fputs("\n", stdout);
	}
}

static int
handle_lever_drop(tdgame_t *game, int row, int col,
		  uint_t coins[], uint_t next_coins[])
{
	lever_t *lever = &game->levers[LEVER_INDEX(row, col)];

	int head = lever->state == LEVER_STATE_RIGHT;
	int tail = lever->state == LEVER_STATE_LEFT; 

	int more_coins = 0;

	if (coins[COIN_INDEX(row, col, head)] > 0) {
		uint_t count = coins[COIN_INDEX(row, col, head)];
		if (!lever->coin) {
			lever->coin = 1;
			count -= 1;
		}

		coins[COIN_INDEX(row, col, tail)] += count;
	}

	if (coins[COIN_INDEX(row, col, tail)] > 0) {
		uint_t count = coins[COIN_INDEX(row, col, tail)];
		more_coins = 1;

		if (lever->coin) {
			next_coins[COIN_INDEX(row, col, head)] += 1;
			lever->coin = 0;
		}

		if ((count % 2) == 1) {
			lever->state = (lever->state == LEVER_STATE_LEFT) ?
				LEVER_STATE_RIGHT : LEVER_STATE_LEFT;
		}

		next_coins[COIN_INDEX(row+1, col+tail, head)] += count;
	}

	return more_coins;
}

static uint_t
tdgame_drop_coin(tdgame_t* game, uint_t slot)
{
	uint_t score = 0;
	uint_t coins[6*9*2];
	memset(coins, '\0', sizeof(coins));

	coins[slot] += 1;

	uint_t more_coins = 1;
	while (more_coins) {
		uint_t next_coins[6*9*2];
		memset(next_coins, '\0', sizeof(next_coins));
		more_coins = 0;

		for (int row = 0; row < 5; row++) {
			for (int col = 0; col < row+4; col++) {
				int r = handle_lever_drop(game, row, col,
							  coins, next_coins);
				more_coins = r || more_coins;
			}
		}

		uint_t *score_slots = &coins[5*9*2+1];
		for (int i = 0; i < 16; i++) {
			if (score_slots[i] > 0) {
				score += score_slots[i] *
					score_from_slot(game, i);
			}
		}

		memcpy(coins, next_coins, sizeof(coins));
	}

	game->round_scores[game->current_player] += score;
	game->scores[game->current_player] += score;

	if (game->last_round_drop) {
		game->round += 1;
		game->last_round_drop = 0;
		game->round_scores[0] = 0;
		game->round_scores[1] = 0;
	} else if (game->round_scores[game->current_player] >
		   round_target[game->round]) {
		game->last_round_drop = 1;
	}

	game->current_player = (game->current_player + 1) % 2;

	return score;
}

static void
tdgame_id_init(tdgame_id_t *id, const tdgame_t *game)
{
	/* Generate lever state. */
	for (int row = 0; row < 5; row++) {
		for (int col = 0; col < row+4; col++) {
			const lever_t *lever =
				&game->levers[LEVER_INDEX(row, col)];
			id->s.lever = (id->s.lever << 2) |
				((lever->state == LEVER_STATE_RIGHT) << 1) |
				lever->coin;
		}
	}

	id->s.lever = (id->s.lever << 4) |
		((game->round & 7) << 1) |
		game->last_round_halved;

	/* Generate score state. */
	uint_t other_player = (game->current_player + 1) % 2;
	id->s.score =
		((game->scores[game->current_player] & 0xffff) << 16) |
		(game->scores[other_player] & 0xffff);
	id->s.rscore =
		((game->round_scores[game->current_player] & 0xffff) << 16) |
		(game->round_scores[other_player] & 0xffff);
}

static uint32_t
tdgame_id_hash(const tdgame_id_t *id)
{
	/* FNV-1 */
	uint32_t hash = 2166136261;
	for (int i = 0; i < 16; i++) {
		hash *= 16777619;
		hash ^= id->id[i];
	}

	return hash;
}

static int
tdgame_ht_init(tdgame_ht_t *ht, size_t size)
{
	ht->size = size;
	ht->entries = calloc(size, sizeof(tdgame_ht_entry_t *));
	if (ht->entries == NULL) return -1;

	ht->entry_count = 0;

	return 0;
}

static tdgame_ht_value_t *
tdgame_ht_lookup(tdgame_ht_t *ht, const tdgame_id_t *id)
{
	uint32_t hash = tdgame_id_hash(id);
	tdgame_ht_entry_t *entry =
		(tdgame_ht_entry_t *)&ht->entries[hash % ht->size];

	/* The first entry pointed to is not really an
	   entry but a sentinel. */
	while (entry->next != NULL) {
		entry = entry->next;

		if (memcmp(id, &entry->id, sizeof(tdgame_id_t)) == 0) {
			return &entry->value;
		}
	}

	return NULL;
}

static tdgame_ht_value_t *
tdgame_ht_store(tdgame_ht_t *ht, const tdgame_id_t *id)
{
	uint32_t hash = tdgame_id_hash(id);
	tdgame_ht_entry_t *entry =
		(tdgame_ht_entry_t *)&ht->entries[hash % ht->size];

	/* The first entry pointed to is not really an
	   entry but a sentinel. */
	while (entry->next != NULL) {
		entry = entry->next;

		if (memcmp(id, &entry->id, sizeof(tdgame_id_t)) == 0) {
			return &entry->value;
		}
	}

	tdgame_ht_entry_t *new_entry = calloc(1, sizeof(tdgame_ht_entry_t));
	if (new_entry == NULL) return NULL;

	ht->entry_count += 1;

	entry->next = new_entry;
	memcpy(&new_entry->id, id, sizeof(tdgame_id_t));

	return &new_entry->value;
}

static float
tdgame_evaluate_done(const tdgame_t *game)
{
	uint_t other_player = (game->current_player + 1) % 2;

	if (game->scores[game->current_player] >
	    game->scores[other_player]) {
		return INFINITY;
	} else if (game->scores[game->current_player] <
		   game->scores[other_player]) {
		return -INFINITY;
	}

	return 0.0;
}

static float
tdgame_evaluate(const tdgame_t *game)
{
	uint_t other_player = (game->current_player + 1) % 2;

	if (tdgame_is_done(game)) {
		return tdgame_evaluate_done(game);
	}

	return (float)game->scores[game->current_player] -
		(float)game->scores[other_player];
}

static uint_t
tdgame_fill_move_list(const tdgame_t *game, move_list_t *moves)
{
	const uint_t default_order[] = { 0, 7, 1, 6, 2, 5, 3, 4 };

	uint_t move_count = 0;
	for (int i = 0; i < 8; i++) {
		uint_t slot = default_order[i];
		const lever_t *lever = &game->levers[LEVER_INDEX(0, slot/2)];

		if (!lever->coin ||
		    ((slot % 2) == (lever->state == LEVER_STATE_LEFT))) {
			moves->slot[move_count] = slot;
			moves->value[slot] = -INFINITY;
			move_count += 1;
		}
	}

	return move_count;
}

static void
moves_sort_by_table(slot_t moves[], uint_t size, const float table[8])
{
	/* Insertion sort. */
	for (uint_t i = 1; i < size; i++) {
		slot_t key = moves[i];

		int j = i - 1;
		while (j >= 0 && table[moves[j]] < table[key]) {
			moves[j+1] = moves[j];
			j -= 1;
		}

		moves[j+1] = key;
	}
}

static uint_t
tdgame_predict(const tdgame_t *game, tdgame_ht_t *ht, int depth,
	       float alpha, float beta, move_list_t *moves)
{
	if (tdgame_is_done(game)) {
		moves->slot[0] = 0;
		moves->value[0] = tdgame_evaluate_done(game);
		return 1;
	}

	tdgame_id_t id = {};
	tdgame_id_init(&id, game);
	tdgame_ht_value_t *stored = tdgame_ht_lookup(ht, &id);
	if (stored != NULL) {
	        float best_stored = stored->moves.value[stored->moves.slot[0]];

		if (stored->depth >= depth) {
			if (best_stored > stored->alpha &&
			    best_stored < stored->beta) {
				/* Exact value */
				memcpy(moves, &stored->moves,
				       sizeof(move_list_t));
				return stored->move_count;
			} else if (best_stored >= stored->beta) {
				/* Lower bound */
				alpha = max(alpha, best_stored);
			} else if (best_stored <= stored->alpha) {
				/* Upper bound */
				beta = min(beta, best_stored);
			}

			if (alpha >= beta) {
				memcpy(moves, &stored->moves,
				       sizeof(move_list_t));
				return stored->move_count;
			}
		}
	}

	float score = -INFINITY;
	uint_t move_count = tdgame_fill_move_list(game, moves);

	if (stored != NULL) {
		moves_sort_by_table(moves->slot, move_count,
				    stored->moves.value);
	}

	uint_t next_move_count = 0;

	for (int i = 0; i < move_count; i++) {
		uint_t slot = moves->slot[i];

		tdgame_t test_game;
		memcpy(&test_game, game, sizeof(tdgame_t));
		tdgame_drop_coin(&test_game, slot);

		float value = 0.0;
		if (depth <= 0) {
			value = -tdgame_evaluate(&test_game);
		} else {
			move_list_t bmoves;
			tdgame_predict(&test_game, ht, depth-1,
				       -beta, -max(alpha, score), &bmoves);
			value = -bmoves.value[bmoves.slot[0]];
		}

		moves->value[slot] = value;
		next_move_count += 1;

		score = max(score, value);
		if (score >= beta) break;
	}

	moves_sort_by_table(moves->slot, next_move_count, moves->value);

	if (stored == NULL || stored->depth <= depth) {
		if (stored == NULL) stored = tdgame_ht_store(ht, &id);
		stored->depth = depth;
		stored->alpha = alpha;
		stored->beta = beta;

		stored->move_count = next_move_count;
		memcpy(&stored->moves, moves, sizeof(move_list_t));
	}

	return next_move_count;
}

int
main(int argc, char *argv[])
{
	int r;

	tdgame_t game;
	tdgame_init(&game, 0);

	tdgame_ht_t game_ht;
	r = tdgame_ht_init(&game_ht, 16*1024*1024);
	if (r < 0) {
		fputs("Initialization of hashtable failed.\n", stderr);
		exit(EXIT_FAILURE);
	}

	tdgame_drop_coin(&game, 4);
	tdgame_drop_coin(&game, 1);
	tdgame_drop_coin(&game, 3);
	tdgame_drop_coin(&game, 0);
	tdgame_drop_coin(&game, 2);
	tdgame_drop_coin(&game, 7);

	tdgame_drop_coin(&game, 4);
	tdgame_drop_coin(&game, 1);
	tdgame_drop_coin(&game, 3);
	tdgame_drop_coin(&game, 0);
	tdgame_drop_coin(&game, 2);
	tdgame_drop_coin(&game, 7);

	tdgame_print(&game);

	int depth = atoi(argv[1]);
	int max_depth = atoi(argv[2]);
	while (depth <= max_depth) {
		move_list_t moves;
		uint_t move_count = tdgame_predict(&game, &game_ht, depth,
						   -INFINITY, INFINITY,
						   &moves);

		printf("%i: Suggests: ", depth);
		for (uint_t i = 0; i < move_count; i++) {
			if (i > 0) fputs(", ", stdout);
			printf("%i: %.1f", moves.slot[i]+1,
			       moves.value[moves.slot[i]]);
		}
		fputs("\n", stdout);
		printf("  entries: %i\n", game_ht.entry_count);

		depth += 1;
	}

	return EXIT_SUCCESS;
}
