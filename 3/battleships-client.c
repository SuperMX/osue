/*!
	\file	battleships-client.h
	\author	Christian Fiedler <e1363562@student.tuwien.ac.at>
	\date	10.01.2015

	\brief	battleships game client

	\details
		with this program you can play battleships
		over a server application with another person
*/
#include <common.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

#include <curses.h>


const char *progname;
static int player_nr = 0;
static int mysem = -1;
static const char *status = NULL;

static const char *STAGE_STR[] = {
	"Waiting for your opponent ...",
	"Position your ship! Use Space to toggle blocks and Enter to confirm!",
	"It's your turn! Fire with Space! or surrender with Ctrl+C...",
	"It's your opponent's turn!",
	"Server shutting down"
};

/*! 
	\brief this function leaves the server gracefully
	\details
		sem[SEM_EXIT] will be posted
		shared->players will be decremented
		ncurses will be stopped, if needed
		exit is called with EXIT_SUCCESS
*/
static void cleanup(void)
{
	if(!isendwin())
		endwin();

	if(shared != NULL){
		++shared->players_gone;
		shared->stage = STAGE_SHUTDOWN;
		if(player_nr == 1 || player_nr == 2)
			++shared->surrender[player_nr-1];
	}

	for(int i = 0; i < LEN(sem); i++)
		if(i != SEM_START && sem[i] != SEM_FAILED)
			sem_post(sem[i]);

	exit(EXIT_SUCCESS);
}

/*! 
	\brief this function maps stage numbers to strings
	\param stage
		stage number (see unnamed enum #2 in struct SharedStructure)
	\return a string corresponding to the given stage
	\details
		a direct mapping with STAGE_STR[stage] is not possible
		to get the correct string, player_nr has to be taken
		into account.
*/
static const char* map_stage_str(int stage)
{
	assert(player_nr == 1 || player_nr == 2);
	assert(stage >= 0 && stage < STAGE_LAST);

	if(player_nr == 2){
		if(shared->stage == STAGE_TURN1)
			return STAGE_STR[STAGE_TURN2];
		else if(shared->stage == STAGE_TURN2)
			return STAGE_STR[STAGE_TURN1];
	}
	return STAGE_STR[shared->stage];
}

/*! checks if the server was shutdown, if yes it calls cleanup*/
static void check_shutdown(void)
{
	if(shared->stage == STAGE_SHUTDOWN){
		exitsig = 1;
		endwin();
		(void)fprintf(stderr, "Server shutdown!\n");
		(void)fprintf(stderr, "%s\n", SRV_MSG[shared->errorcode]);
		if(shared->errorcode == EC_GAMEOVER){
			(void)fprintf(stderr,player_nr == shared->won ? "You won!\n" : "You lost!\n");
		}
		cleanup();
	}
}

#define GFX_W	(FIELD_W*2+1+2)
#define GFX_H	(FIELD_H+4)

/*!
	\brief this function draws the game board using ncurses
	\param win
		ncurses window with dimensions (GFX_W, GFX_H)
	\param gamef
		const pointer to game file structure
	\param cursor_x
		the player's cursor position on the X axis
	\param cursor_y
		the player's cursor position on the Y axis
	\param stage_assume
		the stage that is assumed to be the current one
		if negative, the function will use shared->stage instead.
*/
static void draw_game_field(WINDOW *win, const struct Field *const gamef, int cursor_x, int cursor_y, int stage_assume)
{
	int x, y;
	(void)clear();
	(void)wclear(win);
	(void)box(win, '|', '-');

	for(y = 0; y < FIELD_H; y++){
		for(x = 0; x < FIELD_W; x++){
			(void)mvwaddch(win, y+2, x*2+2, FIELD_CHAR[gamef->buf[x][y]]);
		}
	}
	
	cursor_y += 2;
	cursor_x = cursor_x*2+2;

	(void)mvwaddch(win, cursor_y, cursor_x-1, '>');
	(void)mvwaddch(win, cursor_y, cursor_x+1, '<');

	(void)touchwin(win);
	(void)refresh();
	(void)wrefresh(win);

	if(player_nr == 1)
		(void)mvaddstr(0, 2, "PLAYER 1");
	else if(player_nr == 2)
		(void)mvaddstr(0, 2, "PLAYER 2");
	else
		assert(0);
	
	if(shared != NULL && stage_assume < 0)
		(void)mvaddstr(1, 2, map_stage_str(shared->stage));
	else
		(void)mvaddstr(1, 2, STAGE_STR[stage_assume]);
	
	if(status != NULL)
		(void)mvaddstr(LINES-1, 2, status);

	(void)refresh();

	(void)move(0,0);
}

/*!
	\brief builds a Ship data structure from a Field data structure
	\param gamef
		a const pointer to the game field (struct Field) where
		the ship has been positioned
	\return
		nonzero on success, zero otherwise
	\details
		if the functions succeeds, the corresponding ship data structure
		in the shared data structure (shared->ship[player_nr-1])
		will be set to correct values
*/
static int build_ship(const struct Field *const gamef)
{
	struct Ship *const myship = &shared->ship[player_nr-1];
	int coordn = 0, x, y;
	for(y = 0; y < FIELD_H; y++){
		for(x = 0; x < FIELD_W; x++){
			if(gamef->buf[x][y] != FIELD_NIL){
				if(coordn == SHIP_COORDS) return 0;
				myship->c[coordn].x = x;
				myship->c[coordn].y = y;
				coordn++;
			}
		}
	}
	if(coordn != SHIP_COORDS) return 0;

	return ship_check(myship);
}

/*!
	\brief this function lets the player position his/her ship
	\param win
		ncurses window with dimensions (GFX_W, GFX_H)
*/
static void set_ship(WINDOW *win)
{
	struct Field gamef = {{{0}}};
	int enter = 0, x,y;

	for(y = 0; y < FIELD_H; y++){
		for(x = 0; x < FIELD_W; x++){
			gamef.buf[x][y] = FIELD_NIL;
		}
	}

	x = y = 0;

	status = NULL;
	do{
		draw_game_field(win, &gamef, x, y, -1);
		int ch = getch();

		check_shutdown();

		switch(ch){
		case KEY_ENTER:
		case '\n': case '\r':
			enter = build_ship(&gamef);
			if(enter == 0)
				status = "Error! Try again!";
			break;
		case KEY_LEFT:
		case 'A': case 'a':
			if(x > 0) x--;
			break;
		case KEY_RIGHT:
		case 'D': case 'd':
			if(x < FIELD_W-1) x++;
			break;
		case KEY_UP:
		case 'W': case 'w':
			if(y > 0) y--;
			break;
		case KEY_DOWN:
		case 'S': case 's':
			if(y < FIELD_H-1) y++;
			break;
		case ' ':
			if(gamef.buf[x][y] == FIELD_NIL)
				gamef.buf[x][y] = FIELD_HIT;
			else
				gamef.buf[x][y] = FIELD_NIL;
			break;
		}
	}while(enter == 0);
	status = NULL;
}


/*!
	\brief this function lets the player shoot at a position
	\param win
		ncurses window with dimensions (GFX_W, GFX_H)
	\param gamef
		Field data structure (game field)
	\param x
		the player's cursor position on the X axis
	\param y
		the player's cursor position on the Y axis
	\return
		a Coord struct (by value!) with the shot coordinates
	\details
		the function will not return until the player
		selected a position, that he didn't shoot at, yet.
*/
static struct Coord shoot(WINDOW *win, struct Field *gamef, int *const x, int *const y)
{
	struct Coord shot;
	int space = 0;

	assert(*x >= 0 && *y >= 0 && *x < FIELD_W && *y < FIELD_H);

	do{
		draw_game_field(win, gamef, *x, *y, -1);
		const int ch = getch();
		switch(ch){
		case KEY_LEFT:
		case 'A': case 'a':
			if(*x > 0) (*x)--;
			break;
		case KEY_RIGHT:
		case 'D': case 'd':
			if(*x < FIELD_W-1) (*x)++;
			break;
		case KEY_UP:
		case 'W': case 'w':
			if(*y > 0) (*y)--;
			break;
		case KEY_DOWN:
		case 'S': case 's':
			if(*y < FIELD_H-1) (*y)++;
			break;
		case ' ':
			shot.x = *x;
			shot.y = *y;

			if(gamef->buf[*x][*y] == FIELD_HIT || gamef->buf[*x][*y] == FIELD_WATER)
				status = "You already shot there!";
			else
				space = 1;
			break;
		}

	}while(space == 0);
	status = NULL;
	
	return shot;
}

/*! sets surrender flag in shared and calls cleanup */
static void set_surrender(int x)
{
	cleanup();
}

/*! \brief does everything */
int main(int argc, char **argv)
{
	struct Field gamef = {{{0}}};
	int cursor_x, cursor_y;

	progname = argv[0];
	if(argc != 1){
		usage();
		return EXIT_FAILURE;
	}

	if(setup_signal_handler(set_surrender) == 0)
		bail_out("setup_signal_handler");

	atexit(free_common_ressources);

	const int oflag = 0;
	sem[SEM_START] = sem_open(SEM_NAME[SEM_START], oflag);
	sem[SEM_EXIT] = sem_open(SEM_NAME[SEM_EXIT], oflag);
	sem[SEM_GLOBAL] = sem_open(SEM_NAME[SEM_GLOBAL], oflag);
	sem[SEM_1] = sem_open(SEM_NAME[SEM_1], oflag);
	sem[SEM_2] = sem_open(SEM_NAME[SEM_2], oflag);
	sem[SEM_SYNC] = sem_open(SEM_NAME[SEM_SYNC], oflag);

	for(int i = 0; i < LEN(sem); i++){
		if(sem[i] == SEM_FAILED){
			bail_out("sem_open");
		}
	}

	if(allocate_shared(0) == 0)
		bail_out("allocate_shared");

	
	/* requesting access and setting player number */
	(void)printf("Connecting to server...\n");
	do{
		sem_wait_cb(sem[SEM_START], cleanup);
		check_shutdown();
	}while(shared->stage != STAGE_WAIT && shared->players >= 2);

	(void)initscr();
	(void)cbreak();
	(void)keypad(stdscr, TRUE);
	WINDOW *win = newwin(GFX_H,GFX_W,3,2);

	cursor_x = cursor_y = 0;

	player_nr = ++shared->players;
	if(player_nr == 1)
		mysem = SEM_1;
	else if(player_nr == 2)
		mysem = SEM_2;
	else
		assert(0);

	draw_game_field(win, &gamef, cursor_x, cursor_y, STAGE_WAIT);
	sem_post(sem[mysem]);

	/* waiting for SET stage */
	sem_wait_cb(sem[SEM_SYNC],cleanup);
	check_shutdown();
	assert(shared->stage == STAGE_SET);

	set_ship(win);

	draw_game_field(win, &gamef, cursor_x, cursor_y, STAGE_WAIT);
	sem_post(sem[mysem]);
	sem_wait_cb(sem[SEM_GLOBAL],cleanup);
	check_shutdown();

	for(;;){
		sem_wait_cb(sem[mysem],cleanup);
		check_shutdown();

		/* it's our turn */
		struct Coord shot = shoot(win, &gamef, &cursor_x, &cursor_y);
		shared->shot = shot;
		
		sem_post(sem[SEM_SYNC]);
		sem_wait_cb(sem[mysem],cleanup);
		check_shutdown();

		/* the server tells us, if we hit, now */
		gamef.buf[shot.x][shot.y] = (shared->hit == 0 ? FIELD_WATER : FIELD_HIT);
		
		draw_game_field(win, &gamef, cursor_x, cursor_y, STAGE_TURN2);
		sem_post(sem[SEM_SYNC]);

	}

	delwin(win); 
	endwin();
	sem_post(sem[SEM_EXIT]);

	cleanup();
}
