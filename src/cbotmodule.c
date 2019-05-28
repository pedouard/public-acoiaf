#pragma GCC optimize "O3,omit-frame-pointer,inline"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>

int64_t i2 = 0x0000444400004444LL;

#define SIZE_ ((int64_t) +12)
#define SIZE ((int64_t) +2 + SIZE_)

#define SIZE_SQ ((int64_t) +SIZE_*SIZE_)

#define INCOME_CELL ((int64_t) +1)
#define INCOME_MINE ((int64_t) +4)

#define UPKEEP1 ((int64_t) +1)
#define UPKEEP2 ((int64_t) +4)
#define UPKEEP3 ((int64_t) +20)

#define COST1 ((int64_t) +10)
#define COST2 ((int64_t) +20)
#define COST3 ((int64_t) +30)

#define TOWER_COST ((int64_t) +15)
#define MINE_BASE_COST ((int64_t) +20)
#define MINE_INC_COST ((int64_t) +4)

#define TYPE_MINE ((int64_t) +1)
#define TYPE_TOWER ((int64_t) +2)

#define CMD_TRAIN ((int64_t) +0)
#define CMD_BUILD ((int64_t) +1)
#define CMD_MOVE ((int64_t) +2)

#define MAX_COST ((int64_t) +150)

#define ABS(x) ((x)<0 ? -(x) : (x))
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

struct xy {
	int64_t x;
	int64_t y;
} typedef xy_t;

struct unit {
	xy_t xy;

	int64_t id;
	int64_t lvl;
	int64_t upkeep;
	int64_t has_moved;

	int64_t decaying;
} typedef unit_t;

struct cell {
	xy_t xy;
	int64_t wall;
	int64_t hq;
	int64_t minable;

	int64_t owner;
	int64_t tower;
	int64_t mine;
	int64_t building;

	int64_t p1_cost;
	int64_t p2_cost;
	int64_t protected;
	int64_t active;
	int64_t has_unit;
	unit_t * unit;

	int64_t expl;
} typedef cell_t;

struct player {
	xy_t hq;
	int64_t is_red;
	int64_t identity;

	int64_t nunits;
	unit_t units[SIZE_SQ];
	int64_t gold;

	int64_t lost;
	int64_t bankrupt;
	int64_t nmines;
	int64_t mine_cost;
	int64_t income;
	int64_t nactive;
} typedef player_t;


struct cmd {
	int64_t type;
	int64_t param;
	xy_t xy;
} typedef cmd_t;


struct ctx {
	int64_t turn;
	int64_t id_counter;

	cell_t board[SIZE*SIZE];

	cmd_t doable[SIZE_SQ*6];
	int64_t ndoable;

	player_t p1;
	player_t p2;
} typedef ctx_t;


ctx_t ctx = {0};
ctx_t ctx_future = {0};

clock_t tstart;

void print_board(ctx_t *ctx_) {
	int64_t x, y;
	cell_t *c;

	for(x=1; x<SIZE-1; x++) {
		for(y=1; y<SIZE-1; y++) {
			c = &(ctx_->board[y + SIZE*x]); // reverse for visualzation
			fprintf(stderr, " ");

			if (c->wall == 1) {
				fprintf(stderr, "#");

			} else if (c->owner == 1) {
				if (c->active == 1) {
					fprintf(stderr, "O");
				} else {
					fprintf(stderr, "o");
				}

			} else if (c->owner == -1) {
				if (c->active == 1) {
					fprintf(stderr, "X");
				} else {
					fprintf(stderr, "x");
				}
			} else {
				fprintf(stderr, ".");
			}

			if (c->wall == 1) {
				fprintf(stderr, "#");

			} else if (c->has_unit == 1) {
				fprintf(stderr, "%ld", c->unit->lvl);

			} else if (c->tower == 1) {
				fprintf(stderr, "T");

			} else if (c->hq == 1) {
				fprintf(stderr, "H");

			} else if (c->mine == 1) {
				fprintf(stderr, "M");

			} else if (c->minable == 1) {
				fprintf(stderr, "m");

			} else {
				fprintf(stderr, ".");
			}
		}
		fprintf(stderr, "\n");
	}
}

/**************************** GAME ENGINE ****************************/

void copy_ctx();
void reset_all(ctx_t *ctx_);
void reset_partial(ctx_t *ctx_);
void reset_expl(ctx_t *ctx_);
void complete_state(ctx_t *ctx_);
void list_doable(ctx_t * ctx_);
void apply_action(ctx_t *ctx_, cmd_t *cmd);
void print_action(cmd_t *cmd);

void set_unit_pointers(ctx_t *ctx_, player_t *p);

void explore_grid_bis(ctx_t *ctx_, cell_t *c1, cell_t *c2, int64_t identity, int64_t cost);
void explore_grid(ctx_t *ctx_, int64_t x, int64_t y, int64_t identity, int64_t cost);

int64_t can_move(unit_t *u, cell_t * c, int64_t identity) {
	if (c->wall == 1 || u->has_moved == 1) {
		return 0;
	}

	if (c->owner == identity) {
		// Moving on self territory
		if (c->has_unit || c->building) {
			return 0; // can't move on myself
		} else {
			return 1;
		}

	} else {
		// Moving somewhere else

		if (u->lvl == 3) {
			return 1; // lvl 3 goes wherever it wants
		}

		// inactive towers are unprotected but can't be taken TODO CHECK THAT
		if (c->protected == 1 || c->tower == 1) {
			return 0;
		}

		if (c->has_unit == 1 && c->unit->lvl >= u->lvl) {
			return 0;
		}

		return 1;
	}
}

int64_t can_train(cell_t * c, int64_t identity) {
	if (c->wall == 1) {
		return -1;
	}

	if (c->owner == identity) {
		if (c->has_unit || c->building) {
			return 0; // can't train on myself
		} else {
			// Training on self territory
			if (c->p2_cost <= COST1) {
				// maybe defend on self territory ?
				return COST1;
			} else {
				return 0;
			}
		}

	} else {
		// Moving somewhere else

		if (c->protected) {
			return COST3;
		}

		// inactive towers are unprotected but can't be taken TODO CHECK THAT
		if (c->tower == 1) {
			return COST3;
		}

		if (c->has_unit) {
			if (c->unit->lvl >= 2) {
				return COST3;
			} else {
				return COST2;
			}
		}

		return 10;
	}
}

void copy_ctx(ctx_t *ctx_target, ctx_t *ctx_source) {
	memcpy(ctx_target, ctx_source, sizeof(ctx_t));
	set_unit_pointers(ctx_target, &(ctx_target->p1));
	set_unit_pointers(ctx_target, &(ctx_target->p2));
}

void set_unit_pointers(ctx_t *ctx_, player_t *p) {
	int64_t i;
	unit_t * u;

	for (i=0; i < p->nunits; i++) {
		u = &(p->units[i]);
		if (u->id >= 0) {
			ctx_->board[u->xy.x + SIZE*u->xy.y].unit = u;
		}
	}
}

void reset_all(ctx_t *ctx_) {
	int64_t i, j;
	cell_t * c;

	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			c = &(ctx_->board[i + SIZE*j]);

			c->owner = 0;
			c->tower = 0;
			c->mine = 0;
			c->building = 0;
		}
	}

	ctx_->p1.nunits = 0;
	ctx_->p2.nunits = 0;

	reset_partial(ctx_);
}

void reset_partial(ctx_t *ctx_) {
	int64_t i, j;
	cell_t * c;

	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			c = &(ctx_->board[i + SIZE*j]);
			c->protected = 0;
			c->active = 0;
			c->has_unit = 0;
			c->p1_cost = MAX_COST;
			c->p2_cost = MAX_COST;
		}
	}

	ctx_->p1.income = 0;
	ctx_->p1.nactive = 0;
	ctx_->p1.nmines = 0;
	ctx_->p1.mine_cost = 0;
	ctx_->p1.bankrupt = 0;
	ctx_->p1.lost = 0;

	ctx_->p2.income = 0;
	ctx_->p2.nactive = 0;
	ctx_->p2.nmines = 0;
	ctx_->p2.mine_cost = 0;
	ctx_->p2.bankrupt = 0;
	ctx_->p2.lost = 0;

	for (i=0; i < ctx_->p1.nunits; i++) {
		ctx_->p1.units[i].decaying = 0;
	}
	for (i=0; i < ctx_->p2.nunits; i++) {
		ctx_->p2.units[i].decaying = 0;
		ctx_->p2.units[i].has_moved = 0;
	}
}

void reset_expl(ctx_t *ctx_) {
	int64_t i, j;
	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			ctx_->board[i + SIZE*j].expl = 0;
		}
	}
}


void explore_grid_bis(ctx_t *ctx_, cell_t *c1, cell_t *c2, int64_t identity, int64_t cost) {
	// looks like a good condition for activation
	if (c1->active == 1 && c1->owner == identity && c2->owner == identity && c2->active == 0) {
		c2->active = 1;
		explore_grid(ctx_, c2->xy.x, c2->xy.y, identity, cost);
		return;
	}

	// Find wether you can go or not and if you can, the cost
	int64_t new_cost, can_go, target_cost;
	new_cost = 0;
	can_go = 0;
	if (c2->owner == identity) {
		can_go = 1;
		new_cost = 0;
	} else if (c1->has_unit && c1->owner == identity && can_move(c1->unit, c2, identity) == 1) {
		can_go = 1;
		new_cost = 0;
	} else {
		new_cost = can_train(c2, identity);
		if (new_cost > 0) {
			can_go = 1;
		}
	}

	if (identity == 1) {
		target_cost = c2->p1_cost;
	} else {
		target_cost = c2->p2_cost;
	}

	if (can_go == 1 && cost + new_cost < target_cost) {
		explore_grid(ctx_, c2->xy.x, c2->xy.y, identity, cost + new_cost);
	}
}

void explore_grid(ctx_t *ctx_, int64_t x, int64_t y, int64_t identity, int64_t cost) {
	cell_t *c1, *c2;
	c1 = &(ctx_->board[x + SIZE*y]);

	// Apply new cost
	if (identity == 1) {
		c1->p1_cost = cost;
	} else {
		c1->p2_cost = cost;
	}

	c2 = &(ctx_->board[x+1 + SIZE*y]);
	explore_grid_bis(ctx_, c1, c2, identity, cost);

	c2 = &(ctx_->board[x + SIZE*(y+1)]);
	explore_grid_bis(ctx_, c1, c2, identity, cost);

	c2 = &(ctx_->board[x-1 + SIZE*y]);
	explore_grid_bis(ctx_, c1, c2, identity, cost);

	c2 = &(ctx_->board[x + SIZE*(y-1)]);
	explore_grid_bis(ctx_, c1, c2, identity, cost);
}

void put_units(player_t *p, ctx_t *ctx_) {
	int64_t i, j;
	unit_t * u;

	for (i=0; i < p->nunits; i++) {
		u = &(p->units[i]);

		if (u->id < 0) {
			continue; // ded
		}

		ctx_->board[u->xy.x + SIZE*u->xy.y].has_unit = 1;
		ctx_->board[u->xy.x + SIZE*u->xy.y].unit = u;
	}
}

void complete_player(player_t *p, ctx_t *ctx_) {
	int64_t i, j;
	cell_t * c;
	unit_t * u;

	// Those who lost connection are decaying
	for (i=0; i < p->nunits; i++) {
		u = &(p->units[i]);
		c = &(ctx_->board[u->xy.x + SIZE*u->xy.y]);

		if (c->active == 0) {
			u->decaying = 1;
		} else {
			u->decaying = 0;
		}
	}

	for (i=1; i < SIZE-1; i++) {
		for (j=1; j < SIZE-1; j++) {
			c = &(ctx_->board[i + SIZE*j]);

			if (c->active && c->owner == p->identity) {
				p->nactive += 1;

				if (c->mine == 1) {
					p->nmines += 1;
				}
			}
		}
	}

	p->income += p->nactive*INCOME_CELL;
	p->income += p->nmines*INCOME_MINE;

	for (i=0; i < p->nunits; i++) {
		u = &(p->units[i]);

		if (u->id < 0) {
			continue; // ded
		}

		if (u->decaying == 0) {
			if (u->lvl == 1) {
				p->income -= UPKEEP1;
			} else if (u->lvl == 2) {
				p->income -= UPKEEP2;
			} else {
				p->income -= UPKEEP3;
			}
		}
	}

	p->mine_cost = MINE_BASE_COST + MINE_INC_COST*p->nmines;

	if (ctx_->board[p->hq.x + SIZE*p->hq.y].owner != p->identity) {
		p->lost = 1;
	}

	if (p->gold + p->income < 0) {
		p->bankrupt = 1;
	}
}

void complete_state(ctx_t *ctx_) {
	int64_t i, j, x, y;
	cell_t *c1, *c2;

	reset_partial(ctx_);

	put_units(&(ctx_->p1), ctx_);
	put_units(&(ctx_->p2), ctx_);

	// Active
	reset_expl(ctx_);
	ctx_->board[ctx_->p1.hq.x + SIZE*ctx_->p1.hq.y].expl = 1;
	ctx_->board[ctx_->p1.hq.x + SIZE*ctx_->p1.hq.y].active = 1;
	explore_grid(ctx_, ctx_->p1.hq.x, ctx_->p1.hq.y, ctx_->p1.identity, 0);

	reset_expl(ctx_);
	ctx_->board[ctx_->p2.hq.x + SIZE*ctx_->p2.hq.y].expl = 1;
	ctx_->board[ctx_->p2.hq.x + SIZE*ctx_->p2.hq.y].active = 1;
	explore_grid(ctx_, ctx_->p2.hq.x, ctx_->p2.hq.y, ctx_->p2.identity, 0);


	// Towers
	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			c1 = &(ctx_->board[i + SIZE*j]);

			if (c1->tower == 1 && c1->active == 1) {
				c1->protected = 1;

				c2 = &(ctx_->board[i+1 + SIZE*j]);
				if (c2->wall == 0 && c2->owner == c1->owner) {
					c2->protected = 1;
				}
				c2 = &(ctx_->board[i + SIZE*(j+1)]);
				if (c2->wall == 0 && c2->owner == c1->owner) {
					c2->protected = 1;
				}
				c2 = &(ctx_->board[i-1 + SIZE*j]);
				if (c2->wall == 0 && c2->owner == c1->owner) {
					c2->protected = 1;
				}
				c2 = &(ctx_->board[i + SIZE*(j-1)]);
				if (c2->wall == 0 && c2->owner == c1->owner) {
					c2->protected = 1;
				}
			}
		}
	}

	complete_player(&(ctx_->p1), ctx_);
	complete_player(&(ctx_->p2), ctx_);
}

void try_add_train_action(ctx_t *ctx_, cell_t *c, player_t *p) {
	int64_t cost;
	cmd_t * cmd;

	cost = can_train(c, p->identity);
	if (cost > 0) {
		if (cost == COST3 && p->gold >= COST3) {
			cmd = &(ctx_->doable[ctx_->ndoable]);
			cmd->type = CMD_TRAIN;
			cmd->param = 3;
			cmd->xy = c->xy;
			ctx_->ndoable += 1;
		}

		if (cost == COST2 && p->gold >= COST2) {
			cmd = &(ctx_->doable[ctx_->ndoable]);
			cmd->type = CMD_TRAIN;
			cmd->param = 2;
			cmd->xy = c->xy;
			ctx_->ndoable += 1;
		}

		if (cost == COST1 && p->gold >= COST1) {
			cmd = &(ctx_->doable[ctx_->ndoable]);
			cmd->type = CMD_TRAIN;
			cmd->param = 1;
			cmd->xy = c->xy;
			ctx_->ndoable += 1;
		}
	}
}

void find_trainable(ctx_t *ctx_, int64_t x, int64_t y, player_t *p) {
	cell_t * c;

	c = &(ctx_->board[x + SIZE*y]);
	c->expl = 1;

	if (c->owner != p->identity || c->active == 0) {
		// no exploration
		return;
	}

	c = &(ctx_->board[x+1 + SIZE*y]);
	if (c->expl == 0) {
		try_add_train_action(ctx_, c, p);

		if (c->owner == p->identity && c->active == 1) {
			// Explore if in territory
			find_trainable(ctx_, x+1, y, p);
		}
	}
	c->expl = 1;

	c = &(ctx_->board[x + SIZE*(y+1)]);
	if (c->expl == 0) {
		try_add_train_action(ctx_, c, p);

		if (c->owner == p->identity && c->active == 1) {
			// Explore if in territory
			find_trainable(ctx_, x, y+1, p);
		}
	}
	c->expl = 1;

	c = &(ctx_->board[x-1 + SIZE*y]);
	if (c->expl == 0) {
		try_add_train_action(ctx_, c, p);

		if (c->owner == p->identity && c->active == 1) {
			// Explore if in territory
			find_trainable(ctx_, x-1, y, p);
		}
	}
	c->expl = 1;

	c = &(ctx_->board[x + SIZE*(y-1)]);
	if (c->expl == 0) {
		try_add_train_action(ctx_, c, p);

		if (c->owner == p->identity && c->active == 1) {
			// Explore if in territory
			find_trainable(ctx_, x, y-1, p);
		}
	}
	c->expl = 1;
}

void list_doable(ctx_t * ctx_) {
	int64_t i, j, x, y;
	cell_t * c;
	player_t * p;
	unit_t * u;
	cmd_t * cmd;

	p = &(ctx_->p1);
	ctx_->ndoable = 0;

	// CMD_MOVE
	for (i=0; i < p->nunits; i++) {
		u = &(p->units[i]);

		if (u->id < 0) {
			continue; // ded
		}

		x = u->xy.x + 1;
		y = u->xy.y;
		c = &(ctx_->board[x + SIZE*y]);
		if (can_move(u, c, p->identity)) {
			cmd = &(ctx_->doable[ctx_->ndoable]);
			cmd->type = CMD_MOVE;
			cmd->param = u->id;
			cmd->xy = c->xy;
			ctx_->ndoable += 1;
		}

		x = u->xy.x;
		y = u->xy.y + 1;
		c = &(ctx_->board[x + SIZE*y]);
		if (can_move(u, c, p->identity)) {
			cmd = &(ctx_->doable[ctx_->ndoable]);
			cmd->type = CMD_MOVE;
			cmd->param = u->id;
			cmd->xy = c->xy;
			ctx_->ndoable += 1;
		}

		x = u->xy.x - 1;
		y = u->xy.y;
		c = &(ctx_->board[x + SIZE*y]);
		if (can_move(u, c, p->identity)) {
			cmd = &(ctx_->doable[ctx_->ndoable]);
			cmd->type = CMD_MOVE;
			cmd->param = u->id;
			cmd->xy = c->xy;
			ctx_->ndoable += 1;
		}

		x = u->xy.x;
		y = u->xy.y - 1;
		c = &(ctx_->board[x + SIZE*y]);
		if (can_move(u, c, p->identity)) {
			cmd = &(ctx_->doable[ctx_->ndoable]);
			cmd->type = CMD_MOVE;
			cmd->param = u->id;
			cmd->xy = c->xy;
			ctx_->ndoable += 1;
		}
	}

	// CMD_BUILD
	for (i=1; i < SIZE - 1; i++) {
		for (j=1; j < SIZE - 1; j++) {
			c = &(ctx_->board[i + SIZE*j]);

			if (c->active == 0 || c->owner != p->identity || c->has_unit == 1 || c->building == 1) {
				continue;
			}

			if (c->minable == 1 && c->mine == 0 && p->gold >= ctx_->p1.mine_cost) {
				cmd = &(ctx_->doable[ctx_->ndoable]);
				cmd->type = CMD_BUILD;
				cmd->param = TYPE_MINE;
				cmd->xy = c->xy;
				ctx_->ndoable += 1;
			}

			if (c->minable == 0 && p->gold >= TOWER_COST && c->p2_cost < 120 && c->protected == 0) {
				cmd = &(ctx_->doable[ctx_->ndoable]);
				cmd->type = CMD_BUILD;
				cmd->param = TYPE_TOWER;
				cmd->xy = c->xy;
				ctx_->ndoable += 1;
			}
		}
	}

	// CMD_TRAIN
	if (p->gold >= COST1) {
		reset_expl(ctx_);
		find_trainable(ctx_, ctx_->p1.hq.x, ctx_->p1.hq.y, &(ctx_->p1));
	}
}

void apply_action(ctx_t *ctx_, cmd_t *cmd) {
	int64_t i;
	cell_t * c;
	unit_t * u;
	player_t * p;

	p = &(ctx_->p1);
	c = &(ctx_->board[cmd->xy.x + SIZE*cmd->xy.y]);

	if (cmd->type == CMD_MOVE) {
		// MOVE

		for (i=0; i<p->nunits; i++) {
			if (p->units[i].id == cmd->param) {
				u = &(p->units[i]);
				break;
			}
		}

		u->has_moved = 1;
		u->xy = cmd->xy;

		c->owner = p->identity;
		c->tower = 0;
		c->mine = 0;
		c->building = 0;

		if (c->has_unit) {
			c->unit->id = -1; // kill it
		}
	} else if (cmd->type == CMD_TRAIN) {
		// TRAIN

		c->owner = p->identity;
		c->tower = 0;
		c->mine = 0;
		c->building = 0;

		if (c->has_unit) {
			c->unit->id = -1; // kill it
		}

		p->units[p->nunits].id = ctx_->id_counter; // arbitrary
		ctx_->id_counter += 1;
		p->units[p->nunits].lvl = cmd->param;
		p->units[p->nunits].has_moved = 1;
		p->units[p->nunits].xy = cmd->xy;

		if (cmd->param == 1) {
			p->units[p->nunits].upkeep = UPKEEP1;
			p->gold -= COST1;
		} else if (cmd->param == 2) {
			p->units[p->nunits].upkeep = UPKEEP2;
			p->gold -= COST2;
		} else if (cmd->param == 3) {
			p->units[p->nunits].upkeep = UPKEEP3;
			p->gold -= COST3;
		}

		p->nunits += 1;

	} else {
		// BUILD
		c->building = 1;
		if (cmd->param == TYPE_MINE) {
			c->mine = 1;
			p->gold -= p->mine_cost;
		} else {
			c->tower = 1;
			p->gold -= TOWER_COST;
		}
	}

	reset_partial(ctx_);
	complete_state(ctx_);
}

void print_action(cmd_t *cmd) {
	if (cmd->type == CMD_MOVE) {
		printf("MOVE %ld ", cmd->param);
	} else if (cmd->type == CMD_TRAIN) {
		printf("TRAIN %ld ", cmd->param);
	} else {
		if (cmd->param == TYPE_MINE) {
			printf("BUILD MINE ");
		} else {
			printf("BUILD TOWER ");
		}
	}

	int64_t x, y;
	if (ctx.p1.is_red == 1) {
		x = cmd->xy.x - 1;
		y = cmd->xy.y - 1;
	} else {
		x = SIZE - cmd->xy.x - 2;
		y = SIZE - cmd->xy.y - 2;
	}

	printf("%ld %ld;", x, y);
}

/**************************** MAIN LOOP ****************************/

void init();
void update();
void run();

int64_t main() {
	init();

	while (1) {
		update();
		run();
	}

	return 0;
}

void set_initial_state() {
	int64_t i, x, y, n;

	// Set borders as walls
	for (i=0; i < SIZE; i++) {
		ctx.board[i + SIZE*0].wall = 1;
		ctx.board[i + SIZE*(SIZE-1)].wall = 1;

		ctx.board[0 + SIZE*i].wall = 1;
		ctx.board[SIZE-1 + SIZE*i].wall = 1;
	}

	// Set hq positions
	ctx.board[1 + SIZE*1].hq = 1;
	ctx.board[1 + SIZE*1].building = 1;
	ctx.board[SIZE - 2 + SIZE*(SIZE-2)].hq = 1;
	ctx.board[SIZE - 2 + SIZE*(SIZE-2)].building = 1;

	ctx.p1.identity = 1;
	ctx.p1.hq.x = 1;
	ctx.p1.hq.y = 1;

	ctx.p2.identity = -1;
	ctx.p2.hq.x = SIZE - 2;
	ctx.p2.hq.y = SIZE - 2;

	// set xy of each cell
	for (x=0; x < SIZE; x++) {
		for (y=0; y < SIZE; y++) {
			ctx.board[x + SIZE*y].xy.x = x;
			ctx.board[x + SIZE*y].xy.y = y;
		}
	}
}

void init() {
	int64_t i, x, y, n;
	set_initial_state();

	scanf("%ld", &n);
	for (i=0; i < n; i++) {
		scanf("%ld%ld", &x, &y);

		ctx.board[x + 1 + SIZE*(y + 1)].minable = 1;
	}
}

void update() {
	int64_t i, j, is_red, x, y, n, iter, owner, type, id, lvl;
	char board_line[SIZE_ + 1];
	char c;
	player_t *p;

	scanf("%ld", &(ctx.p1.gold));
	scanf("%ld", &(ctx.p1.income));
	scanf("%ld", &(ctx.p2.gold));
	scanf("%ld", &(ctx.p2.income));

	reset_all(&ctx);

	for (i=0; i < SIZE_; i++) {
		scanf("%s", board_line);

		// Figure out which player is where
		if (ctx.turn == 0) {
			if (board_line[0] == 'O') {
				ctx.p1.is_red = 1;
			} else {
				ctx.p2.is_red = 1;
			}
		}
		is_red = ctx.p1.is_red;

		for (j=0; j < SIZE_; j++) {
			c = board_line[j];

			if (is_red) {
				x = j + 1;
				y = i + 1;
			} else {
				x =  SIZE_ - j;
				y =  SIZE_ - i;
			}

			if (c == 'o' || c == 'O') {
				ctx.board[x + SIZE*y].owner = 1;
			} else if (c == 'x' || c == 'X') {
				ctx.board[x + SIZE*y].owner = -1;
			} else if (c == '#') {
				ctx.board[x + SIZE*y].wall = 1;
			}
		}
	}

	scanf("%ld", &n);
	for (iter=0; iter < n; iter++) {
		scanf("%ld%ld%ld%ld", &owner, &type, &i, &j);

		if (is_red) {
			x = i + 1;
			y = j + 1;
		} else {
			x =  SIZE_ - i;
			y =  SIZE_ - j;
		}

		ctx.board[x + SIZE*y].building = 1;

		if (type == TYPE_MINE) {
			ctx.board[x + SIZE*y].mine = 1;
		} else if (type == TYPE_TOWER) {
			ctx.board[x + SIZE*y].tower = 1;
		}
	}

	scanf("%ld", &n);
	for (iter=0; iter<n; iter++) {
		scanf("%ld%ld%ld%ld%ld", &owner, &id, &lvl, &i, &j);

		if (is_red) {
			x = i + 1;
			y = j + 1;
		} else {
			x =  SIZE_ - i;
			y =  SIZE_ - j;
		}

		if (owner == 0) {
			p = &(ctx.p1);
		} else {
			p = &(ctx.p2);
		}

		p->units[p->nunits].xy.x = x;
		p->units[p->nunits].xy.y = y;
		p->units[p->nunits].lvl = lvl;
		p->units[p->nunits].id = id;
		p->units[p->nunits].has_moved = 0;

		if (lvl == 1) {
			p->units[p->nunits].upkeep = UPKEEP1;
		} else if (lvl == 2) {
			p->units[p->nunits].upkeep = UPKEEP2;
		} else {
			p->units[p->nunits].upkeep = UPKEEP3;
		}

		p->nunits += 1;
	}

	tstart = clock();
	complete_state(&ctx);
}

void run() {
	clock_t tend;
	tend = clock();
	printf("MSG %.2lf\n", (float) (1000*(tend - tstart) / CLOCKS_PER_SEC));
	ctx.turn += 1;
}

/*PYTHON_UTILS*/
void py_get_ctx(ctx_t *in) {
	copy_ctx(in, &ctx);
}

void py_set_ctx(ctx_t *out) {
	copy_ctx(&ctx, out);
}

void py_reset() {
	memset(&ctx, 0, sizeof(ctx_t));
	ctx.id_counter = 10000;
	set_initial_state();
}

void py_swap() {
	// player swap
	player_t p;
	unit_t *u;
	p = ctx.p1;
	ctx.p1 = ctx.p2;
	ctx.p2 = p;

	ctx.p1.identity = 1;
	ctx.p1.hq.x = 1;
	ctx.p1.hq.y = 1;

	ctx.p2.identity = -1;
	ctx.p2.hq.x = 12;
	ctx.p2.hq.y = 12;

	int64_t i, j, i_, j_;
	cell_t c;
	for (i=0; i<SIZE; i++) {
		for (j=0; j<SIZE/2; j++) {
			i_ = SIZE - i - 1;
			j_ = SIZE - j - 1;

			c = ctx.board[i + SIZE*j];
			ctx.board[i + SIZE*j] = ctx.board[i_ + SIZE*j_];
			ctx.board[i_ + SIZE*j_] = c;
		}
	}

	for (i=0; i<SIZE; i++) {
		for (j=0; j<SIZE; j++) {
			ctx.board[i + SIZE*j].xy.x = i;
			ctx.board[i + SIZE*j].xy.y = j;
			ctx.board[i + SIZE*j].owner *= -1;
		}
	}

	for (i=0; i<ctx.p1.nunits; i++) {
		u = &(ctx.p1.units[i]);
		u->xy.x = SIZE - u->xy.x - 1;
		u->xy.y = SIZE - u->xy.y - 1;
	}

	for (i=0; i<ctx.p2.nunits; i++) {
		u = &(ctx.p2.units[i]);
		u->xy.x = SIZE - u->xy.x - 1;
		u->xy.y = SIZE - u->xy.y - 1;
	}


	reset_partial(&ctx);
	complete_state(&ctx);
}

void py_new_turn() {
	int64_t i, j = 0;
	unit_t *u;

	// Update gold
	ctx.p1.gold += ctx.p1.income;
	if (ctx.p1.gold < 0) {
		ctx.p1.gold = 0;
	}

	// Remove ded units from memory
	for (i=0; i<ctx.p1.nunits; i++) {
		u = &(ctx.p1.units[i]);

		if (u->decaying == 0 && u->id >= 0 && ctx.p1.bankrupt == 0) {
			// The unit is still alive
			u->has_moved = 0;
			ctx.p1.units[j] = *u;
			j += 1;
		}
	}
	ctx.p1.nunits = j;

	reset_partial(&ctx);
	complete_state(&ctx);
}

void py_print_state() {
	fprintf(stderr, "%ld (%ld) - (%ld) %ld\n", ctx.p1.gold, ctx.p1.income, ctx.p2.income, ctx.p2.gold);
	print_board(&ctx);
}

void py_apply_action(cmd_t *cmd) {
	apply_action(&ctx, cmd);
}

void py_list_doable(int64_t move) {
	list_doable(&ctx);
}

