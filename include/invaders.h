/**
 * @file games.h
 * @author Luka Keserič
 * @date 15 July 2020
 * @brief Functions to preform basic interaction between objects 
 * in the game Space invaders
 *
 */

#ifndef __INVADERS_H__
#define __INVADERS_H__

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/**
 * @defgroup GAMES
 *
 * Objects and functions to imitate a Space Invaders game 
 *
 * @{
 */

/**
 * @name Set the number 2-dimensional arrays used in the game
 *
 * Parameters passed to the structures to initialize them with
 * correct dimensions
 *
 * @{
 */

#define ENEMY_ROWS 5

#define ENEMY_COLUMNS 11

#define BUNKER_WIDTH 8 

#define BUNKER_HEIGHT 6 

/**@}*/


/**
 * @brief Object to represent a player 
 *
 * A player is created with a wall_t structure which is described more thourougly 
 * in TUM_Ball.h header file, but it essencialy represents a rectangle i.e. 
 * boundary for the player's avatar
 *
 * With it there is an associated ship position which is the absolute position in pixles 
 * of the middle of the player's avatar
 * 
 * Player also has lives and score that are determined by the game developer
 * and are tracked in the player structure for easier tracking of the numbers
 * 
 * Lastly the structure includes a Semaphore handle for locking when player's
 * data is being updated or just read.
 */
typedef struct player_data {
    wall_t *ship;
    signed int ship_position;
    unsigned short lives;
    unsigned short score;
    SemaphoreHandle_t lock;
} space_ship_t;

/**
 * @brief Object to represent bullet 
 *
 * A player is created with a ball_t structure which is described more thourougly 
 * in TUM_Ball.h header file, but it essencialy represents a circle i.e. 
 * boundary for the player's or invader's bullet.
 *
 * With it there is an associated bullet_state (either ATTACKING or PASSIVE)
 * which is used to signal when a player can shoot since there can only be
 * a limited number of active bullets in this implementation of the game
 * 
 * In this implementation it is also used to send signals to AI Binary
 * 
 */
typedef struct bullet_data {
    ball_t *bullet;
    char bullet_state; // ATTACKING (on the screeen) or PASSIVE (not on she screen)
} bullet_t;

/**
 * @brief Object to represent individual enemies and their respective states or positions.
 *
 * An enemy is created with a wall_t structure which is described more thourougly 
 * in TUM_Ball.h header file, but it essencialy represents a rectangle i.e. 
 * boundary for the enemy's avatar.
 *
 * With it there is an associated image state that is used for animating the enemys between
 * arbitrary number of states.
 * 
 * Every enemy is initialised in a dead state and then it is set to ALIVE state, when we want to
 * detect colisions with it and draw it's associated avatar.
 *
 * All enemys in space invaders game have a certain number of points which are determined by the 
 * game developer himself/herself.
 * 
 * Lastly the structure includes a Semaphore handle for locking when enemy's
 * data is being updated or just read.
 */
typedef struct enemy_unit_data {
    wall_t *enemy;
    unsigned short image_state;							
    unsigned short dead;		
    unsigned short *points; 	
    SemaphoreHandle_t lock; // will only be initialized for the mothership
} enemy_t;

/**
 * @brief Object to keep track of the all invaders in the game
 *
 * This structure is initialized with a 2-dimensional array of enemy units,
 * where the dimensions are determined by how many enemy rows and columns we want to have.
 * 
 * There is also a counter for how many invaders the player has killed to navigate to the next level
 * when the player has killed all the enemys.
 * 
 * Direction of movement is the same for all invaders that's why it is kept with the group data,
 * and it is switched with the calling of certain functions to change the direction of movement.
 */
typedef struct invader_group_data {
    enemy_t enemys[ENEMY_ROWS][ENEMY_COLUMNS];
    unsigned int killed_invaders;
    unsigned short direction;
} invaders_t;

/**
 * @brief Object to represent a small block of the bigger bunker
 *
 * A bunker block is created with a wall_t structure which is described more thourougly 
 * in TUM_Ball.h header file, but it essencialy represents a rectangle i.e. 
 * boundary for the small bunker block (that is part of the bigger bunker).
 *
 * With it there are an associated row and column that store the relative location of the block
 * in the bigger bunker structure.
 * 
 * Every bunker block is initialised in a dead state and then it is set to ALIVE state, 
 * when we want to detect colisions with it and draw it.
 */
typedef struct bunker_block {
    wall_t *bunker_block;
    unsigned short block_row;
    unsigned short block_column;
    unsigned short dead;
} block_t;

/**
 * @brief Object to represent individual bunker, made up of many smaller bunker blocks.
 *
 * A bunker structure is initialised with a 2-dimensional array of smaller bunker blocks,
 * where dimensions determine the number of smaller blocks we want in either rows or columns
 * 
 * Every bunker has a different location. Since there are 4 bunkers, the x and y coordinates
 * represent a point in the top left corner of each individual the bunker, which is used
 * as a starting point for initialization of smaller bunker blocks.
 */
typedef struct bunker_data {
    block_t bunker[BUNKER_HEIGHT][BUNKER_WIDTH];
    unsigned int bunker_y_location; 
    unsigned int bunker_x_location;
} bunker_t;

/**
 * @brief An array where currently pressed buttons are shown
 *
 * If the individual button is pressed can be verified with passing a SDL Scancode value. 
 * More detailed description on the following webpage: https://wiki.libsdl.org/SDL_Scancode
 *
 */
typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

/**
 * @brief Gets player keyboard input
 *
 * Updates the array of pressed and not pressed buttons
 *
 */
void xGetButtonInput(void);


/**
 * @brief Evaluates score and adds an extra life if conditions are met
 *
 * @param *score a pointer to the variable where the player's score is stored
 * @param *lives a pointer to the variable where the player's lives are stored
 * @param *reset_score a pointer to the variable where the reset score is stored
 * (reset score is normally set to zero, but can be changed with cheats)
 *
 */
void vExtraLives(unsigned short *lives, unsigned short *score, unsigned short *reset_score);

/**
 * @brief Increments the absolute player position
 *
 * @param *position a pointer to the variable where the player's location is stored
 * @param obj_width is a value of player's avatar width
 * @param speed of the player determined by the developer
 * acts as a counter limiter, to increment speed number of times
 *
 */
void vIncrement(signed int *position, int obj_width, int speed);

/**
 * @brief Decrements the absolute player position
 *
 * @param *position a pointer to the variable where the player's location is stored
 * @param obj_width is a value of player's avatar width
 * @param speed of the player determined by the developer
 * acts as a counter limiter, to decrement speed number of times
 *
 */
void vDecrement(signed int *position, int obj_width, int speed);


/**
 * @brief Checks if the player has pressed a button and changes position accordingly
 *
 * @param *player_position_x a pointer to absolute location on x coordinate
 * @param obj_width is a value of player's avatar width
 *
 */
unsigned char xCheckPlayerInput(signed int *player_position_x, int obj_width);

/**
 * @brief Resets all the values of the bunker to default (as initialized)
 *
 * @param *bunker a pointer to a bunker object that will be reset
 *
 */
void resetBunkers (bunker_t *bunker);

/**
 * @brief Resets all the values of invaders group to default
 *
 * @param *invaders a pointer to the invaders group
 * 
 */
void resetInvaders (invaders_t *invaders);

/**
 * @brief Resets the mothership values to default 
 *
 * @param *mysteryship a pointer to the mothership
 * @param *direction the direction we want the mothership to COME FROM
 * 
 */
void resetMothership(enemy_t *mysteryship, unsigned int *direction);

/**
 * @brief Resets player's score and lives to default
 *
 * @param *player a pointer to player object
 * @param *reset_score a score to which we reset the socre to 
 * (reset_score is normally set to zero, but can be changed with cheats)
 * 
 */
void resetPlayerData (space_ship_t *player, unsigned short *reset_score);

/**
 * @brief Increments and updates the position of the mothership
 *
 * @param *mothership pointer to the mothership object
 * @param *direction the direction we want the mothership to MOVE TO
 * 
 */
void updateMothershipPosition(enemy_t *mothership, unsigned int *direction);

/**
 * @brief Increments and updates the position of the invaders
 *
 * @param *invaders pointer to the invaders group
 * @param reset flag to reset the counters in the function since 
 * one call of the funciton increments only one invader
 * 
 */
void updateInvadersPosition(invaders_t *invaders, unsigned char reset);

/**
 * @brief Checks if the invaders have reached the bunkers
 *
 * @param *invaders a pointer to invaders group where the location of alive
 * invaders is evaluated if it is below the bunkers
 * @return 1 if invaders have reached the bunkers otherwise 0
 */
unsigned int invadersReachedBunkers(invaders_t *invaders);

/**
 * @brief Checks if the given bullet has hit the given bunker
 *
 * @param *bunker a pointer to the bunker object that stores bunker blocks locations
 * @param *bullet a pointer to the bullet object
 * @return 1 if colision is detected otherwise 0
 */
unsigned int bulletHitBunker(bunker_t *bunker, bullet_t *bullet_obj);

/**
 * @brief Checks if the given bullet has hit the mothership
 *
 * @param *mothership a pointer to the mothership object
 * @param *bullet a pointer to the bullet object
 * @return 1 if colision is detected otherwise 0
 */
unsigned int bulletHitMothership(enemy_t *mothership, bullet_t *bullet_obj);

/**
 * @brief Checks if the given bullet has hit any of the invaders
 *
 * @param *invaders a pointer to the invaders group object
 * @param *bullet a pointer to the bullet object
 * @return 1 if colision is detected otherwise 0
 */
unsigned int bulletHitInvader(invaders_t *invaders, bullet_t *bullet_obj);

/**
 * @brief Checks if the given bullet has hit the player
 *
 * @param *player a pointer to the player object
 * @param *bullet a pointer to the bullet object
 * @return 1 if colision is detected otherwise 0
 */
unsigned int bulletHitPlayer(space_ship_t *player, bullet_t *bullet_obj);

/**
 * @brief Shoots a bullet from the player's position
 *
 * @param ship_position the absolute position of the middle of the ship in pixles
 * @param *bullet a pointer to the bullet object
 * @return ball_t object with updated location and speed (refer to TUM_Ball.h for more details)
 * 
 */
ball_t *shootBulletPlayer(bullet_t *bullet_obj, signed int ship_position);

/**
 * @brief Shoots a bullet from a random invader
 *
 * @param *invaders a pointer to the invaders group object where only the alive 
 * invaders get considered for bullet starting position, the bullet originates from the 
 * bottom most invaders in each column
 * @param *bullet a pointer to the bullet object
 * @return ball_t object with updated location and speed (refer to TUM_Ball.h for more details)
 * 
 */
ball_t *shootBulletInvader(bullet_t *bullet_obj, invaders_t *invaders);

/**
 * @brief Updates a given bullet's position
 *
 * @param time_since_last_update time difference that is evaluated with the bullet's speed
 * to calculate a the updated bullet position
 * @param *bullet a pointer to the bullet object
 * @return ball_t object with updated location
 * 
 */
void updateBulletPosition(bullet_t *bullet_obj, unsigned int time_since_last_update);

/**
 * @brief Initializes all the tasks, queues, semaphores necesary for the game
 *
 */
int gamesInit(void);

extern TaskHandle_t InvaderNControlTask;
extern TaskHandle_t UDPControlTask;
extern TaskHandle_t MothershipTask;
extern TaskHandle_t PlayerTask;
extern buttons_buffer_t buttons;

/** @}*/
#endif // __INVADERS_H__
